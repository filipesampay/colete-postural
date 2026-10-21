// Leitura dos sensores: MPU-6050 (gravidade filtrada), flex 1 e 2 (média móvel) e FSR.
#pragma once

#include <stdint.h>

#include "tilt.h"

struct CanalAdc {
  int bruto;        // última leitura (0..4095)
  float media;      // média móvel
  int amplitude;    // max - min da janela
  bool detectado;   // leitura plausível de divisor resistivo há tempo suficiente
};

struct Leituras {
  bool mpuOk;
  uint8_t mpuEndereco;
  uint8_t mpuWhoAmI;
  bool giroCalibrado;
  Vec3 acc;      // g
  Vec3 gyro;     // °/s
  Vec3 g;        // gravidade filtrada (unitária)
  CanalAdc flex;
  CanalAdc flex2;
  CanalAdc fsr;
};

void sensores_iniciar();
// Chamar a cada loop: lê cada sensor no seu próprio período.
void sensores_atualizar(uint32_t agoraMs);
const Leituras& sensores();

// Endereços que responderam no barramento I2C. Retorna quantos achou.
int sensores_scanI2C(uint8_t* encontrados, int maximo);
