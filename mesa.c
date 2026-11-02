#include "torneo.h"   // tiene que ir primero porque adentro esta el _POSIX_C_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// aqui esta todo lo que pasa dentro de una mesa: el juez y las 13 fichas.
//
// la idea es que hay UN candado (mutex) para todo el tablero. cada ficha:
//   1. toma el candado, mira el tablero, elige donde ir y lo suelta
//   2. piensa entre 50 y 300 ms sin el candado
//   3. vuelve a tomar el candado e intenta moverse
// mientras pensaba alguien pudo haberle ganado la casilla, por eso el juez
// revisa el movimiento con el tablero como esta en ese momento


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



// el candado es "robusto": si una ficha se muere teniendolo tomado, el siguiente que
// lo pide no se queda esperando para siempre, sino que pthread_mutex_lock le
// devuelve EOWNERDEAD (algo como "el dueno se murio")
void revisar_dueno_muerto(Mesa *m, int resultado_lock) {

    if (resultado_lock != EOWNERDEAD) {
        return;
    }

    // hay que avisar que ya lo arreglamos, si no el candado no se puede volver a usar
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




// esta es la parte mas importante: revisar y mover pasa todo con el candado tomado,
// asi es imposible que 2 fichas se queden con la misma casilla
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

        // 1. mirar el tablero
        tomar_candado(m, f->id);
        if (m->estado != EN_JUEGO || f->viva == 0) {
            soltar_candado(m);
            break;
        }

        // aqui se simula que el jugador se cae. lo hice CON el candado tomado
        // y sin soltarlo porque es el peor caso que puede pasar
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

        // 2. pensar. sin el candado, si no las otras 12 fichas quedan esperando
        int espera = config.retardo_min_ms;
        if (config.retardo_max_ms > config.retardo_min_ms) {
            espera = espera + rand_r(&f->semilla) % (config.retardo_max_ms - config.retardo_min_ms + 1);
        }
        dormir_ms(espera);

        if (hay_movimiento == 0) {
            continue;   // estaba encerrada, vuelve a mirar
        }

        // 3. intentar moverse
        tomar_candado(m, f->id);
        if (m->estado == EN_JUEGO) {
            juez_revisar_movimiento(m, f, desde, hacia);
        }
        soltar_candado(m);
    }

    return NULL;
}


// si el perro se mueve de 'desde' a 'hacia', el puma se lo podria comer?
int quedaria_expuesto(Mesa *m, int desde, int hacia) {

    int puma = m->fichas[FICHA_PUMA].posicion;

    for (int k = 0; k < num_saltos[puma]; k++) {

        if (salto_sobre[puma][k] == hacia) {
            int cae = salto_destino[puma][k];
            // ojo que 'desde' tambien cuenta como vacia porque el perro se va de ahi
            if (m->casillas[cae] == VACIA || cae == desde) {
                return 1;
            }
        }
    }
    return 0;
}

// devuelve 0 si la ficha no tiene a donde moverse. se usa con el candado tomado
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
        // los perros le dan puntos a cada vecina libre:
        //   +2 si ahi el puma no se los puede comer
        //   +1 si quedan mas cerca del puma
        // y eligen al azar entre las que tienen mas puntos
        // (si se movieran totalmente al azar se dejarian comer muy facil)
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
