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

#ifdef SIMULADOR  // no PC tudo roda numa linha só (sim/sim_main.cpp)
#define TRAVAR()
#define DESTRAVAR()
#else
static portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
#define TRAVAR() portENTER_CRITICAL(&trava)
#define DESTRAVAR() portEXIT_CRITICAL(&trava)
#endif

// Protegidos pela trava (lidos/escritos pelas duas tarefas)
static Config cfg;
static Calibracao cal;
static Status stPublico;
static int8_t pedidoCalibracao = -1;  // Origem, ou -1
static bool pedidoMotorTeste = false;

// Fila de eventos controle -> loop
enum { EVT_CONFIG = 1, EVT_JSON_CALIB_OK = 2 };
struct Evento {
  char tag[8];
  char nivel[6];
  char console[96];
  char app[64];
  uint8_t flags;
};
static const int FILA_N = 8;
static Evento fila[FILA_N];
static int filaIni = 0, filaN = 0;

// Só da tarefa de controle
static DetectorPostura detector;
static Calibrador calibrador;
static Origem origemCalibracao = ORIGEM_BOTAO;
static Status st;
static Params params;  // cópia local de cfg.params
static uint8_t intensidadeAplicada = 255;
static Vec3 gBoot = {0, 1, 0};  // referência antes da 1ª calibração (só para mostrar movimento)
static bool temGBoot = false;
static bool mpuAvisado = true;
static uint32_t ultimoPostura = 0;

// Só do loop
static uint32_t ultimoEnvio = 0;

// ------------------------------------------------------------------ eventos

static void emitir(const char* tag, const char* nivel, const char* console, const char* app, uint8_t flags = 0) {
  Evento e;
  strlcpy(e.tag, tag, sizeof(e.tag));
  strlcpy(e.nivel, nivel, sizeof(e.nivel));
  strlcpy(e.console, console ? console : "", sizeof(e.console));
  strlcpy(e.app, app ? app : "", sizeof(e.app));
  e.flags = flags;
  TRAVAR();
  if (filaN < FILA_N) {
    fila[(filaIni + filaN) % FILA_N] = e;
    filaN++;
  }
  DESTRAVAR();
}

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

void app_testarMotor() {
  TRAVAR();
  pedidoMotorTeste = true;
  DESTRAVAR();
}

Config app_config() {
  TRAVAR();
  Config c = cfg;
  DESTRAVAR();
  return c;
}

Calibracao app_calibracao() {
  TRAVAR();
  Calibracao c = cal;
  DESTRAVAR();
  return c;
}

void app_definirConfig(const Config& c) {
  Config v = c;
  v.params = validarParams(v.params);
  TRAVAR();
  cfg = v;
  DESTRAVAR();
  settings_salvarConfig(v);
  web_enviarConfig();
}

Status app_status() {
  TRAVAR();
  Status s = stPublico;
  DESTRAVAR();
  return s;
}

// ------------------------------------------------------------------ tarefa de controle

static bool botaoEstavel = HIGH, botaoLido = HIGH;
static uint32_t botaoMudou = 0;

static void iniciarCalibracao(Origem origem, uint32_t agora) {
  if (!sensores().mpuOk) {
    emitir("calib", "erro", "MPU-6050 não encontrado: confira os fios e rode 'diag'", "MPU-6050 não encontrado");
    return;
  }
  if (calibrador.ativo()) return;
  origemCalibracao = origem;
  calibrador.iniciar(agora, CALIBRACAO_ESPERA_MS, CALIBRACAO_MS);
  detector.reset();
  motor_pulsos(1, MOTOR_CONFIRMA_MS, 0);  // 1 pulso: "começou, fique parado"
  emitir("calib", "info", "calibrando... fique parado na postura correta (2,5 s)", "Calibrando... fique parado");
}

static void concluirCalibracao() {
  calibrador.encerrar();
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
  nova.flex0 = L.flex.detectado || c.flexModo == MODO_ON ? calibrador.flex() : 0;
  nova.flex20 = L.flex2.detectado || c.flex2Modo == MODO_ON ? calibrador.flex2() : 0;
  TRAVAR();
  cal = nova;
  DESTRAVAR();
  settings_salvarCalibracao(nova);
  detector.reset();
  motor_pulsos(2, MOTOR_CONFIRMA_MS, MOTOR_CONFIRMA_MS);  // 2 pulsos: "pronto"

  char msg[96];
  snprintf(msg, sizeof(msg), "concluída (%d amostras, flex %.0f, flex2 %.0f)", calibrador.amostras(), nova.flex0,
           nova.flex20);
  uint8_t flags = EVT_CONFIG | (origemCalibracao == ORIGEM_SERIAL_JSON ? EVT_JSON_CALIB_OK : 0);
  emitir("calib", "ok", msg, "Postura calibrada", flags);
}

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
  if (params.intensidade != intensidadeAplicada) {
    intensidadeAplicada = params.intensidade;
    motor_intensidade(intensidadeAplicada);
  }
}

static void atualizarBotao(uint32_t agora) {
  bool v = digitalRead(PIN_BUTTON);
  if (v != botaoLido) {
    botaoLido = v;
    botaoMudou = agora;
  }
  if (botaoLido != botaoEstavel && agora - botaoMudou >= BOTAO_DEBOUNCE_MS) {
    botaoEstavel = botaoLido;
    if (botaoEstavel == LOW) iniciarCalibracao(ORIGEM_BOTAO, agora);
  }
}

static bool sensorAtivo(ModoSensor modo, bool detectado, float referencia) {
  if (modo == MODO_OFF) return false;
  // A referência precisa ter sido gravada com o sensor ligado.
  bool refOk = referencia >= ADC_MIN_VALIDO && referencia <= ADC_MAX_VALIDO;
  if (modo == MODO_ON) return refOk;
  return detectado && refOk;
}

static void atualizarPostura(uint32_t agora) {
  const Leituras& L = sensores();
  Config c = app_config();
  Calibracao k = app_calibracao();

  if (L.mpuOk != mpuAvisado) {
    mpuAvisado = L.mpuOk;
    if (L.mpuOk) {
      emitir("mpu", "ok", "MPU-6050 reconectado", "MPU-6050 reconectado");
    } else {
      emitir("mpu", "erro", "MPU-6050 parou de responder (fio solto?)", "MPU-6050 desconectado");
    }
  }
  if (L.mpuOk && !temGBoot) {
    gBoot = L.g;
    temGBoot = true;
  }
  Vec3 ref = k.valida ? k.g0 : gBoot;

  st.mpuOk = L.mpuOk;
  if (L.mpuOk) {
    st.desvio = angleBetweenDeg(L.g, ref);
    tiltComponentsDeg(L.g, ref, &st.pitch, &st.roll);
  } else {
    st.desvio = st.pitch = st.roll = 0;  // sem MPU só o flex pode alertar
  }

  st.flex = (int)L.flex.media;
  st.flexDetectado = L.flex.detectado;
  st.flexAtivo = sensorAtivo(c.flexModo, L.flex.detectado, k.flex0);
  st.flexPct = st.flexAtivo ? flexPct(L.flex.media, k.flex0) : 0;

  st.flex2 = (int)L.flex2.media;
  st.flex2Detectado = L.flex2.detectado;
  st.flex2Ativo = sensorAtivo(c.flex2Modo, L.flex2.detectado, k.flex20);
  st.flex2Pct = st.flex2Ativo ? flexPct(L.flex2.media, k.flex20) : 0;

  st.fsr = (int)L.fsr.media;
  st.fsrDetectado = L.fsr.detectado;
  st.calibrado = k.valida;
  st.calibrando = calibrador.ativo();

  if (calibrador.ativo()) {
    if (calibrador.coletando(agora)) calibrador.amostra(L.g, L.flex.media, L.flex2.media, norm(L.gyro));
    if (calibrador.terminou(agora)) concluirCalibracao();
    st.estado = ESTADO_OK;
    st.tempoRuimMs = 0;
  } else {
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

  motor_alerta(st.estado == ESTADO_ALERTA);
}

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
static void tarefaControle(void*) {
  for (;;) {
    passoControle(millis());
    vTaskDelay(1);  // 1 ms
  }
}
#endif

// ------------------------------------------------------------------ setup / loop

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  motor_iniciar();
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
  xTaskCreatePinnedToCore(tarefaControle, "controle", 8192, NULL, 3, NULL, 1);
#endif
}

void loop() {
  uint32_t agora = millis();
#ifdef SIMULADOR
  passoControle(agora);
#endif

  drenarEventos();
  console_atualizar(agora);
  web_atualizar();

  if (agora - ultimoEnvio >= PERIODO_ENVIO_MS) {
    ultimoEnvio = agora;
    Status s = app_status();
    web_enviarDados(s);
    console_enviarDados(s, agora);
  }
}
