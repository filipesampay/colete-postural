// Testes da lógica pura (rodam no PC): pio test -e native
//
// Cobrem os três arquivos sem dependência de hardware: tilt (vetor gravidade, ângulos, filtro
// complementar), posture (detector de postura, parâmetros, calibrador) e filters (média móvel,
// detecção de sensor presente). O env "native" do platformio.ini compila só tilt.cpp e
// posture.cpp (filters.h é só cabeçalho) com o framework Unity.
// Rodar depois de qualquer mudança em src/tilt.*, src/posture.* ou src/filters.h.
// Documentação: Documentação/09-simulador-testes-ferramentas.md (tabela dos testes)
#include <math.h>
#include <unity.h>

#include "filters.h"
#include "posture.h"
#include "tilt.h"

static const float GRAU = 0.0174532925f;  // π/180: graus -> radianos

// Gravidade vista por um sensor em pé nas costas (eixo Y para cima),
// com o tronco inclinado "frente" graus para a frente e "lado" graus para o lado.
// É o mesmo modelo do MPU falso do simulador (sim/sim_main.cpp, TwoWire::requestFrom).
static Vec3 gravidadeColete(float frente, float lado) {
  // Inclinar para a frente leva a gravidade para +Z; para o lado, para X.
  Vec3 g = v3(sinf(lado * GRAU), cosf(frente * GRAU) * cosf(lado * GRAU), sinf(frente * GRAU) * cosf(lado * GRAU));
  return normalize(g);
}

// Entrada do detector já calibrada, com o desvio pedido e os dois flex inativos.
static Entrada entrada(float desvio) {
  Entrada e = {true, desvio, false, 0, false, 0};
  return e;
}

// Exigidos pelo Unity (rodam antes/depois de cada teste); não há estado para preparar.
void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------ tilt

// Prova que angleBetweenDeg dá o ângulo certo em casos conhecidos (0°, 90°, 30°) e continua
// preciso para ângulos pequenos (0,5°), onde acos() perderia precisão. Base do "desvio".
void test_angulo_entre_vetores() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, angleBetweenDeg(v3(0, 1, 0), v3(0, 1, 0)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, angleBetweenDeg(v3(0, 1, 0), v3(0, 0, 1)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, angleBetweenDeg(gravidadeColete(0, 0), gravidadeColete(30, 0)));
  // ângulo pequeno continua preciso
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, angleBetweenDeg(gravidadeColete(0, 0), gravidadeColete(0.5f, 0)));
}

// Prova a decisão de projeto principal: com o sensor em pé e 10° de inclinação lateral, passar
// pela vertical (frente de -2° a +2°) não pode dar salto no desvio (passos de 0,1° variam < 0,5°).
void test_desvio_sem_salto_perto_da_vertical() {
  // Era o problema da MPU6050_light: com o sensor vertical e um pouco de inclinação lateral,
  // o ângulo pulava ~20° ao passar pela vertical. Aqui a variação tem que ser suave.
  Vec3 ref = gravidadeColete(0, 0);
  float anterior = angleBetweenDeg(ref, gravidadeColete(-2, 10));
  for (float f = -2; f <= 2; f += 0.1f) {
    float d = angleBetweenDeg(ref, gravidadeColete(f, 10));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, anterior, d);
    anterior = d;
  }
}

// Prova que tiltComponentsDeg separa o desvio em frente/trás e lateral sem misturar os eixos:
// só frente (+25°, -10°) não gera lado; só lado (15°) não gera frente. É o que o app mostra.
void test_componentes_frente_e_lado() {
  Vec3 ref = gravidadeColete(0, 0);
  float frente, lado;
  tiltComponentsDeg(gravidadeColete(25, 0), ref, &frente, &lado);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, frente);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, lado);

  tiltComponentsDeg(gravidadeColete(-10, 0), ref, &frente, &lado);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -10.0f, frente);

  tiltComponentsDeg(gravidadeColete(0, 15), ref, &frente, &lado);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, frente);
  // o sinal do lado depende da orientação da placa; só a magnitude é garantida
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, fabsf(lado));
}

// Prova que a decomposição não depende de como o sensor foi preso: com a placa invertida
// (referência -Y), inclinar para a frente continua dando "frente" positivo.
void test_componentes_com_sensor_de_cabeca_para_baixo() {
  // Mesmo com o sensor girado 180° (pinos para cima), "frente" continua positivo para a frente.
  Vec3 ref = v3(0, -1, 0);
  Vec3 g = normalize(v3(0, -cosf(20 * GRAU), sinf(20 * GRAU)));
  float frente;
  tiltComponentsDeg(g, ref, &frente, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, frente);
}

// Prova o caso de borda da bancada: com a placa deitada (referência = eixo Z), o eixo "frente"
// troca para Y e o cálculo não gera NaN (que travaria o app e o detector).
void test_componentes_com_placa_deitada() {
  // Na bancada (placa deitada, Z para cima) não pode dar NaN.
  float frente, lado;
  tiltComponentsDeg(normalize(v3(0, sinf(10 * GRAU), cosf(10 * GRAU))), v3(0, 0, 1), &frente, &lado);
  TEST_ASSERT_FALSE(isnan(frente));
  TEST_ASSERT_FALSE(isnan(lado));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, fabsf(frente));
}

// Prova que o filtro complementar converge para o acelerômetro quando o giroscópio está parado
// (a parcela 0,02 corrige a deriva): após 10 s a 200 Hz, erro < 0,5°.
void test_filtro_converge_para_acelerometro() {
  TiltFilter f(0.98f);
  f.update(v3(0, 1, 0), v3(0, 0, 0), 0.005f);  // 1ª chamada só inicializa com o acelerômetro
  TEST_ASSERT_TRUE(f.initialized());
  Vec3 alvo = gravidadeColete(30, 0);
  for (int i = 0; i < 2000; i++) f.update(alvo, v3(0, 0, 0), 0.005f);  // 10 s
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, angleBetweenDeg(f.gravity(), alvo));
}

// Prova duas coisas do TiltFilter::update: (1) com |acc| = 2 g (choque) o acelerômetro é ignorado
// e só o giroscópio conta; (2) a convenção dg/dt = g × ω leva a gravidade para o lado certo
// (-90 °/s em X por 0,2 s = 18° para +Z, ou seja, "frente").
void test_filtro_segue_giroscopio() {
  // Girar -90°/s em torno de X durante 0,2 s com o acelerômetro "preso" em choque (ignorado)
  // deve levar a gravidade 18° para +Z (inclinar para a frente).
  // (a gravidade vista pela placa gira ao contrário da placa)
  TiltFilter f(0.98f);
  f.reset(v3(0, 1, 0));
  for (int i = 0; i < 40; i++) f.update(v3(0, 2.0f, 0), v3(-90, 0, 0), 0.005f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 18.0f, angleBetweenDeg(f.gravity(), v3(0, 1, 0)));
  TEST_ASSERT_TRUE(f.gravity().z > 0);
}

// Prova que um dt grande (> 0,5 s: MPU que reconectou, travamento) reinicia o filtro direto no
// acelerômetro em vez de integrar o giroscópio por um intervalo absurdo.
void test_filtro_reinicia_com_dt_grande() {
  TiltFilter f;
  f.reset(v3(0, 1, 0));
  f.update(v3(0, 0, 1), v3(0, 0, 0), 2.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, angleBetweenDeg(f.gravity(), v3(0, 0, 1)));
}

// ------------------------------------------------------------------ detector

// Requisito: sem calibração o colete não vibra (não existe "postura correta" de referência),
// mesmo com 80° de desvio por 10 s.
void test_sem_calibracao_nunca_alerta() {
  DetectorPostura d;
  Params p = paramsPadrao();
  Entrada e = entrada(80);
  e.calibrado = false;
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(e, p, 0));
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(e, p, 10000));
}

// Requisito: tempo mínimo. Acima do limite o estado vai para "atenção" e só vira "alerta"
// (motor) quando completa tempoMs (3000 ms): em 3999 ms ainda é atenção, em 4000 ms é alerta.
void test_atencao_e_depois_alerta() {
  DetectorPostura d;
  Params p = paramsPadrao();  // 20°, 3°, 3000 ms
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(entrada(10), p, 0));
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(entrada(25), p, 1000));
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(entrada(25), p, 3999));
  TEST_ASSERT_EQUAL(ESTADO_ALERTA, d.atualizar(entrada(25), p, 4000));
}

// Requisito: histerese. Em alerta, voltar um pouco abaixo do limite (18°) não desliga o motor;
// só abaixo de limite - histerese (17°) a postura volta a "ok". Evita liga-desliga na fronteira.
void test_histerese() {
  DetectorPostura d;
  Params p = paramsPadrao();
  d.atualizar(entrada(25), p, 0);
  TEST_ASSERT_EQUAL(ESTADO_ALERTA, d.atualizar(entrada(25), p, 5000));
  // 18° está abaixo do limite mas dentro da histerese (20 - 3 = 17): continua em alerta
  TEST_ASSERT_EQUAL(ESTADO_ALERTA, d.atualizar(entrada(18), p, 5100));
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(entrada(16.9f), p, 5200));
}

// Requisito: tempo mínimo. Um pico curto (pegar algo no chão, 0,5 s) não vibra, e a contagem
// recomeça do zero no episódio seguinte (não acumula entre episódios).
void test_pico_curto_nao_alerta() {
  DetectorPostura d;
  Params p = paramsPadrao();
  d.atualizar(entrada(30), p, 0);
  d.atualizar(entrada(10), p, 500);  // voltou antes de 3 s
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(entrada(10), p, 5000));
  // e o tempo recomeça do zero na próxima vez
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(entrada(30), p, 6000));
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(entrada(30), p, 8000));
  TEST_ASSERT_EQUAL(ESTADO_ALERTA, d.atualizar(entrada(30), p, 9000));
}

// Requisito: o flex sozinho dispara o alerta (desvio de 5° está ok) e também respeita a
// histerese, em pontos percentuais: entra acima de 15 %, só sai abaixo de 12 %.
void test_flex_dispara_e_respeita_histerese() {
  DetectorPostura d;
  Params p = paramsPadrao();  // limiteFlex 15 %
  Entrada e = entrada(5);
  e.flexAtivo = true;
  e.flexPct = 20;
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(e, p, 0));
  e.flexPct = 13;  // dentro da histerese (15 - 3 = 12)
  TEST_ASSERT_EQUAL(ESTADO_ALERTA, d.atualizar(e, p, 3000));
  e.flexPct = 11;
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(e, p, 3100));
}

// Requisito: fallback do flex. Flex inativo (solto, quebrado, desligado) é ignorado, mesmo
// marcando 100 % de curvatura; o colete segue só com o MPU.
void test_flex_inativo_e_ignorado() {
  DetectorPostura d;
  Params p = paramsPadrao();
  Entrada e = entrada(5);
  e.flexAtivo = false;
  e.flexPct = 100;  // flex solto lê 0 => 100 %: não pode alertar
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(e, p, 0));
  TEST_ASSERT_EQUAL(ESTADO_OK, d.atualizar(e, p, 10000));
}

// Prova que o 2º flex entra no "OU" da má postura: com o flex 1 reto, o flex 2 dobrado (30 %)
// já leva a "atenção" (montagem do BioSpine V3, com dois flex).
void test_flex2() {
  DetectorPostura d;
  Params p = paramsPadrao();
  Entrada e = entrada(5);
  e.flexAtivo = true;
  e.flexPct = 0;
  e.flex2Ativo = true;
  e.flex2Pct = 30;
  TEST_ASSERT_EQUAL(ESTADO_ATENCAO, d.atualizar(e, p, 0));
}

// Prova a conversão da leitura do flex em curvatura: % de QUEDA em relação à calibração
// (2048 -> 1536 = 25 %), nunca negativa (esticou = 0 %) e 0 sem referência gravada.
void test_flex_pct() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, flexPct(2048, 2048));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, flexPct(1536, 2048));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, flexPct(2500, 2048));  // esticou: 0, não negativo
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, flexPct(1000, 0));     // sem referência
}

// Prova que validarParams prende valores absurdos vindos da serial/app nas faixas seguras
// (limite 2–60°, flex 2–90 %, tempo até 60 s, motor até 100 %), mantém a histerese menor que
// os limites (senão o alerta nunca sairia) e troca NaN por um valor válido.
void test_validar_params() {
  Params p = paramsPadrao();
  p.limiteGraus = 500;
  p.histerese = 100;
  p.tempoMs = 999999;
  p.limiteFlex = -5;
  p.intensidade = 250;
  p = validarParams(p);
  TEST_ASSERT_EQUAL_FLOAT(60, p.limiteGraus);
  TEST_ASSERT_EQUAL_FLOAT(2, p.limiteFlex);
  TEST_ASSERT_TRUE(p.histerese < p.limiteFlex);
  TEST_ASSERT_EQUAL_UINT32(60000, p.tempoMs);
  TEST_ASSERT_EQUAL_UINT8(100, p.intensidade);

  Params q = paramsPadrao();
  q.limiteGraus = NAN;
  q = validarParams(q);
  TEST_ASSERT_FALSE(isnan(q.limiteGraus));
}

// Prova a linha do tempo da calibração (espera 1,5 s, depois coleta 1 s) e o que ela grava:
// média do flex, maior velocidade angular (para recusar calibração "tremida") e a gravidade
// média normalizada (a referência g0).
void test_calibrador_media() {
  Calibrador c;
  c.iniciar(1000, 1500, 1000);  // espera 1,5 s, coleta 1 s
  TEST_ASSERT_TRUE(c.ativo());
  TEST_ASSERT_FALSE(c.coletando(2000));
  TEST_ASSERT_TRUE(c.coletando(2500));
  c.amostra(gravidadeColete(10, 0), 2000, 0, 3);
  c.amostra(gravidadeColete(10, 0), 2100, 0, 12);
  TEST_ASSERT_FALSE(c.terminou(3000));
  TEST_ASSERT_TRUE(c.terminou(3500));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2050.0f, c.flex());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, c.giroMax());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, angleBetweenDeg(c.gravidade(), gravidadeColete(10, 0)));
}

// ------------------------------------------------------------------ filtros

// Prova a média móvel circular: média com a janela incompleta, descarte do mais antigo quando
// cheia, amplitude (max - min, usada para detectar pino solto) e a última amostra.
void test_media_movel() {
  MediaMovel<4> m;
  m.push(10);
  m.push(20);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, m.media());
  TEST_ASSERT_FALSE(m.cheio());
  m.push(30);
  m.push(40);
  m.push(50);  // descarta o 10
  TEST_ASSERT_TRUE(m.cheio());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, m.media());
  TEST_ASSERT_EQUAL(30, m.amplitude());
  TEST_ASSERT_EQUAL(50, m.ultimo());
}

// Prova a persistência da detecção de sensor: só passa a "presente" depois de 2 s seguidos de
// leitura válida (uma amostra ruim no meio reinicia a contagem) e só sai depois de 1 s inválido.
// É o que impede o flex de "piscar" entre ativo e inativo com ruído.
void test_presenca_com_persistencia() {
  Presenca p(2000, 1000);
  p.atualizar(false, 0);
  TEST_ASSERT_FALSE(p.presente());
  p.atualizar(true, 100);
  p.atualizar(true, 1500);
  TEST_ASSERT_FALSE(p.presente());  // ainda não ficou 2 s válido
  p.atualizar(false, 1600);         // ruído: reinicia a contagem
  p.atualizar(true, 1700);
  p.atualizar(true, 3500);  // conta desde a última amostra inválida (1600)
  TEST_ASSERT_FALSE(p.presente());
  p.atualizar(true, 3600);
  TEST_ASSERT_TRUE(p.presente());
  p.atualizar(false, 4000);
  p.atualizar(false, 5000);
  TEST_ASSERT_FALSE(p.presente());
}

// Prova os critérios de leitura plausível do divisor do flex: faixa 100–4000 e janela estável.
// Cada caso falso é um defeito de montagem real (sem resistor, flex solto, pino flutuando).
void test_leitura_adc_valida() {
  TEST_ASSERT_TRUE(leituraAdcValida(2048, 30, 100, 4000, 300));
  TEST_ASSERT_FALSE(leituraAdcValida(4095, 0, 100, 4000, 300));  // falta o resistor
  TEST_ASSERT_FALSE(leituraAdcValida(0, 0, 100, 4000, 300));     // flex solto
  TEST_ASSERT_FALSE(leituraAdcValida(2000, 900, 100, 4000, 300)); // pino flutuando
}

// Lista de testes executados pelo Unity (21). Teste novo: acrescente aqui e na tabela de
// Documentação/09-simulador-testes-ferramentas.md.
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_angulo_entre_vetores);
  RUN_TEST(test_desvio_sem_salto_perto_da_vertical);
  RUN_TEST(test_componentes_frente_e_lado);
  RUN_TEST(test_componentes_com_sensor_de_cabeca_para_baixo);
  RUN_TEST(test_componentes_com_placa_deitada);
  RUN_TEST(test_filtro_converge_para_acelerometro);
  RUN_TEST(test_filtro_segue_giroscopio);
  RUN_TEST(test_filtro_reinicia_com_dt_grande);
  RUN_TEST(test_sem_calibracao_nunca_alerta);
  RUN_TEST(test_atencao_e_depois_alerta);
  RUN_TEST(test_histerese);
  RUN_TEST(test_pico_curto_nao_alerta);
  RUN_TEST(test_flex_dispara_e_respeita_histerese);
  RUN_TEST(test_flex_inativo_e_ignorado);
  RUN_TEST(test_flex2);
  RUN_TEST(test_flex_pct);
  RUN_TEST(test_validar_params);
  RUN_TEST(test_calibrador_media);
  RUN_TEST(test_media_movel);
  RUN_TEST(test_presenca_com_persistencia);
  RUN_TEST(test_leitura_adc_valida);
  return UNITY_END();
}
