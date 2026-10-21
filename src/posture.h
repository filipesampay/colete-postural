// Detecção de má postura: limite, histerese e tempo mínimo (lógica pura, testável no PC).
//
// Papel no sistema: decide QUANDO vibrar. main.cpp (tarefa de controle, 50 Hz) monta uma
// Entrada com o desvio do MPU e a curvatura dos flex, chama DetectorPostura::atualizar e liga
// o motor quando o estado é ESTADO_ALERTA. O Calibrador faz a média da postura correta que
// vira a referência (g0, flex0, flex20) salva na NVS.
// Sem Arduino: roda igual no ESP32, nos testes nativos (pio test -e native) e no simulador.
//
// Documentação: Documentação/referencia/posture.md e Documentação/04-deteccao-e-calibracao.md
#pragma once

#include <stdint.h>

#include "tilt.h"

// Parâmetros ajustáveis pela serial ("set ...") e pelo app; salvos na NVS (settings.cpp).
// Sempre passam por validarParams antes de serem usados.
struct Params {
  float limiteGraus;    // desvio máximo de inclinação (°)
  float histerese;      // margem para sair da má postura, atenção ou alerta (° para o MPU, pontos % para o flex)
  uint32_t tempoMs;     // tempo em má postura antes de vibrar
  float limiteFlex;     // queda máxima da leitura do flex em relação à calibração (%)
  uint8_t intensidade;  // potência do motor (%)
};

// Valores de fábrica (PADRAO_* em config.h): 20°, 3, 3000 ms, 15 %, 100 %.
Params paramsPadrao();
// Prende cada parâmetro numa faixa segura (valores vindos da serial/app podem ser qualquer coisa).
// Faixas: limiteGraus 2–60°, limiteFlex 2–90 %, histerese 0 até (menor limite − 1),
// tempoMs até 60 000 ms, intensidade até 100 %. NaN vira o mínimo da faixa.
Params validarParams(Params p);

// Estado da postura. Os valores numéricos (0, 1, 2) também indexam a tabela de nomes do
// JSON do BioSpine em console.cpp ("GOOD", "WARNING", "BAD").
enum Estado { ESTADO_OK = 0, ESTADO_ATENCAO = 1, ESTADO_ALERTA = 2 };
// Nome do estado no protocolo do app: "ok", "atencao" ou "alerta".
const char* estadoNome(Estado e);

// Curvatura do flex em % de queda da leitura em relação à calibração (a leitura CAI ao dobrar).
// Volta 0..100; 0 se não houver referência.
// Ex.: referência 2000 e leitura 1700 -> 15 %. Leitura acima da referência (flex "esticado"
// para o outro lado) dá 0, não negativo.
float flexPct(float leitura, float referencia);

// O que o detector precisa saber a cada atualização (montado em main.cpp, atualizarPostura).
struct Entrada {
  bool calibrado;  // existe referência salva; sem ela nunca alerta
  float desvio;  // ° em relação à postura calibrada
  bool flexAtivo;   // flex 1 entra na decisão (modo != off e referência válida; no auto, também detectado)
  float flexPct;    // curvatura do flex 1 (%)
  bool flex2Ativo;  // idem para o flex 2
  float flex2Pct;   // curvatura do flex 2 (%)
};

// ok -> atencao (passou do limite, contando tempo) -> alerta (motor ligado).
// Entra em "ruim" acima do limite; só sai abaixo de (limite - histerese).
// Basta UM sensor acima do limite para entrar; para sair, TODOS os sensores ativos precisam
// estar abaixo de (limite - histerese). Sem calibração fica sempre em ok.
class DetectorPostura {
 public:
  DetectorPostura() { reset(); }

  // Volta para ok e esquece o tempo acumulado (usado ao iniciar e ao concluir a calibração).
  void reset();
  // Avalia uma nova Entrada. agoraMs = millis(). Retorna o novo estado.
  Estado atualizar(const Entrada& e, const Params& p, uint32_t agoraMs);

  // Último estado calculado.
  Estado estado() const { return estado_; }
  // true enquanto a postura está ruim (atenção ou alerta).
  bool ruim() const { return ruim_; }
  // Há quanto tempo a postura está ruim (0 se não está).
  uint32_t tempoRuimMs(uint32_t agoraMs) const { return ruim_ ? agoraMs - inicio_ : 0; }

 private:
  bool ruim_;        // postura ruim (com histerese), independente do tempo
  uint32_t inicio_;  // millis() em que a postura ficou ruim
  Estado estado_;    // último estado devolvido
};

// Calibração: espera a pessoa se ajeitar (depois de apertar o botão), depois faz a média das
// leituras (gravidade + flex) e registra o maior movimento para recusar calibração "tremida".
// Linha do tempo: iniciar() -> espera (CALIBRACAO_ESPERA_MS, 1,5 s) -> coleta
// (CALIBRACAO_MS, 1 s) -> terminou(). Quem decide aceitar ou recusar (giroMax) é main.cpp.
class Calibrador {
 public:
  Calibrador()
      : ativo_(false), inicio_(0), espera_(0), duracao_(0), n_(0), soma_(v3(0, 0, 0)), somaFlex_(0),
        somaFlex2_(0), giroMax_(0) {}

  // Começa uma calibração nova (zera as somas). Tempos em ms.
  void iniciar(uint32_t agoraMs, uint32_t esperaMs, uint32_t duracaoMs);
  // true quando já passou a espera (as amostras contam)
  bool coletando(uint32_t agoraMs) const { return ativo_ && agoraMs - inicio_ >= espera_; }
  // Soma uma amostra: g = gravidade filtrada (unitária), flex/flex2 = média do ADC,
  // giroDps = módulo da velocidade angular (°/s). Ignorada se não estiver ativo.
  void amostra(Vec3 g, float flex, float flex2, float giroDps);
  // true quando a espera e a coleta já passaram (hora de concluir).
  bool terminou(uint32_t agoraMs) const { return ativo_ && agoraMs - inicio_ >= espera_ + duracao_; }
  // Marca como inativo; as médias continuam disponíveis para leitura.
  void encerrar() { ativo_ = false; }

  bool ativo() const { return ativo_; }
  // Quantas amostras entraram na média.
  int amostras() const { return n_; }
  // Média da gravidade: a soma de vetores unitários normalizada (direção média).
  Vec3 gravidade() const { return normalize(soma_); }
  // Média das leituras do flex 1 e 2 (0 se não houve amostras).
  float flex() const { return n_ ? somaFlex_ / n_ : 0; }
  float flex2() const { return n_ ? somaFlex2_ / n_ : 0; }
  float giroMax() const { return giroMax_; }  // maior velocidade angular durante a coleta (°/s)

 private:
  bool ativo_;                      // calibração em andamento
  uint32_t inicio_, espera_, duracao_;  // ms: quando começou, espera inicial, duração da coleta
  int n_;                           // amostras somadas
  Vec3 soma_;                       // soma das gravidades
  float somaFlex_, somaFlex2_;      // soma das leituras dos flex
  float giroMax_;                   // maior |giroscópio| visto na coleta (°/s)
};
