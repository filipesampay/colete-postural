// Comandos pela serial (115200 baud). Digite "help" para a lista.
// Aceita também os comandos do BioSpine V3: START, STOP, STATUS, CALIBRATE (respostas em JSON).
#pragma once

#include <stdint.h>

#include "app.h"

void console_iniciar();
void console_atualizar(uint32_t agoraMs);

// Chamado a cada envio (10 Hz): saída do Serial Plotter e/ou JSON contínuo.
void console_enviarDados(const Status& s, uint32_t agoraMs);

// Mensagem para quem está no monitor serial: "[tag] msg"
void console_evento(const char* tag, const char* msg);
// {"success":...,"message":"..."} no formato do BioSpine V3
void console_respostaJson(bool ok, const char* msg);
