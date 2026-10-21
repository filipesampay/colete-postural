#include "mpu.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

// Registradores (datasheet MPU-6000/6050, rev. 4.2)
static const uint8_t REG_SMPLRT_DIV = 0x19;
static const uint8_t REG_CONFIG = 0x1A;
static const uint8_t REG_GYRO_CONFIG = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_ACCEL_CONFIG2 = 0x1D;  // só MPU-6500/9250
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_PWR_MGMT_1 = 0x6B;
static const uint8_t REG_WHO_AM_I = 0x75;

static const float LSB_POR_G = 16384.0f;   // ±2 g
static const float LSB_POR_DPS = 131.0f;   // ±250 °/s

static bool responde(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool Mpu6050::escrever(uint8_t reg, uint8_t valor) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(valor);
  return Wire.endTransmission() == 0;
}

bool Mpu6050::lerBytes(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr_, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool Mpu6050::iniciar() {
  ok_ = false;
  falhas_ = 0;
  if (responde(MPU_ADDR_A)) {
    addr_ = MPU_ADDR_A;
  } else if (responde(MPU_ADDR_B)) {
    addr_ = MPU_ADDR_B;
  } else {
    addr_ = 0;
    return false;
  }

  if (!escrever(REG_PWR_MGMT_1, 0x80)) return false;  // reset
  delay(100);
  bool r = escrever(REG_PWR_MGMT_1, 0x01);  // acorda, clock do giroscópio X (mais estável)
  r = r && escrever(REG_SMPLRT_DIV, 0x00);
  r = r && escrever(REG_CONFIG, 0x03);        // filtro passa-baixa interno ~44 Hz
  r = r && escrever(REG_GYRO_CONFIG, 0x00);   // ±250 °/s
  r = r && escrever(REG_ACCEL_CONFIG, 0x00);  // ±2 g
  if (!r) return false;

  // Clones (MPU-6500, 9250...) respondem com outro valor mas são compatíveis nesses registradores.
  uint8_t w = 0;
  lerBytes(REG_WHO_AM_I, &w, 1);
  whoAmI_ = w;
  // Na família MPU-6500 o filtro do acelerômetro fica em outro registrador (0x1D): sem ele,
  // a vibração do motor entra na leitura.
  if (w == 0x70 || w == 0x71 || w == 0x73 || w == 0x74) escrever(REG_ACCEL_CONFIG2, 0x03);
  delay(50);

  Vec3 a, g;
  ok_ = lerBruto(&a, &g);
  return ok_;
}

bool Mpu6050::lerBruto(Vec3* acc, Vec3* gyro) {
  uint8_t b[14];
  if (!lerBytes(REG_ACCEL_XOUT_H, b, 14)) return false;
  int16_t ax = (int16_t)((b[0] << 8) | b[1]);
  int16_t ay = (int16_t)((b[2] << 8) | b[3]);
  int16_t az = (int16_t)((b[4] << 8) | b[5]);
  // b[6..7] = temperatura
  int16_t gx = (int16_t)((b[8] << 8) | b[9]);
  int16_t gy = (int16_t)((b[10] << 8) | b[11]);
  int16_t gz = (int16_t)((b[12] << 8) | b[13]);
  *acc = v3(ax / LSB_POR_G, ay / LSB_POR_G, az / LSB_POR_G);
  *gyro = v3(gx / LSB_POR_DPS, gy / LSB_POR_DPS, gz / LSB_POR_DPS);
  return true;
}

bool Mpu6050::ler(Vec3* acc, Vec3* gyro) {
  if (!ok_) return false;
  if (!lerBruto(acc, gyro)) {
    if (++falhas_ >= 10) ok_ = false;  // fio solto: o loop tenta reconectar
    return false;
  }
  falhas_ = 0;
  acumularOffset(*gyro);
  *gyro = *gyro - offsetGiro_;
  return true;
}

// Recalcula o offset do giroscópio sozinho sempre que o sensor fica parado por ~1 s
// (200 leituras). Cobre o caso de ligar o colete já vestido, quando o boot não consegue medir.
void Mpu6050::acumularOffset(Vec3 g) {
  if (janN_ == 0) {
    janSoma_ = v3(0, 0, 0);
    janMin_ = g;
    janMax_ = g;
  }
  janSoma_ = janSoma_ + g;
  janMin_ = v3(min(janMin_.x, g.x), min(janMin_.y, g.y), min(janMin_.z, g.z));
  janMax_ = v3(max(janMax_.x, g.x), max(janMax_.y, g.y), max(janMax_.z, g.z));
  if (++janN_ < 200) return;

  Vec3 faixa = janMax_ - janMin_;
  Vec3 media = janSoma_ * (1.0f / janN_);
  janN_ = 0;
  bool parado = faixa.x < GIRO_PARADO_DPS && faixa.y < GIRO_PARADO_DPS && faixa.z < GIRO_PARADO_DPS;
  bool plausivel = fabsf(media.x) < GIRO_OFFSET_MAX && fabsf(media.y) < GIRO_OFFSET_MAX && fabsf(media.z) < GIRO_OFFSET_MAX;
  if (parado && plausivel) {
    offsetGiro_ = media;
    giroCalibrado_ = true;
  }
}

bool Mpu6050::calibrarGiro() {
  if (!ok_) return false;
  const int N = 200;
  Vec3 soma = v3(0, 0, 0);
  Vec3 mn = v3(1e6, 1e6, 1e6), mx = v3(-1e6, -1e6, -1e6);
  int n = 0;
  for (int i = 0; i < N; i++) {
    Vec3 a, g;
    if (lerBruto(&a, &g)) {
      soma = soma + g;
      mn = v3(min(mn.x, g.x), min(mn.y, g.y), min(mn.z, g.z));
      mx = v3(max(mx.x, g.x), max(mx.y, g.y), max(mx.z, g.z));
      n++;
    }
    delay(5);
  }
  if (n < N / 2) return false;
  // Se a placa mexeu durante a medição, o offset sairia errado: fica sem offset
  // (o filtro complementar corrige a deriva de qualquer jeito).
  Vec3 faixa = mx - mn;
  if (faixa.x > GIRO_PARADO_DPS || faixa.y > GIRO_PARADO_DPS || faixa.z > GIRO_PARADO_DPS) return false;
  offsetGiro_ = soma * (1.0f / n);
  giroCalibrado_ = true;
  return true;
}
