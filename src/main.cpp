// Colete Postural — firmware ESP32
//
// Duas tarefas:
//   controle (FreeRTOS, prioridade maior): sensores -> filtro -> postura -> motor, botão, calibração
//   loop() do Arduino:                     Wi-Fi (página + WebSocket) e serial
//
// O Wi-Fi pode travar o loop por vários segundos (celular que sai do alcance faz o envio TCP
// esperar). Separado assim, o colete continua detectando e vibrando mesmo nessa hora: ele
// funciona sozinho, sem celular e sem computador. As tarefas conversam só por:
//   pedidos (loop -> controle): calibrar, testar motor, nova configuração
//   eventos (controle -> loop): mensagens para a serial e o app
//   cópias protegidas de Status / Config / Calibracao
//
// Regra de ouro: a tarefa de controle NUNCA chama Serial nem web_* (podem bloquear). Para avisar
// alguém, ela usa emitir(), e o loop entrega a mensagem em drenarEventos().
//
// Também implementa as funções app_* declaradas em app.h (a API usada pela serial e pelo app).
// Documentação: Documentação/referencia/main.md e Documentação/02-arquitetura.md
#include <Arduino.h>

#include "app.h"
#include "config.h"
#include "console.h"
#include "motor.h"
#include "posture.h"
#include "sensors.h"
#include "settings.h"
#include "web.h"

// ------------------------------------------------------------------ sincronização

// A trava é um spinlock do FreeRTOS (portMUX): enquanto está fechada, as interrupções do núcleo
// ficam desligadas e nenhuma outra tarefa entra. Por isso só se copia dados com ela fechada
// (alguns bytes, microssegundos); nunca se chama Serial, NVS ou Wi-Fi lá dentro.
#ifdef SIMULADOR  // no PC tudo roda numa linha só (sim/sim_main.cpp)
#define TRAVAR()
#define DESTRAVAR()
#else
static portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
#define TRAVAR() portENTER_CRITICAL(&trava)
#define DESTRAVAR() portEXIT_CRITICAL(&trava)
#endif

// Protegidos pela trava (lidos/escritos pelas duas tarefas)
static Config cfg;                     // configuração em vigor (escrita pelo loop, lida pelo controle)
static Calibracao cal;                 // referência da postura correta (escrita pelo controle)
static Status stPublico;               // última foto publicada pelo controle (lida pelo loop)
static int8_t pedidoCalibracao = -1;  // Origem, ou -1
static bool pedidoMotorTeste = false;  // loop pediu o pulso de teste de 1 s

// Fila de eventos controle -> loop
// Flags extras de um evento: EVT_CONFIG = reenviar o {"t":"config"} ao app (ex.: "calibrado"
// mudou); EVT_JSON_CALIB_OK = responder em JSON ao CALIBRATE do BioSpine V3.
enum { EVT_CONFIG = 1, EVT_JSON_CALIB_OK = 2 };
// Uma mensagem pronta, com tamanho fixo (sem alocação dinâmica dentro da tarefa de controle).
// Textos maiores que o campo são cortados pelo strlcpy.
struct Evento {
  char tag[8];       // prefixo da serial: "[calib]", "[mpu]"
  char nivel[6];     // nível do aviso no app: "info", "ok" ou "erro"
  char console[96];  // texto para a serial ("" = não imprime)
  char app[64];      // texto curto para o aviso (toast) do app ("" = não envia)
  uint8_t flags;     // EVT_CONFIG | EVT_JSON_CALIB_OK
};
// Fila circular: filaIni = posição do mais antigo, filaN = quantos há.
// Cheia (8 eventos sem o loop drenar), o evento novo é descartado: perder um aviso é melhor
// que travar a tarefa de controle esperando o Wi-Fi.
static const int FILA_N = 8;
static Evento fila[FILA_N];
static int filaIni = 0, filaN = 0;

// Só da tarefa de controle
static DetectorPostura detector;                // máquina de estados ok/atenção/alerta
static Calibrador calibrador;                   // média da postura correta durante a calibração
static Origem origemCalibracao = ORIGEM_BOTAO;  // quem pediu a calibração em andamento
static Status st;                               // foto em construção (copiada para stPublico no fim do passo)
static Params params;  // cópia local de cfg.params
static uint8_t intensidadeAplicada = 255;  // última intensidade passada ao motor (255 = nenhuma ainda)
static Vec3 gBoot = {0, 1, 0};  // referência antes da 1ª calibração (só para mostrar movimento)
static bool temGBoot = false;   // gBoot já foi gravado com uma leitura real do MPU
static bool mpuAvisado = true;  // último estado do MPU já avisado (para avisar só quando muda)
static uint32_t ultimoPostura = 0;  // millis() da última atualizarPostura()

// Só do loop
static uint32_t ultimoEnvio = 0;  // millis() do último envio de dados (app e plotter)

// ------------------------------------------------------------------ eventos

// Põe uma mensagem na fila para o loop entregar (serial e/ou app). Chamada pela tarefa de
// controle. console/app nulos ou "" = não entrega naquele canal. Fila cheia = descarta.
static void emitir(const char* tag, const char* nivel, const char* console, const char* app, uint8_t flags = 0) {
  Evento e;
  strlcpy(e.tag, tag, sizeof(e.tag));
  strlcpy(e.nivel, nivel, sizeof(e.nivel));
  strlcpy(e.console, console ? console : "", sizeof(e.console));
  strlcpy(e.app, app ? app : "", sizeof(e.app));
  e.flags = flags;
  // Monta o evento fora da trava e só copia para a fila dentro dela (trava curta).
  TRAVAR();
  if (filaN < FILA_N) {
    fila[(filaIni + filaN) % FILA_N] = e;
    filaN++;
  }
  DESTRAVAR();
}

// Entrega todos os eventos pendentes (chamada pelo loop a cada volta). Tira um evento por vez
// com a trava fechada e o entrega com ela aberta: Serial e WebSocket podem demorar.
static void drenarEventos() {
  for (;;) {
    Evento e;
    TRAVAR();
    bool tem = filaN > 0;
    if (tem) {
      e = fila[filaIni];
      filaIni = (filaIni + 1) % FILA_N;
      filaN--;
    }
    DESTRAVAR();
    if (!tem) return;
    if (e.console[0]) console_evento(e.tag, e.console);
    if (e.flags & EVT_JSON_CALIB_OK) console_respostaJson(true, "Calibration completed");
    if (e.app[0]) web_evento(e.nivel, e.app);
    if (e.flags & EVT_CONFIG) web_enviarConfig();
  }
}

// ------------------------------------------------------------------ app_* (chamados do loop: serial e web)

// Registra o pedido; quem executa é a tarefa de controle no próximo passo (processarPedidos).
// A checagem do MPU aqui serve para responder na hora a quem pediu; o controle confere de novo.
bool app_calibrar(Origem origem) {
  if (!sensores().mpuOk) {
    console_evento("calib", "MPU-6050 não encontrado: confira os fios e rode 'diag'");
    web_evento("erro", "MPU-6050 não encontrado");
    return false;
  }
  TRAVAR();
  pedidoCalibracao = origem;
  DESTRAVAR();
  return true;
}

// Só marca o pedido; o pulso é disparado pela tarefa de controle (dona do motor).
void app_testarMotor() {
  TRAVAR();
  pedidoMotorTeste = true;
  DESTRAVAR();
}

// Cópia da configuração (a struct inteira, para nunca ler meio atualizada).
Config app_config() {
  TRAVAR();
  Config c = cfg;
  DESTRAVAR();
  return c;
}

// Cópia da calibração em vigor.
Calibracao app_calibracao() {
  TRAVAR();
  Calibracao c = cal;
  DESTRAVAR();
  return c;
}

// Nova configuração vinda da serial ("set", "reset") ou do app ({"cmd":"set"}, "reset").
// Ordem: valida (prende cada valor na faixa segura) -> troca em memória -> grava na NVS ->
// avisa todos os clientes do app. A tarefa de controle pega os parâmetros novos no próximo
// processarPedidos() (em até ~1 ms).
void app_definirConfig(const Config& c) {
  Config v = c;
  v.params = validarParams(v.params);
  TRAVAR();
  cfg = v;
  DESTRAVAR();
  settings_salvarConfig(v);
  web_enviarConfig();
}

// Cópia da última foto publicada pela tarefa de controle.
Status app_status() {
  TRAVAR();
  Status s = stPublico;
  DESTRAVAR();
  return s;
}

// ------------------------------------------------------------------ tarefa de controle

// Debounce do botão: botaoLido = última leitura crua, botaoEstavel = valor aceito,
// botaoMudou = quando a leitura crua mudou pela última vez. HIGH = solto (INPUT_PULLUP).
static bool botaoEstavel = HIGH, botaoLido = HIGH;
static uint32_t botaoMudou = 0;

// Começa uma calibração: 1 pulso curto ("fique parado"), espera CALIBRACAO_ESPERA_MS para a
// pessoa se ajeitar e depois faz a média por CALIBRACAO_MS (ver Calibrador em posture.h).
// Ignora o pedido se já houver uma em andamento (vários cliques no botão = uma calibração).
static void iniciarCalibracao(Origem origem, uint32_t agora) {
  if (!sensores().mpuOk) {
    emitir("calib", "erro", "MPU-6050 não encontrado: confira os fios e rode 'diag'", "MPU-6050 não encontrado");
    return;
  }
  if (calibrador.ativo()) return;
  origemCalibracao = origem;
  calibrador.iniciar(agora, CALIBRACAO_ESPERA_MS, CALIBRACAO_MS);
  detector.reset();  // zera o estado: a postura antiga não vale mais e o motor para
  motor_pulsos(1, MOTOR_CONFIRMA_MS, 0);  // 1 pulso: "começou, fique parado"
  emitir("calib", "info", "calibrando... fique parado na postura correta (2,5 s)", "Calibrando... fique parado");
}

// Fim da média: aceita ou recusa a calibração.
//   recusada (mexeu mais que CALIBRACAO_GIRO_MAX °/s): 1 pulso longo, mantém a calibração antiga
//   aceita: grava g0/flex0/flex20 em memória e na NVS, 2 pulsos curtos, avisa serial e app
static void concluirCalibracao() {
  calibrador.encerrar();
  // A média de quem se mexeu não representa uma postura: melhor pedir para repetir.
  if (calibrador.giroMax() > CALIBRACAO_GIRO_MAX) {
    motor_pulsos(1, MOTOR_ERRO_MS, 0);
    char msg[96];
    snprintf(msg, sizeof(msg), "recusada: mexeu durante a média (%.0f °/s). Tente de novo, parado.", calibrador.giroMax());
    emitir("calib", "erro", msg, "Mexeu durante a calibração: tente de novo");
    return;
  }

  const Leituras& L = sensores();
  Config c = app_config();
  Calibracao nova;
  nova.valida = true;
  nova.g0 = calibrador.gravidade();
  // A referência do flex só é gravada se o sensor estiver ligado agora (detectado, ou forçado
  // com "on"). Referência 0 = sensor fica fora da detecção até a próxima calibração (sensorAtivo).
  nova.flex0 = L.flex.detectado || c.flexModo == MODO_ON ? calibrador.flex() : 0;
  nova.flex20 = L.flex2.detectado || c.flex2Modo == MODO_ON ? calibrador.flex2() : 0;
  TRAVAR();
  cal = nova;
  DESTRAVAR();
  settings_salvarCalibracao(nova);  // NVS: sobrevive a desligar a placa
  detector.reset();
  motor_pulsos(2, MOTOR_CONFIRMA_MS, MOTOR_CONFIRMA_MS);  // 2 pulsos: "pronto"

  char msg[96];
  snprintf(msg, sizeof(msg), "concluída (%d amostras, flex %.0f, flex2 %.0f)", calibrador.amostras(), nova.flex0,
           nova.flex20);
  // EVT_CONFIG: o app atualiza o "calibrado" do {"t":"config"}.
  uint8_t flags = EVT_CONFIG | (origemCalibracao == ORIGEM_SERIAL_JSON ? EVT_JSON_CALIB_OK : 0);
  emitir("calib", "ok", msg, "Postura calibrada", flags);
}

// Lê e zera os pedidos do loop numa trava só, e copia os parâmetros em vigor para "params"
// (cópia local: o resto do passo usa os parâmetros sem precisar travar de novo).
static void processarPedidos(uint32_t agora) {
  TRAVAR();
  int8_t calib = pedidoCalibracao;
  bool motorTeste = pedidoMotorTeste;
  pedidoCalibracao = -1;
  pedidoMotorTeste = false;
  params = cfg.params;
  DESTRAVAR();

  if (calib >= 0) iniciarCalibracao((Origem)calib, agora);
  if (motorTeste) motor_pulsos(1, MOTOR_TESTE_MS, 0);
  // Só reconfigura o PWM quando a intensidade muda (não a cada passo).
  if (params.intensidade != intensidadeAplicada) {
    intensidadeAplicada = params.intensidade;
    motor_intensidade(intensidadeAplicada);
  }
}

// Botão com debounce: a leitura só é aceita depois de ficar igual por BOTAO_DEBOUNCE_MS.
// Calibra na borda de descida (solto -> apertado), uma vez por aperto; segurar não repete.
static void atualizarBotao(uint32_t agora) {
  bool v = digitalRead(PIN_BUTTON);
  if (v != botaoLido) {
    botaoLido = v;
    botaoMudou = agora;  // mudou: recomeça a contar o tempo de estabilidade
  }
  if (botaoLido != botaoEstavel && agora - botaoMudou >= BOTAO_DEBOUNCE_MS) {
    botaoEstavel = botaoLido;
    if (botaoEstavel == LOW) iniciarCalibracao(ORIGEM_BOTAO, agora);
  }
}

// Decide se um sensor flex entra na detecção de má postura.
//   modo off  -> nunca
//   modo on   -> sempre, desde que a calibração tenha uma referência válida dele
//   modo auto -> só se o sensor estiver detectado agora E tiver referência válida
// Sem referência válida não há com o que comparar (flexPct sem sentido): fica fora.
static bool sensorAtivo(ModoSensor modo, bool detectado, float referencia) {
  if (modo == MODO_OFF) return false;
  // A referência precisa ter sido gravada com o sensor ligado.
  bool refOk = referencia >= ADC_MIN_VALIDO && referencia <= ADC_MAX_VALIDO;
  if (modo == MODO_ON) return refOk;
  return detectado && refOk;
}

// Coração do colete (50 Hz, PERIODO_POSTURA_MS): leituras -> desvio e curvaturas -> estado ->
// motor. Também conduz a calibração em andamento (coleta das amostras e conclusão).
static void atualizarPostura(uint32_t agora) {
  const Leituras& L = sensores();
  Config c = app_config();
  Calibracao k = app_calibracao();

  // Avisa só na mudança (MPU caiu / voltou), não a cada passo.
  if (L.mpuOk != mpuAvisado) {
    mpuAvisado = L.mpuOk;
    if (L.mpuOk) {
      emitir("mpu", "ok", "MPU-6050 reconectado", "MPU-6050 reconectado");
    } else {
      emitir("mpu", "erro", "MPU-6050 parou de responder (fio solto?)", "MPU-6050 desconectado");
    }
  }
  // Sem calibração ainda, mede o desvio em relação à posição do boot: o app já mostra o
  // movimento, mas o detector não alerta (Entrada::calibrado = false).
  if (L.mpuOk && !temGBoot) {
    gBoot = L.g;
    temGBoot = true;
  }
  Vec3 ref = k.valida ? k.g0 : gBoot;

  // Desvio = ângulo entre a gravidade atual e a de referência (não depende de como o sensor
  // foi preso). pitch/roll são só a decomposição frente/lado para mostrar (tilt.cpp).
  st.mpuOk = L.mpuOk;
  if (L.mpuOk) {
    st.desvio = angleBetweenDeg(L.g, ref);
    tiltComponentsDeg(L.g, ref, &st.pitch, &st.roll);
  } else {
    st.desvio = st.pitch = st.roll = 0;  // sem MPU só o flex pode alertar
  }

  // Flex 1 e 2: curvatura em % de queda em relação à leitura da calibração.
  st.flex = (int)L.flex.media;
  st.flexDetectado = L.flex.detectado;
  st.flexAtivo = sensorAtivo(c.flexModo, L.flex.detectado, k.flex0);
  st.flexPct = st.flexAtivo ? flexPct(L.flex.media, k.flex0) : 0;

  st.flex2 = (int)L.flex2.media;
  st.flex2Detectado = L.flex2.detectado;
  st.flex2Ativo = sensorAtivo(c.flex2Modo, L.flex2.detectado, k.flex20);
  st.flex2Pct = st.flex2Ativo ? flexPct(L.flex2.media, k.flex20) : 0;

  // FSR: só telemetria (vai para o app e para o JSON do BioSpine, não entra na detecção).
  st.fsr = (int)L.fsr.media;
  st.fsrDetectado = L.fsr.detectado;
  st.calibrado = k.valida;
  st.calibrando = calibrador.ativo();

  if (calibrador.ativo()) {
    // Durante a calibração não há alerta: a pessoa está se ajeitando de propósito.
    // A norma do giroscópio (°/s) entra para o Calibrador saber se ela se mexeu.
    if (calibrador.coletando(agora)) calibrador.amostra(L.g, L.flex.media, L.flex2.media, norm(L.gyro));
    if (calibrador.terminou(agora)) concluirCalibracao();
    st.estado = ESTADO_OK;
    st.tempoRuimMs = 0;
  } else {
    // Detecção: limite, histerese e tempo mínimo (DetectorPostura em posture.cpp).
    Entrada e;
    e.calibrado = k.valida;
    e.desvio = st.desvio;
    e.flexAtivo = st.flexAtivo;
    e.flexPct = st.flexPct;
    e.flex2Ativo = st.flex2Ativo;
    e.flex2Pct = st.flex2Pct;
    st.estado = detector.atualizar(e, params, agora);
    st.tempoRuimMs = detector.tempoRuimMs(agora);
  }

  // Só o estado "alerta" vibra; "atenção" é a contagem do tempo mínimo (sem vibrar).
  motor_alerta(st.estado == ESTADO_ALERTA);
}

// Um passo da tarefa de controle (~1 ms). Cada etapa tem o próprio período por millis():
// sensores_atualizar lê o MPU a 200 Hz e o ADC a 100 Hz; a postura roda a 50 Hz; o botão e o
// motor rodam todo passo (debounce e tempos dos pulsos precisam de resolução de ms).
// No fim publica a foto (st -> stPublico) para o loop.
static void passoControle(uint32_t agora) {
  processarPedidos(agora);
  sensores_atualizar(agora);
  atualizarBotao(agora);
  if (agora - ultimoPostura >= PERIODO_POSTURA_MS) {
    ultimoPostura = agora;
    atualizarPostura(agora);
  }
  motor_atualizar(agora);
  st.motor = motor_ligado();
  TRAVAR();
  stPublico = st;
  DESTRAVAR();
}

#ifndef SIMULADOR
// Corpo da tarefa de controle: um passo a cada 1 ms, para sempre. O vTaskDelay(1) devolve a
// CPU por 1 tick (1 ms) para o loop, o Wi-Fi e o watchdog rodarem.
static void tarefaControle(void*) {
  for (;;) {
    passoControle(millis());
    vTaskDelay(1);  // 1 ms
  }
}
#endif

// ------------------------------------------------------------------ setup / loop

// Boot: serial, botão, motor desligado, NVS (config + calibração), sensores (mede o offset do
// giroscópio: ~1 s parado), Wi-Fi/app e console. Por último cria a tarefa de controle.
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  motor_iniciar();  // primeiro: garante o motor desligado durante o resto do boot
  settings_carregar(&cfg, &cal);
  params = cfg.params;

  Serial.println();
  Serial.println(F("=== Colete Postural " FIRMWARE_VERSAO " ==="));
  Serial.println(F("Mantenha o colete parado por 2 s (calibrando o giroscópio)..."));
  sensores_iniciar();
  mpuAvisado = sensores().mpuOk;
  if (!mpuAvisado) Serial.println(F("[mpu] MPU-6050 não encontrado: rode 'diag'. Tentando de novo a cada 2 s."));
  web_iniciar();
  console_iniciar();

  // um pulso curto: "liguei"
  motor_pulsos(1, MOTOR_CONFIRMA_MS, 0);

#ifndef SIMULADOR
  // Núcleo 1 (o mesmo do loop), prioridade acima do loop (1): o controle sempre ganha a CPU.
  // Pilha de 8 KB. As tarefas internas do Wi-Fi do ESP32 ficam, por padrão, no núcleo 0.
  xTaskCreatePinnedToCore(tarefaControle, "controle", 8192, NULL, 3, NULL, 1);
#endif
}

// loop() do Arduino: só comunicação. Pode travar (Wi-Fi) sem afetar a detecção.
// No simulador ele também roda o passo de controle (não há tarefas no PC).
void loop() {
  uint32_t agora = millis();
#ifdef SIMULADOR
  passoControle(agora);
#endif

  drenarEventos();          // avisos da tarefa de controle -> serial e app
  console_atualizar(agora);  // comandos digitados na serial
  web_atualizar();          // HTTP (página) e WebSocket (comandos do app)

  // 10 Hz: dados para o app (WebSocket) e para o Serial Plotter / JSON do BioSpine.
  if (agora - ultimoEnvio >= PERIODO_ENVIO_MS) {
    ultimoEnvio = agora;
    Status s = app_status();
    web_enviarDados(s);
    console_enviarDados(s, agora);
  }
}
