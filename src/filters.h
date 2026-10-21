// Média móvel e detecção de sensor presente (lógica pura, testável no PC).
#pragma once

#include <stdint.h>

template <int N>
class MediaMovel {
 public:
  MediaMovel() : n_(0), i_(0), soma_(0) {}

  void push(int v) {
    if (n_ == N) {
      soma_ -= buf_[i_];
    } else {
      n_++;
    }
    buf_[i_] = v;
    soma_ += v;
    i_ = (i_ + 1) % N;
  }

  float media() const { return n_ ? (float)soma_ / n_ : 0.0f; }
  bool cheio() const { return n_ == N; }
  int ultimo() const { return n_ ? buf_[(i_ + N - 1) % N] : 0; }

  // max - min da janela: um pino solto (sem sensor) oscila muito mais que um divisor resistivo.
  int amplitude() const {
    if (!n_) return 0;
    int mn = buf_[0], mx = buf_[0];
    for (int k = 1; k < n_; k++) {
      if (buf_[k] < mn) mn = buf_[k];
      if (buf_[k] > mx) mx = buf_[k];
    }
    return mx - mn;
  }

 private:
  int buf_[N];
  int n_, i_;
  long soma_;
};

// Liga/desliga com persistência: só muda de estado se a condição contrária durar o tempo pedido.
// Evita que o flex "pisque" entre ativo e inativo por causa de ruído.
class Presenca {
 public:
  Presenca(uint32_t ligaMs, uint32_t desligaMs)
      : ligaMs_(ligaMs), desligaMs_(desligaMs), presente_(false), iniciado_(false), desde_(0) {}

  void atualizar(bool okAgora, uint32_t agora) {
    if (!iniciado_) {
      iniciado_ = true;
      presente_ = okAgora;
      desde_ = agora;
      return;
    }
    if (okAgora == presente_) {
      desde_ = agora;
      return;
    }
    uint32_t espera = okAgora ? ligaMs_ : desligaMs_;
    if (agora - desde_ >= espera) {
      presente_ = okAgora;
      desde_ = agora;
    }
  }

  bool presente() const { return presente_; }

 private:
  uint32_t ligaMs_, desligaMs_;
  bool presente_, iniciado_;
  uint32_t desde_;
};

// Leitura plausível de um divisor resistivo (flex com resistor de 10 kΩ).
inline bool leituraAdcValida(float media, int amplitude, int minimo, int maximo, int ruidoMax) {
  return media >= minimo && media <= maximo && amplitude <= ruidoMax;
}
