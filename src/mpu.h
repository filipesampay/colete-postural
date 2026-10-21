// Driver mínimo do MPU-6050 (registradores direto, sem biblioteca).
// Procura o sensor em 0x68 e 0x69 e configura ±250 °/s e ±2 g (texto do TCC).
#pragma once

#include <stdint.h>

#include "tilt.h"

class Mpu6050 {
 public:
  // Procura e configura. Retorna true se encontrou.
  bool iniciar();
  // Lê aceleração (g) e velocidade angular (°/s, já sem o offset do giroscópio).
  bool ler(Vec3* acc, Vec3* gyro);
  // Média do giroscópio parado (~1 s, bloqueante: só no boot). Só grava se a placa estiver parada.
  bool calibrarGiro();

  bool ok() const { return ok_; }
  // Offset do giroscópio medido (no boot ou depois, sozinho, sempre que o sensor fica parado ~1 s).
  bool giroCalibrado() const { return giroCalibrado_; }
  uint8_t endereco() const { return addr_; }
  uint8_t whoAmI() const { return whoAmI_; }

 private:
  bool escrever(uint8_t reg, uint8_t valor);
  bool lerBytes(uint8_t reg, uint8_t* buf, uint8_t n);
  bool lerBruto(Vec3* acc, Vec3* gyro);

  bool ok_ = false;
  uint8_t addr_ = 0;
  uint8_t whoAmI_ = 0;
  uint8_t falhas_ = 0;
  void acumularOffset(Vec3 giroBruto);

  Vec3 offsetGiro_ = {0, 0, 0};
  bool giroCalibrado_ = false;
  // janela para recalcular o offset quando parado
  Vec3 janSoma_ = {0, 0, 0}, janMin_ = {0, 0, 0}, janMax_ = {0, 0, 0};
  int janN_ = 0;
};
