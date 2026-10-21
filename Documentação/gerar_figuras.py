#!/usr/bin/env python3
"""Gera as figuras (PNG e SVG) de todos os diagramas Mermaid da documentação.

Os diagramas são escritos em Mermaid dentro dos próprios .md (texto, editável junto com o código).
Este script desenha cada um em imagem, para colar no texto do TCC (Word/PDF).

    python3 Documentação/gerar_figuras.py                  # todos os .md
    python3 Documentação/gerar_figuras.py 04-deteccao-e-calibracao.md

Saída: Documentação/figuras/<documento>-<n>.png e .svg, onde <n> é a ordem do diagrama no documento
(1 = primeiro bloco ```mermaid do arquivo). Ao acrescentar um diagrama no meio de um documento, os números
dos seguintes mudam: gere de novo.

Precisa de Node.js (npx). O mermaid-cli é baixado pelo npx na primeira vez. Usa o Chrome/Chromium já
instalado quando encontra um; senão o puppeteer baixa o dele. Para usar um mmdc já instalado: MMDC=/caminho/mmdc.
Só usa a biblioteca padrão do Python.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

DOC = os.path.dirname(os.path.abspath(__file__))
SAIDA = os.path.join(DOC, "figuras")
MERMAID_CLI = "@mermaid-js/mermaid-cli@11.4.2"

# Navegadores conhecidos (macOS e Linux). O primeiro que existir é usado pelo puppeteer.
NAVEGADORES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    shutil.which("google-chrome") or "",
    shutil.which("chromium") or "",
    shutil.which("chromium-browser") or "",
]


def comando_mmdc():
    """Linha de comando do mermaid-cli: $MMDC, se definido, senão via npx."""
    if os.environ.get("MMDC"):
        return [os.environ["MMDC"]]
    if not shutil.which("npx"):
        sys.exit("npx não encontrado: instale o Node.js ou defina MMDC=/caminho/para/mmdc")
    return ["npx", "-y", "-p", MERMAID_CLI, "mmdc"]


def config_puppeteer(pasta):
    """Arquivo de configuração do puppeteer apontando para o navegador local, se houver."""
    cfg = {"args": ["--no-sandbox"]}
    for nav in NAVEGADORES:
        if nav and os.path.exists(nav):
            cfg["executablePath"] = nav
            break
    caminho = os.path.join(pasta, "puppeteer.json")
    with open(caminho, "w") as f:
        json.dump(cfg, f)
    return caminho, "executablePath" in cfg


def blocos_mermaid(md):
    """Lista o conteúdo de cada bloco ```mermaid do arquivo, na ordem."""
    with open(md, encoding="utf-8") as f:
        return re.findall(r"^```mermaid\n(.*?)^```", f.read(), re.S | re.M)


def main():
    nomes = sys.argv[1:]
    if nomes:
        arquivos = [n if os.path.isabs(n) else os.path.join(DOC, n) for n in nomes]
    else:
        arquivos = sorted(glob.glob(os.path.join(DOC, "*.md")) + glob.glob(os.path.join(DOC, "referencia", "*.md")))
    os.makedirs(SAIDA, exist_ok=True)
    mmdc = comando_mmdc()
    env = dict(os.environ)
    with tempfile.TemporaryDirectory() as tmp:
        cfg, tem_nav = config_puppeteer(tmp)
        if tem_nav:
            env["PUPPETEER_SKIP_DOWNLOAD"] = "1"  # não baixa outro Chromium se já há um navegador
        total = erros = 0
        for md in arquivos:
            base = os.path.splitext(os.path.relpath(md, DOC))[0].replace(os.sep, "-")
            for n, codigo in enumerate(blocos_mermaid(md), 1):
                total += 1
                fonte = os.path.join(tmp, "%s-%d.mmd" % (base, n))
                with open(fonte, "w", encoding="utf-8") as f:
                    f.write(codigo)
                for ext, extra in ((".png", ["-b", "white", "-s", "2"]), (".svg", ["-b", "white"])):
                    destino = os.path.join(SAIDA, "%s-%d%s" % (base, n, ext))
                    r = subprocess.run(mmdc + ["-q", "-p", cfg, "-i", fonte, "-o", destino] + extra,
                                       capture_output=True, text=True, env=env)
                    if r.returncode != 0:
                        erros += 1
                        print("ERRO em %s, diagrama %d (%s):\n%s" % (os.path.relpath(md, DOC), n, ext,
                                                                    (r.stderr or r.stdout).strip()[:800]))
                        break
                else:
                    print("ok  %s-%d" % (base, n))
    print("%d diagramas, %d com erro. Figuras em %s" % (total, erros, SAIDA))
    sys.exit(1 if erros else 0)


if __name__ == "__main__":
    main()
