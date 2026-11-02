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
