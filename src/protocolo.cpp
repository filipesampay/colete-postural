#include "protocolo.h"

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "web.h"

// Arredonda para 1 casa: menos bytes no Wi-Fi e no CSV.
static float r1(float v) { return roundf(v * 10.0f) / 10.0f; }

size_t protocolo_dados(const Status& s, uint32_t uptimeMs, char* out, size_t tam) {
  JsonDocument d;
  d["t"] = "data";
  d["pitch"] = r1(s.pitch);
  d["roll"] = r1(s.roll);
  d["desvio"] = r1(s.desvio);
  d["flex"] = s.flex;
  d["flexPct"] = r1(s.flexPct);
  d["flexAtivo"] = s.flexAtivo;
  d["flexDetectado"] = s.flexDetectado;
  d["flex2"] = s.flex2;
  d["flex2Pct"] = r1(s.flex2Pct);
  d["flex2Ativo"] = s.flex2Ativo;
  d["flex2Detectado"] = s.flex2Detectado;
  d["fsr"] = s.fsr;
  d["fsrDetectado"] = s.fsrDetectado;
  d["estado"] = estadoNome(s.estado);
  d["tempoRuimMs"] = s.tempoRuimMs;
  d["calibrado"] = s.calibrado;
  d["calibrando"] = s.calibrando;
  d["mpuOk"] = s.mpuOk;
  d["motor"] = s.motor;
  d["uptime"] = uptimeMs;
  return serializeJson(d, out, tam);
}

size_t protocolo_config(char* out, size_t tam) {
  const Config& c = app_config();
  JsonDocument d;
  d["t"] = "config";
  d["limiteGraus"] = r1(c.params.limiteGraus);
  d["histerese"] = r1(c.params.histerese);
  d["tempoMs"] = c.params.tempoMs;
  d["limiteFlex"] = r1(c.params.limiteFlex);
  d["intensidade"] = c.params.intensidade;
  d["flexModo"] = modoNome(c.flexModo);
  d["flex2Modo"] = modoNome(c.flex2Modo);
  d["calibrado"] = app_calibracao().valida;
  d["versao"] = FIRMWARE_VERSAO;
  return serializeJson(d, out, tam);
}

size_t protocolo_evento(const char* nivel, const char* msg, char* out, size_t tam) {
  JsonDocument d;
  d["t"] = "evt";
  d["nivel"] = nivel;
  d["msg"] = msg;
  return serializeJson(d, out, tam);
}

static void aplicarSet(JsonDocument& d) {
  Config c = app_config();
  if (d["limiteGraus"].is<float>()) c.params.limiteGraus = d["limiteGraus"];
  if (d["histerese"].is<float>()) c.params.histerese = d["histerese"];
  if (d["tempoMs"].is<float>()) {
    float t = d["tempoMs"];
    c.params.tempoMs = t < 0 ? 0 : (uint32_t)t;
  }
  if (d["limiteFlex"].is<float>()) c.params.limiteFlex = d["limiteFlex"];
  if (d["intensidade"].is<float>()) {
    int i = d["intensidade"];
    c.params.intensidade = (uint8_t)(i < 0 ? 0 : (i > 100 ? 100 : i));
  }
  if (d["flexModo"].is<const char*>()) modoDeTexto(d["flexModo"], &c.flexModo);
  if (d["flex2Modo"].is<const char*>()) modoDeTexto(d["flex2Modo"], &c.flex2Modo);
  app_definirConfig(c);  // valida, salva e envia {"t":"config"} para todos
}

bool protocolo_comando(const char* payload, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, payload, len)) {
    web_evento("erro", "comando inválido");
    return false;
  }
  const char* cmd = d["cmd"] | "";
  if (!strcmp(cmd, "calib")) {
    app_calibrar(ORIGEM_APP);
  } else if (!strcmp(cmd, "motor_test")) {
    app_testarMotor();
  } else if (!strcmp(cmd, "get")) {
    return true;
  } else if (!strcmp(cmd, "set")) {
    aplicarSet(d);
    web_evento("ok", "Parâmetros salvos");
  } else if (!strcmp(cmd, "reset")) {
    app_definirConfig(configPadrao());
    web_evento("ok", "Parâmetros padrão restaurados");
  } else {
    web_evento("erro", "comando desconhecido");
  }
  return false;
}
