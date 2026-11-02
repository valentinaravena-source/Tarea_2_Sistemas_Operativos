#include "torneo.h"   // tiene que ir primero porque adentro esta el _POSIX_C_SOURCE
#include <stdlib.h>
#include <string.h>
#define NUM_LINEAS 13
#define LARGO_LINEA 7

// en vez de escribir a mano los vecinos de las 28 casillas anote las lineas rectas
// del tablero y que el programa los saque solo. el -1 es donde termina la linea
// 2 casillas seguidas en una linea son vecinas y 3 seguidas es un salto del puma
int lineas[NUM_LINEAS][LARGO_LINEA] = {
    { 3,  4,  5,  6,  7, -1},
    { 8,  9, 10, 11, 12, -1},
    {13, 14, 15, 16, 17, -1},
    {18, 19, 20, 21, 22, -1},
    {23, 24, 25, 26, 27, -1},
    { 3,  8, 13, 18, 23, -1},
    { 1,  4,  9, 14, 19, 24, -1},   // estas 3 columnas suben hasta el trono
    { 0,  5, 10, 15, 20, 25, -1},
    { 2,  6, 11, 16, 21, 26, -1},
    { 7, 12, 17, 22, 27, -1},
    { 3,  1,  0, -1},
    { 7,  2,  0, -1},
    { 1,  2, -1}
};

int vecinos[NUM_CASILLAS][MAX_VECINOS];
int num_vecinos[NUM_CASILLAS];
int salto_sobre[NUM_CASILLAS][MAX_VECINOS];
int salto_destino[NUM_CASILLAS][MAX_VECINOS];
int num_saltos[NUM_CASILLAS];


void agregar_vecino(int a, int b) {

    vecinos[a][num_vecinos[a]] = b;
    num_vecinos[a]++;
}

void agregar_salto(int desde, int sobre, int hacia) {

    salto_sobre[desde][num_saltos[desde]] = sobre;
    salto_destino[desde][num_saltos[desde]] = hacia;
    num_saltos[desde]++;
}


// se llama una sola vez en el main, antes de crear los hilos
void preparar_tablero(void) {

    memset(num_vecinos, 0, sizeof(num_vecinos));
    memset(num_saltos, 0, sizeof(num_saltos));

    for (int l = 0; l < NUM_LINEAS; l++) {

        for (int k = 0; lineas[l][k] != -1; k++) {

            int a = lineas[l][k];
            int b = lineas[l][k + 1];
            if (b == -1) {
                break;
            }

            agregar_vecino(a, b);
            agregar_vecino(b, a);

            int c = lineas[l][k + 2];
            if (c != -1) {
                agregar_salto(a, b, c);   // desde a salta sobre b y cae en c
                agregar_salto(c, b, a);   // y al reves
            }
        }
    }
}



int es_perro(int contenido) {

    if (contenido >= 0 && contenido < NUM_PERROS) {
        return 1;
    }
    return 0;
}

int son_vecinos(int a, int b) {

    for (int k = 0; k < num_vecinos[a]; k++) {
        if (vecinos[a][k] == b) {
            return 1;
        }
    }
    return 0;
}

// si se puede saltar de desde a hacia devuelve la casilla del medio, si no -1
int buscar_salto(int desde, int hacia) {

    for (int k = 0; k < num_saltos[desde]; k++) {
        if (salto_destino[desde][k] == hacia) {
            return salto_sobre[desde][k];
        }
    }
    return -1;
}


// si devuelve 0 el puma quedo encerrado y ganan los perros
int puma_puede_moverse(int casillas[]) {

    int pos = -1;
    for (int c = 0; c < NUM_CASILLAS; c++) {
        if (casillas[c] == FICHA_PUMA) {
            pos = c;
        }
    }
    if (pos == -1) {
        return 0;
    }

    for (int k = 0; k < num_vecinos[pos]; k++) {
        if (casillas[vecinos[pos][k]] == VACIA) {
            return 1;
        }
    }

    // aunque tenga todo ocupado alrededor, si se puede comer a alguien no esta encerrado
    for (int k = 0; k < num_saltos[pos]; k++) {
        if (es_perro(casillas[salto_sobre[pos][k]]) && casillas[salto_destino[pos][k]] == VACIA) {
            return 1;
        }
    }
    return 0;
}


// fila -2 es la T, fila -1 el trono y de 0 a 4 el cuadrado
void fila_y_columna(int c, int *fila, int *col) {

    if (c == 0) {
        *fila = -2;
        *col = 2;
    }
    else if (c == 1) {
        *fila = -1;
        *col = 1;
    }
    else if (c == 2) {
        *fila = -1;
        *col = 3;
    }
    else {
        *fila = (c - 3) / 5;
        *col = (c - 3) % 5;
    }
}

// no es exacta (no sigue las lineas) pero sirve pa q los perros sepan si se acercan al puma
int distancia(int a, int b) {

    int fila_a, col_a, fila_b, col_b;
    fila_y_columna(a, &fila_a, &col_a);
    fila_y_columna(b, &fila_b, &col_b);
    return abs(fila_a - fila_b) + abs(col_a - col_b);
}
