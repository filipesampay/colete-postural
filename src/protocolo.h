// Protocolo JSON do app (WebSocket). Sem dependência de Wi-Fi: roda também no simulador.
//
// ESP32 -> app:
//   {"t":"data", pitch, roll, desvio, flex, flexPct, flexAtivo, flexDetectado, flex2, flex2Pct, flex2Ativo,
//    flex2Detectado, fsr, fsrDetectado, estado:"ok|atencao|alerta", tempoRuimMs, calibrado, calibrando,
//    mpuOk, motor, uptime}                                            10 Hz
//   {"t":"config", limiteGraus, histerese, tempoMs, limiteFlex, intensidade,
//    flexModo:"auto|on|off", flex2Modo, calibrado, versao}             ao conectar e após mudanças
//   {"t":"evt", nivel:"info|ok|erro", msg}                              avisos
// app -> ESP32:
//   {"cmd":"calib"} {"cmd":"motor_test"} {"cmd":"get"} {"cmd":"reset"}
//   {"cmd":"set", ...qualquer subconjunto dos campos de config}
//
// Tipos e unidades de cada campo: Documentação/07-protocolo-e-interfaces.md.
// Mudou um campo aqui? Atualize também src/web/index.html e tools/mock_server.py.
// Documentação: Documentação/referencia/protocolo.md
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app.h"

// Cada função monta uma mensagem em "out" (até "tam" bytes) e retorna quantos bytes escreveu.
// Quem envia é web.cpp (ou o simulador), com buffers que têm folga para a maior mensagem.

// {"t":"data",...} a partir de um Status. uptimeMs = millis() do envio.
size_t protocolo_dados(const Status& s, uint32_t uptimeMs, char* out, size_t tam);
// {"t":"config",...} com a configuração e a calibração em vigor (lidas por app_config/app_calibracao).
size_t protocolo_config(char* out, size_t tam);
// {"t":"evt","nivel":...,"msg":...}: aviso que o app mostra como toast.
size_t protocolo_evento(const char* nivel, const char* msg, char* out, size_t tam);

// Executa um comando do app. Retorna true se o cliente que mandou deve receber o config.
// JSON inválido ou comando desconhecido viram um {"t":"evt","nivel":"erro"} para todos.
bool protocolo_comando(const char* payload, size_t len);
