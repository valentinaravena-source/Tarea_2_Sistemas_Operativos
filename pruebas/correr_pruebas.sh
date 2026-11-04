#!/bin/bash
# Corre las pruebas de la Tarea 2 y dice si cada una paso.
# Uso (desde la carpeta principal):  bash pruebas/correr_pruebas.sh

cd "$(dirname "$0")/.." || exit 1
make -s torneo || exit 1

pasaron=0
fallaron=0

revisar() {
    # $1 = nombre de la prueba, $2 = 0 si paso
    if [ "$2" -eq 0 ]; then
        echo "  OK    $1"
        pasaron=$((pasaron + 1))
    else
        echo "  FALLO $1"
        fallaron=$((fallaron + 1))
    fi
}

salida=$(mktemp)

echo "== Pruebas basicas"
./torneo 40 5 pruebas/rapido.txt > "$salida"
[ $? -eq 0 ] && grep -q "Partidas jugadas: 20" "$salida"
revisar "40 jugadores, 5 mesas: se juegan 20 partidas" $?

grep -q "Mesas activas al mismo tiempo (maximo): 5 de 5" "$salida"
revisar "nunca hay mas de M mesas activas" $?

./torneo 5 2 pruebas/rapido.txt > "$salida"
grep -q "se quedo sin rival" "$salida" && grep -q "Partidas jugadas: 2" "$salida"
revisar "N impar: un jugador queda sin rival" $?

./torneo 12 1 pruebas/rapido.txt > "$salida"
grep -q "Mesas activas al mismo tiempo (maximo): 1 de 1" "$salida" && grep -q "Partidas jugadas: 6" "$salida"
revisar "M = 1: las parejas esperan su turno" $?

./torneo 2 1 pruebas/puma_encerrado.txt > "$salida"
grep -q "encerraron al puma (0 movimientos" "$salida"
revisar "puma encerrado desde el inicio: ganan los perros" $?

echo "== Resiliencia"
./torneo 20 4 pruebas/fallas.txt > "$salida"
[ $? -eq 0 ] && [ "$(grep -c 'fin de la partida.*por abandono' "$salida")" -eq 10 ]
revisar "jugador que se cae: las 10 partidas terminan por abandono" $?

./torneo 30 3 > "$salida" &
pid=$!
sleep 2
kill -INT $pid
wait $pid
codigo=$?
[ $codigo -eq 130 ] && grep -q "llego Ctrl+C" "$salida" && grep -q "RESUMEN" "$salida"
revisar "Ctrl+C: cierra ordenado y sale con codigo 130" $?

echo "== Errores de entrada"
./torneo > /dev/null; [ $? -eq 1 ]
revisar "sin argumentos" $?
./torneo 0 3 > /dev/null; [ $? -eq 1 ]
revisar "N = 0" $?
./torneo 4 abc > /dev/null; [ $? -eq 1 ]
revisar "M no es numero" $?
./torneo 4 2 no_existe.txt > /dev/null 2>&1; [ $? -eq 1 ]
revisar "config que no existe" $?
./torneo 4 2 pruebas/config_malo.txt > /dev/null; [ $? -eq 1 ]
revisar "config con valores malos" $?
./torneo 4 2 pruebas/casilla_repetida.txt > /dev/null; [ $? -eq 1 ]
revisar "config con casilla repetida" $?

echo "== Estres (2000 jugadores, 100 mesas)"
./torneo 2000 100 pruebas/estres.txt > "$salida"
[ $? -eq 0 ] && grep -q "Partidas jugadas: 1000" "$salida"
revisar "2000 jugadores: se juegan 1000 partidas" $?

if command -v valgrind > /dev/null; then
    echo "== Fugas de memoria (valgrind)"
    valgrind --leak-check=full --error-exitcode=9 ./torneo 20 4 pruebas/rapido.txt > /dev/null 2> "$salida"
    grep -q "All heap blocks were freed" "$salida"
    revisar "valgrind: cero fugas (caso normal)" $?

    valgrind --leak-check=full --error-exitcode=9 ./torneo 20 4 pruebas/fallas.txt > /dev/null 2> "$salida"
    grep -q "All heap blocks were freed" "$salida"
    revisar "valgrind: cero fugas (con jugadores que se caen)" $?

    valgrind --leak-check=full ./torneo 30 3 > /dev/null 2> "$salida" &
    pid=$!
    sleep 4
    kill -INT $pid
    wait $pid
    grep -q "All heap blocks were freed" "$salida"
    revisar "valgrind: cero fugas (con Ctrl+C)" $?
else
    echo "(valgrind no esta instalado: sudo apt install valgrind)"
fi

rm -f "$salida"
echo
echo "Pasaron: $pasaron   Fallaron: $fallaron"
