#include "sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "filters.h"
#include "mpu.h"

static Mpu6050 mpu;
static TiltFilter filtro(MPU_FILTRO_GIRO);
static Leituras L;

static MediaMovel<ADC_MEDIA_N> mFlex, mFlex2, mFsr;
static Presenca pFlex(FLEX_LIGA_MS, FLEX_DESLIGA_MS), pFlex2(FLEX_LIGA_MS, FLEX_DESLIGA_MS);
// FSR sem pressão lê ~0, igual a pino solto: depois de detectado uma vez, fica detectado até reiniciar.
static Presenca pFsr(FLEX_LIGA_MS, UINT32_MAX);

static uint32_t ultimoMpu = 0, ultimoAdc = 0, ultimaTentativaMpu = 0;
static uint32_t ultimoMpuUs = 0;

// No boot mede o offset do giroscópio (bloqueia ~1 s). Na reconexão não: o offset anterior
// continua valendo e o recálculo automático (mpu.cpp) cuida do resto.
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

static void lerAdc(int pino, MediaMovel<ADC_MEDIA_N>& m, CanalAdc& c) {
  c.bruto = analogRead(pino);
  m.push(c.bruto);
  c.media = m.media();
  c.amplitude = m.amplitude();
}

void sensores_iniciar() {
  memset(&L, 0, sizeof(L));

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(MPU_I2C_HZ);
  Wire.setTimeOut(20);

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
  pFlex.atualizar(leituraAdcValida(L.flex.media, L.flex.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
  // O flex 2 é opcional e o GPIO35 fica solto na montagem padrão: começa ausente e
  // só passa a presente depois de FLEX_LIGA_MS de leitura estável.
  pFlex2.atualizar(false, agora);
  pFsr.atualizar(false, agora);
  L.flex.detectado = pFlex.presente();
  L.flex2.detectado = pFlex2.presente();
}

void sensores_atualizar(uint32_t agora) {
  // MPU: tenta reconectar se caiu (fio solto, mau contato)
  if (!L.mpuOk) {
    if (agora - ultimaTentativaMpu >= MPU_REINICIO_MS) {
      ultimaTentativaMpu = agora;
      iniciarMpu(false);
    }
  } else if (agora - ultimoMpu >= PERIODO_MPU_MS) {
    ultimoMpu = agora;
    Vec3 a, g;
    if (mpu.ler(&a, &g)) {
      uint32_t us = micros();
      float dt = (us - ultimoMpuUs) * 1e-6f;
      ultimoMpuUs = us;
      L.acc = a;
      L.gyro = g;
      filtro.update(a, g, dt);
      L.g = filtro.gravity();
    }
    L.mpuOk = mpu.ok();
    L.giroCalibrado = mpu.giroCalibrado();
  }

  if (agora - ultimoAdc >= PERIODO_ADC_MS) {
    ultimoAdc = agora;
    lerAdc(PIN_FLEX, mFlex, L.flex);
    lerAdc(PIN_FLEX2, mFlex2, L.flex2);
    lerAdc(PIN_FSR, mFsr, L.fsr);
    pFlex.atualizar(leituraAdcValida(L.flex.media, L.flex.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
    pFlex2.atualizar(leituraAdcValida(L.flex2.media, L.flex2.amplitude, ADC_MIN_VALIDO, ADC_MAX_VALIDO, ADC_RUIDO_MAX), agora);
    L.flex.detectado = pFlex.presente();
    L.flex2.detectado = pFlex2.presente();
    bool fantasma = fabsf(L.fsr.media - L.flex.media) < ADC_FANTASMA_DIF;
    pFsr.atualizar(L.fsr.media > FSR_LIMIAR && L.fsr.amplitude <= ADC_RUIDO_MAX && !fantasma, agora);
    L.fsr.detectado = pFsr.presente();
  }
}

const Leituras& sensores() { return L; }

int sensores_scanI2C(uint8_t* encontrados, int maximo) {
  int n = 0;
  for (uint8_t addr = 1; addr < 127 && n < maximo; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) encontrados[n++] = addr;
  }
  return n;
}
