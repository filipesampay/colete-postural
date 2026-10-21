// Simulador nativo do firmware (roda no PC, sem placa): tools/sim.sh
//
// Compila main.cpp, console, sensores, motor e NVS de verdade, trocando só o hardware:
//   - MPU-6050 falso no I2C (acelerômetro e giroscópio coerentes com a inclinação pedida)
//   - ADC, botão e motor falsos
//   - Wi-Fi/app desligados (web_* viram mensagens no terminal)
//
// Roteiro pela entrada padrão, uma linha por comando:
//   !espera 3000        avança 3000 ms de tempo virtual
//   !frente 25          inclina o tronco 25° para a frente (rampa de 60°/s)
//   !lado -10           inclinação lateral
//   !flex 1800          leitura do flex 1 (GPIO34)   !flex2 1800 | !flex2 solto
//   !fsr 300            leitura do FSR (GPIO32)
//   !botao              aperta o botão de calibração por 200 ms
//   !mpu off|on         desconecta/reconecta o MPU     !mpuaddr 69
//   !eco texto          imprime texto
//   !ws on|off          imprime o JSON que iria para o app (linhas "WS>")
//   !ws conectar        simula um app conectando (recebe o config)
//   !ws {"cmd":...}     comando vindo do app
//   # comentário
//
// Com --tempo-real o relógio acompanha o relógio do PC e os comandos chegam a qualquer momento
// (usado pelo tools/sim_web.py, que liga o simulador na página web de verdade).
//   qualquer outra linha vai para a serial do firmware (diag, calib, set limite 15...)
#include <math.h>

#include <iostream>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Preferences.h"
#include "Wire.h"
#include "app.h"
#include "config.h"
#include "protocolo.h"
#include "web.h"

#include <sys/select.h>
#include <time.h>
#include <unistd.h>

void setup();
void loop();

SimSerial Serial;
TwoWire Wire;

// ------------------------------------------------------------------ estado do "mundo"

static uint64_t relogioUs = 0;
static std::string entradaSerial;

static float frente = 0, lado = 0;          // inclinação atual (°)
static float frenteAlvo = 0, ladoAlvo = 0;  // pedida pelo roteiro
static float taxaFrente = 0, taxaLado = 0;  // °/s no último passo
static const float RAMPA_DPS = 60.0f;

static bool mpuLigado = true;
static uint8_t mpuAddr = 0x68;
static int adcFlex = 2048, adcFlex2 = 0, adcFsr = 0;
static bool flex2Solto = true;
static bool botaoApertado = false;
static uint32_t dutyMotor = 0;

static float ruido(float amp) { return ((rand() % 2001) - 1000) / 1000.0f * amp; }

static void passoFisica(float dt) {
  float df = frenteAlvo - frente, dl = ladoAlvo - lado;
  float maxPasso = RAMPA_DPS * dt;
  float pf = fabsf(df) > maxPasso ? copysignf(maxPasso, df) : df;
  float pl = fabsf(dl) > maxPasso ? copysignf(maxPasso, dl) : dl;
  frente += pf;
  lado += pl;
  taxaFrente = pf / dt;
  taxaLado = pl / dt;
}

static void avancarUs(uint64_t us) {
  // física em passos de 1 ms
  while (us > 0) {
    uint64_t p = us > 1000 ? 1000 : us;
    relogioUs += p;
    passoFisica(p * 1e-6f);
    us -= p;
  }
}

// ------------------------------------------------------------------ Arduino falso

uint32_t millis() { return (uint32_t)(relogioUs / 1000); }
uint32_t micros() { return (uint32_t)relogioUs; }
void delay(uint32_t ms) { avancarUs((uint64_t)ms * 1000); }

void pinMode(int, int) {}
int digitalRead(int pino) {
  if (pino == PIN_BUTTON) return botaoApertado ? LOW : HIGH;
  return LOW;
}
void digitalWrite(int, int) {}

int analogRead(int pino) {
  int v = 0;
  if (pino == PIN_FLEX) v = adcFlex + (int)ruido(8);
  if (pino == PIN_FLEX2) v = flex2Solto ? rand() % 4096 : adcFlex2 + (int)ruido(8);
  if (pino == PIN_FSR) v = adcFsr + (int)ruido(5);
  return constrain(v, 0, 4095);
}
void analogReadResolution(int) {}
void analogSetPinAttenuation(int, int) {}

void ledcSetup(int, uint32_t, int) {}
void ledcAttachPin(int, int) {}
void ledcWrite(int, uint32_t duty) {
  if ((duty > 0) != (dutyMotor > 0)) {
    printf("[sim %8.3f s] motor %s", relogioUs / 1e6, duty ? "LIGA" : "desliga");
    if (duty) printf(" (duty %u)", duty);
    printf("\n");
  }
  dutyMotor = duty;
}

int SimSerial::available() { return (int)entradaSerial.size(); }
int SimSerial::read() {
  if (entradaSerial.empty()) return -1;
  int c = (unsigned char)entradaSerial[0];
  entradaSerial.erase(0, 1);
  return c;
}

// ------------------------------------------------------------------ MPU-6050 falso

static uint8_t regPtr = 0;

void TwoWire::beginTransmission(uint8_t addr) {
  addr_ = addr;
  n_ = 0;
}
size_t TwoWire::write(uint8_t b) {
  if (n_ < sizeof(buf_)) buf_[n_++] = b;
  return 1;
}
uint8_t TwoWire::endTransmission(bool) {
  if (!mpuLigado || addr_ != mpuAddr) return 2;  // NACK
  if (n_ >= 1) regPtr = buf_[0];
  return 0;
}

static void poe16(uint8_t* b, float v) {
  int x = (int)lroundf(v);
  x = constrain(x, -32768, 32767);
  b[0] = (uint8_t)((x >> 8) & 0xFF);
  b[1] = (uint8_t)(x & 0xFF);
}

uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t n) {
  rxN_ = rxI_ = 0;
  if (!mpuLigado || addr != mpuAddr) return 0;
  uint8_t regs[128] = {0};
  regs[0x75] = 0x68;  // WHO_AM_I
  // Sensor em pé nas costas (Y para cima, componentes para fora).
  const float G = 0.0174532925f;
  float ax = sinf(lado * G), ay = cosf(frente * G) * cosf(lado * G), az = sinf(frente * G) * cosf(lado * G);
  poe16(&regs[0x3B], (ax + ruido(0.01f)) * 16384);
  poe16(&regs[0x3D], (ay + ruido(0.01f)) * 16384);
  poe16(&regs[0x3F], (az + ruido(0.01f)) * 16384);
  // Giroscópio coerente com o movimento (+ offset de fábrica e ruído).
  poe16(&regs[0x43], (-taxaFrente + 0.8f + ruido(0.2f)) * 131);
  poe16(&regs[0x45], (-0.5f + ruido(0.2f)) * 131);
  poe16(&regs[0x47], (taxaLado + 0.3f + ruido(0.2f)) * 131);
  for (uint8_t i = 0; i < n && i < sizeof(rx_); i++) rx_[rxN_++] = regs[(regPtr + i) & 0x7F];
  return rxN_;
}
int TwoWire::read() { return rxI_ < rxN_ ? rx_[rxI_++] : -1; }

// ------------------------------------------------------------------ web falso

static bool wsLigado = false;
static char wsBuf[640];

static void wsEnviar(size_t n) {
  if (wsLigado) printf("WS>%.*s\n", (int)n, wsBuf);
}

void web_iniciar() { printf("[sim] Wi-Fi simulado: use !ws para ver/mandar o JSON do app\n"); }
void web_atualizar() {}
void web_enviarDados(const Status& s) { wsEnviar(protocolo_dados(s, millis(), wsBuf, sizeof(wsBuf))); }
void web_enviarConfig() { wsEnviar(protocolo_config(wsBuf, sizeof(wsBuf))); }
void web_evento(const char* nivel, const char* msg) {
  if (wsLigado) {
    wsEnviar(protocolo_evento(nivel, msg, wsBuf, sizeof(wsBuf)));
  } else {
    printf("[app:%s] %s\n", nivel, msg);
  }
}
bool web_ativo() { return true; }
const char* web_ip() { return "192.168.4.1 (sim)"; }
int web_clientes() { return 0; }

// ------------------------------------------------------------------ roteiro

static void rodar(uint32_t ms) {
  uint64_t fim = relogioUs + (uint64_t)ms * 1000;
  while (relogioUs < fim) {
    loop();
    avancarUs(1000);  // ~1 kHz de loop
  }
}

static void mostrarEstado() {
  Status s = app_status();
  printf("[sim %8.3f s] desvio %5.1f° frente %5.1f° lado %5.1f° flex %4d (%4.1f%%%s) flex2 %4d (%4.1f%%%s) %-7s cal %d mpu %d\n",
         relogioUs / 1e6, s.desvio, s.pitch, s.roll, s.flex, s.flexPct, s.flexAtivo ? "" : " inativo", s.flex2,
         s.flex2Pct, s.flex2Ativo ? "" : " inativo", estadoNome(s.estado), s.calibrado, s.mpuOk);
}

static bool executarLinha(const std::string& linha, bool ligado);

static uint64_t agoraRealUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

// Relógio virtual acompanha o real; linhas da entrada são executadas assim que chegam.
static int tempoReal() {
  setup();
  uint64_t base = agoraRealUs() - relogioUs;
  std::string pendente;
  char bloco[4096];
  for (;;) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    timeval tv = {0, 0};
    if (select(1, &fds, NULL, NULL, &tv) > 0) {
      ssize_t n = ::read(0, bloco, sizeof(bloco));
      if (n <= 0) return 0;
      pendente.append(bloco, n);
      size_t pos;
      while ((pos = pendente.find('\n')) != std::string::npos) {
        std::string linha = pendente.substr(0, pos);
        pendente.erase(0, pos + 1);
        if (!linha.empty() && linha[0] == '!' && linha.compare(0, 7, "!espera") == 0) continue;
        executarLinha(linha, false);
      }
    }
    uint64_t alvo = agoraRealUs() - base;
    int voltas = 0;
    while (relogioUs < alvo && voltas++ < 200) {
      loop();
      avancarUs(1000);
    }
    usleep(1000);
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "--tempo-real")) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    srand(42);
    return tempoReal();
  }
  setvbuf(stdout, NULL, _IOLBF, 0);
  srand(42);

  // Linhas antes de "!boot" (se houver) montam o hardware antes de ligar a placa.
  std::vector<std::string> linhas;
  std::string linha;
  bool temBoot = false;
  while (std::getline(std::cin, linha)) {
    temBoot |= linha == "!boot";
    linhas.push_back(linha);
  }
  size_t i = 0;
  if (temBoot) {
    for (; linhas[i] != "!boot"; i++) executarLinha(linhas[i], false);
    i++;
  }
  // começa já na inclinação pedida
  frente = frenteAlvo;
  lado = ladoAlvo;
  setup();
  rodar(100);
  for (; i < linhas.size(); i++) executarLinha(linhas[i], true);
  return 0;
}

static bool executarLinha(const std::string& linha, bool ligado) {
  {
    if (linha.empty() || linha[0] == '#') return true;
    if (linha[0] != '!') {
      printf("> %s\n", linha.c_str());
      entradaSerial += linha + "\n";
      if (ligado) rodar(50);
      return true;
    }
    char cmd[32] = {0}, arg[1024] = {0};
    sscanf(linha.c_str() + 1, "%31s %1023[^\n]", cmd, arg);
    std::string c = cmd;
    if (c == "espera") {
      rodar(atoi(arg));
    } else if (c == "frente") {
      frenteAlvo = atof(arg);
    } else if (c == "lado") {
      ladoAlvo = atof(arg);
    } else if (c == "flex") {
      adcFlex = atoi(arg);
    } else if (c == "flex2") {
      flex2Solto = !strcmp(arg, "solto");
      adcFlex2 = atoi(arg);
    } else if (c == "fsr") {
      adcFsr = atoi(arg);
    } else if (c == "botao") {
      printf("[sim %8.3f s] botão apertado\n", relogioUs / 1e6);
      botaoApertado = true;
      rodar(200);
      botaoApertado = false;
    } else if (c == "mpu") {
      mpuLigado = strcmp(arg, "off") != 0;
      printf("[sim %8.3f s] MPU %s\n", relogioUs / 1e6, mpuLigado ? "conectado" : "desconectado");
    } else if (c == "mpuaddr") {
      mpuAddr = (uint8_t)strtol(arg, NULL, 16);
    } else if (c == "estado") {
      mostrarEstado();
    } else if (c == "ws") {
      std::string a = arg;
      if (a == "on" || a == "off") {
        wsLigado = a == "on";
      } else if (a == "conectar") {
        wsLigado = true;
        web_enviarConfig();
      } else if (protocolo_comando(a.c_str(), a.size())) {
        web_enviarConfig();
      }
    } else if (c == "eco") {
      printf("\n=== %s ===\n", arg);
    } else {
      printf("[sim] comando desconhecido: %s\n", cmd);
    }
  }
  return true;
}
