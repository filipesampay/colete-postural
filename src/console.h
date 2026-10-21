// Comandos pela serial (115200 baud). Digite "help" para a lista.
// Aceita também os comandos do BioSpine V3: START, STOP, STATUS, CALIBRATE (respostas em JSON).
//
// Tudo aqui roda no loop (nunca na tarefa de controle): usa Serial à vontade e chama o núcleo
// pelas funções app_* (app.h). Exceção: o diag lê sensores() e varre o I2C direto, só para diagnóstico.
// Documentação: Documentação/referencia/console.md e Documentação/07-protocolo-e-interfaces.md
#pragma once

#include <stdint.h>

#include "app.h"

// Mensagem de boas-vindas e aviso de calibração pendente (fim do setup).
void console_iniciar();
// Lê os caracteres que chegaram (sem bloquear) e executa cada linha completa.
void console_atualizar(uint32_t agoraMs);

// Chamado a cada envio (10 Hz): saída do Serial Plotter e/ou JSON contínuo.
void console_enviarDados(const Status& s, uint32_t agoraMs);

// Mensagem para quem está no monitor serial: "[tag] msg"
void console_evento(const char* tag, const char* msg);
// {"success":...,"message":"..."} no formato do BioSpine V3
void console_respostaJson(bool ok, const char* msg);
