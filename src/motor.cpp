#include "motor.h"

#include <Arduino.h>

#include "config.h"

static bool alertaAtivo = false;
static uint8_t intensidade = PADRAO_INTENSIDADE;
static bool saida = false;
static uint32_t marco = 0;  // início da fase atual

// Sequência curta pendente
static uint8_t seqRestantes = 0;  // fases restantes (cada pulso = 2 fases: ligado, desligado)
static uint16_t seqOn = 0, seqOff = 0;

static void escrever(bool ligado) {
  saida = ligado;
  uint32_t maximo = (1u << MOTOR_LEDC_BITS) - 1;
  uint32_t duty = ligado ? (maximo * intensidade) / 100 : 0;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_MOTOR, duty);
#else
  ledcWrite(MOTOR_LEDC_CANAL, duty);
#endif
  digitalWrite(PIN_LED, ligado ? HIGH : LOW);
}

void motor_iniciar() {
  pinMode(PIN_LED, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_MOTOR, MOTOR_LEDC_FREQ, MOTOR_LEDC_BITS);
#else
  ledcSetup(MOTOR_LEDC_CANAL, MOTOR_LEDC_FREQ, MOTOR_LEDC_BITS);
  ledcAttachPin(PIN_MOTOR, MOTOR_LEDC_CANAL);
#endif
  escrever(false);
}

void motor_alerta(bool ativo) {
  if (ativo == alertaAtivo) return;
  alertaAtivo = ativo;
  if (seqRestantes) return;  // a sequência termina e depois o alerta assume
  marco = millis();
  escrever(ativo);  // começa já ligado: resposta imediata
}

void motor_pulsos(uint8_t quantidade, uint16_t ligadoMs, uint16_t desligadoMs) {
  if (!quantidade) return;
  seqRestantes = quantidade * 2;
  seqOn = ligadoMs;
  seqOff = desligadoMs;
  marco = millis();
  escrever(true);
}

void motor_intensidade(uint8_t pct) {
  intensidade = pct > 100 ? 100 : pct;
  if (saida) escrever(true);
}

bool motor_ligado() { return saida; }

void motor_atualizar(uint32_t agora) {
  uint32_t passou = agora - marco;

  if (seqRestantes) {
    if (passou < (saida ? seqOn : seqOff)) return;
    seqRestantes--;
    marco = agora;
    if (seqRestantes) {
      escrever(!saida);
    } else {
      escrever(alertaAtivo);  // fim da sequência: volta ao alerta, se houver
    }
    return;
  }

  if (!alertaAtivo) {
    if (saida) escrever(false);
    return;
  }
  if (passou >= (saida ? MOTOR_ALERTA_ON_MS : MOTOR_ALERTA_OFF_MS)) {
    marco = agora;
    escrever(!saida);
  }
}
