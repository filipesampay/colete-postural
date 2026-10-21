// Parâmetros e calibração salvos na NVS (memória não volátil emulada na flash).
//
// O ESP32 não tem EEPROM: a NVS é uma área da flash organizada em pares chave/valor
// (biblioteca Preferences do core Arduino). O que é salvo aqui sobrevive a desligar a placa.
// Quem guarda os valores em uso é main.cpp (cfg, cal); este módulo só lê e grava na flash.
// Documentação: Documentação/referencia/settings.md
#pragma once

#include <stdint.h>

#include "posture.h"
#include "tilt.h"

// auto = usa se o sensor for detectado; on/off = força.
// Os valores numéricos vão para a NVS: não reordenar.
enum ModoSensor : uint8_t { MODO_AUTO = 0, MODO_ON = 1, MODO_OFF = 2 };
// Nome do modo para a serial e o app: "auto", "on" ou "off".
const char* modoNome(ModoSensor m);
// Converte texto em modo. Aceita "on"/"1"/"true", "off"/"0"/"false" e "auto".
// Retorna false (e não mexe em *m) se o texto não for reconhecido.
bool modoDeTexto(const char* txt, ModoSensor* m);

// Tudo que o usuário ajusta pela serial ou pelo app.
struct Config {
  Params params;         // limite, histerese, tempo mínimo, limite do flex, intensidade
  ModoSensor flexModo;   // flex 1 (GPIO34): auto por padrão
  ModoSensor flex2Modo;  // flex 2 (GPIO35): off por padrão (ver configPadrao)
};

// Referência da postura correta, gravada pela calibração.
struct Calibracao {
  bool valida;  // false = nunca calibrado (o colete não vibra)
  Vec3 g0;      // gravidade na postura correta
  float flex0;  // leitura do flex 1 na postura correta
  float flex20; // leitura do flex 2 na postura correta
};

// Configuração de fábrica (PADRAO_* de config.h, flex 1 auto, flex 2 off).
Config configPadrao();

// Lê config e calibração da NVS (no boot). Chave ausente = valor padrão; valores fora da faixa
// são corrigidos (validarParams) e calibração com g0 inválido é descartada.
void settings_carregar(Config* c, Calibracao* cal);
// Grava todos os campos da configuração (chamada pelo loop em app_definirConfig).
void settings_salvarConfig(const Config& c);
// Grava a calibração (chamada pela tarefa de controle ao concluir a calibração).
void settings_salvarCalibracao(const Calibracao& cal);
