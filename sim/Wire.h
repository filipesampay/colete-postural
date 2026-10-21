// Simulador: barramento I2C com um MPU-6050 falso (ver sim_main.cpp).
#pragma once

#include <stdint.h>

class TwoWire {
 public:
  void begin(int sda, int scl) {}
  void setClock(uint32_t) {}
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t addr);
  size_t write(uint8_t b);
  uint8_t endTransmission(bool stop = true);
  uint8_t requestFrom(uint8_t addr, uint8_t n);
  int read();

 private:
  uint8_t addr_ = 0;
  uint8_t buf_[8];
  uint8_t n_ = 0;
  uint8_t rx_[32];
  uint8_t rxN_ = 0, rxI_ = 0;
};
extern TwoWire Wire;
