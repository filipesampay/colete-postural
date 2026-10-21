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
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app.h"

size_t protocolo_dados(const Status& s, uint32_t uptimeMs, char* out, size_t tam);
size_t protocolo_config(char* out, size_t tam);
size_t protocolo_evento(const char* nivel, const char* msg, char* out, size_t tam);

// Executa um comando do app. Retorna true se o cliente que mandou deve receber o config.
bool protocolo_comando(const char* payload, size_t len);
