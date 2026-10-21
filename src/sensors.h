// Leitura dos sensores: MPU-6050 (gravidade filtrada), flex 1 e 2 (média móvel) e FSR.
//
// Papel no sistema: junta todo o hardware de entrada num único struct Leituras, atualizado
// pela tarefa de controle (sensores_atualizar a cada passo, ~1 kHz). Cada sensor tem seu
// período: MPU a 200 Hz (PERIODO_MPU_MS), ADC a 100 Hz (PERIODO_ADC_MS). Também detecta se
// cada sensor está ligado (flex solto, MPU caído) para o colete seguir com o que tiver.
// Roda em: tarefa de controle (ESP32) e simulador. As leituras não têm trava: só a tarefa de
// controle escreve; o loop só lê para mostrar (diag) e para checar se o MPU existe.
//
// Documentação: Documentação/referencia/sensors.md, Documentação/03-algoritmo-inclinacao.md
// e Documentação/05-sensores-analogicos.md
#pragma once

#include <stdint.h>

#include "tilt.h"

// Um canal analógico (flex 1, flex 2 ou FSR) depois da média móvel.
struct CanalAdc {
  int bruto;        // última leitura (0..4095)
  float media;      // média móvel
  int amplitude;    // max - min da janela
  bool detectado;   // leitura plausível de divisor resistivo há tempo suficiente
};

// Tudo que os sensores informam. Lido por main.cpp (postura, calibração) e console.cpp (diag).
struct Leituras {
  bool mpuOk;           // MPU respondendo
  uint8_t mpuEndereco;  // 0x68, 0x69 ou 0
  uint8_t mpuWhoAmI;    // identificação do chip (ver mpu.cpp)
  bool giroCalibrado;   // offset do giroscópio já medido
  Vec3 acc;      // g
  Vec3 gyro;     // °/s
  Vec3 g;        // gravidade filtrada (unitária)
  CanalAdc flex;   // flex 1 (GPIO34)
  CanalAdc flex2;  // flex 2 (GPIO35), opcional
  CanalAdc fsr;    // sensor de pressão (GPIO32), opcional, só telemetria
};

// Configura I2C e ADC, inicia o MPU (com medição do offset do giroscópio, ~1 s bloqueante)
// e enche a média móvel. Chamar uma vez no setup(), com o colete parado.
void sensores_iniciar();
// Chamar a cada passo da tarefa de controle (~1 ms): lê cada sensor no seu próprio período.
// Também tenta reconectar o MPU a cada MPU_REINICIO_MS (2 s) se ele tiver caído.
void sensores_atualizar(uint32_t agoraMs);
// Últimas leituras (referência para o struct interno; não guardar entre passos).
const Leituras& sensores();

// Endereços que responderam no barramento I2C. Retorna quantos achou.
// Varre 0x01 a 0x7E; usado pelo comando "diag".
int sensores_scanI2C(uint8_t* encontrados, int maximo);
