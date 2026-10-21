// Média móvel e detecção de sensor presente (lógica pura, testável no PC).
//
// Papel no sistema: tratamento dos sensores analógicos (flex 1, flex 2, FSR) em sensors.cpp.
// MediaMovel suaviza a leitura do ADC e mede quanto ela oscila; leituraAdcValida diz se a
// janela parece um divisor resistivo de verdade; Presenca transforma esse "parece válido"
// instantâneo em "sensor presente" estável, com tempo mínimo para ligar e para desligar.
// Roda em: tarefa de controle (ESP32) e testes nativos.
//
// Documentação: Documentação/referencia/filters.md e Documentação/05-sensores-analogicos.md
#pragma once

#include <stdint.h>

// Média móvel de N amostras inteiras com buffer circular e soma corrente (O(1) por amostra).
// No firmware N = ADC_MEDIA_N (10): com o ADC lido a 100 Hz, a janela cobre 100 ms.
template <int N>
class MediaMovel {
 public:
  MediaMovel() : n_(0), i_(0), soma_(0) {}

  // Acrescenta uma amostra. Com o buffer cheio, a mais antiga sai da soma antes.
  void push(int v) {
    if (n_ == N) {
      soma_ -= buf_[i_];  // i_ aponta para a mais antiga (próxima a ser sobrescrita)
    } else {
      n_++;
    }
    buf_[i_] = v;
    soma_ += v;
    i_ = (i_ + 1) % N;
  }

  // Média das amostras presentes (0 se vazia). Antes de encher, divide só pelas que existem.
  float media() const { return n_ ? (float)soma_ / n_ : 0.0f; }
  // true quando já tem N amostras.
  bool cheio() const { return n_ == N; }
  // Amostra mais recente (0 se vazia).
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
  int buf_[N];  // amostras (buffer circular)
  int n_, i_;   // quantas amostras válidas / posição da próxima escrita
  long soma_;   // soma das amostras no buffer
};

// Liga/desliga com persistência: só muda de estado se a condição contrária durar o tempo pedido.
// Evita que o flex "pisque" entre ativo e inativo por causa de ruído.
// O primeiro atualizar() adota o valor recebido na hora (sem esperar).
// Em sensors.cpp: flex com 2 s para ligar (FLEX_LIGA_MS) e 1 s para desligar
// (FLEX_DESLIGA_MS); o FSR usa UINT32_MAX para desligar, ou seja, nunca desliga.
class Presenca {
 public:
  // ligaMs: tempo com okAgora = true para passar a presente.
  // desligaMs: tempo com okAgora = false para passar a ausente.
  Presenca(uint32_t ligaMs, uint32_t desligaMs)
      : ligaMs_(ligaMs), desligaMs_(desligaMs), presente_(false), iniciado_(false), desde_(0) {}

  // okAgora = a leitura deste instante parece válida. agora = millis().
  void atualizar(bool okAgora, uint32_t agora) {
    if (!iniciado_) {
      iniciado_ = true;
      presente_ = okAgora;
      desde_ = agora;
      return;
    }
    // Condição concorda com o estado: reinicia o cronômetro (a contagem tem que ser contínua).
    if (okAgora == presente_) {
      desde_ = agora;
      return;
    }
    // Condição contrária: só troca depois de durar o tempo pedido sem interrupção.
    uint32_t espera = okAgora ? ligaMs_ : desligaMs_;
    if (agora - desde_ >= espera) {
      presente_ = okAgora;
      desde_ = agora;
    }
  }

  // Estado atual (com persistência).
  bool presente() const { return presente_; }

 private:
  uint32_t ligaMs_, desligaMs_;  // tempos de persistência (ms)
  bool presente_, iniciado_;     // estado / já recebeu a primeira leitura
  uint32_t desde_;               // millis() da última vez em que a condição concordou com o estado
};

// Leitura plausível de um divisor resistivo (flex com resistor de 10 kΩ).
// Válida se a média estiver em [minimo, maximo] e a janela oscilar no máximo ruidoMax.
// No firmware: 100–4000 (ADC_MIN_VALIDO, ADC_MAX_VALIDO) e amplitude <= 300 (ADC_RUIDO_MAX).
// ~0 = flex solto/quebrado; ~4095 = falta o resistor de 10 kΩ; oscilando = pino flutuando.
inline bool leituraAdcValida(float media, int amplitude, int minimo, int maximo, int ruidoMax) {
  return media >= minimo && media <= maximo && amplitude <= ruidoMax;
}
