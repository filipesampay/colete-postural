// Estado do colete e ações que a serial e o app web podem pedir (implementado em main.cpp).
//
// É a "porta de entrada" do núcleo para as interfaces (console.cpp, protocolo.cpp, web.cpp):
// elas nunca mexem direto no detector ou no motor; só chamam as funções app_*. Exceção: o diag
// (console.cpp) lê sensores() e varre o I2C direto, só para diagnóstico.
// Documentação: Documentação/referencia/main.md
#pragma once

#include <stdint.h>

#include "posture.h"
#include "settings.h"

// Foto do colete num instante, montada pela tarefa de controle a cada passo e lida pelo loop
// (app web a 10 Hz, plotter, diag) sempre como cópia (app_status()).
struct Status {
  float pitch;   // ° frente/trás em relação à referência
  float roll;    // ° lateral em relação à referência
  float desvio;  // ° total em relação à referência
  int flex;      // leitura média do flex 1 (0..4095)
  float flexPct;       // curvatura do flex 1: % de queda em relação à calibração (0 se inativo)
  bool flexAtivo;      // entra na detecção
  bool flexDetectado;  // sensor parece ligado
  int flex2;           // leitura média do flex 2 (0..4095)
  float flex2Pct;      // curvatura do flex 2 (%), 0 se inativo
  bool flex2Ativo;     // flex 2 entra na detecção
  bool flex2Detectado; // flex 2 parece ligado
  int fsr;            // leitura média do FSR (0..4095), só telemetria
  bool fsrDetectado;  // já leu pressão estável desde o boot
  Estado estado;          // ok / atenção / alerta (DetectorPostura)
  uint32_t tempoRuimMs;   // há quanto tempo a postura está ruim (ms), 0 se está boa
  bool calibrado;   // existe referência gravada (sem ela o colete não vibra)
  bool calibrando;  // calibração em andamento (espera + média)
  bool mpuOk;       // MPU-6050 respondendo
  bool motor;       // saída do motor ligada neste instante
};

// Quem pediu a calibração. Só muda a resposta no fim: ORIGEM_SERIAL_JSON também imprime o
// {"success":true,...} esperado pelo programa do BioSpine V3.
enum Origem {
  ORIGEM_BOTAO,        // botão físico (GPIO27)
  ORIGEM_SERIAL,       // comando "calib" no monitor serial
  ORIGEM_SERIAL_JSON,  // comando "CALIBRATE" do BioSpine V3
  ORIGEM_APP           // botão "Calibrar postura" do app web
};

// Funções para o loop (serial e web). São seguras entre tarefas: pedidos são executados pela
// tarefa de controle e as leituras devolvem cópias.

// Pede a calibração (espera 1,5 s + média de 1 s). Retorna false se o MPU não estiver presente.
// O resultado chega depois, como evento ("[calib] concluída" / "recusada").
bool app_calibrar(Origem origem);
// Pede um pulso de teste de 1 s no motor (MOTOR_TESTE_MS).
void app_testarMotor();
// Cópia da configuração atual (parâmetros + modo dos sensores flex).
Config app_config();
// Valida, aplica e salva na NVS. Avisa o app web.
// Só pode ser chamada do loop (envia pelo WebSocket na hora).
void app_definirConfig(const Config& c);
// Cópia da calibração atual (referência g0, flex0, flex20).
Calibracao app_calibracao();
// Cópia do último Status publicado pela tarefa de controle.
Status app_status();
