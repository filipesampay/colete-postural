// Driver mínimo do MPU-6050 (registradores direto, sem biblioteca).
// Procura o sensor em 0x68 e 0x69 e configura ±250 °/s e ±2 g (texto do TCC).
//
// Papel no sistema: única parte do firmware que fala I2C com o MPU. sensors.cpp cria um
// objeto Mpu6050, chama iniciar() no boot (e a cada 2 s se o sensor cair), ler() a 200 Hz e
// passa o resultado para o TiltFilter (tilt.h).
// Por que não a biblioteca MPU6050_light: o calcOffsets() dela supõe o sensor deitado, o
// endereço 0x68 é fixo e o ângulo dela salta perto da vertical (ver tilt.h).
// Roda em: tarefa de controle (ESP32); no simulador, contra um MPU falso (sim/sim_main.cpp).
//
// Documentação: Documentação/referencia/mpu.md e Documentação/03-algoritmo-inclinacao.md
#pragma once

#include <stdint.h>

#include "tilt.h"

class Mpu6050 {
 public:
  // Procura e configura. Retorna true se encontrou.
  // Tenta 0x68 (MPU_ADDR_A) e depois 0x69 (MPU_ADDR_B). Bloqueia ~150 ms (reset do chip).
  bool iniciar();
  // Lê aceleração (g) e velocidade angular (°/s, já sem o offset do giroscópio).
  // Retorna false se a leitura falhou; 10 falhas seguidas marcam o sensor como caído (ok() = false).
  bool ler(Vec3* acc, Vec3* gyro);
  // Média do giroscópio parado (~1 s, bloqueante: só no boot). Só grava se a placa estiver parada.
  // Retorna true se o offset foi gravado.
  bool calibrarGiro();

  // Sensor respondendo (iniciado e sem 10 falhas seguidas).
  bool ok() const { return ok_; }
  // Offset do giroscópio medido (no boot ou depois, sozinho, sempre que o sensor fica parado ~1 s).
  bool giroCalibrado() const { return giroCalibrado_; }
  // Endereço I2C em uso (0x68, 0x69 ou 0 se não achou).
  uint8_t endereco() const { return addr_; }
  // Valor do registrador WHO_AM_I lido no iniciar() (0x68 = MPU-6050 original; ver mpu.cpp).
  uint8_t whoAmI() const { return whoAmI_; }

 private:
  // Escreve um registrador. true se o sensor confirmou (ACK).
  bool escrever(uint8_t reg, uint8_t valor);
  // Lê n registradores seguidos a partir de reg (leitura em rajada).
  bool lerBytes(uint8_t reg, uint8_t* buf, uint8_t n);
  // Lê acelerômetro e giroscópio convertidos para g e °/s, SEM descontar o offset.
  bool lerBruto(Vec3* acc, Vec3* gyro);

  bool ok_ = false;      // sensor respondendo
  uint8_t addr_ = 0;     // endereço I2C encontrado
  uint8_t whoAmI_ = 0;   // identificação do chip
  uint8_t falhas_ = 0;   // leituras seguidas com erro
  // Janela de recálculo automático do offset (chamada a cada ler() bem-sucedido).
  void acumularOffset(Vec3 giroBruto);

  Vec3 offsetGiro_ = {0, 0, 0};  // offset descontado do giroscópio (°/s)
  bool giroCalibrado_ = false;   // offsetGiro_ já foi medido alguma vez
  // janela para recalcular o offset quando parado
  Vec3 janSoma_ = {0, 0, 0}, janMin_ = {0, 0, 0}, janMax_ = {0, 0, 0};  // soma, mínimo e máximo por eixo
  int janN_ = 0;  // leituras na janela atual (fecha em 200)
};
