// Detecção de má postura: limite, histerese e tempo mínimo (lógica pura, testável no PC).
#pragma once

#include <stdint.h>

#include "tilt.h"

struct Params {
  float limiteGraus;    // desvio máximo de inclinação (°)
  float histerese;      // margem para sair do alerta (° para o MPU, pontos % para o flex)
  uint32_t tempoMs;     // tempo em má postura antes de vibrar
  float limiteFlex;     // queda máxima da leitura do flex em relação à calibração (%)
  uint8_t intensidade;  // potência do motor (%)
};

Params paramsPadrao();
// Prende cada parâmetro numa faixa segura (valores vindos da serial/app podem ser qualquer coisa).
Params validarParams(Params p);

enum Estado { ESTADO_OK = 0, ESTADO_ATENCAO = 1, ESTADO_ALERTA = 2 };
const char* estadoNome(Estado e);

// Curvatura do flex em % de queda da leitura em relação à calibração (a leitura CAI ao dobrar).
// Volta 0..100; 0 se não houver referência.
float flexPct(float leitura, float referencia);

struct Entrada {
  bool calibrado;
  float desvio;  // ° em relação à postura calibrada
  bool flexAtivo;
  float flexPct;
  bool flex2Ativo;
  float flex2Pct;
};

// ok -> atencao (passou do limite, contando tempo) -> alerta (motor ligado).
// Entra em "ruim" acima do limite; só sai abaixo de (limite - histerese).
class DetectorPostura {
 public:
  DetectorPostura() { reset(); }

  void reset();
  Estado atualizar(const Entrada& e, const Params& p, uint32_t agoraMs);

  Estado estado() const { return estado_; }
  bool ruim() const { return ruim_; }
  // Há quanto tempo a postura está ruim (0 se não está).
  uint32_t tempoRuimMs(uint32_t agoraMs) const { return ruim_ ? agoraMs - inicio_ : 0; }

 private:
  bool ruim_;
  uint32_t inicio_;
  Estado estado_;
};

// Calibração: espera a pessoa se ajeitar (depois de apertar o botão), depois faz a média das
// leituras (gravidade + flex) e registra o maior movimento para recusar calibração "tremida".
class Calibrador {
 public:
  Calibrador()
      : ativo_(false), inicio_(0), espera_(0), duracao_(0), n_(0), soma_(v3(0, 0, 0)), somaFlex_(0),
        somaFlex2_(0), giroMax_(0) {}

  void iniciar(uint32_t agoraMs, uint32_t esperaMs, uint32_t duracaoMs);
  // true quando já passou a espera (as amostras contam)
  bool coletando(uint32_t agoraMs) const { return ativo_ && agoraMs - inicio_ >= espera_; }
  void amostra(Vec3 g, float flex, float flex2, float giroDps);
  bool terminou(uint32_t agoraMs) const { return ativo_ && agoraMs - inicio_ >= espera_ + duracao_; }
  void encerrar() { ativo_ = false; }

  bool ativo() const { return ativo_; }
  int amostras() const { return n_; }
  Vec3 gravidade() const { return normalize(soma_); }
  float flex() const { return n_ ? somaFlex_ / n_ : 0; }
  float flex2() const { return n_ ? somaFlex2_ / n_ : 0; }
  float giroMax() const { return giroMax_; }  // maior velocidade angular durante a coleta (°/s)

 private:
  bool ativo_;
  uint32_t inicio_, espera_, duracao_;
  int n_;
  Vec3 soma_;
  float somaFlex_, somaFlex2_;
  float giroMax_;
};
