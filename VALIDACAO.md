# Validação no hardware — roteiro para o agente (21/10/2026)

Documento para o agente que vai acompanhar o teste **in loco**, com a placa, junto com o desenvolvedor.
Leia inteiro antes de começar. Siga as fases **em ordem**: cada uma depende da anterior.

Para cada ponto: execute, compare com o **critério**, registre o resultado na tabela do final
(seção 9) e, se falhar, use a coluna **Se falhar**. Regra geral: **se o `diag` dá ❌, o problema é fio,
não código.** Só mexa no código depois de descartar a montagem.

---

## 0. Contexto mínimo

- Projeto: `~/Pessoal/Colete/colete-postural/` (PlatformIO, ESP32 Arduino core 2.0.17). Visão geral: `README.md`. Plano original: `../GUIA.md`. Montagem: `../montagem.pdf`.
- Prazo: **hoje é a entrega**. Prioridade: colete vibrando com postura ruim + app mostrando dados + vídeo gravado + gráfico do TCC. Todo o resto é secundário.
- Tudo foi desenvolvido **sem a placa**. O que já foi verificado e o que **nunca rodou no hardware** está na seção 1. Os pontos da seção 1.2 são os mais prováveis de dar problema.
- O firmware atende **duas pinagens ao mesmo tempo** (GUIA/montagem.pdf e README do BioSpine V3 em `../ESP32/`, repositório de outra pessoa: **não editar**). Ver tabela no `README.md`.

### Decisões já tomadas (não reverter sem motivo forte)
1. **Não usar a biblioteca `MPU6050_light`.** Com o sensor em pé nas costas, a fórmula de ângulo dela salta ~20° perto da vertical; o `calcOffsets()` dela assume o sensor deitado; o endereço 0x68 é fixo. O driver próprio está em `src/mpu.cpp`.
2. **Desvio = ângulo entre a gravidade atual e a gravidade gravada na calibração** (`src/tilt.cpp`, `angleBetweenDeg`). Não depende da orientação de montagem. `pitch`/`roll` são só para exibição.
3. **Sem calibração o colete NÃO vibra** (intencional: sem referência não existe "postura correta"). A calibração fica salva na NVS.
4. Interface web: `src/web/index.html`, feita por outra sessão, embutida via `board_build.embed_txtfiles`. Protocolo: topo de `src/protocolo.h`. A página tem as caixas "Inverter frente/trás" e "Inverter lado" na vista 3D (salvas no navegador), para acertar o sentido sem regravar.
5. **Duas tarefas** (`src/main.cpp`): a tarefa `controle` (FreeRTOS, prioridade 3, núcleo 1) roda sensores → postura → motor e o botão; o `loop()` roda só Wi-Fi e serial. Motivo: um celular que some da rede trava o envio TCP do `loop()` por segundos (constantes fixas do `WiFiClient` do core). Assim o colete continua funcionando. **Nunca** chamar `Serial`/`web_*` da tarefa de controle: use `emitir()` (fila de eventos).
6. **Flex 2 desligado por padrão** (`set flex2 auto` para usar). Um GPIO35 solto "copia" a tensão do GPIO34 pelo capacitor do ADC e pareceria um flex de verdade.
7. **Calibração**: 1 pulso ao começar, espera 1,5 s, média de 1 s, 2 pulsos quando conclui. Se a pessoa se mexer (> 30 °/s), é recusada com 1 pulso longo (600 ms).

### Comandos
```bash
cd ~/Pessoal/Colete/colete-postural
pio run -t upload && pio device monitor     # grava e abre a serial (115200)
pio test -e native                          # 21 testes da lógica (rodar após qualquer mudança em src/)
tools/sim.sh                                # simulador do firmware (rodar após qualquer mudança)
tools/sim.sh tools/roteiro_biospine.txt     # cenário pinagem BioSpine
tools/sim.sh tools/roteiro_sem_mpu.txt      # cenário MPU solto
python3 tools/sim_web.py --demo             # página web + firmware simulado em http://localhost:8080
tools/gerar_bin.sh                          # dist/colete-postural.bin (gravar em 0x0)
~/.venvs/colete/bin/python tools/grafico_serial.py -t 60    # captura gráfico (CSV + PNG)
```
`pio` = `~/.local/bin/pio`. Para sair do monitor: `Ctrl+C`. **Feche o monitor antes de gravar ou rodar o `grafico_serial.py`** (a porta só aceita um programa por vez).

---

## 1. O que já foi validado e o que não foi

### 1.1 Validado sem hardware (20/10)
| Item | Como |
|---|---|
| Lógica de postura (limite, histerese, tempo mínimo, flex, calibração) | 21 testes nativos (`test/test_posture/test_main.cpp`) |
| Filtro de inclinação: sem salto perto da vertical, converge, segue o giroscópio | testes nativos |
| Integração main/console/sensores/motor/NVS, comandos serial, `diag` | simulador nativo com MPU falso (`sim/`), 3 roteiros |
| Fluxos: calibrar por botão/serial/app, pico curto não vibra, alerta após 3 s, pulsos 300/700, histerese, alerta pelo flex, flex desconectado é desativado, MPU cai e reconecta, MPU em 0x69, flex2, FSR, comandos BioSpine | simulador |
| JSON do app (dados 10 Hz, config, eventos, comandos, JSON inválido) | simulador + `tools/sim_web.py` |
| Compilação para ESP32 sem warnings; RAM 14,6%, flash ~63% | `pio run` |
| Gráfico do TCC a partir do CSV | `tools/grafico_serial.py` com dados simulados |
| Página web real (`src/web/index.html`) contra o firmware simulado | sessão web, via `tools/sim_web.py --demo` |
| Calibração recusada quando a pessoa se mexe | simulador |

### 1.2 Nunca rodou no hardware (maiores riscos, em ordem)
1. **Driver do MPU** (`src/mpu.cpp`): registradores, escala, sinal dos eixos, I2C a 100 kHz. Validar em F2.
1b. **Arquitetura de duas tarefas** (o simulador roda tudo numa linha só e não testa concorrência). Validar em V1.3, V5.12 e com uso geral: travamento, reset (`Guru Meditation`, `Task watchdog`) ou dado corrompido.
2. **Convenção de sinal giroscópio × acelerômetro** no filtro (`src/tilt.cpp`, `dg/dt = g × ω`). Foi conferida contra a convenção da MPU6050_light, mas o simulador usa a mesma convenção do código, então não prova nada. Validar em F2-V2.6 (movimento rápido sem "repique").
3. **ADC real**: ruído com Wi-Fi ligado (limite de amplitude `ADC_RUIDO_MAX = 300`), e o comportamento do **GPIO35 solto** (pode ser detectado como flex2 por engano). Validar em F3.
4. **Wi-Fi AP + página + WebSocket** no ESP32 e no celular dela. Validar em F5.
5. **Reset por queda de tensão** quando o motor liga junto com o Wi-Fi. Validar em F4/F7.
6. **Power bank desligando sozinho** por consumo baixo (~80–150 mA). Validar em F7.

---

## 2. Fase F0 — Ambiente do PC (antes de ligar a placa)

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V0.1 | `groups` | aparece `dialout` | Fazer logout/login. Paliativo: `sudo chmod a+rw /dev/ttyUSB0` (vale até desconectar) |
| V0.2 | `systemctl is-enabled brltty-udev.service` | `masked` | `sudo systemctl mask brltty-udev.service brltty.service`. **NÃO** rodar `apt remove brltty` (remove o `neon-common` do KDE neon) |
| V0.3 | `pio --version` e `pio test -e native` | 6.2.x; 21 testes OK | `export PATH=$HOME/.local/bin:$PATH` |
| V0.4 | `pio run` | SUCCESS | Ler o erro; se for na página web, ver `src/web/` |

## 3. Fase F1 — Placa, gravação e boot (placa sozinha no USB, ou já montada)

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V1.1 | Ligar no USB → `ls /dev/ttyUSB* /dev/ttyACM*` | aparece uma porta | Cabo só de carga (trocar). `dmesg \| tail` mostra se a CH340/CP210x foi vista e se o brltty a pegou |
| V1.2 | `pio run -t upload` | `Hard resetting via RTS pin...` sem erro | Segurar **BOOT** quando aparecer `Connecting....`; baixar `upload_speed` para 460800 ou 115200 em `platformio.ini` |
| V1.3 | `pio device monitor` e apertar **EN** | aparece `=== Colete Postural 1.0.0 ===`, `[wifi] AP "BioSpine" ... http://192.168.4.1`, `Digite 'help'...` | Lixo na tela = baud errado (deve ser 115200). Reboot em loop = ver backtrace (o filtro `esp32_exception_decoder` decodifica) e registrar |
| V1.4 | Observar no boot | 1 pulso curto do motor (120 ms) e LED azul piscando junto | Sem motor: ver F4. Sem LED azul: algumas placas não têm LED no GPIO2 (ignorar) |
| V1.5 | `help` | lista de comandos | — |

## 4. Fase F2 — MPU-6050 (o mais importante)

Começar com o MPU **na protoboard, parado na mesa**.

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V2.1 | `diag` | `I2C scan ........ 0x68 ✅` (ou 0x69) e `MPU6050 ... ✅ (0x68, WHO_AM_I 0x68)` | `nada ❌`: SDA/SCL trocados (D21=SDA, D22=SCL), VCC/GND soltos, MPU no VIN em vez do 3V3. Outros endereços além do MPU são normais se houver outros módulos |
| V2.2 | Anotar o `WHO_AM_I` | 0x68 = MPU-6050 original | 0x70/0x71/0x73/0x74 = família MPU-6500 (o firmware já liga o filtro do acelerômetro no reg. 0x1D); 0x98 e outros = clone. Os registradores usados são compatíveis: **seguir**, mas anotar. Se os valores parecerem absurdos, avisar |
| V2.3 | `diag` com a placa parada | **não** aparece `giro sem offset` | Aparece se a placa mexeu no boot. O offset é recalculado sozinho a cada ~1 s parado; esperar 2 s e rodar `diag` de novo. Se continuar com a placa parada: o ruído do giroscópio passa de `GIRO_PARADO_DPS` (5 °/s, `src/config.h:27`); subir para 8 |
| V2.4 | `plot on`, placa parada 10 s | `desvio` oscila menos de ~0,5° | Ruído alto: MPU solto/balançando, fio longo. Aumentar `MPU_FILTRO_GIRO` (`src/config.h:25`) de 0.98 para 0.99 suaviza mais |
| V2.5 | `calib` com a placa parada; inclinar a protoboard ~30° devagar e voltar | `desvio` vai a ~30 e volta a ~0 | Valor muito diferente (ex.: 15 ou 60): escala errada em `src/mpu.cpp` (`LSB_POR_G`, `LSB_POR_DPS`), conferir registradores `GYRO_CONFIG`/`ACCEL_CONFIG` = 0 |
| V2.6 | Inclinar ~30° **rápido** (meio segundo) e parar; observar o `desvio` no plotter/app | sobe direto até ~30 e para; **sem** ir primeiro para o lado errado e **sem** passar muito de 30 e voltar devagar (~0,3 s) | **Repique** (vai na direção oposta ou ultrapassa e volta devagar) = sinal do giroscópio invertido no filtro. Correção: em `src/tilt.cpp`, `TiltFilter::update`, trocar `cross(g_, w)` por `cross(w, g_)`; rodar `pio test -e native` (o teste `test_filtro_segue_giroscopio` vai falhar: inverter o sinal do giro nele também) e regravar |
| V2.7 | Girar a placa em torno do eixo vertical (como "olhar para o lado") sem inclinar | `desvio` quase não muda | Se muda muito, avisar (girar não muda a gravidade; não deveria alterar) |
| V2.8 | Com o MPU **no colete vestido** (entre as escápulas, pinos para baixo, componentes para fora): calibrar ereto e inclinar o tronco para a frente | `pitch` **positivo** e `desvio` aumenta | `pitch` negativo = sensor com componentes virados para o corpo. **Não afeta o alerta** (o desvio não tem sinal); só o rótulo frente/trás do app fica invertido. Corrigir só se sobrar tempo (inverter o sinal de `frente` em `tiltComponentsDeg`, `src/tilt.cpp`) |
| V2.9 | Puxar o fio SDA com tudo ligado e recolocar | `[mpu] MPU-6050 parou de responder`, depois `[mpu] MPU-6050 reconectado` em ~2–4 s; o app mostra o aviso | Se travar: registrar. A reconexão bloqueia ~1,2 s (calibração do giro) |

## 5. Fase F3 — Sensores analógicos (flex, flex2, FSR)

Leitura esperada do flex com divisor 3V3 → flex → D34 → 10 kΩ → GND: reto ≈ **1800–2300**; ao dobrar a leitura **cai**. Tolerância do flex ±30%.

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V3.1 | `diag` com o flex reto | `Flex (GPIO34) ... ~2000 ✅` | `~4095 ❌` falta o resistor de 10 kΩ. `~0 ❌` flex solto/quebrado na base. `instável ❌` mau contato. Em todos esses casos o colete funciona **só com o MPU** (é o fallback esperado) |
| V3.2 | `plot on`, flex reto, Wi-Fi ligado (celular conectado), 10 s | Leitura estável (variação < ~100); o flex continua "ativo" (`get` → `flex ... auto (ativo)` depois de calibrar) | Se o flex alterna entre ativo/inativo com o Wi-Fi: o ruído passou de `ADC_RUIDO_MAX` (`src/config.h:31`). Medir a amplitude e subir o limite (ex.: 500) |
| V3.3 | `calib` com o flex reto; dobrar o flex como na postura "corcunda" | `flexPct` sobe (anotar o valor máximo da dobra realista: ______ %) | Se a dobra realista der < 15%, o flex nunca alerta com o padrão: ajustar `flexlim` (V6.4). Se `flexPct` não muda: o flex não acompanha a curvatura (posição no colete) |
| V3.4 | `diag`: linha `Flex 2 (GPIO35)` | Padrão: `desligado (se houver 2º flex: set flex2 auto)`. Anotar a leitura com o pino solto (provavelmente parecida com a do flex 1: é o "fantasma" do ADC) | — |
| V3.5 | **Só se houver 2º flex:** `set flex2 auto`, esperar 3 s, `diag` → `detectado ✅`; recalibrar; dobrar só o flex 2 | `flex2Pct` sobe e alerta como o flex 1 | Se não houver 2º flex, deixar `off`. **Nunca** deixar `auto` com o pino solto |
| V3.6 | FSR (só montagem BioSpine): apertar o sensor 3 s, `diag` | `FSR (GPIO32) ... detectado ✅` | É só telemetria; não afeta o alerta. Leitura parecida com a do flex 1 (diferença < 300) é tratada como fantasma e não detecta: apertar mais forte. Na montagem do GUIA deve ficar `não detectado` |

## 6. Fase F4 — Motor, LED e botão

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V4.1 | `motor` | Vibra 1 s; LED azul acende junto | **LED acende e motor não**: problema no circuito do motor (pernas do transistor E-B-C, tabela da seção 4 do montagem.pdf; diodo invertido; resistor de 1 kΩ). **Nem o LED**: firmware não está rodando o comando |
| V4.2 | Motor vibra **o tempo todo**, mesmo sem comando | não acontece | Coletor/emissor trocados ou motor direto no GND (hardware) |
| V4.3 | `set motor 50` → `motor` → `set motor 100` | Vibração mais fraca a 50% | Se não muda: o motor está ligado direto (sem PWM pelo transistor). Se a 100% vibra fraco ou faz ruído: testar `MOTOR_LEDC_FREQ` 1000 em `src/config.h:39` (alguns transistores/motores respondem mal a 20 kHz) |
| V4.4 | Com o celular conectado no Wi-Fi, `motor` 5 vezes seguidas | **Não reinicia** (não aparece o banner de boot de novo) | Reiniciou = queda de tensão. Hardware: capacitor 100 µF perto do motor, cabo USB melhor/curto, power bank melhor. Firmware já reduz a potência do Wi-Fi (11 dBm em `src/web.cpp`); pode baixar para `WIFI_POWER_8_5dBm` |
| V4.5 | Apertar o botão e ficar parado | 1 pulso curto + `[calib] calibrando...`; ~2,5 s depois `[calib] concluída` + **2 pulsos curtos** | Nada: botão nas pernas erradas (usar diagonal: D27 e GND). `diag` mostra `apertado` com o botão solto = botão ligado errado (curto para GND). **1 pulso longo** + `recusada: mexeu` = moveu durante a média: repetir parado (se acontecer sempre parado, subir `CALIBRACAO_GIRO_MAX`, `src/config.h:59`) |
| V4.6 | Apertar o botão rápido várias vezes | Uma calibração por aperto, sem travar | — |

## 7. Fase F5 — Wi-Fi e app no celular dela

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V5.1 | No celular: Wi-Fi `BioSpine`, senha `colete123` | Conecta | Rede não aparece: ver serial `[wifi] falha ao criar o Access Point`. Senha recusada: conferir `src/config.h:65` |
| V5.2 | Android avisa "sem internet" | Tocar **Manter conexão** / "Usar sem internet" | — |
| V5.3 | Abrir `http://192.168.4.1` | A página carrega em poucos segundos | **Não carrega com dados móveis ligados** = o Android manda o tráfego pelo 4G: **desligar os dados móveis**. Digitar `http://` explicitamente (alguns navegadores forçam https) |
| V5.4 | Indicador de conexão da página | "conectado"; números mudando ~10×/s | Página abre mas não conecta: WebSocket na porta 81 (`ws://192.168.4.1:81/`). Testar no PC conectado no mesmo AP |
| V5.5 | Inclinar o colete | O número grande (desvio) acompanha sem atraso perceptível | Atraso grande: muitos clientes conectados ou sinal fraco |
| V5.6 | Botão **Calibrar** no app | Aviso "Calibrando..." → "Postura calibrada"; motor dá 2 pulsos; o estado passa a "calibrado" | — |
| V5.7 | Botão **Testar motor** | Vibra 1 s | — |
| V5.8 | Mudar limite/histerese/tempo no app e salvar | Aviso "Parâmetros salvos"; `get` na serial mostra os valores novos | — |
| V5.9 | Desligar o Wi-Fi do celular 10 s e religar | A página mostra "desconectado" e reconecta sozinha em ~2 s após a volta | Recarregar a página |
| V5.10 | "Adicionar à tela inicial" | Ícone na tela; abre a página | Opcional |
| V5.11 | Botão de baixar CSV: baixar e abrir | Cabeçalho `t,desvio,limite,pitch,roll,flex,flexPct,limiteFlex,estado,alerta`, decimal com ponto. `~/.venvs/colete/bin/python tools/grafico_serial.py --csv arquivo.csv` gera o PNG | Avisar; o gráfico pode ser feito pela serial (F8) |
| V5.12 | **Teste da arquitetura de duas tarefas:** colete em ALERTA (motor pulsando) com o app aberto → desligar o Wi-Fi do celular de repente (ou sair de alcance) e esperar 30 s; depois endireitar a postura | O motor **continua** pulsando 300/700 normalmente e **para** ao endireitar, mesmo com o celular fora. A serial pode ficar alguns segundos sem responder (normal: ela está no `loop()`, junto com o Wi-Fi) | Se o motor travar ligado/desligado ou o colete parar de responder à postura: a tarefa de controle não está isolada. Registrar a saída da serial e avisar (ver `src/main.cpp`, `tarefaControle`) |
| V5.13 | 3D da página: inclinar para a frente | O boneco inclina para a frente | Marcar "Inverter frente/trás" / "Inverter lado" na página (fica salvo no navegador) |
| V5.14 | Modo **Expandir** da vista do colete (bom para a apresentação) | Manequim em tela cheia com a leitura de cada sensor no corpo, atualizando em tempo real; sair volta ao painel | Se a tela cheia não abrir (iPhone/Safari não permite), o modo ainda ocupa a página inteira: seguir |

## 8. Fase F6 — Detecção com o colete vestido (o teste que importa)

Pessoa vestindo o colete, MPU entre as escápulas, flex ao longo da coluna torácica, motor perto do ombro/nuca.

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V6.1 | Postura correta → botão de calibrar → ficar parada 1 s | 2 pulsos; `desvio` ≈ 0 | — |
| V6.2 | Curvar ("corcunda") e manter | App: **ATENÇÃO** na hora; depois de **3 s**: **ALERTA** + motor pulsando 300 ms/700 ms | Não chega a ATENÇÃO: o desvio realista não passa de 20° → baixar o limite (V6.4) |
| V6.3 | Voltar à postura correta | Motor para; estado OK | Continua vibrando um pouco acima do normal: é a histerese (precisa ficar abaixo de limite − histerese); se incomodar, aumentar a histerese ou o limite |
| V6.4 | Ajuste fino no corpo dela | Anotar desvio da postura ruim típica: ___° e da postura normal mexendo (andar, sentar, pegar algo): ___°. Escolher o limite entre os dois (sugestão: 60–70% da postura ruim). Idem para `flexlim` com o `flexPct` | — |
| V6.5 | Movimentos normais: andar, sentar, virar o tronco, pegar algo no chão rápido | **Não vibra** em movimentos curtos (< tempo) | Vibra à toa → aumentar `tempo` (ex.: 4000–5000) ou o limite |
| V6.6 | Inclinação lateral sustentada (> limite, > 3 s) | Vibra (o desvio é total, inclui o lateral) | Se não for desejado, avisar |
| V6.7 | Desligar e ligar a placa | Continua calibrado (`get` → `calibração ... salva`) e com os parâmetros ajustados; volta a alertar sem recalibrar | Perdeu = problema na NVS: registrar |
| V6.8 | Flex desconectado com o colete em uso (puxar um fio do flex) | Em ~1 s o flex fica inativo, **não vibra** por isso, e o MPU continua funcionando | — |

## 9. Fase F7 — Alimentação e apresentação

| ID | Ação | Critério | Se falhar |
|---|---|---|---|
| V7.1 | Ligar no **power bank** (sem o PC) e esperar 5 min parado com o app conectado | Continua ligado | Alguns power banks **desligam com consumo baixo** (~100 mA). Trocar o power bank, ou deixar o motor/app ativos. Registrar o modelo que funcionou |
| V7.2 | No power bank: forçar alerta 30 s | Não reinicia | Ver V4.4 |
| V7.3 | Colete funciona **sem celular** | Alerta sozinho | — |

## 10. Fase F8 — Material para o TCC e a apresentação

| ID | Ação | Critério |
|---|---|---|
| V8.1 | Fechar o monitor; `~/.venvs/colete/bin/python tools/grafico_serial.py -t 60` enquanto a pessoa alterna postura correta → corcunda (> 3 s) → correta → dobra só o flex | Gera `postura_*.csv` e `postura_*.png` com desvio, limite, faixas de alerta e flex |
| V8.2 | Gravar **vídeo**: colete vibrando + tela do app mostrando ALERTA | Vídeo salvo (reserva para a apresentação) |
| V8.3 | Fotos da montagem final (de cima, com luz boa) | Para o TCC |
| V8.4 | Anotar os parâmetros finais (`get`) | Para o texto do TCC |

## 11. (Só se usarem a montagem/protocolo do BioSpine V3)

| ID | Ação | Critério |
|---|---|---|
| V9.1 | `STATUS` | `{"angle":..,"flex_left":..,"flex_right":..,"pressure":..,"status":"GOOD","alert":false,"calibrated":..}` |
| V9.2 | `START`, aguardar 3 s, `STOP` | JSON a cada 1 s; para no STOP |
| V9.3 | `CALIBRATE` | `{"success":true,"message":"Calibration completed"}` após ~1 s |

---

## 12. Ajustes possíveis sem recompilar (serial ou app; salvos na NVS)

| Parâmetro | Serial | Padrão | Faixa | Quando mexer |
|---|---|---|---|---|
| Limite de inclinação | `set limite 20` | 20° | 2–60 | Postura ruim real não passa do limite / vibra à toa |
| Histerese | `set histerese 3` | 3 | 0 a (menor limite − 1) | Liga/desliga perto do limite |
| Tempo mínimo | `set tempo 3000` | 3000 ms | 0–60000 | Vibra em movimentos curtos |
| Limite do flex | `set flexlim 15` | 15 % | 2–90 | Conforme o `flexPct` medido em V3.3 |
| Intensidade | `set motor 100` | 100 % | 0–100 | Conforto |
| Flex 1 / Flex 2 | `set flex on\|off\|auto`, `set flex2 ...` | flex: auto; flex2: **off** | — | Forçar quando a detecção errar; flex2 só com 2º sensor ligado |
| Voltar ao padrão | `reset` | — | — | Mantém a calibração |

## 13. Ajustes que exigem recompilar (`src/config.h`)

Depois de qualquer mudança: `pio test -e native && tools/sim.sh > /dev/null && pio run -t upload`.

| Constante | Linha | Padrão | Quando mexer |
|---|---|---|---|
| `MPU_I2C_HZ` | 24 | 100000 | Pode subir para 400000 se o I2C estiver estável (não é necessário) |
| `MPU_FILTRO_GIRO` | 25 | 0.98 | Desvio ruidoso (subir para 0.99) ou lento para responder (descer para 0.95) |
| `GIRO_PARADO_DPS` | 27 | 5 | `giro sem offset` mesmo parado (subir para 8) |
| `ADC_MIN_VALIDO` / `ADC_MAX_VALIDO` | 31–32 | 100 / 4000 | Flex real com leitura fora disso (ex.: resistor diferente de 10 kΩ) |
| `ADC_RUIDO_MAX` | 33 | 300 | Flex real pisca entre ativo/inativo com Wi-Fi (subir) |
| `FLEX_LIGA_MS` / `FLEX_DESLIGA_MS` | 35–36 | 2000 / 1000 | Detecção de presença do flex lenta/rápida demais |
| `ADC_FANTASMA_DIF` | 40 | 300 | FSR real não detecta por ler parecido com o flex |
| `MOTOR_LEDC_FREQ` | 44 | 20000 | Motor fraco ou com ruído (testar 1000) |
| `MOTOR_ALERTA_ON_MS` / `OFF_MS` | 47–48 | 300 / 700 | Padrão de vibração |
| `CALIBRACAO_ESPERA_MS` / `CALIBRACAO_GIRO_MAX` | 57 / 59 | 1500 / 30 | Calibração lenta demais / recusada sem motivo |
| `WIFI_SSID` / `WIFI_SENHA` | 64–65 | BioSpine / colete123 | Se pedirem outro nome (senha ≥ 8 caracteres) |
| Padrões `PADRAO_*` | 70–74 | — | Para deixar os valores calibrados no corpo dela como padrão de fábrica |
| Potência do Wi-Fi | `src/web.cpp` (`setTxPower`) | 11 dBm | Reinícios com o motor (baixar para 8,5 dBm) |

Mudança no `src/tilt.cpp`, `src/posture.cpp` ou `src/filters.h` → **obrigatório** `pio test -e native`.

## 14. Diagnóstico rápido por sintoma

| Sintoma | Causa provável | Ver |
|---|---|---|
| PC não vê a placa | cabo só de carga; brltty; falta dialout | V0.1, V0.2, V1.1 |
| Gravação falha em `Connecting...` | segurar BOOT; velocidade | V1.2 |
| `I2C scan ... nada` | SDA/SCL trocados, alimentação do MPU | V2.1 |
| Desvio pula ou "repica" em movimento rápido | sinal do giroscópio | V2.6 |
| Desvio certo mas pitch com sinal trocado | sensor virado para o corpo (só rótulo) | V2.8 |
| Flex sempre 4095 / 0 | resistor faltando / flex solto | V3.1 |
| Vibra sozinho, sem motivo aparente | flex2 em `auto`/`on` sem sensor; limite baixo demais; tempo curto | V3.4, V6.4, V6.5 |
| Calibração dá 1 pulso longo | mexeu durante a média | V4.5 |
| Reinicia com `Guru Meditation` / `watchdog` | bug da arquitetura de duas tarefas | V5.12; registrar o backtrace |
| Nunca vibra | não calibrado; limite alto demais; circuito do motor | V6.1, V6.2, V4.1 |
| Reinicia quando o motor liga | queda de tensão | V4.4 |
| App não abre | dados móveis ligados; `https://` | V5.3 |
| App abre mas "desconectado" | WebSocket :81 | V5.4 |
| Desliga no power bank | power bank com auto-off | V7.1 |

## 15. Achados da revisão de código (20/10, antes do teste)

Uma revisão independente (leitura do código + do core 2.0.17 e das libs) encontrou o seguinte. O que foi **corrigido no mesmo dia** foi testado com `pio test -e native`, os 3 roteiros do simulador e o `pio run`; falta confirmar no hardware.

| # | Achado | Situação | Validar em |
|---|---|---|---|
| R1 | **Celular que some trava o `loop()` por ~10 s a cada envio** (`WiFiClient::write`: 10 tentativas × 1 s, constantes fixas no core; o heartbeat do WebSocket também fica parado). Motor podia ficar preso ligado e a postura congelava. | **Corrigido**: controle numa tarefa FreeRTOS separada (decisão 5) | **V5.12** |
| R2 | Reconexão do MPU bloqueava ~1,25 s (medição do offset do giroscópio) | **Corrigido**: na reconexão não mede o offset; o offset é recalculado sozinho com o sensor parado | V2.9 |
| R3 | GPIO35 solto vira um "flex2 fantasma" (o capacitor do ADC guarda a tensão do GPIO34) e passa na checagem de ruído | **Corrigido**: flex2 desligado por padrão (decisão 6) | V3.4, V3.5 |
| R4 | Calibração começava no clique (pegava o braço voltando do botão) | **Corrigido**: espera 1,5 s e recusa se mexer (decisão 7) | V4.5, V6.1 |
| R5 | Ligar com o colete vestido deixava o giroscópio sem offset (até ~5° de erro) | **Corrigido**: offset recalculado sozinho sempre que o sensor fica parado ~1 s | V2.3 |
| R6 | Clones MPU-6500/9250: o acelerômetro ficava sem filtro (vibração do motor entra) | **Corrigido**: escreve 0x1D=0x03 para WHO_AM_I 0x70/71/73/74 | V2.2 |
| R7 | I2C a 400 kHz com fio longo perto do PWM do motor | **Corrigido**: 100 kHz | V2.1, V2.4 com o motor ligado |
| R8 | A base do NPN (GPIO25) flutua no boot e durante a gravação (tranco no motor) | **Hardware, não corrigido**: se o motor der tranco ao ligar/gravar, colocar 10–100 kΩ entre a base e o GND | V1.2, V1.3 |
| R9 | O FSR (GPIO32) também sofre o fantasma | **Mitigado**: leitura parecida com a do flex 1 (< 300 de diferença) não detecta. É só telemetria | V3.6 |

Conferido e sem problema: sinal do giroscópio × acelerômetro (mesma convenção da MPU6050_light; mesmo assim validar V2.6), start repetido do Wire, Preferences só-leitura sem namespace, LEDC 20 kHz/8 bits, ADC1 com Wi-Fi, `broadcastTXT` dentro do callback do WebSocket, overflow do `millis()`, GPIO2.

## 16. Registro dos resultados (preencher in loco)

Montagem usada: [ ] GUIA/montagem.pdf  [ ] BioSpine V3  [ ] outra: ________
Transistor: ________  WHO_AM_I do MPU: ______  Power bank: ________

| ID | OK / Falhou / N/A | Observação (valores medidos, o que foi feito) |
|---|---|---|
| V0.1–V0.4 | | |
| V1.1–V1.5 | | |
| V2.1 | | |
| V2.2 | | |
| V2.3 | | |
| V2.4 | | ruído do desvio parado: ___° |
| V2.5 | | inclinação 30° → desvio ___° |
| V2.6 | | |
| V2.7 | | |
| V2.8 | | sinal do pitch: ___ |
| V2.9 | | |
| V3.1 | | flex reto: ____ |
| V3.2 | | amplitude com Wi-Fi: ____ |
| V3.3 | | flexPct na dobra realista: ___% |
| V3.4–V3.5 | | flex2 solto: leitura ____ amplitude ____ |
| V3.6 | | |
| V4.1–V4.6 | | |
| V5.1–V5.11 | | modelo do celular: ____ |
| V5.12 | | motor seguiu funcionando com o celular fora? ____ |
| V5.13 | | inversões marcadas na página: ____ |
| V5.14 | | |
| V6.1–V6.8 | | desvio postura ruim: ___°; parâmetros finais: limite __ hist __ tempo __ flexlim __ |
| V7.1–V7.3 | | |
| V8.1–V8.4 | | |

**Ao terminar:** se algo foi alterado no código, rodar `pio test -e native`, `tools/sim.sh`, `tools/gerar_bin.sh` e
atualizar este arquivo e o `README.md` com o que mudou e por quê.
