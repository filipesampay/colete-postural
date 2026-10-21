#!/usr/bin/env bash
# Compila e roda o simulador nativo do firmware (sem placa).
#   tools/sim.sh                      roteiro padrão (tools/roteiro.txt)
#   tools/sim.sh meu_roteiro.txt      outro roteiro
#   tools/sim.sh -                    interativo (digite os comandos)
#   tools/sim.sh --so-compilar        só compila (.pio/sim/colete_sim)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/sim
g++ -std=c++17 -O1 -Wall -Wno-unused-parameter -DSIMULADOR -Isim -Isrc -I.pio/libdeps/esp32dev/ArduinoJson/src \
    src/main.cpp src/console.cpp src/sensors.cpp src/motor.cpp src/settings.cpp \
    src/mpu.cpp src/tilt.cpp src/posture.cpp src/protocolo.cpp sim/sim_main.cpp \
    -o .pio/sim/colete_sim
[ "${1:-}" = "--so-compilar" ] && exit 0
roteiro="${1:-tools/roteiro.txt}"
if [ "$roteiro" = "-" ]; then
  exec .pio/sim/colete_sim
fi
exec .pio/sim/colete_sim < "$roteiro"
