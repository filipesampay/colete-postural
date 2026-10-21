#!/usr/bin/env python3
"""Simulador do colete para desenvolver a página sem a placa (só stdlib).

    python3 colete-postural/tools/mock_server.py            # abrir http://localhost:8080
    python3 colete-postural/tools/mock_server.py --flex2 --fsr --falhas

HTTP na 8080 serve colete-postural/src/web/. WebSocket na 8081 fala o mesmo
protocolo do firmware (WEB_SPEC.md): dados a 10 Hz, config, eventos e comandos.

Opções:
  --flex2    simula o 2º sensor flex (GPIO35) ligado (o modo dele começa em "off", ligue nos Ajustes)
  --fsr      simula o FSR (GPIO32) ligado
  --sem-flex simula o flex 1 ausente
  --falhas   a cada ~20 s um sensor "solta" por alguns segundos (testa o 3D)
  --port N   porta HTTP (o WS fica em N+1)
"""

import argparse
import asyncio
import base64
import hashlib
import json
import math
import mimetypes
import os
import random
import struct
import time

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "web")
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

ADC_MIN, ADC_MAX = 100, 4000
VERSAO = "1.0.0-mock"


def padrao():
    return {"limiteGraus": 20.0, "histerese": 3.0, "tempoMs": 3000, "limiteFlex": 15.0,
            "intensidade": 100, "flexModo": "auto", "flex2Modo": "off"}


def prender(v, mn, mx):
    return mn if v != v else max(mn, min(mx, v))


def validar(c):
    c["limiteGraus"] = prender(float(c["limiteGraus"]), 2.0, 60.0)
    c["limiteFlex"] = prender(float(c["limiteFlex"]), 2.0, 90.0)
    c["histerese"] = prender(float(c["histerese"]), 0.0, min(c["limiteGraus"], c["limiteFlex"]) - 1.0)
    c["tempoMs"] = int(prender(float(c["tempoMs"]), 0, 60000))
    c["intensidade"] = int(prender(float(c["intensidade"]), 0, 100))
    for k in ("flexModo", "flex2Modo"):
        if c[k] not in ("auto", "on", "off"):
            c[k] = "auto"
    return c


class Colete:
    """Mesma lógica de main.cpp/posture.cpp, com sensores inventados."""

    def __init__(self, args):
        self.args = args
        self.t0 = time.monotonic()
        self.cfg = padrao()
        self.calibrado = False
        self.calib_ate = 0.0
        self.ref = (0.0, 0.0)          # pitch/roll "absolutos" na calibração (ou no boot)
        self.flex0 = 0.0
        self.flex20 = 0.0
        self.ruim = False
        self.inicio_ruim = 0.0
        self.estado = "ok"
        self.motor_ate = 0.0           # teste/confirmação do motor
        self.pulsos = []               # lista de (inicio, fim) para pulsos curtos
        # episódio de "corcunda": (início, duração, intensidade em graus, lado)
        self.episodio = None
        self.prox_episodio = 6.0
        self.falha = None              # (sensor, fim)
        self.prox_falha = 15.0
        self.pres = {"mpu": True, "flex": not args.sem_flex, "flex2": args.flex2, "fsr": args.fsr}
        self.flex_det = {"flex": False, "flex2": False, "fsr": False}
        self.flex_det_desde = {"flex": None, "flex2": None, "fsr": None}
        self.eventos = []

    def agora(self):
        return time.monotonic() - self.t0

    def evt(self, nivel, msg):
        self.eventos.append({"t": "evt", "nivel": nivel, "msg": msg})

    # -------------------------------------------------------------- sensores falsos

    def corpo(self, t):
        """Pitch/roll absolutos do tronco e quanto a coluna dobra (0..1)."""
        resp = 1.2 * math.sin(t * 2 * math.pi / 4.0)  # respiração
        balanco = 0.8 * math.sin(t * 0.37) + 0.5 * math.sin(t * 1.3)
        pitch, roll, dobra = 3.0 + resp + balanco, 1.0 + 0.6 * math.sin(t * 0.21), 0.05
        if self.episodio is None and t >= self.prox_episodio:
            self.episodio = (t, random.uniform(5, 10), random.uniform(18, 34), random.uniform(-8, 8))
        if self.episodio:
            ini, dur, amp, lado = self.episodio
            f = (t - ini) / dur
            if f >= 1:
                self.episodio = None
                self.prox_episodio = t + random.uniform(6, 14)
            else:
                env = math.sin(math.pi * min(1, f * 1.6)) if f < 0.31 else (1 if f < 0.8 else math.cos((f - 0.8) / 0.2 * math.pi / 2))
                env = max(0.0, env)
                pitch += amp * env
                roll += lado * env
                dobra += 0.32 * env * (amp / 34)
        return pitch, roll, dobra

    def atualizar_falhas(self, t):
        if not self.args.falhas:
            return
        if self.falha and t >= self.falha[1]:
            nome = self.falha[0]
            self.pres[nome] = True
            if nome == "mpu":
                self.evt("ok", "MPU-6050 reconectado")
            self.falha = None
            self.prox_falha = t + random.uniform(15, 25)
        elif self.falha is None and t >= self.prox_falha:
            candidatos = [k for k, v in self.pres.items() if v]
            if candidatos:
                nome = random.choice(candidatos)
                self.pres[nome] = False
                self.falha = (nome, t + random.uniform(4, 7))
                if nome == "mpu":
                    self.evt("erro", "MPU-6050 desconectado")

    def adc(self, nome, t, dobra):
        if not self.pres[nome]:
            return random.randint(0, 60) if random.random() < 0.5 else random.randint(0, 900)  # pino solto
        if nome == "fsr":
            # sem pressão lê ~0 (igual a pino solto); alguém "aperta" depois de 8 s
            if t < 8:
                return random.randint(0, 15)
            return int(prender(600 + 120 * math.sin(t * 0.4) + random.gauss(0, 8), 0, 4095))
        base = 2050 if nome == "flex" else 1980
        k = 1.0 if nome == "flex" else 0.7
        return int(prender(base * (1 - dobra * k) + random.gauss(0, 12), 0, 4095))

    def detectar(self, nome, valor, t):
        if nome == "fsr":
            # firmware: pressão estável > 200 por 2 s; fica detectado até reiniciar
            if not self.flex_det["fsr"]:
                if valor > 200 and self.pres["fsr"]:
                    self.flex_det_desde["fsr"] = self.flex_det_desde["fsr"] or t
                    self.flex_det["fsr"] = t - self.flex_det_desde["fsr"] >= 2.0
                else:
                    self.flex_det_desde["fsr"] = None
            return self.flex_det["fsr"]
        ok = ADC_MIN <= valor <= ADC_MAX and self.pres[nome]
        if ok:
            if self.flex_det_desde[nome] is None:
                self.flex_det_desde[nome] = t
            if t - self.flex_det_desde[nome] >= 2.0:
                self.flex_det[nome] = True
        else:
            self.flex_det_desde[nome] = None
            self.flex_det[nome] = False
        return self.flex_det[nome]

    @staticmethod
    def ativo(modo, detectado, ref):
        if modo == "off":
            return False
        ref_ok = ADC_MIN <= ref <= ADC_MAX
        return ref_ok if modo == "on" else (detectado and ref_ok)

    @staticmethod
    def pct(leitura, ref):
        return 0.0 if ref < 1 else prender((ref - leitura) / ref * 100, 0, 100)

    def motor_ligado(self, t):
        if t < self.motor_ate or any(a <= t < b for a, b in self.pulsos):
            return True
        if self.estado == "alerta":
            fase = (t - self.inicio_ruim - self.cfg["tempoMs"] / 1000) % 1.0
            return fase < 0.3
        return False

    # -------------------------------------------------------------- passo de 100 ms

    def passo(self):
        t = self.agora()
        self.atualizar_falhas(t)
        pa, ra, dobra = self.corpo(t)
        if t < 0.3 and self.ref == (0.0, 0.0):
            self.ref = (pa, ra)  # "gravidade no boot"

        flex = self.adc("flex", t, dobra)
        flex2 = self.adc("flex2", t, dobra)
        fsr = self.adc("fsr", t, dobra)
        fdet = self.detectar("flex", flex, t)
        f2det = self.detectar("flex2", flex2, t)
        fsrdet = self.detectar("fsr", fsr, t)

        mpu_ok = self.pres["mpu"]
        if mpu_ok:
            pitch = pa - self.ref[0] + random.gauss(0, 0.15)
            roll = ra - self.ref[1] + random.gauss(0, 0.15)
            desvio = math.hypot(pitch, roll)
        else:
            pitch = roll = desvio = 0.0

        calibrando = t < self.calib_ate
        if self.calib_ate and not calibrando and self.calib_ate > 0:
            # terminou agora
            self.calib_ate = 0.0
            if self.episodio:  # estava se mexendo (corcunda em andamento): firmware recusa
                self.evt("erro", "Mexeu durante a calibração: tente de novo")
            else:
                self.calibrado = True
                self.ref = (pa, ra)
                self.flex0 = flex if (fdet or self.cfg["flexModo"] == "on") else 0
                self.flex20 = flex2 if (f2det or self.cfg["flex2Modo"] == "on") else 0
                self.ruim = False
                self.pulsos = [(t, t + 0.12), (t + 0.24, t + 0.36)]
                self.evt("ok", "Postura calibrada")
                self.eventos.append(self.config_msg())

        fat = self.ativo(self.cfg["flexModo"], fdet, self.flex0)
        f2at = self.ativo(self.cfg["flex2Modo"], f2det, self.flex20)
        fpct = self.pct(flex, self.flex0) if fat else 0.0
        f2pct = self.pct(flex2, self.flex20) if f2at else 0.0

        c = self.cfg
        if calibrando:
            self.estado, tempo_ruim = "ok", 0
        else:
            acima = desvio > c["limiteGraus"]
            abaixo = desvio < c["limiteGraus"] - c["histerese"]
            if fat:
                acima = acima or fpct > c["limiteFlex"]
                abaixo = abaixo and fpct < c["limiteFlex"] - c["histerese"]
            if f2at:
                acima = acima or f2pct > c["limiteFlex"]
                abaixo = abaixo and f2pct < c["limiteFlex"] - c["histerese"]
            if not self.calibrado:
                self.ruim = False
            elif not self.ruim and acima:
                self.ruim, self.inicio_ruim = True, t
            elif self.ruim and abaixo:
                self.ruim = False
            if not self.ruim:
                self.estado, tempo_ruim = "ok", 0
            else:
                tempo_ruim = int((t - self.inicio_ruim) * 1000)
                self.estado = "alerta" if tempo_ruim >= c["tempoMs"] else "atencao"

        r1 = lambda v: round(v, 1)
        return {"t": "data", "pitch": r1(pitch), "roll": r1(roll), "desvio": r1(desvio),
                "flex": flex, "flexPct": r1(fpct), "flexAtivo": fat, "flexDetectado": fdet,
                "flex2": flex2, "flex2Pct": r1(f2pct), "flex2Ativo": f2at, "flex2Detectado": f2det,
                "fsr": fsr, "fsrDetectado": fsrdet,
                "estado": self.estado, "tempoRuimMs": tempo_ruim, "calibrado": self.calibrado,
                "calibrando": calibrando, "mpuOk": mpu_ok, "motor": self.motor_ligado(t),
                "uptime": int(t * 1000)}

    def config_msg(self):
        m = {"t": "config"}
        m.update(self.cfg)
        m["calibrado"] = self.calibrado
        m["versao"] = VERSAO
        return m

    def comando(self, d):
        cmd = d.get("cmd", "")
        t = self.agora()
        if cmd == "calib":
            if not self.pres["mpu"]:
                self.evt("erro", "MPU-6050 não encontrado")
            elif not self.calib_ate:
                # firmware: espera 1,5 s + média de 1 s; recusa se a pessoa se mexer
                self.calib_ate = t + 2.5
                self.ruim = False
                self.evt("info", "Calibrando... fique parado")
        elif cmd == "motor_test":
            self.motor_ate = t + 1.0
        elif cmd == "get":
            return [self.config_msg()]
        elif cmd == "set":
            for k in padrao():
                if k in d:
                    self.cfg[k] = d[k]
            try:
                validar(self.cfg)
            except (TypeError, ValueError):
                self.cfg = validar(dict(padrao(), **{k: v for k, v in self.cfg.items() if isinstance(v, (int, float))}))
            self.eventos.append(self.config_msg())
            self.evt("ok", "Parâmetros salvos")
        elif cmd == "reset":
            self.cfg = padrao()
            self.eventos.append(self.config_msg())
            self.evt("ok", "Parâmetros padrão restaurados")
        else:
            self.evt("erro", "comando desconhecido")
        return []


# ------------------------------------------------------------------ WebSocket mínimo

async def ws_ler(reader):
    """Lê um frame de texto. Volta None ao fechar."""
    while True:
        h = await reader.readexactly(2)
        op, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126:
            n = struct.unpack(">H", await reader.readexactly(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", await reader.readexactly(8))[0]
        mask = await reader.readexactly(4) if h[1] & 0x80 else b"\0\0\0\0"
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(await reader.readexactly(n)))
        if op == 0x8:
            return None
        if op == 0x1:
            return data.decode("utf-8", "replace")
        # ping/pong/binário: ignora


def ws_frame(texto):
    b = texto.encode()
    n = len(b)
    if n < 126:
        h = bytes([0x81, n])
    elif n < 65536:
        h = bytes([0x81, 126]) + struct.pack(">H", n)
    else:
        h = bytes([0x81, 127]) + struct.pack(">Q", n)
    return h + b


class Servidor:
    def __init__(self, colete):
        self.colete = colete
        self.clientes = set()

    async def enviar(self, w, obj):
        try:
            w.write(ws_frame(json.dumps(obj, ensure_ascii=False, separators=(",", ":"))))
            await w.drain()
        except (ConnectionError, RuntimeError):
            self.clientes.discard(w)

    async def broadcast(self, obj):
        for w in list(self.clientes):
            await self.enviar(w, obj)

    async def ws_cliente(self, reader, writer):
        try:
            req = await reader.readuntil(b"\r\n\r\n")
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError):
            writer.close()
            return
        chave = None
        for linha in req.decode("latin-1").split("\r\n"):
            if linha.lower().startswith("sec-websocket-key:"):
                chave = linha.split(":", 1)[1].strip()
        if not chave:
            writer.write(b"HTTP/1.1 400 Bad Request\r\n\r\n")
            writer.close()
            return
        aceite = base64.b64encode(hashlib.sha1((chave + WS_GUID).encode()).digest()).decode()
        writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: " + aceite + "\r\n\r\n").encode())
        await writer.drain()
        self.clientes.add(writer)
        print("[ws] cliente conectado (%d)" % len(self.clientes))
        await self.enviar(writer, self.colete.config_msg())
        try:
            while True:
                txt = await ws_ler(reader)
                if txt is None:
                    break
                try:
                    d = json.loads(txt)
                except ValueError:
                    self.colete.evt("erro", "comando inválido")
                    continue
                print("[ws] <-", txt)
                for resp in self.colete.comando(d):
                    await self.enviar(writer, resp)
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self.clientes.discard(writer)
            writer.close()
            print("[ws] cliente saiu (%d)" % len(self.clientes))

    async def laco(self):
        while True:
            dados = self.colete.passo()
            eventos, self.colete.eventos = self.colete.eventos, []
            for e in eventos:
                await self.broadcast(e)
            if self.clientes:
                await self.broadcast(dados)
            await asyncio.sleep(0.1)


async def http_cliente(reader, writer):
    try:
        req = await reader.readuntil(b"\r\n\r\n")
        caminho = req.split(b" ")[1].decode().split("?")[0]
    except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, IndexError):
        writer.close()
        return
    nomes = {"/": "index.html", "/index.html": "index.html", "/manifest.json": "manifest.json", "/icon.svg": "icon.svg"}
    if caminho in nomes:
        arq = os.path.join(WEB_DIR, nomes[caminho])
        with open(arq, "rb") as f:
            corpo = f.read()
        tipo = {"index.html": "text/html; charset=utf-8", "manifest.json": "application/manifest+json",
                "icon.svg": "image/svg+xml"}.get(nomes[caminho]) or mimetypes.guess_type(arq)[0]
        cab = "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n" % (tipo, len(corpo))
        writer.write(cab.encode() + corpo)
    else:
        writer.write(b"HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
    try:
        await writer.drain()
    finally:
        writer.close()


async def main():
    ap = argparse.ArgumentParser(description="Simulador do colete (HTTP + WebSocket)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--flex2", action="store_true")
    ap.add_argument("--fsr", action="store_true")
    ap.add_argument("--sem-flex", action="store_true")
    ap.add_argument("--falhas", action="store_true")
    args = ap.parse_args()

    srv = Servidor(Colete(args))
    h = await asyncio.start_server(http_cliente, "0.0.0.0", args.port)
    w = await asyncio.start_server(srv.ws_cliente, "0.0.0.0", args.port + 1)
    print("Mock do colete: http://localhost:%d  (WS na %d)" % (args.port, args.port + 1))
    async with h, w:
        await asyncio.gather(h.serve_forever(), w.serve_forever(), srv.laco())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
