#include "torneo.h"   // tiene que ir primero porque adentro esta el _POSIX_C_SOURCE
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

// lee el config.txt, que es opcional
// cada linea es "clave = valor" y lo que va despues de un # no se lee
// lo que no aparezca en el archivo se queda con el valor por defecto


void config_por_defecto(Config *c) {

    c->retardo_min_ms = 50;    // los del enunciado
    c->retardo_max_ms = 300;
    c->semilla = 0;
    c->semilla_dada = 0;

    // el puma parte en la punta del trono y los perros en las 12 casillas de abajo
    c->pos_puma = 0;
    int perros[NUM_PERROS] = {13, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    for (int i = 0; i < NUM_PERROS; i++) {
        c->pos_perros[i] = perros[i];
    }

    c->llegada_max_ms = 1000;
    c->prob_falla = 0;
    c->max_movimientos = 500;
    c->log_detalle = 2;
    c->tablero_en_consola = 0;
}


// le saca los espacios del principio y del final
char *recortar(char *s) {

    while (isspace((unsigned char) *s)) {
        s++;
    }
    char *fin = s + strlen(s);
    while (fin > s && isspace((unsigned char) fin[-1])) {
        fin--;
    }
    *fin = '\0';
    return s;
}


// use strtol y no atoi porque atoi("hola") devuelve 0 y no avisa que esta malo.
// devuelve 0 si el numero esta bien y -1 si no
int leer_numero(char *texto, long min, long max, long *salida) {

    char *fin;
    errno = 0;
    long valor = strtol(texto, &fin, 10);

    if (errno != 0 || fin == texto || *recortar(fin) != '\0') {
        return -1;   // no era un numero, o tenia letras despues
    }
    if (valor < min || valor > max) {
        return -1;
    }
    *salida = valor;
    return 0;
}


// "perros = 13 17 18 ..." tienen que venir los 12, ni mas ni menos
int leer_perros(char *texto, Config *c) {

    int cuantos = 0;
    char *pedazo = strtok(texto, " ,\t");

    while (pedazo != NULL) {

        long valor;
        if (cuantos >= NUM_PERROS || leer_numero(pedazo, 0, NUM_CASILLAS - 1, &valor) != 0) {
            return -1;
        }
        c->pos_perros[cuantos] = (int) valor;
        cuantos++;
        pedazo = strtok(NULL, " ,\t");
    }

    if (cuantos != NUM_PERROS) {
        return -1;
    }
    return 0;
}

// que no haya 2 fichas en la misma casilla
int revisar_disposicion(Config *c) {

    int usada[NUM_CASILLAS] = {0};
    usada[c->pos_puma] = 1;

    for (int i = 0; i < NUM_PERROS; i++) {
        if (usada[c->pos_perros[i]] == 1) {
            printf("config: la casilla %d esta repetida en la disposicion inicial\n", c->pos_perros[i]);
            return -1;
        }
        usada[c->pos_perros[i]] = 1;
    }
    return 0;
}



// devuelve -1 si algo esta malo. muestra todos los errores y no solo el primero
int leer_config(char *archivo, Config *c) {

    FILE *f = fopen(archivo, "r");
    if (f == NULL) {
        perror(archivo);
        return -1;
    }

    char linea[512];
    int num_linea = 0;
    int errores = 0;

    while (fgets(linea, sizeof(linea), f) != NULL) {

        num_linea++;

        char *comentario = strchr(linea, '#');
        if (comentario != NULL) {
            *comentario = '\0';   // corto la linea donde empieza el comentario
        }
        char *texto = recortar(linea);
        if (texto[0] == '\0') {
            continue;   // linea vacia
        }

        char *igual = strchr(texto, '=');
        if (igual == NULL) {
            printf("config linea %d: falta el '='\n", num_linea);
            errores++;
            continue;
        }

        *igual = '\0';   // asi queda separado en dos: lo de antes y lo de despues del =
        char *clave = recortar(texto);
        char *valor = recortar(igual + 1);
        long numero = 0;
        int mal = 0;

        if (strcmp(clave, "retardo_min_ms") == 0) {
            mal = leer_numero(valor, 0, 60000, &numero);
            c->retardo_min_ms = (int) numero;
        }
        else if (strcmp(clave, "retardo_max_ms") == 0) {
            mal = leer_numero(valor, 0, 60000, &numero);
            c->retardo_max_ms = (int) numero;
        }
        else if (strcmp(clave, "semilla") == 0) {
            mal = leer_numero(valor, 0, 2147483647L, &numero);
            c->semilla = (unsigned int) numero;
            c->semilla_dada = 1;
        }
        else if (strcmp(clave, "puma") == 0) {
            mal = leer_numero(valor, 0, NUM_CASILLAS - 1, &numero);
            c->pos_puma = (int) numero;
        }
        else if (strcmp(clave, "perros") == 0) {
            mal = leer_perros(valor, c);
        }
        else if (strcmp(clave, "llegada_max_ms") == 0) {
            mal = leer_numero(valor, 0, 600000, &numero);
            c->llegada_max_ms = (int) numero;
        }
        else if (strcmp(clave, "prob_falla") == 0) {
            mal = leer_numero(valor, 0, 100, &numero);
            c->prob_falla = (int) numero;
        }
        else if (strcmp(clave, "max_movimientos") == 0) {
            mal = leer_numero(valor, 1, 1000000, &numero);
            c->max_movimientos = (int) numero;
        }
        else if (strcmp(clave, "log_detalle") == 0) {
            mal = leer_numero(valor, 0, 2, &numero);
            c->log_detalle = (int) numero;
        }
        else if (strcmp(clave, "tablero_en_consola") == 0) {
            mal = leer_numero(valor, 0, 1, &numero);
            c->tablero_en_consola = (int) numero;
        }
        else {
            // una opcion que no existe no la cuento como error, solo aviso
            printf("config linea %d: opcion desconocida '%s' (se ignora)\n", num_linea, clave);
            continue;
        }

        if (mal != 0) {
            printf("config linea %d: valor no valido para '%s'\n", num_linea, clave);
            errores++;
        }
    }
    fclose(f);

    if (c->retardo_min_ms > c->retardo_max_ms) {
        printf("config: retardo_min_ms no puede ser mayor que retardo_max_ms\n");
        errores++;
    }
    if (errores == 0 && revisar_disposicion(c) != 0) {
        errores++;
    }

    if (errores > 0) {
        return -1;
    }
    return 0;
}
