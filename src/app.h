// Estado do colete e ações que a serial e o app web podem pedir (implementado em main.cpp).
#pragma once

#include <stdint.h>

#include "posture.h"
#include "settings.h"

struct Status {
  float pitch;   // ° frente/trás em relação à referência
  float roll;    // ° lateral em relação à referência
  float desvio;  // ° total em relação à referência
  int flex;      // leitura média do flex 1 (0..4095)
  float flexPct;
  bool flexAtivo;      // entra na detecção
  bool flexDetectado;  // sensor parece ligado
  int flex2;
  float flex2Pct;
  bool flex2Ativo;
  bool flex2Detectado;
  int fsr;
  bool fsrDetectado;  // já leu pressão estável desde o boot
  Estado estado;
  uint32_t tempoRuimMs;
  bool calibrado;
  bool calibrando;
  bool mpuOk;
  bool motor;
};

enum Origem { ORIGEM_BOTAO, ORIGEM_SERIAL, ORIGEM_SERIAL_JSON, ORIGEM_APP };

// Funções para o loop (serial e web). São seguras entre tarefas: pedidos são executados pela
// tarefa de controle e as leituras devolvem cópias.

// Pede a calibração (espera 1,5 s + média de 1 s). Retorna false se o MPU não estiver presente.
bool app_calibrar(Origem origem);
void app_testarMotor();
Config app_config();
// Valida, aplica e salva na NVS. Avisa o app web.
void app_definirConfig(const Config& c);
Calibracao app_calibracao();
Status app_status();
