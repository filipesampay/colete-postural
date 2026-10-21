// Leitura e pré-processamento de todos os sensores (ver sensors.h).
//
// Pipeline (chamado pela tarefa de controle a cada passo):
//   MPU (200 Hz): Mpu6050::ler -> TiltFilter::update -> L.g (gravidade filtrada)
//   ADC (100 Hz): analogRead -> MediaMovel (10 amostras) -> leituraAdcValida -> Presenca
//   MPU caído: nova tentativa de iniciar a cada MPU_REINICIO_MS
//
// Documentação: Documentação/referencia/sensors.md, Documentação/03-algoritmo-inclinacao.md
// e Documentação/05-sensores-analogicos.md
#include "sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "filters.h"
#include "mpu.h"

static Mpu6050 mpu;                        // driver do MPU-6050
static TiltFilter filtro(MPU_FILTRO_GIRO);  // filtro complementar (0,98 giroscópio)
static Leituras L;                         // o que sensores() devolve

// Média móvel de cada canal analógico.
static MediaMovel<ADC_MEDIA_N> mFlex, mFlex2, mFsr;
// Flex: 2 s de leitura válida para "presente", 1 s inválida para "ausente".
static Presenca pFlex(FLEX_LIGA_MS, FLEX_DESLIGA_MS), pFlex2(FLEX_LIGA_MS, FLEX_DESLIGA_MS);
// FSR sem pressão lê ~0, igual a pino solto: depois de detectado uma vez, fica detectado até reiniciar.
static Presenca pFsr(FLEX_LIGA_MS, UINT32_MAX);

// Agendamento por millis(): última leitura do MPU, do ADC e última tentativa de reconectar.
static uint32_t ultimoMpu = 0, ultimoAdc = 0, ultimaTentativaMpu = 0;
// micros() da última leitura do MPU: dá o dt real do filtro (o período nominal de 5 ms varia).
static uint32_t ultimoMpuUs = 0;

// No boot mede o offset do giroscópio (bloqueia ~1 s). Na reconexão não: o offset anterior
// continua valendo e o recálculo automático (mpu.cpp) cuida do resto.
// Em qualquer caso o filtro recomeça pela direção do acelerômetro (o estado antigo não vale
// mais depois de um tempo sem leituras).
static void iniciarMpu(bool medirGiro) {
  L.mpuOk = mpu.iniciar();
  L.mpuEndereco = mpu.endereco();
  L.mpuWhoAmI = mpu.whoAmI();
  if (L.mpuOk) {
    if (medirGiro) mpu.calibrarGiro();
    L.giroCalibrado = mpu.giroCalibrado();
    Vec3 a, g;
    if (mpu.ler(&a, &g)) {
      filtro.reset(a);
      L.g = filtro.gravity();
    }
    ultimoMpuUs = micros();
  }
}

// Lê um pino analógico e atualiza o canal: bruto, média e amplitude da janela.
static void lerAdc(int pino, MediaMovel<ADC_MEDIA_N>& m, CanalAdc& c) {
  c.bruto = analogRead(pino);
  m.push(c.bruto);
  c.media = m.media();
  c.amplitude = m.amplitude();
}

// Configuração do hardware de entrada. Chamado uma vez pelo setup().
void sensores_iniciar() {
  memset(&L, 0, sizeof(L));

  // I2C nos pinos do MPU, a 100 kHz (tolera fio longo no colete) e com timeout de 20 ms:
  // sem timeout, um fio solto poderia travar a leitura por muito tempo.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(MPU_I2C_HZ);
  Wire.setTimeOut(20);

  // ADC de 12 bits (0..4095). Atenuação de 11 dB: a faixa de entrada vai até ~3,3 V, o que
  // cobre o divisor ligado no 3V3.
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_FLEX, ADC_11db);  // faixa 0..~3,3 V
  analogSetPinAttenuation(PIN_FLEX2, ADC_11db);
  analogSetPinAttenuation(PIN_FSR, ADC_11db);

  iniciarMpu(true);

  // Enche a média móvel para a detecção de flex já ter um valor ao sair do setup.
  for (int i = 0; i < ADC_MEDIA_N; i++) {
    lerAdc(PIN_FLEX, mFlex, L.flex);
    lerAdc(PIN_FLEX2, mFlex2, L.flex2);
    lerAdc(PIN_FSR, mFsr, L.fsr);
    delay(2);
  }
  uint32_t agora = millis();
  // Flex 1: o primeiro atualizar() da Presenca adota o valor na hora, então um flex bem
  // ligado já sai do setup como presente.
  pFlex.atualizar(leituraAdcValida(L.flex.media, L.flex.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
  // O flex 2 é opcional e o GPIO35 fica solto na montagem padrão: começa ausente e
  // só passa a presente depois de FLEX_LIGA_MS de leitura estável.
  pFlex2.atualizar(false, agora);
  pFsr.atualizar(false, agora);
  L.flex.detectado = pFlex.presente();
  L.flex2.detectado = pFlex2.presente();
}

// Um passo de leitura. Chamado a cada passo da tarefa de controle (~1 ms); cada sensor só é
// lido quando o seu período venceu.
void sensores_atualizar(uint32_t agora) {
  // MPU: tenta reconectar se caiu (fio solto, mau contato)
  if (!L.mpuOk) {
    if (agora - ultimaTentativaMpu >= MPU_REINICIO_MS) {
      ultimaTentativaMpu = agora;
      iniciarMpu(false);  // sem medir o giro: não bloqueia a tarefa de controle por 1 s
    }
  } else if (agora - ultimoMpu >= PERIODO_MPU_MS) {
    ultimoMpu = agora;
    Vec3 a, g;
    if (mpu.ler(&a, &g)) {
      // dt medido em µs: o filtro integra o giroscópio, então precisa do intervalo real.
      uint32_t us = micros();
      float dt = (us - ultimoMpuUs) * 1e-6f;
      ultimoMpuUs = us;
      L.acc = a;
      L.gyro = g;
      filtro.update(a, g, dt);
      L.g = filtro.gravity();
    }
    // Leitura com falha mantém os valores anteriores; depois de 10 falhas mpu.ok() vira false
    // e o bloco de reconexão acima assume.
    L.mpuOk = mpu.ok();
    L.giroCalibrado = mpu.giroCalibrado();
  }

  if (agora - ultimoAdc >= PERIODO_ADC_MS) {
    ultimoAdc = agora;
    lerAdc(PIN_FLEX, mFlex, L.flex);
    lerAdc(PIN_FLEX2, mFlex2, L.flex2);
    lerAdc(PIN_FSR, mFsr, L.fsr);
    // Flex: presente se a média estiver em 100..4000 e a janela oscilar até 300 (com persistência).
    pFlex.atualizar(leituraAdcValida(L.flex.media, L.flex.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
    pFlex2.atualizar(leituraAdcValida(L.flex2.media, L.flex2.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
    L.flex.detectado = pFlex.presente();
    L.flex2.detectado = pFlex2.presente();
    // FSR: o critério é outro, porque sem pressão ele lê ~0 (igual a pino solto). Conta como
    // presente quando lê pressão estável (> FSR_LIMIAR, pouco ruído) que NÃO seja o
    // "fantasma": um pino analógico solto fica com a tensão do canal lido antes, que sobrou
    // no capacitor de amostragem do ADC. O FSR é lido depois do flex 2, que solto também
    // copia o flex 1; por isso leitura do FSR parecida com a do flex 1 é descartada.
    bool fantasma = fabsf(L.fsr.media - L.flex.media) < ADC_FANTASMA_DIF;
    pFsr.atualizar(L.fsr.media > FSR_LIMIAR && L.fsr.amplitude <= ADC_RUIDO_MAX && !fantasma, agora);
    L.fsr.detectado = pFsr.presente();
  }
}

// Acesso às leituras (somente leitura).
const Leituras& sensores() { return L; }

// Varredura do barramento: transmissão vazia em cada endereço; ACK = tem dispositivo.
int sensores_scanI2C(uint8_t* encontrados, int maximo) {
  int n = 0;
  for (uint8_t addr = 1; addr < 127 && n < maximo; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) encontrados[n++] = addr;
  }
  return n;
}
