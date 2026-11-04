CC = gcc
CFLAGS = -Wall -Wextra -std=c17
LIBS = -lpthread
ARCHIVOS = torneo.c mesa.c tablero.c lector_config.c


torneo: $(ARCHIVOS) torneo.h
	$(CC) $(CFLAGS) -o torneo $(ARCHIVOS) $(LIBS)

# para buscar fugas de memoria y errores de memoria
torneo_asan: $(ARCHIVOS) torneo.h
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -o torneo_asan $(ARCHIVOS) $(LIBS)


# para buscar condiciones de carrera
torneo_tsan: $(ARCHIVOS) torneo.h
	$(CC) $(CFLAGS) -g -fsanitize=thread -o torneo_tsan $(ARCHIVOS) $(LIBS)

clean:
	rm -f torneo torneo_asan torneo_tsan
	rm -rf logs
