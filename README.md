**Tarea 2: Torneo de Komikan Concurrente**  
Integrantes: Valentín Aravena y Gustavo Femenias  
   
 Sistemas Operativos, UDP  
**Cómo compilar y ejecutar**  
```  
make  
 ./torneo N M [config.txt]  
   
```  
Sin make, el comando es:  
```  
gcc -Wall -Wextra -std=c17 -o torneo torneo.c mesa.c tablero.c lector_config.c -lpthread  
   
```  
- N: cuántos jugadores llegan al servidor.  
- M: cuántas mesas pueden estar jugando al mismo tiempo, como máximo.  
- config.txt: es opcional. Si no se pone, se usan los valores por defecto (los mismos que trae el config.txt de ejemplo).  
Algunos ejemplos:  
```  
./torneo 10 3  
 ./torneo 2 1 pruebas/demo.txt      # una sola partida, dibujando el tablero en la terminal  
 ./torneo 20 4 pruebas/fallas.txt    # en todas las partidas se cae un jugador  
 bash pruebas/correr_pruebas.sh      # corre todas las pruebas de una vez  
   
```  
Cada partida deja su registro en la carpeta logs (partida_001.log, partida_002.log, etc.), con cada movimiento y cómo quedó el tablero. Con Ctrl+C el torneo se cierra ordenado y el programa termina con código 130.  
**Archivos**  
- torneo.h: las constantes, las structs (Config, Ficha y Mesa) y las funciones que se usan entre archivos.  
- tablero.c: la forma del tablero (vecinos y saltos), las reglas básicas y el dibujo en texto.  
- mesa.c: el juez de cada mesa y los hilos de las fichas (el puma y los 12 perros).  
- torneo.c: el main, el servidor que arma las parejas, los clientes y el Ctrl+C.  
- lector_config.c: lee el config.txt.  
- config.txt: configuración de ejemplo, con cada opción explicada.  
- Makefile: atajo para compilar.  
- pruebas/: configuraciones para probar casos especiales y el script correr_pruebas.sh.  
- informe.pdf: el informe de 2 páginas.  
**Cómo funciona**  
Todo corre en un solo proceso, con hilos. Hay un hilo portero (Ctrl+C), un hilo servidor, N hilos cliente y, por cada mesa que está jugando, un hilo juez y 13 hilos ficha.  
1. Cada cliente duerme un tiempo al azar (entre 0 y llegada_max_ms), llega al servidor y se anota en la sala de espera.  
2. El servidor duerme hasta que hay 2 jugadores en la sala. El primero que llegó juega con el puma y el segundo con los perros.  
3. Para conseguir mesa, el servidor hace sem_wait en un semáforo que parte en M. Si las M mesas están ocupadas, el servidor (y la pareja) se quedan dormidos hasta que un juez termine y haga sem_post. Así nunca hay más de M mesas jugando.  
4. El juez de la mesa arma el tablero, crea el mutex y la variable de condición, lanza las 13 fichas y duerme hasta que la partida termine.  
5. Cada ficha repite: mirar el tablero, pensar entre 50 y 300 ms, e intentar moverse. El juez revisa el movimiento con el tablero como está en ese momento y lo acepta o lo rechaza.  
6. Cuando termina, el juez espera a las 13 fichas (pthread_join), destruye el mutex y la variable de condición, cierra el log y devuelve la mesa (sem_post).  
**El tablero**  
Lo sacamos del dibujo del enunciado. Son 28 casillas: el cuadrado de 5x5 y un trono de 3 casillas arriba.  
```  
        0  
       / | \  
     1---+---2  
   / |   |   | \  
  3--4---5---6--7  
  |  |   |   |  |  
  8--9--10--11--12  
  |  |   |   |  |  
 13-14--15--16--17  
  |  |   |   |  |  
 18-19--20--21--22  
  |  |   |   |  |  
 23-24--25--26--27  
   
```  
- El cuadrado no tiene diagonales, porque el dibujo no las tiene. Las únicas diagonales están en el trono.  
- El + no es casilla: la línea que baja de la T llega directo a la 5.  
- En el código el tablero se anota como líneas rectas. Dos casillas seguidas en una línea son vecinas, y tres seguidas permiten un salto del puma (puma, perro en el medio y casilla vacía al otro lado).  
En los logs, P es el puma, o es un perro y . es una casilla vacía.  
**Sincronización**  
- El tablero: un mutex por mesa (candado). Solo quien lo tiene puede mirar o cambiar el tablero.  
- El juez espera a que pase algo: una variable de condición por mesa (aviso). Una ficha lo despierta cuando alguien gana o cuando detecta que un jugador se cayó.  
- Un jugador que se cae con el tablero tomado: el mutex es robusto. El siguiente que lo pide recibe EOWNERDEAD en vez de quedarse esperando para siempre, lo deja usable de nuevo con pthread_mutex_consistent y avisa al juez.  
- Máximo M mesas: un semáforo (mesas_libres) que parte en M.  
- La sala de espera: un mutex y una variable de condición del servidor. Los clientes se anotan y despiertan al servidor.  
- Ctrl+C: está bloqueado en todos los hilos menos en el portero, que lo espera con sigwait.  
- Avisar a todos que se cierra: la variable cerrando, con su propio mutex. Los jueces la revisan cada 200 ms.  
**Funciones**  
**tablero.c**  
- preparar_tablero: arma las listas de vecinos y saltos a partir de las líneas del tablero. Se llama una vez antes de crear los hilos y después solo se lee, por eso no necesita mutex.  
- agregar_vecino y agregar_salto: las usa preparar_tablero para ir llenando las listas.  
- es_perro, son_vecinos y buscar_salto: reglas básicas del tablero.  
- puma_puede_moverse: dice si el puma tiene algún movimiento. Si no tiene, ganan los perros.  
- fila_y_columna y distancia: dan la distancia aproximada entre dos casillas, para que los perros sepan si se están acercando al puma.  
- dibujar_tablero: deja el dibujo del tablero en un texto, para mandarlo al log o a la pantalla.  
- imprimir_mapa: muestra los números de cada casilla (sale cuando el programa se usa mal).  
**mesa.c**  
- nombre_ficha, anotar y mostrar_tablero: escriben en el log y en la terminal.  
- revisar_dueno_muerto: si el lock devolvió EOWNERDEAD, deja el mutex usable de nuevo, ve quién murió con él y declara el abandono.  
- tomar_candado y soltar_candado: toman y sueltan el mutex del tablero, y anotan quién lo tiene.  
- juez_revisar_movimiento: las reglas. Con el candado tomado revisa que la ficha siga viva, que el destino esté vacío y que sea una vecina (o un salto sobre un perro, si es el puma). Si vale lo aplica, si no lo rechaza.  
- revisar_victoria: el puma con 6 capturas, el puma sin movimientos, o el límite de movimientos. Si terminó, despierta al juez.  
- ficha: el hilo de cada ficha. Toma el candado y elige, lo suelta y piensa, lo vuelve a tomar e intenta moverse.  
- quedaria_expuesto: dice si un perro quedaría a tiro del puma en una casilla.  
- elegir_movimiento: el puma prefiere comerse un perro. Los perros prefieren casillas donde no se los puedan comer y que los acerquen al puma.  
- preparar_partida: crea el mutex robusto y la variable de condición, pone las fichas, decide si en esta partida se va a caer un jugador y abre el log.  
- juez: el hilo de cada mesa. Prepara la partida, lanza las fichas, duerme hasta que termine, espera a las fichas, anuncia el resultado, destruye el mutex y la variable de condición y devuelve la mesa.  
**torneo.c**  
- main: revisa los argumentos, lee el config, bloquea Ctrl+C antes de crear hilos, crea el portero, el servidor y los N clientes, los espera, muestra el resumen y libera todo.  
- cliente: el hilo de cada jugador. Duerme un tiempo al azar, se anota en la sala y despierta al servidor.  
- servidor: espera a que haya 2 en la sala, pide una mesa con sem_wait, busca cuál quedó libre y lanza a su juez. Al final espera a todos los jueces.  
- devolver_mesa: la llama el juez al terminar. Anota el resultado, deja la mesa libre y hace sem_post.  
- portero: espera Ctrl+C con sigwait. Cuando llega, marca el cierre y despierta al servidor.  
- hay_que_cerrar y pedir_cierre: leen y cambian la variable cerrando con su mutex.  
- dormir_ms, dormir_atento_ms y ahora_ms: para dormir y medir el tiempo. dormir_atento_ms duerme de a 50 ms para despertar rápido si llega Ctrl+C.  
- nombre_resultado, leer_argumento e imprimir_resumen: ayudan al main.  
**lector_config.c**  
- config_por_defecto: pone los valores por defecto.  
- leer_config: lee el archivo línea por línea (clave = valor), se salta los comentarios y avisa los errores.  
- recortar, leer_numero, leer_perros y revisar_disposicion: ayudan a leer y revisar cada valor (que sea número, que esté en rango, que no haya dos fichas en la misma casilla).  
**Archivo de configuración**  
- retardo_min_ms y retardo_max_ms: cuánto piensa una ficha antes de moverse (50 y 300 por defecto).  
- semilla: para los números al azar. Si no se pone, se usa la hora y se imprime al inicio para poder repetirla.  
- puma y perros: dónde parte cada ficha. Por defecto el puma en la punta del trono (0) y los perros en las 12 casillas de abajo.  
- llegada_max_ms: los jugadores llegan entre 0 y este tiempo (1000 por defecto).  
- prob_falla: porcentaje de partidas en que se cae un jugador (0 por defecto).  
- max_movimientos: si una partida llega a esto es empate (500 por defecto).  
- log_detalle: 0 sin logs, 1 solo lo que pasa, 2 lo que pasa más el tablero (2 por defecto).  
- tablero_en_consola: 1 para dibujar el tablero en la terminal (0 por defecto).  
**Decisiones de diseño**  
- Hilos y no procesos: el enunciado deja elegir. Con hilos el tablero ya es memoria compartida (todos los hilos ven las mismas variables), así que no hace falta shm_open ni mmap.  
- Un candado por tablero y no uno por casilla: un movimiento toca 2 casillas y una captura toca 3. Con un candado por casilla habría que tomar varios a la vez y podría haber deadlock. El candado se tiene unos pocos microsegundos y cada ficha piensa entre 50 y 300 ms, así que casi nunca hay que esperarlo.  
- Mirar, pensar, intentar: la ficha no tiene el candado mientras piensa. Cuando vuelve el tablero pudo cambiar, por eso el juez revisa con el tablero de ese momento. Si otra ficha llegó antes a la casilla, el movimiento se rechaza. Así se cumple que la primera ficha que reclama la casilla se la queda.  
- Cómo se cae un jugador: el enunciado no lo dice, así que lo simulamos con una probabilidad (prob_falla). La ficha hace pthread_exit con el candado tomado, que es el peor caso. Una caída de verdad (por ejemplo un segmentation fault) mataría a todo el programa, porque los hilos comparten el proceso.  
- Ctrl+C con un hilo portero: en una función que se llama directo al llegar la señal (como en la tarea 1) no se pueden usar mutex sin arriesgarse, y en un hilo normal sí.  
- Semáforo sin nombre (sem_init): no quedan semáforos guardados en el sistema si el programa se cae.  
- Las mesas se reusan: hay un arreglo de M mesas. Antes de reusar una, el servidor espera al juez anterior con pthread_join.  
- Reglas que tuvimos que decidir: el puma hace un solo salto por movimiento, y si una partida llega a max_movimientos es empate (como las fichas se mueven al azar, una partida podría no terminar nunca).  
- Semilla: con la misma semilla se repiten las llegadas, los tiempos y las decisiones de cada ficha. Lo que no se puede controlar es el orden en que el sistema operativo despierta a los hilos, así que las partidas salen muy parecidas pero no siempre iguales (por ejemplo la misma partida terminó con 141, 142, 144 o 145 movimientos).  
- Orden de los candados: nunca se tienen a la vez el candado de una mesa y el del servidor, y con el candado de cerrando tomado no se pide ningún otro. Así no puede haber deadlock entre ellos.  
**Detalles técnicos**  
- torneo.h va primero en cada .c: adentro está el #define \_POSIX_C_SOURCE, que tiene que ir antes de cualquier #include del sistema. Si no, rand_r, sigwait y clock_gettime no compilan con -std=c17.  
- rand_r y no rand: rand guarda su estado escondido y no es seguro con varios hilos. Cada ficha y cada cliente tiene su propia semilla.  
- while y no if en las esperas (pthread_cond_wait y pthread_cond_timedwait): un hilo puede despertar sin que haya pasado lo que esperaba.  
- El juez despierta solo cada 200 ms (pthread_cond_timedwait) para darse cuenta del Ctrl+C.  
- Ctrl+C se bloquea antes de crear los hilos, porque los hilos nuevos copian las señales bloqueadas del que los crea.  
- Cada cliente recibe su propia variable con su número (ids\[i\]). Si se le pasara &i, todos verían la misma variable.  
**Pruebas que hicimos**  
El script pruebas/correr_pruebas.sh corre 17 pruebas y todas pasan:  
- 40 jugadores y 5 mesas: 20 partidas, y nunca más de 5 mesas a la vez.  
- Número impar de jugadores (5): 2 partidas y 1 jugador sin rival.  
- 12 jugadores y 1 mesa: 6 partidas, una detrás de otra.  
- Puma encerrado desde el inicio (pruebas/puma_encerrado.txt): ganan los perros sin moverse.  
- Todos los jugadores se caen (pruebas/fallas.txt): las 10 partidas terminan por abandono sin quedarse pegadas.  
- Ctrl+C a la mitad: se cierra ordenado y termina con código 130.  
- Argumentos y configuraciones malas: mensaje de error y código 1.  
- Estrés con 2000 jugadores y 100 mesas: 1000 partidas.  
- valgrind (normal, con fallas y con Ctrl+C): 0 fugas de memoria.  
Con ayuda de la IA también se corrieron estas pruebas:  
- 10.000 jugadores y 200 mesas (pruebas/estres.txt): 5000 partidas en unos 20 segundos.  
- ThreadSanitizer (make torneo_tsan): 0 avisos de condiciones de carrera.  
- AddressSanitizer (make torneo_asan): 0 errores.  
- Espera sin gastar CPU: con 6 jugadores y 1 mesa el programa dura 15 segundos pero usa menos de 0,1 segundos de CPU.  
Algo que hay que saber: con prob_falla mayor que 0, ThreadSanitizer marca 3 avisos. Son todos el mismo caso: no entiende cuando un mutex robusto cambia de dueño porque el anterior se murió. Los accesos que marca ocurren con el candado tomado.  
**Declaración de uso de Inteligencia Artificial**  
Herramientas utilizadas: Gemini.  
Para qué se usó:  
1. Para entender el enunciado y los conceptos: qué primitiva de sincronización sirve para cada requisito, cómo leer el tablero del dibujo y los riesgos de deadlock, livelock y starvation.  
2. Antes del programa completo se pidió un ejemplo corto (que no está en la entrega): 12 hilos que quieren la misma casilla, sin mutex y con mutex. Sirvió para ver la condición de carrera y cómo la arregla el mutex.  
3. El código entregado (torneo.h, tablero.c, mesa.c, torneo.c y lector_config.c), el Makefile, el config.txt, los archivos de pruebas/, el informe y este README fueron generados con asistencia de Gemini.  
4. Las pruebas las propuso y las ejecutó la IA, y después las corrimos nosotros con el script.  
Nos repartimos el trabajo así: Valentín se encargó de tablero.c y mesa.c (el tablero, el juez y las fichas) y Gustavo de torneo.c y lector_config.c (el servidor, el semáforo de mesas, el Ctrl+C y el archivo de configuración). Cada uno subió su parte con sus propios commits.  
Como no tenemos mucha experiencia programando en C con hilos, nos apoyamos bastante en la IA para aprender. Hay partes que la IA tuvo que resolver porque solos no habríamos podido. Esto no quiere decir que le pedimos el trabajo, sino que nos diera una solución para analizarla, entenderla y decidir si incluirla o no.  
Qué hicimos nosotros:  
- Decidimos cómo repartirnos el trabajo y trabajamos por partes, para entender cada una antes de juntarlas.  
- Revisamos el código, corrimos las pruebas en nuestros computadores, probamos el Ctrl+C y las fallas, y leímos y entendimos la gran mayoría de las funciones (no nos las sabemos de memoria).  
- Cada uno preparó la explicación de su parte y después nos la explicamos entre los dos.  
Revisamos el código entregado y nos hacemos responsables de su contenido. Podemos explicar su funcionamiento en una defensa oral.  
