/* torneo.h
 * Tarea 2 - Torneo de Komikan Concurrente (Sistemas Operativos)
 *
 * Todo lo que comparten los archivos del programa: constantes,
 * estructuras y funciones.
 */

#ifndef TORNEO_H
#define TORNEO_H

/* para usar sigwait, nanosleep, rand_r, mutex robustos, etc. con -std=c17 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

/* ------------------------- tablero ------------------------- */

#define NUM_CASILLAS 28        /* 25 del cuadrado + 3 del trono */
#define NUM_PERROS 12
#define NUM_FICHAS 13          /* 12 perros + 1 puma */
#define FICHA_PUMA 12          /* las fichas 0..11 son perros, la 12 es el puma */
#define VACIA -1               /* casilla sin ficha */
#define MAX_VECINOS 8
#define CAPTURAS_PARA_GANAR 6

/* como queda una partida */
#define EN_JUEGO 0
#define GANA_PUMA 1
#define GANAN_PERROS 2
#define ABANDONO_PUMA 3        /* se cayo el jugador del puma -> ganan los perros */
#define ABANDONO_PERROS 4      /* se cayo el jugador de los perros -> gana el puma */
#define EMPATE 5               /* se llego al limite de movimientos */
#define CANCELADA 6            /* Ctrl+C */
#define NUM_RESULTADOS 7

/* que jugador falla (para simular desconexiones) */
#define LADO_NADIE 0
#define LADO_PUMA 1
#define LADO_PERROS 2

/* quien tiene el candado del tablero: 0..12 es una ficha */
#define NADIE -1
#define QUIEN_JUEZ 100

/* ------------------------- estructuras ------------------------- */

typedef struct {
    int retardo_min_ms;        /* cuanto piensa una ficha antes de moverse */
    int retardo_max_ms;
    unsigned int semilla;
    int semilla_dada;          /* 1 si la semilla venia en el config.txt */
    int pos_puma;              /* disposicion inicial */
    int pos_perros[NUM_PERROS];
    int llegada_max_ms;        /* los clientes llegan entre 0 y este tiempo */
    int prob_falla;            /* % de partidas donde un jugador se cae */
    int max_movimientos;       /* si se llega a esto, la partida es empate */
    int log_detalle;           /* 0 = sin log, 1 = solo eventos, 2 = eventos + tablero */
    int tablero_en_consola;    /* 1 = tambien dibuja el tablero en la terminal */
} Config;

struct Mesa;

typedef struct {
    int id;                    /* 0..11 perros, 12 puma */
    int posicion;              /* casilla donde esta */
    int viva;                  /* 0 si el puma se la comio */
    int creada;                /* 1 si su hilo se alcanzo a crear */
    unsigned int semilla;      /* para rand_r */
    pthread_t hilo;
    struct Mesa *mesa;
} Ficha;

typedef struct Mesa {
    int numero;                /* 1..M */
    int ocupada;               /* lo protege el candado del servidor */
    int tiene_juez;            /* 1 si hay un hilo juez que esperar con join */
    pthread_t hilo_juez;

    int partida;               /* numero de partida en el torneo */
    int jugador_puma;
    int jugador_perros;
    unsigned int semilla;
    long long inicio_ms;
    FILE *log;

    /* ---- todo lo de abajo lo protege 'candado' ---- */
    pthread_mutex_t candado;   /* un candado para todo el tablero (robusto) */
    pthread_cond_t aviso;      /* el juez duerme aqui hasta que algo pase */
    int casillas[NUM_CASILLAS];/* VACIA o el id de la ficha que esta ahi */
    Ficha fichas[NUM_FICHAS];
    int estado;                /* EN_JUEGO, GANA_PUMA, ... */
    int capturas;
    int movimientos;
    int rechazados;            /* movimientos que el juez no acepto */
    int quien_tiene_candado;   /* para saber quien se cayo con el candado tomado */
    int lado_que_falla;        /* LADO_NADIE si nadie falla en esta partida */
    int falla_en_movimiento;   /* en que movimiento se cae */
} Mesa;

/* ------------------------- variables globales ------------------------- */

extern Config config;

/* tablero.c: se llenan una sola vez al principio y despues solo se leen */
extern int vecinos[NUM_CASILLAS][MAX_VECINOS];
extern int num_vecinos[NUM_CASILLAS];
extern int salto_sobre[NUM_CASILLAS][MAX_VECINOS];
extern int salto_destino[NUM_CASILLAS][MAX_VECINOS];
extern int num_saltos[NUM_CASILLAS];

/* ------------------------- funciones ------------------------- */

/* tablero.c */
void preparar_tablero(void);
int es_perro(int contenido);
int son_vecinos(int a, int b);
int buscar_salto(int desde, int hacia);
int puma_puede_moverse(const int casillas[]);
int distancia(int a, int b);
void dibujar_tablero(const int casillas[], char *texto, int tam);
void imprimir_mapa(void);

/* mesa.c */
void *juez(void *arg);

/* lector_config.c */
void config_por_defecto(Config *c);
int leer_config(const char *archivo, Config *c);

/* torneo.c */
int hay_que_cerrar(void);
void devolver_mesa(Mesa *m, int resultado);
void dormir_ms(int ms);
long long ahora_ms(void);
const char *nombre_resultado(int resultado);

#endif
