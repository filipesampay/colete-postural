#!/usr/bin/env bash
# Compila e gera dist/colete-postural.bin: imagem única (bootloader + partições + app),
# gravável no endereço 0x0 por qualquer ferramenta — inclusive pelo navegador em
# https://espressif.github.io/esptool-js/ (Chrome/Edge), sem instalar nada.
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${PIO:-$HOME/.local/bin/pio}"
"$PIO" run -e esp32dev
B=.pio/build/esp32dev
FW="$HOME/.platformio/packages/framework-arduinoespressif32"
mkdir -p dist
"$HOME/.platformio/penv/bin/python" "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32 merge_bin \
  -o dist/colete-postural.bin --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" \
  0xe000 "$FW/tools/partitions/boot_app0.bin" 0x10000 "$B/firmware.bin"
cp "$B/firmware.bin" dist/firmware-app-0x10000.bin
ls -la dist
echo "Gravar: esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 dist/colete-postural.bin"
