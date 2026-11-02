#include "torneo.h"   // tiene que ir primero porque adentro esta el _POSIX_C_SOURCE
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>   // para mkdir (la carpeta logs)

// aqui esta el main, el servidor que arma las parejas, los clientes y el ctrl + c
//
// hilos que hay:
//   portero  -> el unico que recibe el ctrl + c
//   servidor -> junta jugadores de a 2 y les da una mesa (maximo M mesas)
//   clientes -> N jugadores que llegan en momentos al azar
//   y por cada mesa un juez + 13 fichas (eso esta en mesa.c)


Config config;

int N;   // jugadores
int M;   // mesas
Mesa *mesas;   // son M mesas y se van reusando

sem_t mesas_libres;   // parte en M, cada partida ocupa uno


// la sala de espera y los contadores se tocan solo con candado_servidor tomado
pthread_mutex_t candado_servidor = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t llego_alguien = PTHREAD_COND_INITIALIZER;
int *sala;   // fila con los jugadores que esperan pareja
int sala_inicio = 0;
int sala_fin = 0;
int clientes_listos = 0;     // cuantos ya llegaron (o se fueron por el ctrl + c)
int clientes_llegaron = 0;

// contadores pal resumen del final
int resultados[NUM_RESULTADOS];
int partidas_creadas = 0;
int mesas_activas = 0;
int max_mesas_activas = 0;
int esperas_por_mesa = 0;
long total_movimientos = 0;
long total_rechazados = 0;
int sin_jugar = 0;


// 'cerrando' se pone en 1 cuando llega ctrl + c. tiene su propio candado y mientras
// se tiene ese candado no se pide ningun otro, asi no se puede trabar con los demas
pthread_mutex_t candado_cierre = PTHREAD_MUTEX_INITIALIZER;
int cerrando = 0;



void dormir_ms(int ms) {

    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

long long ahora_ms(void) {

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}


int hay_que_cerrar(void) {

    pthread_mutex_lock(&candado_cierre);
    int valor = cerrando;
    pthread_mutex_unlock(&candado_cierre);
    return valor;
}

void pedir_cierre(void) {

    pthread_mutex_lock(&candado_cierre);
    cerrando = 1;
    pthread_mutex_unlock(&candado_cierre);
}

// duerme de a 50 ms pa no quedarse dormido un segundo entero si llega ctrl + c
void dormir_atento_ms(int ms) {

    while (ms > 0 && hay_que_cerrar() == 0) {

        int trozo = 50;
        if (ms < 50) {
            trozo = ms;
        }
        dormir_ms(trozo);
        ms = ms - trozo;
    }
}

char *nombre_resultado(int resultado) {

    switch (resultado) {
        case GANA_PUMA:       return "Gana el puma (6 capturas)";
        case GANAN_PERROS:    return "Ganan los perros (puma encerrado)";
        case ABANDONO_PUMA:   return "Ganan los perros por abandono";
        case ABANDONO_PERROS: return "Gana el puma por abandono";
        case EMPATE:          return "Empate (limite de movimientos)";
        case CANCELADA:       return "Canceladas por Ctrl+C";
    }
    return "?";
}




// cada cliente es un hilo: espera un rato al azar, llega y se anota en la sala
void *cliente(void *arg) {

    int id = *(int *) arg;
    unsigned int semilla = config.semilla + 104729u * id;

    dormir_atento_ms(rand_r(&semilla) % (config.llegada_max_ms + 1));

    pthread_mutex_lock(&candado_servidor);
    clientes_listos++;

    if (hay_que_cerrar()) {
        // llego cuando el torneo ya estaba cerrando
        sin_jugar++;
        pthread_cond_signal(&llego_alguien);
        pthread_mutex_unlock(&candado_servidor);
        return NULL;
    }

    sala[sala_fin] = id;
    sala_fin++;
    clientes_llegaron++;
    printf("[cliente %d] llego al servidor (esperando pareja: %d)\n", id, sala_fin - sala_inicio);

    pthread_cond_signal(&llego_alguien);   // despertar al servidor
    pthread_mutex_unlock(&candado_servidor);
    return NULL;
}



void *servidor(void *arg) {

    (void) arg;   // no lo uso

    while (1) {

        // 1. esperar dormido a que haya 2 en la sala
        pthread_mutex_lock(&candado_servidor);
        // while y no if, por si despierta y todavia no hay 2
        while (sala_fin - sala_inicio < 2 && clientes_listos < N && hay_que_cerrar() == 0) {
            pthread_cond_wait(&llego_alguien, &candado_servidor);
        }

        if (hay_que_cerrar() || sala_fin - sala_inicio < 2) {
            // o llego ctrl + c, o ya llegaron todos y no alcanza pa otra pareja
            pthread_mutex_unlock(&candado_servidor);
            break;
        }

        int jugador_puma = sala[sala_inicio];        // el primero que llego juega con el puma
        int jugador_perros = sala[sala_inicio + 1];
        sala_inicio = sala_inicio + 2;
        pthread_mutex_unlock(&candado_servidor);

        printf("[servidor] pareja: jugador %d (puma) y jugador %d (perros)\n",
               jugador_puma, jugador_perros);


        // 2. conseguir una mesa. si estan las M ocupadas, sem_wait deja al servidor
        // dormido hasta que un juez termine y haga sem_post.
        // el sem_trywait es solo para poder avisar que va a tener que esperar
        if (sem_trywait(&mesas_libres) != 0) {

            printf("[servidor] no hay mesas libres (M=%d): la pareja %d-%d espera dormida\n",
                   M, jugador_puma, jugador_perros);
            pthread_mutex_lock(&candado_servidor);
            esperas_por_mesa++;
            pthread_mutex_unlock(&candado_servidor);

            int r = sem_wait(&mesas_libres);
            while (r != 0 && errno == EINTR) {
                r = sem_wait(&mesas_libres);   // lo interrumpio una senal, vuelve a esperar
            }
        }

        if (hay_que_cerrar()) {
            printf("[servidor] cierre: la pareja %d-%d no alcanzo a jugar\n",
                   jugador_puma, jugador_perros);
            pthread_mutex_lock(&candado_servidor);
            sin_jugar = sin_jugar + 2;
            pthread_mutex_unlock(&candado_servidor);
            break;
        }


        // 3. si paso el sem_wait seguro hay una mesa libre, falta ver cual es
        pthread_mutex_lock(&candado_servidor);
        int i = 0;
        while (mesas[i].ocupada == 1) {
            i++;
        }
        mesas[i].ocupada = 1;
        mesas_activas++;
        if (mesas_activas > max_mesas_activas) {
            max_mesas_activas = mesas_activas;
        }
        partidas_creadas++;
        int numero_partida = partidas_creadas;
        pthread_mutex_unlock(&candado_servidor);

        Mesa *m = &mesas[i];
        if (m->tiene_juez == 1) {
            pthread_join(m->hilo_juez, NULL);   // esperar al juez de la partida anterior de esta mesa
            m->tiene_juez = 0;
        }
        m->partida = numero_partida;
        m->jugador_puma = jugador_puma;
        m->jugador_perros = jugador_perros;
        m->semilla = config.semilla * 31u + 1000003u * numero_partida;


        // 4. lanzar al juez
        if (pthread_create(&m->hilo_juez, NULL, juez, m) != 0) {

            printf("[servidor] ERROR: no se pudo crear el juez de la mesa %d\n", m->numero);
            pthread_mutex_lock(&candado_servidor);
            m->ocupada = 0;
            mesas_activas--;
            sin_jugar = sin_jugar + 2;
            pthread_mutex_unlock(&candado_servidor);
            sem_post(&mesas_libres);
            continue;
        }
        m->tiene_juez = 1;
        // printf("mesa %d ocupada, activas = %d\n", m->numero, mesas_activas);
    }


    // los que quedaron en la sala sin poder jugar
    pthread_mutex_lock(&candado_servidor);
    int quedaron = sala_fin - sala_inicio;
    sin_jugar = sin_jugar + quedaron;
    pthread_mutex_unlock(&candado_servidor);

    if (quedaron == 1 && hay_que_cerrar() == 0) {
        printf("[servidor] el jugador %d se quedo sin rival\n", sala[sala_inicio]);
    }
    else if (quedaron > 0) {
        printf("[servidor] %d jugadores quedaron en la sala sin jugar\n", quedaron);
    }

    // esperar a las mesas que siguen jugando
    for (int i = 0; i < M; i++) {
        if (mesas[i].tiene_juez == 1) {
            pthread_join(mesas[i].hilo_juez, NULL);
            mesas[i].tiene_juez = 0;
        }
    }
    return NULL;
}


// la llama el juez cuando termina su partida
void devolver_mesa(Mesa *m, int resultado) {

    pthread_mutex_lock(&candado_servidor);
    resultados[resultado]++;
    total_movimientos = total_movimientos + m->movimientos;
    total_rechazados = total_rechazados + m->rechazados;
    m->ocupada = 0;
    mesas_activas--;
    pthread_mutex_unlock(&candado_servidor);

    sem_post(&mesas_libres);   // si el servidor estaba esperando una mesa, despierta
}




// el ctrl + c esta bloqueado en todos los hilos y solo este lo espera con sigwait.
// lo hice asi porque en una funcion que se llama directo al llegar la senal
// (como en la tarea 1) no se pueden usar mutex sin arriesgarse, y en un hilo normal si
void *portero(void *arg) {

    sigset_t *senales = (sigset_t *) arg;

    while (1) {

        int senal;
        if (sigwait(senales, &senal) != 0) {
            continue;
        }

        if (senal == SIGUSR1) {
            break;   // esta se la manda el main cuando el torneo termino normal
        }

        if (hay_que_cerrar()) {
            printf("[portero] ya estamos cerrando, espera un poco...\n");
            continue;
        }

        printf("\n[portero] llego Ctrl+C: no entran mas jugadores y se cierran las partidas\n");
        pedir_cierre();

        // despertar al servidor, este esperando jugadores o esperando mesa
        pthread_mutex_lock(&candado_servidor);
        pthread_cond_broadcast(&llego_alguien);
        pthread_mutex_unlock(&candado_servidor);
        sem_post(&mesas_libres);

        // a los jueces no hay que avisarles, ellos revisan hay_que_cerrar() cada 200 ms
    }
    return NULL;
}



// pasa el texto a numero y revisa que este entre 1 y 1000000
int leer_argumento(char *texto, int *salida) {

    char *fin;
    errno = 0;
    long valor = strtol(texto, &fin, 10);

    if (errno != 0 || *fin != '\0' || fin == texto || valor < 1 || valor > 1000000) {
        return -1;
    }
    *salida = (int) valor;
    return 0;
}

void imprimir_resumen(long long duracion_ms) {

    printf("\n================ RESUMEN DEL TORNEO ================\n");
    printf("Jugadores: %d   Mesas maximas: %d   Semilla: %u\n", N, M, config.semilla);
    printf("Jugadores que llegaron: %d\n", clientes_llegaron);
    printf("Partidas jugadas: %d\n", partidas_creadas);
    for (int r = GANA_PUMA; r < NUM_RESULTADOS; r++) {
        printf("  %-36s %d\n", nombre_resultado(r), resultados[r]);
    }
    printf("Jugadores que no jugaron (sin rival o por Ctrl+C): %d\n", sin_jugar);
    printf("Mesas activas al mismo tiempo (maximo): %d de %d\n", max_mesas_activas, M);
    printf("Veces que una pareja espero mesa: %d\n", esperas_por_mesa);
    printf("Movimientos aceptados: %ld   rechazados por el juez: %ld\n",
           total_movimientos, total_rechazados);
    printf("Duracion: %.2f s\n", duracion_ms / 1000.0);
    printf("====================================================\n");
}




int main(int argc, char *argv[]) {

    if (argc < 3 || argc > 4 || leer_argumento(argv[1], &N) != 0 || leer_argumento(argv[2], &M) != 0) {

        printf("Uso: %s N M [config.txt]\n", argv[0]);
        printf("  N = cantidad de jugadores (1 o mas)\n");
        printf("  M = maximo de mesas al mismo tiempo (1 o mas)\n\n");
        imprimir_mapa();
        return 1;
    }

    config_por_defecto(&config);
    if (argc == 4 && leer_config(argv[3], &config) != 0) {
        printf("Error en el archivo de configuracion '%s'\n", argv[3]);
        return 1;
    }
    if (config.semilla_dada == 0) {
        config.semilla = (unsigned int) time(NULL);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);   // igual que en la tarea 1, pa que cada printf salga al tiro

    preparar_tablero();

    if (config.log_detalle > 0 && mkdir("logs", 0755) != 0 && errno != EEXIST) {
        perror("no se pudo crear la carpeta logs");
        config.log_detalle = 0;
    }

    printf("=== Torneo de Komikan Concurrente ===\n");
    printf("Jugadores: %d | Mesas: %d | Retardo: %d-%d ms | Semilla: %u | Prob. de falla: %d%%\n\n",
           N, M, config.retardo_min_ms, config.retardo_max_ms, config.semilla, config.prob_falla);


    // IMPORTANTE: esto va antes de crear cualquier hilo. los hilos nuevos copian
    // las senales bloqueadas del que los crea, asi que ninguno va a recibir el
    // ctrl + c, solo el portero que lo espera con sigwait
    sigset_t senales;
    sigemptyset(&senales);
    sigaddset(&senales, SIGINT);
    sigaddset(&senales, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &senales, NULL);


    mesas = calloc(M, sizeof(Mesa));
    sala = malloc(N * sizeof(int));
    int *ids = malloc(N * sizeof(int));
    pthread_t *hilos_clientes = malloc(N * sizeof(pthread_t));
    int *cliente_creado = calloc(N, sizeof(int));

    if (mesas == NULL || sala == NULL || ids == NULL || hilos_clientes == NULL || cliente_creado == NULL) {
        printf("Error: no hay memoria suficiente\n");
        free(mesas);
        free(sala);
        free(ids);
        free(hilos_clientes);
        free(cliente_creado);
        return 1;
    }

    for (int i = 0; i < M; i++) {
        mesas[i].numero = i + 1;
    }

    sem_init(&mesas_libres, 0, M);   // el 0 es porque se comparte entre hilos y no entre procesos

    long long inicio = ahora_ms();

    pthread_t hilo_portero;
    pthread_t hilo_servidor;
    int error_hilos = 0;

    if (pthread_create(&hilo_portero, NULL, portero, &senales) != 0) {
        error_hilos = 1;
    }
    else if (pthread_create(&hilo_servidor, NULL, servidor, NULL) != 0) {
        error_hilos = 1;
        pthread_kill(hilo_portero, SIGUSR1);
        pthread_join(hilo_portero, NULL);
    }

    if (error_hilos == 1) {
        printf("Error: no se pudieron crear los hilos principales\n");
        sem_destroy(&mesas_libres);
        free(mesas);
        free(sala);
        free(ids);
        free(hilos_clientes);
        free(cliente_creado);
        return 1;
    }


    for (int i = 0; i < N; i++) {

        ids[i] = i + 1;   // cada cliente recibe su propia variable, si le paso &i todos verian la misma
        if (pthread_create(&hilos_clientes[i], NULL, cliente, &ids[i]) == 0) {
            cliente_creado[i] = 1;
        }
        else {
            // si no se pudo crear el hilo cuenta como que llego y se fue
            printf("[main] no se pudo crear el hilo del cliente %d\n", i + 1);
            pthread_mutex_lock(&candado_servidor);
            clientes_listos++;
            sin_jugar++;
            pthread_cond_signal(&llego_alguien);
            pthread_mutex_unlock(&candado_servidor);
        }
    }

    for (int i = 0; i < N; i++) {
        if (cliente_creado[i] == 1) {
            pthread_join(hilos_clientes[i], NULL);
        }
    }
    pthread_join(hilo_servidor, NULL);   // y el servidor espera a todos los jueces antes de terminar

    pthread_kill(hilo_portero, SIGUSR1);   // avisarle al portero que ya puede terminar
    pthread_join(hilo_portero, NULL);

    imprimir_resumen(ahora_ms() - inicio);
    int hubo_ctrl_c = hay_que_cerrar();


    // liberar todo
    sem_destroy(&mesas_libres);
    pthread_mutex_destroy(&candado_servidor);
    pthread_cond_destroy(&llego_alguien);
    pthread_mutex_destroy(&candado_cierre);
    free(mesas);
    free(sala);
    free(ids);
    free(hilos_clientes);
    free(cliente_creado);

    if (hubo_ctrl_c) {
        return 130;   // cuando algo termina por ctrl + c
    }
    return 0;
}
