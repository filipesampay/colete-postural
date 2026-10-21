#!/usr/bin/env python3
"""Captura a saída "plot on" do colete pela serial e gera CSV + gráfico PNG (figura do TCC).

Uso (com o colete ligado no USB):
    ~/.venvs/colete/bin/python tools/grafico_serial.py               # captura até Ctrl+C
    ~/.venvs/colete/bin/python tools/grafico_serial.py -t 60          # captura 60 s
    ~/.venvs/colete/bin/python tools/grafico_serial.py --csv dados.csv  # só redesenha um CSV

O script manda "plot on" sozinho e desliga no final. Também funciona com o CSV baixado do app.
"""
import argparse
import csv
import glob
import sys
import time
from datetime import datetime


def achar_porta():
    portas = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not portas:
        sys.exit("Nenhuma porta /dev/ttyUSB* encontrada. O cabo é de dados? A placa está ligada?")
    return portas[0]


def capturar(porta, segundos, arquivo):
    import serial

    linhas = []
    with serial.Serial(porta, 115200, timeout=0.5) as s:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"plot on\n")
        inicio = time.time()
        print(f"Capturando de {porta}... (Ctrl+C para parar)")
        try:
            while segundos <= 0 or time.time() - inicio < segundos:
                bruto = s.readline().decode("utf-8", "replace").strip()
                if not bruto.startswith("desvio:"):
                    if bruto:
                        print("  ", bruto)
                    continue
                campos = dict(p.split(":", 1) for p in bruto.split(",") if ":" in p)
                try:
                    valores = {k: float(v) for k, v in campos.items()}
                except ValueError:
                    continue
                valores["t"] = round(time.time() - inicio, 2)
                linhas.append(valores)
                if len(linhas) % 50 == 0:
                    print(f"  {valores['t']:6.1f} s  desvio {valores.get('desvio', 0):5.1f}°")
        except KeyboardInterrupt:
            pass
        s.write(b"plot off\n")

    if not linhas:
        sys.exit("Nenhum dado recebido. O firmware do colete está gravado?")
    chaves = ["t"] + [k for k in linhas[0] if k != "t"]
    with open(arquivo, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=chaves, extrasaction="ignore")
        w.writeheader()
        w.writerows(linhas)
    print(f"{len(linhas)} amostras salvas em {arquivo}")
    return arquivo


def ler_csv(arquivo):
    with open(arquivo) as f:
        linhas = list(csv.DictReader(f))
    dados = {}
    for k in linhas[0]:
        try:
            dados[k] = [float(l[k]) for l in linhas]
        except (ValueError, TypeError):
            pass
    return dados


def desenhar(arquivo, png):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    d = ler_csv(arquivo)
    t = d.get("t") or d.get("tempo_s")
    if t is None:
        sys.exit("CSV sem coluna de tempo (t ou tempo_s)")
    t0 = t[0]
    t = [x - t0 for x in t]

    tem_flex = "flexPct" in d and max(d["flexPct"]) > 0
    fig, eixos = plt.subplots(2 if tem_flex else 1, 1, figsize=(10, 6 if tem_flex else 4), sharex=True)
    if not tem_flex:
        eixos = [eixos]

    ax = eixos[0]
    ax.plot(t, d["desvio"], color="#2f6fd1", linewidth=1.6, label="Desvio postural (°)")
    if "limite" in d:
        ax.plot(t, d["limite"], color="#d64545", linestyle="--", linewidth=1.2, label="Limite (°)")
    alerta = d.get("alerta")
    if alerta:
        topo = max(max(d["desvio"]), max(d.get("limite", [0]))) * 1.1 or 1
        ax.fill_between(t, 0, topo, where=[a > 0 for a in alerta], color="#e8a33d", alpha=0.25,
                        step="mid", label="Motor em alerta")
        ax.set_ylim(0, topo)
    ax.set_ylabel("Inclinação (°)")
    ax.grid(alpha=0.3)
    ax.legend(loc="upper right")

    if tem_flex:
        ax2 = eixos[1]
        ax2.plot(t, d["flexPct"], color="#e07b00", linewidth=1.6, label="Curvatura do flex (%)")
        lim = d.get("limiteFlex")
        if lim:
            ax2.plot(t, lim, color="#d64545", linestyle="--", linewidth=1.2, label="Limite do flex (%)")
        ax2.set_ylabel("Curvatura (%)")
        ax2.grid(alpha=0.3)
        ax2.legend(loc="upper right")

    eixos[-1].set_xlabel("Tempo (s)")
    fig.suptitle("Colete postural: desvio em relação à postura calibrada")
    fig.tight_layout()
    fig.savefig(png, dpi=200)
    print(f"Gráfico salvo em {png}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--porta", help="porta serial (padrão: primeira /dev/ttyUSB*)")
    ap.add_argument("-t", "--tempo", type=float, default=0, help="segundos de captura (0 = até Ctrl+C)")
    ap.add_argument("--csv", help="não captura: só desenha este CSV")
    ap.add_argument("-o", "--saida", help="nome base dos arquivos (padrão: postura_AAAAMMDD_HHMMSS)")
    a = ap.parse_args()

    base = a.saida or datetime.now().strftime("postura_%Y%m%d_%H%M%S")
    arquivo = a.csv or capturar(a.porta or achar_porta(), a.tempo, base + ".csv")
    desenhar(arquivo, (a.csv.rsplit(".", 1)[0] if a.csv else base) + ".png")


if __name__ == "__main__":
    main()
