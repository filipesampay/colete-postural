#!/usr/bin/env python3
"""Ponto de entrada do .exe para Windows: sobe o tools/mock_server.py e abre o navegador.

Gerado por tools/gerar_exe.sh (PyInstaller dentro do Wine). Os arquivos de src/web/ vão
embutidos no .exe; o PyInstaller os extrai numa pasta temporária (sys._MEIPASS), então o
WEB_DIR do mock precisa apontar para lá. Aceita as mesmas opções do mock (--flex2 etc.).
"""

import asyncio
import os
import sys
import threading
import webbrowser

import mock_server

if getattr(sys, "frozen", False):
    mock_server.WEB_DIR = os.path.join(sys._MEIPASS, "web")


def abrir_navegador():
    porta = 8080
    if "--port" in sys.argv:
        porta = int(sys.argv[sys.argv.index("--port") + 1])
    webbrowser.open("http://localhost:%d" % porta)


if __name__ == "__main__":
    print("Simulador BioSpine. Feche esta janela para encerrar.")
    # 1 s dá tempo de o servidor começar a escutar antes de o navegador pedir a página.
    threading.Timer(1.0, abrir_navegador).start()
    try:
        asyncio.run(mock_server.main())
    except KeyboardInterrupt:
        pass
    except OSError as e:
        # Porta ocupada: provavelmente o simulador já está aberto em outra janela.
        print("Erro ao abrir a porta:", e)
        input("Enter para sair...")
