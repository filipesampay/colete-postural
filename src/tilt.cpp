#include "tilt.h"

static const float RAD_PARA_GRAU = 57.2957795f;
static const float GRAU_PARA_RAD = 0.0174532925f;

Vec3 normalize(Vec3 a) {
  float n = norm(a);
  if (n < 1e-6f) return a;
  return a * (1.0f / n);
}

float angleBetweenDeg(Vec3 a, Vec3 b) {
  // atan2(|a×b|, a·b) é preciso perto de 0°, ao contrário de acos.
  return atan2f(norm(cross(a, b)), dot(a, b)) * RAD_PARA_GRAU;
}

void tiltComponentsDeg(Vec3 g, Vec3 ref, float* frente, float* lado) {
  ref = normalize(ref);
  g = normalize(g);

  // Eixo "frente": eixo Z da placa projetado no plano perpendicular a ref.
  Vec3 eixo = v3(0, 0, 1);
  if (fabsf(dot(eixo, ref)) > 0.9f) eixo = v3(0, 1, 0);  // placa deitada (bancada)
  Vec3 f = normalize(eixo - ref * dot(eixo, ref));
  Vec3 l = cross(ref, f);

  float c = dot(g, ref);
  if (frente) *frente = atan2f(dot(g, f), c) * RAD_PARA_GRAU;
  if (lado) *lado = atan2f(dot(g, l), c) * RAD_PARA_GRAU;
}

void TiltFilter::reset(Vec3 acc) {
  g_ = normalize(acc);
  init_ = norm(acc) > 1e-3f;
}

void TiltFilter::update(Vec3 acc, Vec3 gyroDps, float dt) {
  if (!init_ || dt <= 0.0f || dt > 0.5f) {
    reset(acc);
    return;
  }

  // Gravidade vista pela placa gira ao contrário da placa: dg/dt = g × ω.
  Vec3 w = gyroDps * GRAU_PARA_RAD;
  Vec3 previsto = normalize(g_ + cross(g_, w) * dt);

  // Com aceleração forte (passo, batida), o acelerômetro não mede só a gravidade:
  // nesse instante confia só no giroscópio.
  float modulo = norm(acc);
  if (modulo < 0.7f || modulo > 1.3f) {
    g_ = previsto;
    return;
  }

  g_ = normalize(previsto * coef_ + normalize(acc) * (1.0f - coef_));
}
