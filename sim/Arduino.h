// Simulador nativo: imitação mínima do core Arduino-ESP32 para rodar o firmware no PC.
// Tempo virtual: millis() só anda quando o simulador manda (ver sim_main.cpp).
//
// Como funciona: tools/sim.sh compila com -Isim antes de -Isrc, então todo "#include <Arduino.h>"
// do firmware cai neste arquivo em vez do core verdadeiro. Aqui só ficam as DECLARAÇÕES; as
// implementações (relógio, pinos, ADC, PWM e Serial falsos) estão em sim/sim_main.cpp.
// Só existe o que o firmware usa: se um módulo novo chamar outra função do Arduino, declare-a
// aqui e implemente em sim_main.cpp, senão o simulador não compila.
// Documentação: Documentação/09-simulador-testes-ferramentas.md
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

// ------------------------------------------------------------------ tipos, constantes e macros do core

typedef uint8_t byte;

// Mesmos valores do core Arduino-ESP32 (os números em si não importam no simulador, só a comparação).
#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define ADC_11db 3  // atenuação do ADC (faixa 0..~3,3 V); ignorada no simulador
// F("texto") no ESP32 guarda a string na flash; no PC é só a própria string.
#define F(s) (s)
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
// O core do ESP32 oferece min/max globais; mpu.cpp usa os dois.
using std::max;
using std::min;

// ------------------------------------------------------------------ tempo (virtual)

// ms desde o "boot" no relógio virtual do simulador.
uint32_t millis();
// µs desde o "boot" no relógio virtual (usado para o dt do filtro complementar).
uint32_t micros();
// Não dorme de verdade: avança o relógio virtual (e a física do tronco) em "ms".
void delay(uint32_t ms);

// ------------------------------------------------------------------ pinos e ADC (falsos)

void pinMode(int pino, int modo);
// Botão de calibração: LOW enquanto "!botao" estiver apertado. Outros pinos: LOW.
int digitalRead(int pino);
void digitalWrite(int pino, int valor);
// Flex, flex 2 e FSR com o valor pedido pelo roteiro (!flex, !flex2, !fsr) mais ruído.
int analogRead(int pino);
void analogReadResolution(int bits);
void analogSetPinAttenuation(int pino, int att);

// ------------------------------------------------------------------ PWM do motor (API LEDC do core 2.x)
// ESP_ARDUINO_VERSION_MAJOR não existe aqui, então motor.cpp usa o ramo do core 2.x (canal LEDC).

void ledcSetup(int canal, uint32_t freq, int bits);
void ledcAttachPin(int pino, int canal);
// Imprime "motor LIGA"/"motor desliga" no terminal quando o duty muda entre zero e não zero.
void ledcWrite(int canal, uint32_t duty);

// ------------------------------------------------------------------ Serial

// Serial falsa: a saída vai para o stdout do PC; a entrada vem das linhas do roteiro que não
// começam com "!" (o simulador as coloca num buffer que available()/read() consomem).
class SimSerial {
 public:
  void begin(unsigned long) {}
  // Quantos caracteres do roteiro ainda esperam para ser lidos pelo console.
  int available();
  // Próximo caractere da entrada, ou -1 se não houver.
  int read();
  void print(const char* s) { fputs(s, stdout); }
  void print(int v) { printf("%d", v); }
  void println(const char* s = "") { printf("%s\n", s); }
  void println(int v) { printf("%d\n", v); }
  // Mesmo formato do printf do C; o atributo faz o compilador conferir os argumentos.
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
  }
};
// Instância única, definida em sim_main.cpp (como o Serial global do core).
extern SimSerial Serial;
