// Inclinação a partir do vetor gravidade (lógica pura, sem Arduino: testável no PC).
//
// Por que não usar ângulos de Euler (pitch/roll da MPU6050_light)?
// No colete o sensor fica em pé nas costas. Nessa posição as fórmulas de pitch/roll
// ficam perto da singularidade e "pulam". Aqui o desvio é o ângulo entre a gravidade
// atual e a gravidade gravada na calibração: não depende de como o sensor foi preso.
#pragma once

#include <math.h>

struct Vec3 {
  float x, y, z;
};

inline Vec3 v3(float x, float y, float z) {
  Vec3 v = {x, y, z};
  return v;
}
inline Vec3 operator+(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(Vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float norm(Vec3 a) { return sqrtf(dot(a, a)); }

// Vetor unitário. Vetor nulo volta como está.
Vec3 normalize(Vec3 a);

// Ângulo entre dois vetores, em graus (0..180). Estável para ângulos pequenos.
float angleBetweenDeg(Vec3 a, Vec3 b);

// Decompõe a inclinação de g em relação a ref em dois ângulos (graus):
//   frente: positivo = tronco para a frente (sensor com os componentes virados para fora)
//   lado:   inclinação lateral
// Usa o eixo Z da placa como referência de "frente"; se a placa estiver deitada, usa o Y.
void tiltComponentsDeg(Vec3 g, Vec3 ref, float* frente, float* lado);

// Filtro complementar sobre o vetor gravidade:
// o giroscópio acompanha movimentos rápidos e o acelerômetro corrige a deriva.
class TiltFilter {
 public:
  explicit TiltFilter(float gyroCoef = 0.98f) : coef_(gyroCoef), g_(v3(0, 0, 1)), init_(false) {}

  void reset(Vec3 acc);
  // acc em g, gyro em °/s, dt em segundos.
  void update(Vec3 acc, Vec3 gyroDps, float dt);

  Vec3 gravity() const { return g_; }
  bool initialized() const { return init_; }

 private:
  float coef_;
  Vec3 g_;
  bool init_;
};
