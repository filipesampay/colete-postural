#include "settings.h"

#include <Preferences.h>
#include <string.h>

static const char* NAMESPACE = "colete";

const char* modoNome(ModoSensor m) {
  switch (m) {
    case MODO_ON:
      return "on";
    case MODO_OFF:
      return "off";
    default:
      return "auto";
  }
}

bool modoDeTexto(const char* txt, ModoSensor* m) {
  if (!strcmp(txt, "on") || !strcmp(txt, "1") || !strcmp(txt, "true")) {
    *m = MODO_ON;
  } else if (!strcmp(txt, "off") || !strcmp(txt, "0") || !strcmp(txt, "false")) {
    *m = MODO_OFF;
  } else if (!strcmp(txt, "auto")) {
    *m = MODO_AUTO;
  } else {
    return false;
  }
  return true;
}

Config configPadrao() {
  Config c;
  c.params = paramsPadrao();
  c.flexModo = MODO_AUTO;
  // GPIO35 solto pode parecer um flex (copia a tensão do GPIO34 pelo capacitor do ADC):
  // o 2º flex só entra se for ligado de propósito (set flex2 auto|on).
  c.flex2Modo = MODO_OFF;
  return c;
}

void settings_carregar(Config* c, Calibracao* cal) {
  Preferences p;
  Config d = configPadrao();
  p.begin(NAMESPACE, true);
  c->params.limiteGraus = p.getFloat("limite", d.params.limiteGraus);
  c->params.histerese = p.getFloat("hist", d.params.histerese);
  c->params.tempoMs = p.getUInt("tempo", d.params.tempoMs);
  c->params.limiteFlex = p.getFloat("flexlim", d.params.limiteFlex);
  c->params.intensidade = p.getUChar("motor", d.params.intensidade);
  c->flexModo = (ModoSensor)p.getUChar("flexmodo", d.flexModo);
  c->flex2Modo = (ModoSensor)p.getUChar("flex2modo", d.flex2Modo);
  c->params = validarParams(c->params);
  if (c->flexModo > MODO_OFF) c->flexModo = MODO_AUTO;
  if (c->flex2Modo > MODO_OFF) c->flex2Modo = MODO_AUTO;

  cal->valida = p.getBool("cal_ok", false);
  cal->g0 = v3(p.getFloat("g0x", 0), p.getFloat("g0y", 1), p.getFloat("g0z", 0));
  cal->flex0 = p.getFloat("flex0", 0);
  cal->flex20 = p.getFloat("flex20", 0);
  if (norm(cal->g0) < 0.5f) cal->valida = false;
  p.end();
}

void settings_salvarConfig(const Config& c) {
  Preferences p;
  p.begin(NAMESPACE, false);
  p.putFloat("limite", c.params.limiteGraus);
  p.putFloat("hist", c.params.histerese);
  p.putUInt("tempo", c.params.tempoMs);
  p.putFloat("flexlim", c.params.limiteFlex);
  p.putUChar("motor", c.params.intensidade);
  p.putUChar("flexmodo", c.flexModo);
  p.putUChar("flex2modo", c.flex2Modo);
  p.end();
}

void settings_salvarCalibracao(const Calibracao& cal) {
  Preferences p;
  p.begin(NAMESPACE, false);
  p.putBool("cal_ok", cal.valida);
  p.putFloat("g0x", cal.g0.x);
  p.putFloat("g0y", cal.g0.y);
  p.putFloat("g0z", cal.g0.z);
  p.putFloat("flex0", cal.flex0);
  p.putFloat("flex20", cal.flex20);
  p.end();
}
