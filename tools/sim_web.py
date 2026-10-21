#!/usr/bin/env python3
"""Liga a página web de verdade (src/web) ao firmware simulado no PC.

Diferente do mock_server.py (que imita o ESP32 em Python), aqui quem responde é o próprio
firmware (main.cpp, postura, calibração, protocolo.cpp) compilado para o PC com o MPU falso.

    python3 tools/sim_web.py            # abre em http://localhost:8080
    python3 tools/sim_web.py --demo     # movimenta o "tronco" sozinho (bom para gravar a tela)

No terminal, digite comandos do simulador enquanto olha a página:
    !frente 30    !lado -15    !flex 1500    !botao    !mpu off    diag    set limite 15 ...
Só usa a biblioteca padrão do Python.
"""
import argparse
import asyncio
import base64
import hashlib
import os
import struct
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(RAIZ, "src", "web")
SIM = os.path.join(RAIZ, ".pio", "sim", "colete_sim")
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
TIPOS = {".html": "text/html; charset=utf-8", ".json": "application/manifest+json", ".svg": "image/svg+xml"}

clientes = set()


# ------------------------------------------------------------------ WebSocket mínimo (RFC 6455)

async def ws_ler(reader):
    cab = await reader.readexactly(2)
    op = cab[0] & 0x0F
    n = cab[1] & 0x7F
    if n == 126:
        n = struct.unpack(">H", await reader.readexactly(2))[0]
    elif n == 127:
        n = struct.unpack(">Q", await reader.readexactly(8))[0]
    mascara = await reader.readexactly(4) if cab[1] & 0x80 else b"\0\0\0\0"
    dados = bytearray(await reader.readexactly(n))
    for i in range(n):
        dados[i] ^= mascara[i % 4]
    return op, bytes(dados)


def ws_quadro(texto, op=0x1):
    dados = texto.encode() if isinstance(texto, str) else texto
    n = len(dados)
    if n < 126:
        cab = bytes([0x80 | op, n])
    elif n < 65536:
        cab = bytes([0x80 | op, 126]) + struct.pack(">H", n)
    else:
        cab = bytes([0x80 | op, 127]) + struct.pack(">Q", n)
    return cab + dados


async def tratar_ws(reader, writer, cabecalhos, sim):
    chave = cabecalhos.get("sec-websocket-key", "")
    aceite = base64.b64encode(hashlib.sha1((chave + GUID).encode()).digest()).decode()
    writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Accept: {aceite}\r\n\r\n").encode())
    await writer.drain()
    clientes.add(writer)
    print("[ponte] app conectado")
    enviar_sim(sim, "!ws conectar")
    try:
        while True:
            op, dados = await ws_ler(reader)
            if op == 0x8:  # close
                break
            if op == 0x9:  # ping
                writer.write(ws_quadro(dados, 0xA))
                continue
            if op == 0x1:
                texto = dados.decode("utf-8", "replace").replace("\n", " ")
                print(f"[app -> firmware] {texto}")
                enviar_sim(sim, "!ws " + texto)
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        clientes.discard(writer)
        print("[ponte] app desconectado")


# ------------------------------------------------------------------ HTTP

async def tratar_http(reader, writer, sim, so_ws):
    try:
        pedido = (await reader.readline()).decode(errors="replace").split()
        cab = {}
        while True:
            linha = (await reader.readline()).decode(errors="replace").strip()
            if not linha:
                break
            k, _, v = linha.partition(":")
            cab[k.strip().lower()] = v.strip()
        if cab.get("upgrade", "").lower() == "websocket":
            await tratar_ws(reader, writer, cab, sim)
            return
        if so_ws or len(pedido) < 2:
            writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
            return
        caminho = pedido[1].split("?")[0]
        nome = "index.html" if caminho in ("/", "/index.html") else caminho.lstrip("/")
        arquivo = os.path.join(WEB, os.path.basename(nome))
        if not os.path.isfile(arquivo):
            writer.write(b"HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n")
            return
        with open(arquivo, "rb") as f:
            corpo = f.read()
        tipo = TIPOS.get(os.path.splitext(arquivo)[1], "application/octet-stream")
        writer.write(f"HTTP/1.1 200 OK\r\nContent-Type: {tipo}\r\nContent-Length: {len(corpo)}\r\n"
                     "Cache-Control: no-cache\r\nConnection: close\r\n\r\n".encode() + corpo)
    except (ConnectionError, asyncio.IncompleteReadError):
        pass
    finally:
        try:
            await writer.drain()
            writer.close()
        except Exception:
            pass


# ------------------------------------------------------------------ simulador

def enviar_sim(sim, linha):
    try:
        sim.stdin.write((linha + "\n").encode())
        sim.stdin.flush()
    except BrokenPipeError:
        sys.exit("o simulador terminou")


async def ler_sim(sim):
    loop = asyncio.get_running_loop()
    while True:
        linha = await loop.run_in_executor(None, sim.stdout.readline)
        if not linha:
            print("[ponte] simulador terminou")
            os._exit(1)
        texto = linha.decode("utf-8", "replace").rstrip("\n")
        if texto.startswith("WS>"):
            quadro = ws_quadro(texto[3:])
            for w in list(clientes):
                try:
                    w.write(quadro)
                except Exception:
                    clientes.discard(w)
            if '"t":"data"' not in texto:
                print(f"[firmware -> app] {texto[3:]}")
        else:
            print(texto)


async def ler_terminal(sim):
    loop = asyncio.get_running_loop()
    while True:
        linha = await loop.run_in_executor(None, sys.stdin.readline)
        if not linha:
            return
        enviar_sim(sim, linha.rstrip("\n"))


async def demo(sim):
    roteiro = [
        (3, "!frente 3"), (0, "!flex 2000"), (3, "!botao"), (6, "!frente 12"), (5, "!frente 30"),
        (8, "!frente 4"), (5, "!lado 22"), (2, "!lado 0"), (5, "!flex 1550"), (7, "!flex 2000"), (6, "!frente 0"),
    ]
    await asyncio.sleep(2)
    while True:
        for espera, cmd in roteiro:
            await asyncio.sleep(espera)
            print(f"[demo] {cmd}")
            enviar_sim(sim, cmd)


async def principal(args):
    subprocess.run([os.path.join(RAIZ, "tools", "sim.sh"), "--so-compilar"], check=True)
    sim = subprocess.Popen([SIM, "--tempo-real"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    srv_http = await asyncio.start_server(lambda r, w: tratar_http(r, w, sim, False), args.host, args.porta)
    srv_ws = await asyncio.start_server(lambda r, w: tratar_http(r, w, sim, True), args.host, args.porta + 1)
    print(f"[ponte] página em http://localhost:{args.porta}  (WebSocket na {args.porta + 1})")
    print("[ponte] comandos: !frente 30  !lado -15  !flex 1500  !botao  !mpu off  diag  get  set limite 15")
    tarefas = [ler_sim(sim), ler_terminal(sim), srv_http.serve_forever(), srv_ws.serve_forever()]
    if args.demo:
        tarefas.append(demo(sim))
    await asyncio.gather(*tarefas)


if __name__ == "__main__":
    sys.stdout.reconfigure(line_buffering=True)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", type=int, default=8080)
    ap.add_argument("--host", default="0.0.0.0", help="0.0.0.0 = acessível pelo celular na mesma rede")
    ap.add_argument("--demo", action="store_true", help="movimenta o tronco sozinho")
    try:
        asyncio.run(principal(ap.parse_args()))
    except KeyboardInterrupt:
        pass
