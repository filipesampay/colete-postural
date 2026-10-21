# 02 — Arquitetura do firmware

> Código: `src/main.cpp`, `src/app.h`, todos os módulos de `src/` · Relacionados: [referencia/main.md](referencia/main.md), [10 — Decisões](10-decisoes-de-projeto.md)

## 1. Ideia geral

O firmware é um **pipeline** que se repete o tempo todo:

```
sensores → filtro → postura → motor
```

Em volta dele ficam as **interfaces** (serial e app web), que só leem o estado e fazem pedidos.
Duas escolhas organizam todo o código:

1. **Lógica pura separada do hardware.** O cálculo de inclinação (`tilt`), a decisão de postura (`posture`)
   e os filtros dos sensores (`filters`) não usam nada do Arduino. Por isso rodam e são testados no PC
   ([09](09-simulador-testes-ferramentas.md)).
2. **Duas tarefas.** Uma tarefa de controle, de prioridade maior, faz o pipeline; o `loop()` do Arduino
   cuida do Wi-Fi e da serial. Se o Wi-Fi travar, o colete continua vibrando ([D-12](10-decisoes-de-projeto.md#d-12)).

## 2. Camadas e módulos

Cada caixa é um módulo de `src/` (par `.h`/`.cpp`, ou só `.h`). A seta `A --> B` quer dizer "A usa B".

```mermaid
flowchart TB
  subgraph INT["Interfaces: rodam no loop"]
    CON["console<br/>comandos serial"]
    WEB["web<br/>Wi-Fi AP, HTTP, WebSocket"]
    PRO["protocolo<br/>JSON do app"]
    HTML["web/index.html<br/>página no celular"]
  end
  subgraph ORQ["Orquestração"]
    MAIN["main.cpp + app.h<br/>setup, loop, tarefa de controle,<br/>calibração, botão, fila de eventos"]
  end
  subgraph LOG["Lógica pura: testada no PC"]
    TILT["tilt<br/>vetor gravidade, filtro, ângulos"]
    POS["posture<br/>detector e calibrador"]
    FIL["filters.h<br/>média móvel, presença"]
  end
  subgraph DRV["Drivers e persistência"]
    SEN["sensors<br/>MPU, flex, flex 2, FSR"]
    MPU["mpu<br/>registradores do MPU-6050"]
    MOT["motor<br/>PWM e pulsos"]
    SET["settings<br/>NVS"]
  end
  CFG["config.h<br/>pinos, tempos, padrões"]

  HTML <-->|"WebSocket"| WEB
  WEB --> PRO
  PRO --> MAIN
  CON --> MAIN
  MAIN --> SEN
  MAIN --> POS
  MAIN --> MOT
  MAIN --> SET
  MAIN --> CON
  MAIN --> WEB
  SEN --> MPU
  SEN --> FIL
  SEN --> TILT
  POS --> TILT
  SET --> POS
  CFG -.-> MAIN
  CFG -.-> SEN
  CFG -.-> MOT
```

| Módulo | Papel | Roda em | Referência |
|---|---|---|---|
| `main.cpp`, `app.h` | Liga tudo: `setup`, `loop`, tarefa de controle, calibração, botão, estado compartilhado | as duas tarefas | [referencia/main.md](referencia/main.md) |
| `config.h` | Pinos, períodos, limites e valores padrão em um lugar só | — | [referencia/config.md](referencia/config.md) |
| `tilt` | Álgebra de vetores, filtro complementar sobre a gravidade, desvio, frente/lateral | controle, PC | [referencia/tilt.md](referencia/tilt.md) |
| `posture` | Parâmetros, detector de má postura (limite, histerese, tempo mínimo), calibrador | controle, PC | [referencia/posture.md](referencia/posture.md) |
| `filters.h` | Média móvel, presença com persistência, validade da leitura do ADC | controle, PC | [referencia/filters.md](referencia/filters.md) |
| `mpu` | Driver do MPU-6050 por registradores, offset do giroscópio | controle | [referencia/mpu.md](referencia/mpu.md) |
| `sensors` | Agenda e lê MPU (200 Hz) e ADCs (100 Hz); detecta sensores ligados | controle | [referencia/sensors.md](referencia/sensors.md) |
| `motor` | PWM do motor, alerta em pulsos, sequências de confirmação, LED | controle | [referencia/motor.md](referencia/motor.md) |
| `settings` | Lê e grava parâmetros e calibração na NVS | as duas | [referencia/settings.md](referencia/settings.md) |
| `console` | Comandos da serial, `diag`, saída do plotter, compatibilidade BioSpine | loop | [referencia/console.md](referencia/console.md) |
| `protocolo` | Monta e interpreta o JSON do app; independente do Wi-Fi | loop | [referencia/protocolo.md](referencia/protocolo.md) |
| `web` | Access Point, servidor HTTP (página), servidor WebSocket | loop | [referencia/web.md](referencia/web.md) |

## 3. Duas tarefas

O ESP32 roda FreeRTOS. O core Arduino cria uma tarefa para o `loop()` (prioridade 1, núcleo 1).
O `setup()` cria mais uma, a **tarefa de controle** (`tarefaControle`, prioridade 3, núcleo 1, pilha de 8 kB),
que chama `passoControle(millis())` e dorme 1 ms (`vTaskDelay(1)`), para sempre.

**Por quê:** o envio pelo Wi-Fi pode bloquear o `loop()` por segundos. Quando o celular sai do alcance,
o envio TCP do WebSocket fica esperando o buffer esvaziar. Com tudo numa linha só, o colete pararia de
detectar nesse tempo. Separado assim, a tarefa de controle sempre ganha a CPU (prioridade maior) e o
alerta não depende do Wi-Fi.

O diagrama (simplificado: cada seta mostra um exemplo de chamada) mostra o que cada tarefa faz e os três canais
protegidos entre elas. As exceções sem trava estão na seção 6.

```mermaid
flowchart TB
  subgraph CTRL["Tarefa de controle: prioridade 3, a cada 1 ms"]
    direction LR
    P1["processarPedidos"] --> P2["sensores_atualizar"]
    P2 --> P3["atualizarBotao"]
    P3 --> P4["atualizarPostura<br/>a cada 20 ms"]
    P4 --> P5["motor_atualizar"]
    P5 --> P6["publica Status"]
  end
  subgraph SH["Dados compartilhados, protegidos pela trava"]
    direction LR
    PED["Pedidos<br/>pedidoCalibracao<br/>pedidoMotorTeste"]
    FILA["Fila de eventos<br/>8 posições"]
    COP["Cópias<br/>cfg, cal, stPublico"]
  end
  subgraph LOOP["loop do Arduino: prioridade 1"]
    direction LR
    L1["drenarEventos"] --> L2["console_atualizar<br/>lê a serial"]
    L2 --> L3["web_atualizar<br/>HTTP e WebSocket"]
    L3 --> L4["a cada 100 ms:<br/>web_enviarDados<br/>console_enviarDados"]
  end
  PED -->|"lidos e zerados"| P1
  P4 -->|"emitir"| FILA
  P6 -->|"stPublico = st"| COP
  L2 -->|"app_calibrar<br/>app_testarMotor"| PED
  L3 -->|"app_definirConfig"| COP
  FILA -->|"retirados"| L1
  COP -->|"app_status<br/>app_config"| L4
```

### Regras de convivência

| Dado | Escrito por | Lido por | Como |
|---|---|---|---|
| `cfg` (Config) | loop (`app_definirConfig`) | controle (a cada passo) e loop | cópia sob a trava |
| `cal` (Calibracao) | controle (`concluirCalibracao`) | controle e loop | cópia sob a trava |
| `stPublico` (Status) | controle (fim de cada passo) | loop (`app_status`) | cópia sob a trava |
| `pedidoCalibracao`, `pedidoMotorTeste` | loop | controle | lidos e zerados sob a trava |
| fila de eventos | controle (`emitir`) | loop (`drenarEventos`) | anel de 8 posições sob a trava |

- A trava é um `portMUX` (seção crítica do FreeRTOS). Dentro dela só há **cópias de structs pequenas**:
  nada de serial, Wi-Fi ou NVS, porque a seção crítica desliga as interrupções do núcleo.
- **A tarefa de controle nunca chama `Serial` nem `web_*`.** Quando precisa avisar algo, chama `emitir()`,
  que põe um evento na fila. O loop retira e entrega para a serial (`console_evento`) e para o app (`web_evento`).
  Se a fila estiver cheia (8 eventos sem o loop retirar), o evento novo é descartado: o colete continua
  funcionando, só a mensagem se perde.
- A tarefa de controle trabalha numa cópia local do status (`st`) e só publica no fim do passo. O loop
  nunca vê um status pela metade.
- O **estado** de `sensors`, `motor`, `tilt`, `posture` e `filters` (filtro, detector, calibrador, médias, fases do
  motor) é usado só pela tarefa de controle (e pelo `setup()`, antes de a tarefa existir), por isso não tem trava
  própria. O loop também chama funções **sem estado** de `posture` (`validarParams`, `estadoNome`, `paramsPadrao`),
  o que é seguro. As leituras de `sensors` feitas pelo loop estão na seção 6.

### Modo simulador

Com `-DSIMULADOR` (compilação para o PC, `tools/sim.sh`), não há FreeRTOS: a trava vira vazia e o próprio
`loop()` chama `passoControle()` antes do resto. Todo o resto do código é o mesmo que roda na placa
([09](09-simulador-testes-ferramentas.md)).

## 4. Inicialização

O diagrama mostra a ordem do `setup()`. O motor é configurado primeiro, então o pino do motor já fica em
nível baixo (motor desligado) durante a espera de ~1 s do giroscópio. A configuração e a calibração salvas são
carregadas antes do `console_iniciar`, que já usa a calibração para o lembrete "Calibração pendente".

```mermaid
sequenceDiagram
  participant S as setup
  participant MOT as motor
  participant SET as settings NVS
  participant SEN as sensors e mpu
  participant WEB as web
  participant CON as console
  participant RTOS as FreeRTOS
  S->>S: Serial 115200 e botão INPUT_PULLUP
  S->>MOT: motor_iniciar: LEDC 20 kHz, motor desligado
  S->>SET: settings_carregar: Config e Calibracao
  S->>SEN: sensores_iniciar
  SEN->>SEN: I2C 100 kHz, ADC 12 bits 11 dB
  SEN->>SEN: procura MPU em 0x68 e 0x69, configura
  SEN->>SEN: calibrarGiro: cerca de 1 s parado
  SEN->>SEN: enche a média móvel dos ADCs
  S->>WEB: web_iniciar: AP BioSpine, HTTP 80, WS 81
  S->>CON: console_iniciar: mensagens de ajuda
  S->>MOT: 1 pulso de 120 ms: liguei
  S->>RTOS: cria tarefaControle, prioridade 3, núcleo 1
  Note over RTOS: daqui em diante: controle a cada 1 ms e loop em paralelo
```

## 5. Ciclo de controle e fluxo de dados

`passoControle` roda a cada ~1 ms, mas cada etapa tem o seu período, controlado por `millis()` (sem `delay`):

| Etapa | Período | Constante | Onde |
|---|---|---|---|
| Passo da tarefa de controle | ~1 ms | `vTaskDelay(1)` | `tarefaControle` em `main.cpp` |
| Leitura do MPU + filtro | 5 ms (200 Hz) | `PERIODO_MPU_MS` | `sensores_atualizar` |
| Leitura dos ADCs + média + presença | 10 ms (100 Hz) | `PERIODO_ADC_MS` | `sensores_atualizar` |
| Postura (desvio, detector, calibração, alerta) | 20 ms (50 Hz) | `PERIODO_POSTURA_MS` | `atualizarPostura` |
| Botão (debounce) | cada passo, estável por 50 ms | `BOTAO_DEBOUNCE_MS` | `atualizarBotao` |
| Motor (fases dos pulsos) | cada passo | — | `motor_atualizar` |
| Envio para app e plotter | 100 ms (10 Hz) | `PERIODO_ENVIO_MS` | `loop` |
| JSON contínuo do BioSpine (`START`) | 1 s | — | `console_enviarDados` |
| Nova tentativa de achar o MPU | 2 s | `MPU_REINICIO_MS` | `sensores_atualizar` |
| Heartbeat do WebSocket | 5 s | — | `web_iniciar` |

O diagrama mostra como uma leitura vira vibração e dado no celular.

```mermaid
flowchart LR
  A["MPU-6050<br/>acelerômetro g<br/>giroscópio °/s"] -->|"200 Hz"| B["TiltFilter<br/>filtro complementar"]
  B --> C["g: gravidade<br/>vetor unitário"]
  REF["referência g0<br/>da calibração"] --> D
  C --> D["angleBetweenDeg<br/>desvio °"]
  C --> E["tiltComponentsDeg<br/>frente e lateral °"]
  F["ADC flex<br/>0 a 4095"] -->|"100 Hz"| G["MediaMovel 10<br/>Presenca"]
  G --> H["flexPct %<br/>flexAtivo"]
  D --> I["DetectorPostura<br/>50 Hz"]
  H --> I
  I -->|"ok, atenção, alerta"| J["motor_alerta"]
  J --> K(["Motor em pulsos<br/>300 ms / 700 ms"])
  D --> ST["Status"]
  E --> ST
  H --> ST
  I --> ST
  ST -->|"10 Hz"| W(["App web<br/>JSON data"])
  ST -->|"10 Hz"| SP(["Serial<br/>plot on"])
```

Os capítulos seguintes explicam cada etapa:
inclinação em [03](03-algoritmo-inclinacao.md), decisão e calibração em [04](04-deteccao-e-calibracao.md),
flex e FSR em [05](05-sensores-analogicos.md), motor e memória em [06](06-motor-e-nvs.md),
serial e protocolo em [07](07-protocolo-e-interfaces.md), página em [08](08-app-web.md).

## 6. Limitações conhecidas da arquitetura

- O loop lê as `Leituras` dos sensores (`sensores()`) direto, sem trava, em dois lugares: na checagem
  do MPU de `app_calibrar` e no `diag`. São leituras de exibição de campos simples; no pior caso o `diag`
  mostra números de dois instantes diferentes.
- O `diag` faz a varredura do barramento I2C (`sensores_scanI2C`) a partir do loop, enquanto a tarefa de controle
  lê o MPU pelo mesmo barramento. Depende da trava interna da biblioteca `Wire` do core; só se confirma no hardware.
- A tarefa de controle não fala com Wi-Fi nem serial, mas tem esperas curtas e conhecidas: timeout do I2C
  (até 20 ms) com o MPU solto, ~150 ms quando o MPU volta (reset do chip em `Mpu6050::iniciar`) e a gravação da
  calibração na NVS.
- A calibração grava na NVS (`settings_salvarCalibracao`) a partir da tarefa de controle. Gravar na flash leva
  alguns milissegundos; acontece só uma vez por calibração, quando a pessoa está parada e o alerta está desligado.
- A concorrência das duas tarefas não é testada no PC (o simulador roda tudo numa linha só). Ela só pode ser
  conferida no hardware: pontos V1.3 e V5.12 do `VALIDACAO.md` e uso prolongado sem travamento nem reset.
