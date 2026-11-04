#ifndef TORNEO_H
#define TORNEO_H

#define _POSIX_C_SOURCE 200809L
// sin esto no compilan sigwait, rand_r y nanosleep con -std=c17

#include <stdio.h>
#include <pthread.h>    // hilos, mutex y variables de condicion
#include <semaphore.h>  // el semaforo de las mesas


#define NUM_CASILLAS 28
// 25 del cuadrado + 3 del trono
#define NUM_PERROS 12
#define NUM_FICHAS 13
#define FICHA_PUMA 12
// los perros son las fichas 0 a 11 y el puma es la 12
#define VACIA -1
#define MAX_VECINOS 8
#define CAPTURAS_PARA_GANAR 6

// como puede terminar una partida
#define EN_JUEGO 0
#define GANA_PUMA 1
#define GANAN_PERROS 2
#define ABANDONO_PUMA 3
#define ABANDONO_PERROS 4
// abandono = se cayo ese jugador, asi que gana el otro
#define EMPATE 5
#define CANCELADA 6
// cancelada es cuando se hace ctrl + c
#define NUM_RESULTADOS 7

// cual de los dos jugadores se cae (para probar las fallas)
#define LADO_NADIE 0
#define LADO_PUMA 1
#define LADO_PERROS 2

// para saber quien tiene tomado el candado del tablero
#define NADIE -1
#define QUIEN_JUEZ 100




typedef struct {

    int retardo_min_ms;
    int retardo_max_ms;
    unsigned int semilla;
    int semilla_dada;   // 1 si la semilla venia en el config.txt
    int pos_puma;
    int pos_perros[NUM_PERROS];

    int llegada_max_ms;   // los jugadores llegan entre 0 y este tiempo
    int prob_falla;       // en %
    int max_movimientos;  // si se llega a esto es empate
    int log_detalle;      // 0 nada, 1 solo lo que pasa, 2 lo que pasa + el tablero
    int tablero_en_consola;

} Config;



struct Mesa;
// la Mesa se define mas abajo pero la Ficha necesita saber que existe

typedef struct {

    int id;
    int posicion;
    int viva;     // 0 si el puma se la comio
    int creada;   // 1 si su hilo se alcanzo a crear
    unsigned int semilla;   // cada ficha tiene la suya pa los numeros al azar
    pthread_t hilo;
    struct Mesa *mesa;

} Ficha;



typedef struct Mesa {

    int numero;
    int ocupada;   // ojo: esta la cuida el candado del servidor, no el de la mesa
    int tiene_juez;
    pthread_t hilo_juez;

    int partida;
    int jugador_puma;
    int jugador_perros;
    unsigned int semilla;
    long long inicio_ms;
    FILE *log;

    // todo lo de aca abajo solo se toca con el candado tomado
    pthread_mutex_t candado;   // uno solo para todo el tablero
    pthread_cond_t aviso;      // aqui duerme el juez hasta que pase algo
    int casillas[NUM_CASILLAS];   // VACIA o el numero de la ficha que esta ahi
    Ficha fichas[NUM_FICHAS];
    int estado;
    int capturas;
    int movimientos;
    int rechazados;
    int quien_tiene_candado;
    int lado_que_falla;
    int falla_en_movimiento;

} Mesa;


// extern = existen pero estan creadas en otro archivo
extern Config config;



// estas las llena tablero.c al principio y despues nadie las cambia,
// por eso los hilos las pueden leer sin candado
extern int vecinos[NUM_CASILLAS][MAX_VECINOS];


extern int num_vecinos[NUM_CASILLAS];


extern int salto_sobre[NUM_CASILLAS][MAX_VECINOS];

extern int salto_destino[NUM_CASILLAS][MAX_VECINOS];

extern int num_saltos[NUM_CASILLAS];



// funciones de tablero.c
void preparar_tablero(void);



int es_perro(int contenido);


int son_vecinos(int a, int b);



int buscar_salto(int desde, int hacia);




int puma_puede_moverse(int casillas[]);


int distancia(int a, int b);



void dibujar_tablero(int casillas[], char *texto, int tam);


void imprimir_mapa(void);




// funciones de mesa.c
void *juez(void *arg);




// funciones de lector_config.c
void config_por_defecto(Config *c);


int leer_config(char *archivo, Config *c);




// funciones de torneo.c
int hay_que_cerrar(void);





void devolver_mesa(Mesa *m, int resultado);




void dormir_ms(int ms);

long long ahora_ms(void);



char *nombre_resultado(int resultado);

#endif
