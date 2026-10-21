// Parâmetros e calibração salvos na NVS (memória não volátil emulada na flash).
#pragma once

#include <stdint.h>

#include "posture.h"
#include "tilt.h"

// auto = usa se o sensor for detectado; on/off = força.
enum ModoSensor : uint8_t { MODO_AUTO = 0, MODO_ON = 1, MODO_OFF = 2 };
const char* modoNome(ModoSensor m);
bool modoDeTexto(const char* txt, ModoSensor* m);

struct Config {
  Params params;
  ModoSensor flexModo;
  ModoSensor flex2Modo;
};

struct Calibracao {
  bool valida;
  Vec3 g0;      // gravidade na postura correta
  float flex0;  // leitura do flex 1 na postura correta
  float flex20; // leitura do flex 2 na postura correta
};

Config configPadrao();

void settings_carregar(Config* c, Calibracao* cal);
void settings_salvarConfig(const Config& c);
void settings_salvarCalibracao(const Calibracao& cal);
