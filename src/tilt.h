// Inclinação a partir do vetor gravidade (lógica pura, sem Arduino: testável no PC).
//
// Papel no sistema: é a matemática da postura. sensors.cpp alimenta o TiltFilter com o
// acelerômetro e o giroscópio do MPU-6050 (200 Hz) e guarda a gravidade filtrada; main.cpp
// compara essa gravidade com a referência gravada na calibração (angleBetweenDeg = desvio)
// e a decompõe em frente/trás e lateral só para mostrar (tiltComponentsDeg).
// Roda em: tarefa de controle (ESP32), testes nativos e simulador (PC).
//
// Por que não usar ângulos de Euler (pitch/roll da MPU6050_light)?
// No colete o sensor fica em pé nas costas. Nessa posição as fórmulas de pitch/roll
// ficam perto da singularidade e "pulam". Aqui o desvio é o ângulo entre a gravidade
// atual e a gravidade gravada na calibração: não depende de como o sensor foi preso.
//
// Documentação: Documentação/referencia/tilt.md e Documentação/03-algoritmo-inclinacao.md
#pragma once

#include <math.h>

// Vetor 3D em float. Usado para aceleração (g), velocidade angular (°/s) e para a
// gravidade normalizada (vetor unitário, sem unidade). Eixos = eixos da placa do MPU.
struct Vec3 {
  float x, y, z;  // componentes nos eixos X, Y e Z da placa
};

// Monta um Vec3 (C++11 sem construtor, para o struct continuar sendo um agregado simples).
inline Vec3 v3(float x, float y, float z) {
  Vec3 v = {x, y, z};
  return v;
}
// Operações vetoriais básicas, componente a componente.
inline Vec3 operator+(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
// Multiplica por escalar.
inline Vec3 operator*(Vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
// Produto escalar: |a|·|b|·cos(ângulo). Para vetores unitários, é o cosseno do ângulo entre eles.
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Produto vetorial: perpendicular a a e b, com módulo |a|·|b|·sen(ângulo). Regra da mão direita.
inline Vec3 cross(Vec3 a, Vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
// Módulo (comprimento) do vetor.
inline float norm(Vec3 a) { return sqrtf(dot(a, a)); }

// Vetor unitário. Vetor nulo volta como está.
Vec3 normalize(Vec3 a);

// Ângulo entre dois vetores, em graus (0..180). Estável para ângulos pequenos.
// Não precisa de vetores unitários. É o "desvio" do colete: angleBetweenDeg(g atual, g0).
float angleBetweenDeg(Vec3 a, Vec3 b);

// Decompõe a inclinação de g em relação a ref em dois ângulos (graus):
//   frente: positivo = tronco para a frente (sensor com os componentes virados para fora)
//   lado:   inclinação lateral
// Usa o eixo Z da placa como referência de "frente"; se a placa estiver deitada, usa o Y.
// Só para exibição (app, serial, plotter): a detecção usa o desvio total, que não tem sinal.
// frente/lado podem ser NULL se o chamador não precisar de um deles.
void tiltComponentsDeg(Vec3 g, Vec3 ref, float* frente, float* lado);

// Filtro complementar sobre o vetor gravidade:
// o giroscópio acompanha movimentos rápidos e o acelerômetro corrige a deriva.
// Estado = direção da gravidade vista pela placa (vetor unitário). Chamado a cada leitura
// do MPU (200 Hz, PERIODO_MPU_MS) por sensors.cpp.
class TiltFilter {
 public:
  // gyroCoef: peso do giroscópio na mistura (0..1). O firmware usa MPU_FILTRO_GIRO (0,98).
  // Começa "não inicializado": a primeira update() só copia o acelerômetro.
  explicit TiltFilter(float gyroCoef = 0.98f) : coef_(gyroCoef), g_(v3(0, 0, 1)), init_(false) {}

  // Reinicia a estimativa com a direção do acelerômetro (aceleração em g).
  // Fica inicializado só se o vetor não for nulo (sensor respondendo).
  void reset(Vec3 acc);
  // Um passo do filtro.
  // acc em g, gyro em °/s, dt em segundos.
  // dt <= 0 ou > 0,5 s (leitura atrasada, MPU reconectado) reinicia pelo acelerômetro.
  void update(Vec3 acc, Vec3 gyroDps, float dt);

  // Gravidade estimada (vetor unitário, eixos da placa).
  Vec3 gravity() const { return g_; }
  // true depois de um reset() com aceleração válida.
  bool initialized() const { return init_; }

 private:
  float coef_;  // peso do giroscópio (o acelerômetro pesa 1 - coef_)
  Vec3 g_;      // estimativa atual da gravidade (unitária)
  bool init_;   // já recebeu uma aceleração válida
};
