// Implementação da detecção de má postura e do calibrador (ver posture.h).
// Lógica pura: sem Arduino, testada em test/test_posture/test_main.cpp.
//
// Documentação: Documentação/referencia/posture.md e Documentação/04-deteccao-e-calibracao.md
#include "posture.h"

#include "config.h"

// Prende v em [mn, mx]. NaN (valor inválido, qualquer comparação com ele dá falso) cai no
// mínimo, para nunca chegar ao detector.
static float prender(float v, float mn, float mx) {
  if (v != v) return mn;  // NaN
  return v < mn ? mn : (v > mx ? mx : v);
}

// Valores de fábrica (config.h). Usados no primeiro boot e no comando "reset".
Params paramsPadrao() {
  Params p;
  p.limiteGraus = PADRAO_LIMITE_GRAUS;
  p.histerese = PADRAO_HISTERESE;
  p.tempoMs = PADRAO_TEMPO_MS;
  p.limiteFlex = PADRAO_LIMITE_FLEX;
  p.intensidade = PADRAO_INTENSIDADE;
  return p;
}

// Garante parâmetros coerentes antes de salvar/usar (chamada por app_definirConfig e ao
// carregar da NVS).
Params validarParams(Params p) {
  // 2° mínimo: abaixo disso o ruído do sensor parado já dispararia o alerta.
  // 60° máximo: acima disso não é mais "postura", e o colete nunca vibraria.
  p.limiteGraus = prender(p.limiteGraus, 2.0f, 60.0f);
  p.limiteFlex = prender(p.limiteFlex, 2.0f, 90.0f);
  // A histerese precisa ser menor que os dois limites, senão nunca sairia do alerta.
  // Para sair é preciso ficar abaixo de (limite - histerese); com histerese >= limite esse
  // valor seria <= 0, e desvio e flexPct nunca são negativos. O "- 1" deixa pelo menos
  // 1 ° (ou 1 ponto %) de faixa para a saída.
  float menorLimite = p.limiteGraus < p.limiteFlex ? p.limiteGraus : p.limiteFlex;
  p.histerese = prender(p.histerese, 0.0f, menorLimite - 1.0f);
  if (p.tempoMs > 60000) p.tempoMs = 60000;  // até 1 min em má postura antes de vibrar
  if (p.intensidade > 100) p.intensidade = 100;
  return p;
}

// Nome do estado como vai no JSON do app ({"estado":"..."}).
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

// % de queda da leitura do flex em relação à referência da calibração.
// O divisor é 3V3 -> flex -> pino -> 10 kΩ -> GND: ao dobrar, a resistência do flex sobe e
// a tensão no pino (leitura) cai. Por isso "curvatura" = queda relativa.
float flexPct(float leitura, float referencia) {
  if (referencia < 1.0f) return 0.0f;  // sem referência (flex não calibrado)
  return prender((referencia - leitura) / referencia * 100.0f, 0.0f, 100.0f);
}

// Estado inicial: postura boa, sem tempo acumulado.
void DetectorPostura::reset() {
  ruim_ = false;
  inicio_ = 0;
  estado_ = ESTADO_OK;
}

// Máquina de estados da postura (chamada a 50 Hz pela tarefa de controle).
//
// Duas camadas:
//  1) ruim_ com histerese: liga quando algum sensor passa do limite e só desliga quando
//     todos voltam abaixo de (limite - histerese). Entre os dois valores mantém o que era.
//     Isso evita que o motor fique ligando e desligando quando o desvio oscila no limite.
//  2) tempo mínimo: ruim_ há menos de tempoMs = ATENÇÃO (sem vibrar); há tempoMs ou mais =
//     ALERTA. Um pico curto (pegar algo no chão) não vibra.
Estado DetectorPostura::atualizar(const Entrada& e, const Params& p, uint32_t agoraMs) {
  // MPU: compara o desvio (°) com o limite.
  bool acima = e.desvio > p.limiteGraus;
  bool abaixo = e.desvio < p.limiteGraus - p.histerese;
  // Flex ativos: OR para entrar (qualquer sensor ruim basta), AND para sair (todos precisam
  // estar bons). A mesma histerese vale em pontos %.
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
    inicio_ = agoraMs;  // começa a contar o tempo mínimo
  } else if (ruim_ && abaixo) {
    ruim_ = false;
  }
  // Nos outros casos (dentro da faixa de histerese) o estado anterior se mantém.

  if (!ruim_) {
    estado_ = ESTADO_OK;
  } else {
    // Subtração em uint32_t: continua certa quando millis() dá a volta (~49 dias).
    estado_ = (agoraMs - inicio_ >= p.tempoMs) ? ESTADO_ALERTA : ESTADO_ATENCAO;
  }
  return estado_;
}

// Zera as somas e marca o início. A coleta só começa depois de esperaMs.
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

// Acumula uma amostra (main.cpp chama a 50 Hz enquanto coletando() for true: ~50 amostras).
// A gravidade é somada como vetor; gravidade() normaliza a soma, o que dá a direção média.
// giroMax_ guarda o pior movimento: se passar de CALIBRACAO_GIRO_MAX, main.cpp recusa.
void Calibrador::amostra(Vec3 g, float flex, float flex2, float giroDps) {
  if (!ativo_) return;
  soma_ = soma_ + g;
  somaFlex_ += flex;
  somaFlex2_ += flex2;
  if (giroDps > giroMax_) giroMax_ = giroDps;
  n_++;
}
