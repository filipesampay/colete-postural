// Controle do motor vibratório por PWM e dos padrões de pulso (ver motor.h).
//
// Circuito: GPIO25 -> 1 kΩ -> base do NPN; o transistor liga o motor (60 mA, mais do que
// um GPIO aguenta). O LED azul da placa (GPIO2) repete o estado do motor: dá para ver o
// "alerta" mesmo sem motor ligado.
// Máquina de estados sem delay(): cada fase guarda o instante em que começou (marco) e
// motor_atualizar() troca de fase quando a duração passa.
//
// Documentação: Documentação/referencia/motor.md e Documentação/06-motor-e-nvs.md
#include "motor.h"

#include <Arduino.h>

#include "config.h"

static bool alertaAtivo = false;                  // postura em alerta (pedido por main.cpp)
static uint8_t intensidade = PADRAO_INTENSIDADE;  // potência em % (duty do PWM)
static bool saida = false;                        // motor ligado neste instante
static uint32_t marco = 0;  // início da fase atual

// Sequência curta pendente
static uint8_t seqRestantes = 0;  // fases restantes (cada pulso = 2 fases: ligado, desligado)
static uint16_t seqOn = 0, seqOff = 0;  // duração das fases ligado / desligado (ms)

// Liga ou desliga o motor (com a intensidade atual) e o LED.
static void escrever(bool ligado) {
  saida = ligado;
  // 8 bits: duty de 0 a 255. intensidade em % vira fração do máximo.
  uint32_t maximo = (1u << MOTOR_LEDC_BITS) - 1;
  uint32_t duty = ligado ? (maximo * intensidade) / 100 : 0;
  // O core Arduino-ESP32 3.x mudou a API do LEDC (por pino em vez de por canal). O projeto
  // usa o core 2.x (espressif32@6.9.0), mas o código compila nos dois. No simulador a macro
  // não existe, vale 0 e cai no ramo 2.x.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_MOTOR, duty);
#else
  ledcWrite(MOTOR_LEDC_CANAL, duty);
#endif
  digitalWrite(PIN_LED, ligado ? HIGH : LOW);
}

// PWM a 20 kHz (MOTOR_LEDC_FREQ): acima da faixa audível, então o motor não "chia".
// 8 bits de resolução bastam para ajustar a força em passos de ~0,4 %.
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

// Liga/desliga o padrão de alerta. main.cpp chama a 50 Hz com (estado == ESTADO_ALERTA).
void motor_alerta(bool ativo) {
  if (ativo == alertaAtivo) return;
  alertaAtivo = ativo;
  if (seqRestantes) return;  // a sequência termina e depois o alerta assume
  marco = millis();
  escrever(ativo);  // começa já ligado: resposta imediata
}

// Começa uma sequência de pulsos. Cada pulso tem duas fases (ligado, desligado), por isso
// seqRestantes = 2 × quantidade. Começa na fase ligada.
// Ex.: confirmação de calibração = motor_pulsos(2, 120, 120) -> liga 120, desliga 120,
// liga 120, desliga 120 e volta ao alerta (se houver).
void motor_pulsos(uint8_t quantidade, uint16_t ligadoMs, uint16_t desligadoMs) {
  if (!quantidade) return;
  seqRestantes = quantidade * 2;
  seqOn = ligadoMs;
  seqOff = desligadoMs;
  marco = millis();
  escrever(true);
}

// Muda a potência. Se o motor estiver ligado, reescreve o duty já com o valor novo.
void motor_intensidade(uint8_t pct) {
  intensidade = pct > 100 ? 100 : pct;
  if (saida) escrever(true);
}

bool motor_ligado() { return saida; }

// Avança as fases. Prioridade: sequência > alerta > desligado.
void motor_atualizar(uint32_t agora) {
  uint32_t passou = agora - marco;

  // 1) Sequência em andamento: espera a fase atual acabar e passa para a próxima.
  if (seqRestantes) {
    if (passou < (saida ? seqOn : seqOff)) return;
    seqRestantes--;
    marco = agora;
    if (seqRestantes) {
      escrever(!saida);  // alterna ligado <-> desligado
    } else {
      escrever(alertaAtivo);  // fim da sequência: volta ao alerta, se houver
    }
    return;
  }

  // 2) Sem alerta: garante o motor desligado.
  if (!alertaAtivo) {
    if (saida) escrever(false);
    return;
  }
  // 3) Alerta: alterna MOTOR_ALERTA_ON_MS (300 ms) ligado e MOTOR_ALERTA_OFF_MS (700 ms)
  // desligado. Pulsado incomoda menos que contínuo e gasta menos bateria.
  if (passou >= (saida ? MOTOR_ALERTA_ON_MS : MOTOR_ALERTA_OFF_MS)) {
    marco = agora;
    escrever(!saida);
  }
}
