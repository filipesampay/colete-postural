// Simulador: barramento I2C com um MPU-6050 falso (ver sim_main.cpp).
//
// Substitui o <Wire.h> do core (tools/sim.sh compila com -Isim antes de -Isrc). A interface é a
// mesma que src/mpu.cpp e src/sensors.cpp usam; só um dispositivo responde: o MPU falso, no
// endereço escolhido por "!mpuaddr" (0x68 por padrão) e só enquanto "!mpu on".
// Documentação: Documentação/09-simulador-testes-ferramentas.md
#pragma once

#include <stdint.h>

class TwoWire {
 public:
  // Configuração do barramento: no simulador não há pinos nem clock, nada a fazer.
  void begin(int sda, int scl) {}
  void setClock(uint32_t) {}
  void setTimeOut(uint16_t) {}
  // Começa uma escrita para "addr" (zera o buffer de saída).
  void beginTransmission(uint8_t addr);
  // Acumula um byte da escrita. O 1º byte é o número do registrador.
  size_t write(uint8_t b);
  // Fecha a escrita. 0 = o MPU falso respondeu (ACK); 2 = ninguém nesse endereço (NACK).
  uint8_t endTransmission(bool stop = true);
  // Lê "n" bytes a partir do último registrador escrito. Retorna quantos bytes vieram.
  uint8_t requestFrom(uint8_t addr, uint8_t n);
  // Próximo byte recebido, ou -1 se acabou.
  int read();

 private:
  uint8_t addr_ = 0;   // endereço da transmissão atual
  uint8_t buf_[8];     // bytes escritos (registrador + valor)
  uint8_t n_ = 0;      // quantos bytes em buf_
  uint8_t rx_[32];     // bytes da última leitura (requestFrom)
  uint8_t rxN_ = 0, rxI_ = 0;  // total recebido / próximo a entregar em read()
};
// Instância única, definida em sim_main.cpp (como o Wire global do core).
extern TwoWire Wire;
