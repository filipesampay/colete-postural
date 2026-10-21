#!/usr/bin/env bash
# Compila e roda o simulador nativo do firmware (sem placa).
#   tools/sim.sh                      roteiro padrão (tools/roteiro.txt)
#   tools/sim.sh meu_roteiro.txt      outro roteiro
#   tools/sim.sh -                    interativo (digite os comandos)
#   tools/sim.sh --so-compilar        só compila (.pio/sim/colete_sim)
#
# No modo "-" o simulador lê a entrada até o fim (Ctrl+D) antes de executar. Para mandar
# comandos e ver a resposta na hora, use python3 tools/sim_web.py (modo --tempo-real).
#
# O que entra na compilação: os .cpp do firmware de verdade + sim/sim_main.cpp. Ficam de fora
# src/web.cpp (Wi-Fi do ESP32, trocado pelas funções web_* de sim_main.cpp).
#   -DSIMULADOR   main.cpp roda tudo numa linha só (sem tarefa FreeRTOS nem trava)
#   -Isim antes de -Isrc   Arduino.h, Wire.h e Preferences.h falsos no lugar dos do core
#   ArduinoJson   vem de .pio/libdeps (baixado pelo PlatformIO no primeiro "pio run")
# Documentação: Documentação/09-simulador-testes-ferramentas.md
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/sim
g++ -std=c++17 -O1 -Wall -Wno-unused-parameter -DSIMULADOR -Isim -Isrc -I.pio/libdeps/esp32dev/ArduinoJson/src \
    src/main.cpp src/console.cpp src/sensors.cpp src/motor.cpp src/settings.cpp \
    src/mpu.cpp src/tilt.cpp src/posture.cpp src/protocolo.cpp sim/sim_main.cpp \
    -o .pio/sim/colete_sim
# usado pelo tools/sim_web.py, que depois abre o binário em --tempo-real
[ "${1:-}" = "--so-compilar" ] && exit 0
roteiro="${1:-tools/roteiro.txt}"
if [ "$roteiro" = "-" ]; then
  exec .pio/sim/colete_sim
fi
exec .pio/sim/colete_sim < "$roteiro"
