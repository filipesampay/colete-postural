# Colete Postural — firmware ESP32

Firmware do colete ortopédico inteligente (TCC). ESP32 + MPU-6050 + sensor flex + motor vibratório,
com app web embarcado (Wi-Fi Access Point + WebSocket). Base: `../GUIA.md`.

O colete **funciona sozinho**: sem celular e sem computador, ele vibra quando a postura sai do limite.

**Documentação completa** (arquitetura, algoritmos, diagramas, referência de cada módulo, decisões de projeto e
mapa para o texto do TCC): [`Documentação/README.md`](Documentação/README.md). Em PDF, para ler ou imprimir:
`Documentação/pdf/00-documentacao-completa.pdf` (e um PDF por documento), gerados a partir dos `.md` por
`python3 Documentação/gerar_pdf.py`. Para manter a documentação junto com o código:
[`Documentação/MANUTENCAO.md`](Documentação/MANUTENCAO.md).

## Comandos do dia a dia

```bash
cd colete-postural
pio run                     # compila
pio run -t upload           # grava (placa no USB)
pio device monitor          # serial 115200 (Ctrl+C sai)
pio test -e native          # testes da lógica (21 testes, sem placa)
tools/sim.sh                # simulador do firmware inteiro no PC (sem placa)
python3 tools/sim_web.py    # página web real + firmware simulado: http://localhost:8080
tools/gerar_bin.sh          # gera dist/colete-postural.bin (imagem única, endereço 0x0)
```

`pio` está em `~/.local/bin/pio` (PlatformIO 6.2, instalado em `~/.platformio`).

## Pinagem (compatível com as duas montagens)

| Função | Pino | montagem.pdf (GUIA) | README BioSpine V3 |
|---|---|---|---|
| MPU-6050 SDA / SCL | 21 / 22 | sim | sim |
| Flex 1 | 34 | sim | flex esquerdo |
| Flex 2 | 35 | reserva | flex direito |
| FSR (pressão) | 32 | — | sim |
| Motor (via NPN) | 25 | sim | — |
| Botão de calibração | 27 | sim | — |
| LED azul da placa | 2 | — | sim (acende junto com o motor) |

Nenhum pino conflita, então um firmware só atende as duas montagens. O que não estiver ligado é detectado e ignorado:
- **MPU** em 0x68 **ou** 0x69. Se cair, o firmware tenta reconectar a cada 2 s.
- **Flex 1**: só entra na detecção se a leitura for plausível (100–4000 sem oscilar) **e** se a calibração foi feita com ele ligado. Flex solto, quebrado ou sem resistor é desativado sozinho, e o colete segue só com o MPU.
- **Flex 2**: **desligado por padrão**. Um GPIO35 solto "copia" a tensão do GPIO34 pelo capacitor do ADC e pareceria um sensor de verdade. Com 2º flex ligado: `set flex2 auto` e recalibrar.
- **FSR**: só telemetria. Fica "detectado" depois de ler pressão estável uma vez.
- **Sem motor ou sem botão**: nada quebra. Dá para calibrar pela serial (`calib`) ou pelo app.

Também aceita os comandos serial do BioSpine V3 (`START`, `STOP`, `STATUS`, `CALIBRATE`), com as respostas JSON no formato do README dele.

## Serial (115200)

| Comando | Ação |
|---|---|
| `help` | lista os comandos |
| `diag` | testa cada componente (✅/❌) e pulsa o motor por 1 s |
| `calib` | calibra a postura correta (igual ao botão): 1 pulso, fique parado 2,5 s, 2 pulsos = pronto; 1 pulso longo = mexeu, repita |
| `get` | mostra parâmetros, sensores ativos e calibração |
| `set limite 20` | graus (2–60); idem `histerese`, `tempo` (ms), `flexlim` (%), `motor` (%) |
| `set flex on/off/auto` | força o flex 1 (`flex2` para o segundo) |
| `motor` | testa o motor (1 s) |
| `plot on/off` | saída para o Serial Plotter / `tools/grafico_serial.py` |
| `reset` | parâmetros padrão (mantém a calibração) |

Tudo que é alterado é salvo na NVS e sobrevive a desligar a placa.

## App web

1. No celular, conectar no Wi-Fi **`BioSpine`**, senha **`colete123`**.
2. Abrir **http://192.168.4.1**.
3. Quando o Android avisar "sem internet", tocar em **Manter conexão**. Se não carregar, **desligar os dados móveis**.

A página (`src/web/index.html`) vai embutida no firmware (`board_build.embed_txtfiles`).
Para testar sem placa:
- `python3 tools/sim_web.py --demo` → http://localhost:8080. Quem responde é **o próprio firmware** compilado para o PC, com um MPU falso. No terminal: `!frente 30`, `!lado -15`, `!flex 1500`, `!botao`, `!mpu off`, `diag`... (`--demo` mexe o tronco sozinho, bom para gravar a tela).
- `python3 tools/mock_server.py` → imitação do ESP32 em Python (feita junto com a página).
- **Windows:** `dist/BioSpine-Simulador.exe` é o mesmo `mock_server.py` empacotado; ao abrir, sobe o servidor e abre o navegador. Gerar de novo: `tools/gerar_exe.sh` (PyInstaller no Wine; preparação no topo do script).
Protocolo WebSocket (porta 81): seção 8 do GUIA, com campos a mais (`flex2*`, `fsr*`, `flexModo`, `intensidade`, eventos `{"t":"evt"}`). A definição completa está no topo de `src/protocolo.h`.

## Como funciona (para explicar à banca)

0. **Duas tarefas**: uma tarefa FreeRTOS de prioridade maior cuida de sensores, postura, botão e motor; o `loop()` cuida do Wi-Fi e da serial. Se o celular sair do alcance (o envio pelo Wi-Fi fica esperando), o colete continua detectando e vibrando.

1. **Leitura** (200 Hz): o MPU-6050 é lido direto pelos registradores, configurado em ±250 °/s e ±2 g, com filtro passa-baixa interno de 44 Hz. O giroscópio tem o offset medido no boot, com o colete parado.
2. **Filtro complementar sobre o vetor gravidade**: o giroscópio acompanha os movimentos rápidos e o acelerômetro corrige a deriva (peso 0,98/0,02). Durante acelerações fortes (passo, batida), o acelerômetro é ignorado naquele instante.
3. **Desvio** = ângulo entre a gravidade atual e a gravidade gravada na calibração. Não depende de como o sensor foi preso no colete. Para mostrar, o desvio também é decomposto em frente/trás e lateral.
4. **Flex**: média móvel de 10 amostras; a curvatura é a % de queda em relação à leitura da calibração (a leitura cai ao dobrar).
5. **Detecção**: postura ruim quando desvio > limite **ou** flex > limiteFlex. O alerta só dispara se durar mais que `tempo`, e só sai quando volta abaixo de limite − histerese. Estados: ok → atenção → alerta.
6. **Motor**: PWM a 20 kHz (inaudível), pulsos de 300 ms ligado / 700 ms desligado enquanto durar o alerta.

### Diferenças em relação ao GUIA.md
- **Sem a biblioteca MPU6050_light**: o ângulo dela é calculado com uma fórmula feita para o sensor deitado. No colete o sensor fica em pé, e aí o ângulo **dá um salto de ~20°** ao passar pela vertical quando há inclinação lateral (teste `test_desvio_sem_salto_perto_da_vertical`). Além disso, o `calcOffsets()` dela assume o sensor deitado, e o endereço 0x68 é fixo.
- A calibração grava o **vetor gravidade** (`g0`) em vez de `pitch0/roll0`.
- Calibração com espera de 1,5 s depois do clique (para a pessoa se ajeitar) e recusada se ela se mexer durante a média.
- `plot on` imprime `desvio`, `limite`, `pitch`, `roll`, `flexPct`, `limiteFlex` e `alerta`.

## Amanhã, com o hardware

> **Roteiro completo de validação, ponto a ponto: `VALIDACAO.md`** (feito para um agente acompanhar o teste).
>
> Antes: fazer **logout/login** (o usuário entrou no grupo `dialout` hoje). Se não der, `sudo chmod a+rw /dev/ttyUSB0`.
> O `brltty` foi desativado (`systemctl mask`), então não deve roubar a CH340.

1. Ligar o USB → `ls /dev/ttyUSB*` → `pio run -t upload`. Se a gravação falhar: segurar **BOOT** quando aparecer `Connecting...`, ou baixar `upload_speed` no `platformio.ini`.
2. `pio device monitor` → `diag`. Se algo der ❌, o problema é fio, não código (tabela "Se algo não funcionar" do montagem.pdf).
3. Dobrar o flex e ver o valor mudar (`plot on`). Apertar o botão. Sentir o motor.
4. O flex 2 vem desligado. **Só se houver 2º flex**: `set flex2 auto`, conferir `detectado ✅` no `diag` e recalibrar.
5. Conferir o sinal de pitch: inclinar para a frente deve dar **pitch positivo**. Se der negativo, o sensor está com os componentes virados para o corpo; só o rótulo fica invertido, o desvio e o alerta funcionam igual.
6. **Calibrar** com o colete vestido (botão ou app). Sem calibração o colete **não vibra**.
7. Ajustar limite, histerese e tempo pelo app. Testar o app no celular dela.
8. **Gravar o vídeo** do colete vibrando + tela do app.
9. Gráfico para o TCC: `~/.venvs/colete/bin/python tools/grafico_serial.py -t 60` → gera CSV + PNG.

Se for gravar de outro computador: `dist/colete-postural.bin` no endereço `0x0`, por exemplo pelo navegador em https://espressif.github.io/esptool-js/ (Chrome/Edge).

## Estrutura

```
src/
  main.cpp       setup/loop, calibração, botão, agendamento por millis()
  config.h       pinos e valores padrão
  tilt.*         vetor gravidade, filtro complementar, ângulos (puro, testado)
  posture.*      limite, histerese, tempo mínimo, calibrador (puro, testado)
  filters.h      média móvel, detecção de sensor presente (puro, testado)
  mpu.*          driver do MPU-6050 (registradores)
  sensors.*      MPU + flex + FSR
  motor.*        PWM LEDC + padrões de pulso + LED
  settings.*     NVS (Preferences)
  console.*      comandos serial
  protocolo.*    JSON do app (dados, config, eventos, comandos) — roda também no simulador
  web.*          Wi-Fi AP, HTTP, WebSocket
  web/           index.html, manifest.json, icon.svg (embutidos no firmware)
test/            testes da lógica pura (pio test -e native)
sim/             stubs de Arduino/Wire/Preferences + MPU falso para o simulador
tools/           sim.sh + roteiros, sim_web.py, gerar_bin.sh, grafico_serial.py, mock_server.py
Documentação/    documentação: fonte em .md, PDFs gerados em Documentação/pdf/, guia de manutenção
```
