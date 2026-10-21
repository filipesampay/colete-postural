// Driver do MPU-6050 por registradores (ver mpu.h).
//
// Fluxo: iniciar() acha o endereço, reseta e configura o chip; ler() faz uma leitura em
// rajada de 14 bytes (acelerômetro, temperatura, giroscópio), converte para g e °/s e
// desconta o offset do giroscópio; acumularOffset() recalcula esse offset sozinho sempre que
// o sensor fica parado por ~1 s.
// Roda em: tarefa de controle (ESP32), via sensors.cpp. No simulador, o Wire.h de sim/ responde
// como um MPU falso nos mesmos registradores.
//
// Documentação: Documentação/referencia/mpu.md e Documentação/03-algoritmo-inclinacao.md
#include "mpu.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

// Registradores (datasheet MPU-6000/6050, rev. 4.2)
static const uint8_t REG_SMPLRT_DIV = 0x19;     // divisor da taxa de amostragem
static const uint8_t REG_CONFIG = 0x1A;         // filtro passa-baixa digital (DLPF_CFG)
static const uint8_t REG_GYRO_CONFIG = 0x1B;    // escala do giroscópio (FS_SEL)
static const uint8_t REG_ACCEL_CONFIG = 0x1C;   // escala do acelerômetro (AFS_SEL)
static const uint8_t REG_ACCEL_CONFIG2 = 0x1D;  // só MPU-6500/9250
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;   // primeiro dos 14 bytes de medida
static const uint8_t REG_PWR_MGMT_1 = 0x6B;     // reset, sono e fonte de clock
static const uint8_t REG_WHO_AM_I = 0x75;       // identificação do chip

// Sensibilidade nas escalas escolhidas (especificação do giroscópio e do acelerômetro no
// datasheet): leitura bruta / sensibilidade = unidade física.
static const float LSB_POR_G = 16384.0f;   // ±2 g
static const float LSB_POR_DPS = 131.0f;   // ±250 °/s

// true se algum dispositivo responde (ACK) no endereço: transmissão vazia, só o endereço.
static bool responde(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Escrita de um registrador: [endereço][registrador][valor].
bool Mpu6050::escrever(uint8_t reg, uint8_t valor) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(valor);
  return Wire.endTransmission() == 0;
}

// Leitura de n registradores seguidos. O MPU incrementa o ponteiro de registrador sozinho,
// então uma transação traz vários registradores.
bool Mpu6050::lerBytes(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  // false = "repeated start": não solta o barramento entre escolher o registrador e ler.
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr_, n) != n) return false;  // chegou menos byte que o pedido = falha
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// Procura o sensor, reseta e configura. Chamado no boot e a cada MPU_REINICIO_MS se cair.
bool Mpu6050::iniciar() {
  ok_ = false;
  falhas_ = 0;
  // AD0 solto/GND = 0x68 (padrão do GY-521); AD0 em 3V3 = 0x69.
  if (responde(MPU_ADDR_A)) {
    addr_ = MPU_ADDR_A;
  } else if (responde(MPU_ADDR_B)) {
    addr_ = MPU_ADDR_B;
  } else {
    addr_ = 0;
    return false;
  }

  // PWR_MGMT_1 bit 7 (DEVICE_RESET): volta todos os registradores ao padrão. Garante um
  // estado conhecido também numa reconexão. Depois do reset o chip fica dormindo.
  if (!escrever(REG_PWR_MGMT_1, 0x80)) return false;  // reset
  delay(100);
  // 0x01: SLEEP = 0 (acorda) e CLKSEL = 1 (PLL do giroscópio X). O oscilador interno de
  // 8 MHz varia mais com a temperatura; o datasheet recomenda o clock do giroscópio.
  bool r = escrever(REG_PWR_MGMT_1, 0x01);  // acorda, clock do giroscópio X (mais estável)
  // SMPLRT_DIV = 0: taxa interna = 1 kHz / (1 + 0) = 1 kHz (com o DLPF ligado). O firmware
  // lê a 200 Hz e sempre pega a medida mais recente.
  r = r && escrever(REG_SMPLRT_DIV, 0x00);
  // CONFIG, DLPF_CFG = 3: banda de 44 Hz no acelerômetro e 42 Hz no giroscópio (~5 ms de
  // atraso). Corta a vibração do motor e o ruído acima do que o corpo consegue fazer, e
  // fica abaixo da metade dos 200 Hz de leitura (evita aliasing).
  r = r && escrever(REG_CONFIG, 0x03);        // filtro passa-baixa interno ~44 Hz
  // GYRO_CONFIG, FS_SEL = 0: ±250 °/s, a escala mais sensível (131 LSB por °/s). Movimento de
  // tronco fica bem abaixo de 250 °/s.
  r = r && escrever(REG_GYRO_CONFIG, 0x00);   // ±250 °/s
  // ACCEL_CONFIG, AFS_SEL = 0: ±2 g, a escala mais sensível (16384 LSB por g). Para medir a
  // direção da gravidade (1 g) é a melhor resolução.
  r = r && escrever(REG_ACCEL_CONFIG, 0x00);  // ±2 g
  if (!r) return false;

  // Clones (MPU-6500, 9250...) respondem com outro valor mas são compatíveis nesses registradores.
  // 0x68 = MPU-6050 original. 0x70, 0x71, 0x73, 0x74 = família MPU-6500 (6500, 9250, 9255...).
  // O valor é só informativo (aparece no "diag"); não impede o uso.
  uint8_t w = 0;
  lerBytes(REG_WHO_AM_I, &w, 1);
  whoAmI_ = w;
  // Na família MPU-6500 o filtro do acelerômetro fica em outro registrador (0x1D): sem ele,
  // a vibração do motor entra na leitura.
  // ACCEL_CONFIG2, A_DLPF_CFG = 3: banda de ~44 Hz, igual à do MPU-6050.
  if (w == 0x70 || w == 0x71 || w == 0x73 || w == 0x74) escrever(REG_ACCEL_CONFIG2, 0x03);
  delay(50);  // tempo para o filtro e o PLL estabilizarem antes da primeira leitura

  // Uma leitura de teste confirma que o sensor está entregando dados.
  Vec3 a, g;
  ok_ = lerBruto(&a, &g);
  return ok_;
}

// Lê os 14 bytes a partir de ACCEL_XOUT_H e converte para unidades físicas.
bool Mpu6050::lerBruto(Vec3* acc, Vec3* gyro) {
  uint8_t b[14];
  if (!lerBytes(REG_ACCEL_XOUT_H, b, 14)) return false;
  // Cada medida tem 16 bits com sinal (complemento de 2), byte alto primeiro (big-endian):
  // valor = (alto << 8) | baixo, e o cast para int16_t recupera o sinal.
  int16_t ax = (int16_t)((b[0] << 8) | b[1]);
  int16_t ay = (int16_t)((b[2] << 8) | b[3]);
  int16_t az = (int16_t)((b[4] << 8) | b[5]);
  // b[6..7] = temperatura
  int16_t gx = (int16_t)((b[8] << 8) | b[9]);
  int16_t gy = (int16_t)((b[10] << 8) | b[11]);
  int16_t gz = (int16_t)((b[12] << 8) | b[13]);
  // Bruto / sensibilidade: aceleração em g e velocidade angular em °/s.
  *acc = v3(ax / LSB_POR_G, ay / LSB_POR_G, az / LSB_POR_G);
  *gyro = v3(gx / LSB_POR_DPS, gy / LSB_POR_DPS, gz / LSB_POR_DPS);
  return true;
}

// Leitura normal (200 Hz): bruto, contagem de falhas, recálculo do offset e desconto do offset.
bool Mpu6050::ler(Vec3* acc, Vec3* gyro) {
  if (!ok_) return false;
  if (!lerBruto(acc, gyro)) {
    // Uma falha isolada (ruído no fio) é tolerada; 10 seguidas (~50 ms ou mais, somando os
    // timeouts do I2C) = sensor caiu.
    if (++falhas_ >= 10) ok_ = false;  // fio solto: sensores_atualizar (tarefa de controle) tenta reconectar a cada 2 s
    return false;
  }
  falhas_ = 0;
  acumularOffset(*gyro);  // usa o valor bruto: o offset é medido antes de ser descontado
  *gyro = *gyro - offsetGiro_;
  return true;
}

// Recalcula o offset do giroscópio sozinho sempre que o sensor fica parado por ~1 s
// (200 leituras). Cobre o caso de ligar o colete já vestido, quando o boot não consegue medir.
//
// O giroscópio parado não lê zero: cada eixo tem um offset de fábrica (alguns °/s) que muda
// com a temperatura. Integrado pelo filtro, esse offset viraria deriva do ângulo.
// A janela fecha a cada 200 leituras. Ela só vira o novo offset se:
//   - parado: variação (máx - mín) de cada eixo < GIRO_PARADO_DPS (5 °/s); e
//   - plausível: média de cada eixo < GIRO_OFFSET_MAX (25 °/s); mais que isso é movimento
//     constante, não offset.
// Se não passar, o offset anterior continua valendo e uma nova janela começa.
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

  // Janela completa: avalia e recomeça.
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

// Medição do offset no boot: 200 leituras a cada 5 ms (~1 s, bloqueante).
// Chamada só por sensores_iniciar (no setup, antes da tarefa de controle existir). Na
// reconexão não é chamada, para não travar a tarefa de controle.
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
  if (n < N / 2) return false;  // mais da metade das leituras falhou: barramento ruim
  // Se a placa mexeu durante a medição, o offset sairia errado: fica sem offset
  // (o filtro complementar corrige a deriva de qualquer jeito).
  Vec3 faixa = mx - mn;
  if (faixa.x > GIRO_PARADO_DPS || faixa.y > GIRO_PARADO_DPS || faixa.z > GIRO_PARADO_DPS) return false;
  offsetGiro_ = soma * (1.0f / n);
  giroCalibrado_ = true;
  return true;
}
