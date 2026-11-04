#include "torneo.h"   // tiene que ir primero porque adentro esta el _POSIX_C_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// en este archivo esta lo que pasa en cada mesa, o sea el juez y las 13 fichas, todo el tablero se protege con un solo mutex. la ficha lo toma para mirar
// donde moverse, lo suelta mientras piensa y despues lo vuelve a tomar para moverse.
// como mientras pensaba otra ficha le pudo ganar la casilla, el juez revisa el movimiento de nuevo antes de aceptarlo


#define TAM_DIBUJO 1024


void revisar_victoria(Mesa *m);




int elegir_movimiento(Mesa *m, Ficha *f, int *desde, int *hacia);
// las puse aca arriba porque las uso antes de donde estan escritas



void nombre_ficha(int id, char *nombre) {





    if (id == FICHA_PUMA) {
        strcpy(nombre, "puma");
    }



    else {
        sprintf(nombre, "perro %d", id);
    }
}

// escribe una linea en el log de la partida con los ms que lleva la partida
void anotar(Mesa *m, char *texto) {


    if (m->log == NULL) {
        return;
    }
    fprintf(m->log, "[%6lld ms] %s\n", ahora_ms() - m->inicio_ms, texto);
}


void mostrar_tablero(Mesa *m, char *que_paso) {



    if (config.log_detalle < 2 && config.tablero_en_consola == 0) {
        return;
    }
    char dibujo[TAM_DIBUJO];
    dibujar_tablero(m->casillas, dibujo, TAM_DIBUJO);

    if (m->log != NULL && config.log_detalle >= 2) {
        fprintf(m->log, "%s      capturas: %d/%d   movimientos: %d\n\n",
                dibujo, m->capturas, CAPTURAS_PARA_GANAR, m->movimientos);
    }

    if (config.tablero_en_consola == 1) {
        // todo en un solo printf, si no se mezcla con lo que imprimen las otras mesas
        printf("[mesa %d] mov %d: %s   (capturas %d/%d)\n%s\n", m->numero, m->movimientos,
               que_paso, m->capturas, CAPTURAS_PARA_GANAR, dibujo);
    }
}



// si una ficha se muere teniendolo tomado, el que lo pide despues no se queda pegado esperando, porque pthread_mutex_lock le devuelve EOWNERDEAD
// (que quiere decir que el dueno se murio)
void revisar_dueno_muerto(Mesa *m, int resultado_lock) {


    if (resultado_lock != EOWNERDEAD) {
        return;
    }

    // hay que avisar que ya lo arreglamos, si no el mutex no se puede volver a usar
    pthread_mutex_consistent(&m->candado);

    // el que murio no alcanzo a borrar su numero, asi sabemos quien fue
    int muerto = m->quien_tiene_candado;
    char nombre[16];
    char linea[200];
    nombre_ficha(muerto, nombre);
    snprintf(linea, sizeof(linea), "ANOMALIA: el %s murio con el candado tomado. Candado recuperado.", nombre);
    anotar(m, linea);

    if (m->estado == EN_JUEGO) {


        if (muerto == FICHA_PUMA) {
            m->estado = ABANDONO_PUMA;
        }



        else {
            m->estado = ABANDONO_PERROS;
        }
        pthread_cond_signal(&m->aviso);   // despertar al juez
    }
}


void tomar_candado(Mesa *m, int quien) {

    int r = pthread_mutex_lock(&m->candado);
    revisar_dueno_muerto(m, r);
    m->quien_tiene_candado = quien;
}



void soltar_candado(Mesa *m) {

    m->quien_tiene_candado = NADIE;
    pthread_mutex_unlock(&m->candado);
}



// esta es la parte mas importante. se revisa y se mueve todo con el mutex tomado
// asi es imposible que dos fichas se queden con la misma casilla
void juez_revisar_movimiento(Mesa *m, Ficha *f, int desde, int hacia) {

    char nombre[16];
    char linea[200];
    nombre_ficha(f->id, nombre);


    if (f->viva == 0) {
        m->rechazados++;
        snprintf(linea, sizeof(linea), "RECHAZADO: al %s se lo comieron mientras pensaba", nombre);
        anotar(m, linea);
        return;
    }




    if (f->posicion != desde) {
        m->rechazados++;
        snprintf(linea, sizeof(linea), "RECHAZADO: el %s ya no esta en la casilla %d", nombre, desde);
        anotar(m, linea);
        return;
    }

    if (m->casillas[hacia] != VACIA) {
        m->rechazados++;
        snprintf(linea, sizeof(linea), "RECHAZADO: el %s quiso ir de %d a %d, pero otra ficha llego antes",
                 nombre, desde, hacia);
        anotar(m, linea);
        return;
    }

    int sobre = -1;   // donde esta el perro que se come el puma (si es que come)
    int valido = 0;



    if (son_vecinos(desde, hacia)) {
        valido = 1;
    }


    else if (f->id == FICHA_PUMA) {
        // saltar solo puede el puma, y solo si hay un perro en el medio
        sobre = buscar_salto(desde, hacia);





        if (sobre != -1 && es_perro(m->casillas[sobre])) {
            valido = 1;
        }
    }



    if (valido == 0) {
        m->rechazados++;
        snprintf(linea, sizeof(linea), "RECHAZADO: el %s no puede ir de %d a %d (no es vecina o ya no hay perro que saltar)",
                 nombre, desde, hacia);
        anotar(m, linea);
        return;
    }

    // el movimiento vale
    m->casillas[desde] = VACIA;
    m->casillas[hacia] = f->id;
    f->posicion = hacia;
    m->movimientos++;

    char que_paso[96];



    if (sobre != -1) {

        int comido = m->casillas[sobre];
        m->fichas[comido].viva = 0;   // el hilo de ese perro se da cuenta en su siguiente vuelta y termina
        m->casillas[sobre] = VACIA;
        m->capturas++;

        snprintf(que_paso, sizeof(que_paso), "CAPTURA: el puma salto de %d a %d y se comio al perro %d",
                 desde, hacia, comido);
        snprintf(linea, sizeof(linea), "%s (%d/%d)", que_paso, m->capturas, CAPTURAS_PARA_GANAR);
        anotar(m, linea);
        printf("[mesa %d] el puma se comio al perro %d (%d/%d)\n",
               m->numero, comido, m->capturas, CAPTURAS_PARA_GANAR);
    }





    else {
        snprintf(que_paso, sizeof(que_paso), "%s: %d -> %d", nombre, desde, hacia);
        anotar(m, que_paso);
    }

    mostrar_tablero(m, que_paso);
    revisar_victoria(m);
}



void revisar_victoria(Mesa *m) {




    if (m->capturas >= CAPTURAS_PARA_GANAR) {
        m->estado = GANA_PUMA;
    }





    else if (puma_puede_moverse(m->casillas) == 0) {
        m->estado = GANAN_PERROS;
    }




    else if (m->movimientos >= config.max_movimientos) {
        // si no le pongo limite, como las fichas se mueven al azar podria no terminar nunca
        m->estado = EMPATE;
    }


    if (m->estado != EN_JUEGO) {
        pthread_cond_signal(&m->aviso);   // despertar al juez, ya termino
    }
}





// lo que hace cada ficha (cada una es un hilo)
void *ficha(void *arg) {

    Ficha *f = (Ficha *) arg;
    Mesa *m = f->mesa;

    int lado = LADO_PERROS;
    int jugador = m->jugador_perros;

    if (f->id == FICHA_PUMA) {
        lado = LADO_PUMA;
        jugador = m->jugador_puma;
    }

    char nombre[16];
    char linea[200];
    nombre_ficha(f->id, nombre);





    while (1) {

        int desde = 0;
        int hacia = 0;

        // primero mira el tablero
        tomar_candado(m, f->id);




        if (m->estado != EN_JUEGO || f->viva == 0) {
            soltar_candado(m);
            break;
        }



        // aqui se simula que el jugador se cae. lo hice con el mutex tomado
        // y sin soltarlo porque es lo peor que puede pasar
        if (lado == m->lado_que_falla && m->movimientos >= m->falla_en_movimiento) {

            snprintf(linea, sizeof(linea), "FALLA SIMULADA: el jugador %d (%s) se cayo mientras tenia el tablero",
                     jugador, nombre);
            anotar(m, linea);




            if (lado == LADO_PUMA) {
                printf("[mesa %d] FALLA SIMULADA: se cayo el jugador %d (puma) con el tablero tomado\n", m->numero, jugador);
            }

            else {
                printf("[mesa %d] FALLA SIMULADA: se cayo el jugador %d (perros) con el tablero tomado\n", m->numero, jugador);
            }
            pthread_exit(NULL);
        }

        int hay_movimiento = elegir_movimiento(m, f, &desde, &hacia);
        soltar_candado(m);

        // piensa sin el mutex, si no las otras 12 fichas quedan esperando
        int espera = config.retardo_min_ms;



        if (config.retardo_max_ms > config.retardo_min_ms) {
            espera = espera + rand_r(&f->semilla) % (config.retardo_max_ms - config.retardo_min_ms + 1);
        }
        dormir_ms(espera);


        if (hay_movimiento == 0) {
            continue;   // estaba encerrada, vuelve a mirar
        }

        // y ahora intenta moverse
        tomar_candado(m, f->id);


        if (m->estado == EN_JUEGO) {
            juez_revisar_movimiento(m, f, desde, hacia);
        }
        soltar_candado(m);
    }

    return NULL;
}

// revisa si al moverse a esa casilla el puma se podria comer al perro
int quedaria_expuesto(Mesa *m, int desde, int hacia) {

    int puma = m->fichas[FICHA_PUMA].posicion;



    for (int k = 0; k < num_saltos[puma]; k++) {


        if (salto_sobre[puma][k] == hacia) {
            int cae = salto_destino[puma][k];




            // la casilla de donde sale el perro tambien cuenta como vacia porque se va de ahi
            if (m->casillas[cae] == VACIA || cae == desde) {
                return 1;
            }
        }
    }
    return 0;
}

// devuelve 0 si la ficha no tiene a donde moverse. se usa con el mutex tomado
int elegir_movimiento(Mesa *m, Ficha *f, int *desde, int *hacia) {

    int opciones[2 * MAX_VECINOS];
    int n = 0;
    int pos = f->posicion;
    *desde = pos;



    if (f->id == FICHA_PUMA) {





        // si puede comerse a alguien, come
        for (int k = 0; k < num_saltos[pos]; k++) {


            if (es_perro(m->casillas[salto_sobre[pos][k]]) && m->casillas[salto_destino[pos][k]] == VACIA) {
                opciones[n] = salto_destino[pos][k];
                n++;
            }
        }





        // si no, va a cualquier vecina libre
        if (n == 0) {




            for (int k = 0; k < num_vecinos[pos]; k++) {



                if (m->casillas[vecinos[pos][k]] == VACIA) {
                    opciones[n] = vecinos[pos][k];
                    n++;
                }
            }
        }
    }

    else {
        // los perros le dan puntos a cada casilla libre que tienen al lado. le dan 2 puntos
        // si ahi el puma no se los puede comer y 1 punto mas si quedan mas cerca del puma.
        // despues eligen al azar entre las que tienen mas puntos. si se movieran
        // totalmente al azar se dejarian comer muy facil
        int puma = m->fichas[FICHA_PUMA].posicion;
        int mejor = -1;

        for (int k = 0; k < num_vecinos[pos]; k++) {

            int v = vecinos[pos][k];


            if (m->casillas[v] != VACIA) {
                continue;
            }

            int puntos = 0;



            if (quedaria_expuesto(m, pos, v) == 0) {
                puntos = puntos + 2;
            }


            if (distancia(v, puma) < distancia(pos, puma)) {
                puntos = puntos + 1;
            }



            if (puntos > mejor) {
                mejor = puntos;
                n = 0;   // las que tenia guardadas eran peores, las borro
            }


            if (puntos == mejor) {
                opciones[n] = v;
                n++;
            }
        }
    }




    if (n == 0) {
        return 0;
    }
    *hacia = opciones[rand_r(&f->semilla) % n];
    return 1;
}


void preparar_partida(Mesa *m) {

    // asi se crea un mutex robusto, lo que pasa cuando alguien muere esta en revisar_dueno_muerto
    pthread_mutexattr_t atributos;
    pthread_mutexattr_init(&atributos);
    pthread_mutexattr_setrobust(&atributos, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&m->candado, &atributos);
    pthread_mutexattr_destroy(&atributos);

    pthread_cond_init(&m->aviso, NULL);

    for (int c = 0; c < NUM_CASILLAS; c++) {
        m->casillas[c] = VACIA;
    }




    for (int i = 0; i < NUM_FICHAS; i++) {

        Ficha *f = &m->fichas[i];
        f->id = i;
        f->viva = 1;
        f->creada = 0;
        f->mesa = m;
        f->semilla = m->semilla + 7919u * (i + 1);   // le sumo algo distinto a cada una pa que no piensen igual



        if (i == FICHA_PUMA) {
            f->posicion = config.pos_puma;
        }




        else {
            f->posicion = config.pos_perros[i];
        }
        m->casillas[f->posicion] = i;
    }

    m->estado = EN_JUEGO;
    m->capturas = 0;
    m->movimientos = 0;
    m->rechazados = 0;
    m->quien_tiene_candado = NADIE;
    m->inicio_ms = ahora_ms();

    // se va a caer alguien en esta partida?
    m->lado_que_falla = LADO_NADIE;
    m->falla_en_movimiento = 0;



    if ((int)(rand_r(&m->semilla) % 100) < config.prob_falla) {



        if (rand_r(&m->semilla) % 2 == 0) {
            m->lado_que_falla = LADO_PUMA;
        }



        else {
            m->lado_que_falla = LADO_PERROS;
        }
        m->falla_en_movimiento = 3 + rand_r(&m->semilla) % 28;
    }

    m->log = NULL;



    if (config.log_detalle > 0) {

        char archivo[64];
        snprintf(archivo, sizeof(archivo), "logs/partida_%03d.log", m->partida);
        m->log = fopen(archivo, "w");



        if (m->log == NULL) {
            perror("no se pudo abrir el log de la partida");
        }
    }
}




// el juez de una mesa (un hilo por mesa)
void *juez(void *arg) {

    Mesa *m = (Mesa *) arg;
    char linea[200];

    preparar_partida(m);
    printf("[mesa %d] empieza la partida %d: jugador %d (puma) vs jugador %d (perros)\n",
           m->numero, m->partida, m->jugador_puma, m->jugador_perros);
    snprintf(linea, sizeof(linea), "Partida %d en la mesa %d: jugador %d (puma) vs jugador %d (perros)",
             m->partida, m->numero, m->jugador_puma, m->jugador_perros);
    anotar(m, linea);
    mostrar_tablero(m, "tablero inicial");




    // por si en el config ponen al puma ya encerrado
    if (puma_puede_moverse(m->casillas) == 0) {
        m->estado = GANAN_PERROS;
    }



    for (int i = 0; i < NUM_FICHAS; i++) {





        if (pthread_create(&m->fichas[i].hilo, NULL, ficha, &m->fichas[i]) != 0) {

            printf("[mesa %d] ERROR: no se pudo crear el hilo de una ficha, se cancela la partida\n", m->numero);
            tomar_candado(m, QUIEN_JUEZ);
            m->estado = CANCELADA;
            soltar_candado(m);
            break;
        }
        m->fichas[i].creada = 1;
    }


    // el juez duerme hasta que termine la partida. lo despierta una ficha cuando
    // alguien gana o alguien se cae, y si no igual despierta cada 200 ms para ver si
    // hubo ctrl c. va con while y no con if porque puede despertar sin que haya pasado nada
    tomar_candado(m, QUIEN_JUEZ);

    while (m->estado == EN_JUEGO) {


        if (hay_que_cerrar()) {
            m->estado = CANCELADA;
            anotar(m, "El servidor pidio cerrar (Ctrl+C): partida cancelada");
            break;
        }

        // la hora hasta la que va a dormir, que es ahora mas 200 ms
        struct timespec hasta;
        clock_gettime(CLOCK_REALTIME, &hasta);
        hasta.tv_nsec = hasta.tv_nsec + 200 * 1000000L;



        if (hasta.tv_nsec >= 1000000000L) {
            hasta.tv_sec++;
            hasta.tv_nsec = hasta.tv_nsec - 1000000000L;
        }

        m->quien_tiene_candado = NADIE;   // mientras duerme el mutex queda libre
        int r = pthread_cond_timedwait(&m->aviso, &m->candado, &hasta);
        revisar_dueno_muerto(m, r);   // al despertar tambien puede enterarse de que alguien murio
        m->quien_tiene_candado = QUIEN_JUEZ;
    }
    int resultado = m->estado;
    soltar_candado(m);




    // espera a todas las fichas, tambien a la que se cayo
    for (int i = 0; i < NUM_FICHAS; i++) {


        if (m->fichas[i].creada == 1) {
            pthread_join(m->fichas[i].hilo, NULL);
        }
    }


    char texto[160];





    if (resultado == GANA_PUMA) {
        sprintf(texto, "gana el jugador %d (puma), se comio %d perros", m->jugador_puma, m->capturas);
    }




    else if (resultado == GANAN_PERROS) {
        sprintf(texto, "gana el jugador %d (perros), encerraron al puma", m->jugador_perros);
    }



    else if (resultado == ABANDONO_PUMA) {
        sprintf(texto, "gana el jugador %d (perros) por abandono del puma", m->jugador_perros);
    }

    else if (resultado == ABANDONO_PERROS) {
        sprintf(texto, "gana el jugador %d (puma) por abandono de los perros", m->jugador_puma);
    }





    else if (resultado == EMPATE) {
        sprintf(texto, "empate: se llego al limite de %d movimientos", config.max_movimientos);
    }



    else {
        sprintf(texto, "partida cancelada");
    }

    printf("[mesa %d] fin de la partida %d: %s (%d movimientos, %d rechazados)\n",
           m->numero, m->partida, texto, m->movimientos, m->rechazados);
    snprintf(linea, sizeof(linea), "FIN: %s (%d movimientos, %d rechazados)", texto, m->movimientos, m->rechazados);
    anotar(m, linea);

    // ya no queda ninguna ficha, asi que se puede destruir todo sin problema
    pthread_cond_destroy(&m->aviso);
    pthread_mutex_destroy(&m->candado);




    if (m->log != NULL) {
        fclose(m->log);
        m->log = NULL;
    }

    devolver_mesa(m, resultado);   // la mesa queda libre pa la siguiente pareja
    return NULL;
}
