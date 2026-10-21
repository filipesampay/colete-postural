// Implementação da matemática de inclinação (ver tilt.h).
// Lógica pura: só <math.h>, roda igual no ESP32, nos testes nativos e no simulador.
//
// Convenção: "gravidade" aqui é o que o acelerômetro mede parado, +1 g apontando para CIMA
// (a reação do apoio). Só a direção importa: todos os vetores de gravidade são unitários.
//
// Documentação: Documentação/referencia/tilt.md e Documentação/03-algoritmo-inclinacao.md
#include "tilt.h"

// Fatores de conversão entre radianos (math.h) e graus (resto do firmware).
static const float RAD_PARA_GRAU = 57.2957795f;
static const float GRAU_PARA_RAD = 0.0174532925f;

// Divide pelo módulo. Abaixo de 1e-6 o vetor é tratado como nulo e volta sem mudança
// (evita divisão por zero; quem chama decide o que fazer, ver TiltFilter::reset).
Vec3 normalize(Vec3 a) {
  float n = norm(a);
  if (n < 1e-6f) return a;
  return a * (1.0f / n);
}

// Ângulo entre a e b em graus (0..180).
float angleBetweenDeg(Vec3 a, Vec3 b) {
  // atan2(|a×b|, a·b) é preciso perto de 0°, ao contrário de acos.
  // |a×b| = |a||b|·sen θ e a·b = |a||b|·cos θ, então atan2 devolve θ sem precisar normalizar.
  // Com acos(a·b), perto de 0° o cosseno é quase constante (cos 0,5° = 0,99996): o erro de
  // arredondamento do float vira alguns centésimos de grau (até ~0,04°), e um a·b de 1,0000001 daria NaN.
  // Perto de 0° é justamente onde o colete passa a maior parte do tempo.
  return atan2f(norm(cross(a, b)), dot(a, b)) * RAD_PARA_GRAU;
}

// Decompõe a inclinação de g em relação a ref em frente/trás e lateral (graus).
void tiltComponentsDeg(Vec3 g, Vec3 ref, float* frente, float* lado) {
  ref = normalize(ref);
  g = normalize(g);

  // Monta uma base ortonormal (ref, f, l) presa à postura calibrada:
  //   ref = "cima" da postura correta
  //   f   = "frente": o eixo Z da placa (sai das costas, componentes para fora) com a parte
  //         paralela a ref removida (Gram-Schmidt) e normalizado -> perpendicular a ref
  //   l   = "lado": ref × f, perpendicular aos dois
  // Eixo "frente": eixo Z da placa projetado no plano perpendicular a ref.
  Vec3 eixo = v3(0, 0, 1);
  // Se Z for quase paralelo a ref (|cos| > 0,9, ou seja, menos de ~26° entre eles), a projeção
  // fica curta demais e a direção "frente" fica instável: aí usa o Y da placa.
  if (fabsf(dot(eixo, ref)) > 0.9f) eixo = v3(0, 1, 0);  // placa deitada (bancada)
  Vec3 f = normalize(eixo - ref * dot(eixo, ref));
  Vec3 l = cross(ref, f);

  // Ângulo de g no plano (ref, f) e no plano (ref, l). atan2 com o mesmo cosseno c = g·ref
  // dá ângulos com sinal e sem singularidade perto da postura calibrada.
  float c = dot(g, ref);
  if (frente) *frente = atan2f(dot(g, f), c) * RAD_PARA_GRAU;
  if (lado) *lado = atan2f(dot(g, l), c) * RAD_PARA_GRAU;
}

// Reinicia a estimativa pela direção do acelerômetro.
void TiltFilter::reset(Vec3 acc) {
  g_ = normalize(acc);
  init_ = norm(acc) > 1e-3f;  // vetor nulo = leitura inválida: continua não inicializado
}

// Um passo do filtro complementar (chamado a 200 Hz por sensores_atualizar).
void TiltFilter::update(Vec3 acc, Vec3 gyroDps, float dt) {
  // Sem estimativa anterior, ou intervalo absurdo (relógio voltou, leitura atrasada mais de
  // 0,5 s, MPU reconectado): integrar o giroscópio daria lixo. Recomeça pelo acelerômetro.
  if (!init_ || dt <= 0.0f || dt > 0.5f) {
    reset(acc);
    return;
  }

  // 1) Previsão pelo giroscópio.
  // Gravidade vista pela placa gira ao contrário da placa: dg/dt = g × ω.
  // A gravidade é fixa no mundo; quando a placa gira com velocidade angular ω, qualquer vetor
  // fixo no mundo, visto nos eixos da placa, gira com -ω: dg/dt = -ω × g = g × ω.
  // Integração de Euler de um passo (g + dg/dt·dt) e normalização: o passo de Euler aumenta
  // um pouco o módulo a cada iteração; normalizar mantém g unitário e evita que o erro acumule.
  Vec3 w = gyroDps * GRAU_PARA_RAD;  // °/s -> rad/s
  Vec3 previsto = normalize(g_ + cross(g_, w) * dt);

  // 2) Correção pelo acelerômetro.
  // Com aceleração forte (passo, batida), o acelerômetro não mede só a gravidade:
  // nesse instante confia só no giroscópio.
  // Parado, |acc| = 1 g. Fora de [0,7; 1,3] g há aceleração do corpo somada e a direção de acc
  // não é a vertical: usá-la puxaria a estimativa para o lado errado.
  float modulo = norm(acc);
  if (modulo < 0.7f || modulo > 1.3f) {
    g_ = previsto;
    return;
  }

  // 3) Mistura complementar: coef_ (0,98) do giroscópio + 0,02 do acelerômetro.
  // O giroscópio é bom no curto prazo (sem ruído de vibração) mas deriva; o acelerômetro é
  // ruidoso mas não deriva. Com 200 Hz (dt = 5 ms) a constante de tempo fica
  // τ = coef·dt / (1 - coef) ≈ 0,25 s: movimentos mais rápidos que isso vêm do giroscópio,
  // e a deriva é corrigida pelo acelerômetro em menos de um segundo.
  g_ = normalize(previsto * coef_ + normalize(acc) * (1.0f - coef_));
}
