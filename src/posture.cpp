#include "posture.h"

#include "config.h"

static float prender(float v, float mn, float mx) {
  if (v != v) return mn;  // NaN
  return v < mn ? mn : (v > mx ? mx : v);
}

Params paramsPadrao() {
  Params p;
  p.limiteGraus = PADRAO_LIMITE_GRAUS;
  p.histerese = PADRAO_HISTERESE;
  p.tempoMs = PADRAO_TEMPO_MS;
  p.limiteFlex = PADRAO_LIMITE_FLEX;
  p.intensidade = PADRAO_INTENSIDADE;
  return p;
}

Params validarParams(Params p) {
  p.limiteGraus = prender(p.limiteGraus, 2.0f, 60.0f);
  p.limiteFlex = prender(p.limiteFlex, 2.0f, 90.0f);
  // A histerese precisa ser menor que os dois limites, senão nunca sairia do alerta.
  float menorLimite = p.limiteGraus < p.limiteFlex ? p.limiteGraus : p.limiteFlex;
  p.histerese = prender(p.histerese, 0.0f, menorLimite - 1.0f);
  if (p.tempoMs > 60000) p.tempoMs = 60000;
  if (p.intensidade > 100) p.intensidade = 100;
  return p;
}

const char* estadoNome(Estado e) {
  switch (e) {
    case ESTADO_ATENCAO:
      return "atencao";
    case ESTADO_ALERTA:
      return "alerta";
    default:
      return "ok";
  }
}

float flexPct(float leitura, float referencia) {
  if (referencia < 1.0f) return 0.0f;
  return prender((referencia - leitura) / referencia * 100.0f, 0.0f, 100.0f);
}

void DetectorPostura::reset() {
  ruim_ = false;
  inicio_ = 0;
  estado_ = ESTADO_OK;
}

Estado DetectorPostura::atualizar(const Entrada& e, const Params& p, uint32_t agoraMs) {
  bool acima = e.desvio > p.limiteGraus;
  bool abaixo = e.desvio < p.limiteGraus - p.histerese;
  if (e.flexAtivo) {
    acima = acima || e.flexPct > p.limiteFlex;
    abaixo = abaixo && e.flexPct < p.limiteFlex - p.histerese;
  }
  if (e.flex2Ativo) {
    acima = acima || e.flex2Pct > p.limiteFlex;
    abaixo = abaixo && e.flex2Pct < p.limiteFlex - p.histerese;
  }

  if (!e.calibrado) {
    ruim_ = false;  // sem referência não há como saber o que é postura correta
  } else if (!ruim_ && acima) {
    ruim_ = true;
    inicio_ = agoraMs;
  } else if (ruim_ && abaixo) {
    ruim_ = false;
  }

  if (!ruim_) {
    estado_ = ESTADO_OK;
  } else {
    estado_ = (agoraMs - inicio_ >= p.tempoMs) ? ESTADO_ALERTA : ESTADO_ATENCAO;
  }
  return estado_;
}

void Calibrador::iniciar(uint32_t agoraMs, uint32_t esperaMs, uint32_t duracaoMs) {
  ativo_ = true;
  inicio_ = agoraMs;
  espera_ = esperaMs;
  duracao_ = duracaoMs;
  n_ = 0;
  soma_ = v3(0, 0, 0);
  somaFlex_ = 0;
  somaFlex2_ = 0;
  giroMax_ = 0;
}

void Calibrador::amostra(Vec3 g, float flex, float flex2, float giroDps) {
  if (!ativo_) return;
  soma_ = soma_ + g;
  somaFlex_ += flex;
  somaFlex2_ += flex2;
  if (giroDps > giroMax_) giroMax_ = giroDps;
  n_++;
}
