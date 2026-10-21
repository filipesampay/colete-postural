#!/usr/bin/env python3
"""Gera os PDFs da documentação a partir dos .md.

Os .md são a fonte (texto, editável junto com o código); os PDFs são a saída para ler, imprimir e entregar.
Depois de editar qualquer .md desta pasta, gere de novo:

    python3 Documentação/gerar_pdf.py               # PDF completo + um PDF por documento
    python3 Documentação/gerar_pdf.py --completo    # só o PDF completo
    python3 Documentação/gerar_pdf.py 03-algoritmo-inclinacao.md referencia/mpu.md   # só esses (não refaz o completo)

Saída: Documentação/pdf/00-documentacao-completa.pdf (capa, sumário com páginas, links entre documentos)
e Documentação/pdf/<documento>.pdf (um por .md, no mesmo caminho do .md).

Precisa de Node.js e de um Chrome/Chromium instalado (outro caminho: CHROME=/caminho/do/navegador).
Na primeira vez instala as dependências em Documentação/ferramentas/node_modules (versões fixas no
package-lock.json). O trabalho todo é feito por Documentação/ferramentas/gerar_pdf.mjs.
Só usa a biblioteca padrão do Python.
"""
import os
import shutil
import subprocess
import sys

DOC = os.path.dirname(os.path.abspath(__file__))
FERRAMENTAS = os.path.join(DOC, "ferramentas")


def instalar_dependencias():
    """Instala as dependências do Node na primeira vez (ou se o package-lock.json mudou)."""
    modulos = os.path.join(FERRAMENTAS, "node_modules")
    lock = os.path.join(FERRAMENTAS, "package-lock.json")
    marca = os.path.join(modulos, ".package-lock.json")  # gravado pelo npm a cada instalação
    if os.path.exists(marca) and os.path.getmtime(marca) >= os.path.getmtime(lock):
        return
    if not shutil.which("npm"):
        sys.exit("npm não encontrado: instale o Node.js (https://nodejs.org)")
    print("instalando dependências em Documentação/ferramentas/node_modules ...", flush=True)
    subprocess.run(["npm", "ci", "--no-audit", "--no-fund"], cwd=FERRAMENTAS, check=True)


def main():
    if not shutil.which("node"):
        sys.exit("node não encontrado: instale o Node.js (https://nodejs.org)")
    instalar_dependencias()
    script = os.path.join(FERRAMENTAS, "gerar_pdf.mjs")
    sys.exit(subprocess.run(["node", script] + sys.argv[1:]).returncode)


if __name__ == "__main__":
    main()
