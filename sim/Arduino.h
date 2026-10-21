// Simulador nativo: imitação mínima do core Arduino-ESP32 para rodar o firmware no PC.
// Tempo virtual: millis() só anda quando o simulador manda (ver sim_main.cpp).
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define ADC_11db 3
#define F(s) (s)
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
using std::max;
using std::min;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);

void pinMode(int pino, int modo);
int digitalRead(int pino);
void digitalWrite(int pino, int valor);
int analogRead(int pino);
void analogReadResolution(int bits);
void analogSetPinAttenuation(int pino, int att);

void ledcSetup(int canal, uint32_t freq, int bits);
void ledcAttachPin(int pino, int canal);
void ledcWrite(int canal, uint32_t duty);

class SimSerial {
 public:
  void begin(unsigned long) {}
  int available();
  int read();
  void print(const char* s) { fputs(s, stdout); }
  void print(int v) { printf("%d", v); }
  void println(const char* s = "") { printf("%s\n", s); }
  void println(int v) { printf("%d\n", v); }
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
  }
};
extern SimSerial Serial;
