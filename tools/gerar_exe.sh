#!/usr/bin/env bash
# Gera dist/BioSpine-Simulador.exe (Windows): o mock_server.py + a página, abrindo o navegador.
#
# O PyInstaller não compila de forma cruzada, então roda o Python do Windows dentro do Wine.
# Preparação (uma vez): apt install wine64; Python 3.12 do pacote NuGet (o instalador oficial
# é 32 bits e o wine64 não roda) copiado para C:\Python312 do prefixo; pip install pyinstaller.
# Uso: WINEPREFIX=<prefixo> tools/gerar_exe.sh
set -euo pipefail

: "${WINEPREFIX:?defina WINEPREFIX (prefixo do Wine com C:\\Python312 e PyInstaller)}"
export WINEPREFIX WINEDEBUG=-all
W=/usr/lib/wine/wine64
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
TRAB=$(mktemp -d)
trap 'rm -rf "$TRAB"' EXIT

# Wine monta / como Z:. stdin do /dev/null e saída por pipe: sem isso o Python do Wine
# falha com "init_sys_streams: Invalid handle" fora de um terminal.
z() { echo "Z:${1//\//\\}"; }
"$W" 'C:\Python312\python.exe' -m PyInstaller --noconfirm --onefile --name BioSpine-Simulador \
    --paths "$(z "$RAIZ/tools")" \
    --add-data "$(z "$RAIZ/src/web");web" \
    --distpath "$(z "$RAIZ/dist")" --workpath "$(z "$TRAB/build")" --specpath "$(z "$TRAB")" \
    "$(z "$RAIZ/tools/colete_windows.py")" </dev/null 2>&1 | grep -E "ERROR|completed successfully" || true

ls -la "$RAIZ/dist/BioSpine-Simulador.exe"
