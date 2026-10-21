// Leitura e gravação da configuração e da calibração na NVS (Preferences).
// Chaves no namespace "colete" (tabela em Documentação/referencia/settings.md):
//   config:     limite, hist, tempo, flexlim, motor, flexmodo, flex2modo
//   calibração: cal_ok, g0x, g0y, g0z, flex0, flex20
// Os nomes das chaves são curtos de propósito (a NVS aceita no máximo 15 caracteres).
// Mudar um nome = perder o valor salvo nas placas já gravadas (volta ao padrão).
// Documentação: Documentação/referencia/settings.md
#include "settings.h"

#include <Preferences.h>
#include <string.h>

static const char* NAMESPACE = "colete";  // "pasta" da NVS onde ficam todas as chaves do colete

// Modo -> texto (serial, app e JSON de config).
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

// Texto -> modo. Aceita sinônimos ("1"/"true", "0"/"false") para facilitar a digitação na serial.
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

// Valores de fábrica (primeiro boot, "reset" da serial, "Restaurar padrão" do app).
Config configPadrao() {
  Config c;
  c.params = paramsPadrao();
  c.flexModo = MODO_AUTO;
  // GPIO35 solto pode parecer um flex (copia a tensão do GPIO34 pelo capacitor do ADC):
  // o 2º flex só entra se for ligado de propósito (set flex2 auto|on).
  c.flex2Modo = MODO_OFF;
  return c;
}

// Lê tudo da NVS. Cada get* recebe o padrão, usado quando a chave ainda não existe
// (placa nova ou recém-gravada com a flash apagada).
void settings_carregar(Config* c, Calibracao* cal) {
  Preferences p;
  Config d = configPadrao();
  p.begin(NAMESPACE, true);  // true = só leitura
  c->params.limiteGraus = p.getFloat("limite", d.params.limiteGraus);
  c->params.histerese = p.getFloat("hist", d.params.histerese);
  c->params.tempoMs = p.getUInt("tempo", d.params.tempoMs);
  c->params.limiteFlex = p.getFloat("flexlim", d.params.limiteFlex);
  c->params.intensidade = p.getUChar("motor", d.params.intensidade);
  c->flexModo = (ModoSensor)p.getUChar("flexmodo", d.flexModo);
  c->flex2Modo = (ModoSensor)p.getUChar("flex2modo", d.flex2Modo);
  // A flash pode ter qualquer coisa (versão antiga do firmware, gravação interrompida):
  // tudo passa pelas mesmas faixas seguras da serial/app.
  c->params = validarParams(c->params);
  if (c->flexModo > MODO_OFF) c->flexModo = MODO_AUTO;
  if (c->flex2Modo > MODO_OFF) c->flex2Modo = MODO_AUTO;

  cal->valida = p.getBool("cal_ok", false);
  cal->g0 = v3(p.getFloat("g0x", 0), p.getFloat("g0y", 1), p.getFloat("g0z", 0));
  cal->flex0 = p.getFloat("flex0", 0);
  cal->flex20 = p.getFloat("flex20", 0);
  // g0 deveria ser unitário: muito curto = dado corrompido, então a calibração não vale.
  if (norm(cal->g0) < 0.5f) cal->valida = false;
  p.end();
}

// Grava a configuração inteira (todas as chaves, mesmo as que não mudaram: são poucas).
void settings_salvarConfig(const Config& c) {
  Preferences p;
  p.begin(NAMESPACE, false);  // false = leitura e escrita
  p.putFloat("limite", c.params.limiteGraus);
  p.putFloat("hist", c.params.histerese);
  p.putUInt("tempo", c.params.tempoMs);
  p.putFloat("flexlim", c.params.limiteFlex);
  p.putUChar("motor", c.params.intensidade);
  p.putUChar("flexmodo", c.flexModo);
  p.putUChar("flex2modo", c.flex2Modo);
  p.end();
}

// Grava a calibração. A config fica intocada ("reset" de parâmetros mantém a calibração).
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
