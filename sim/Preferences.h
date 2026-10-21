// Simulador: NVS em memória. Os valores valem só enquanto o processo do simulador roda
// (não há arquivo em disco: cada execução começa sem calibração e com os parâmetros padrão).
//
// Substitui o <Preferences.h> do core (tools/sim.sh compila com -Isim antes de -Isrc). Tem só os
// métodos que src/settings.cpp usa. Tudo é guardado como texto num mapa "namespace/chave" -> valor.
// Documentação: Documentação/09-simulador-testes-ferramentas.md
#pragma once

#include <stdint.h>

#include <map>
#include <string>

class Preferences {
 public:
  // Abre o namespace (no firmware: "colete"). readOnly é ignorado.
  bool begin(const char* ns, bool readOnly = false) {
    ns_ = ns;
    return true;
  }
  void end() {}
  // Leitores: devolvem o valor salvo ou "d" (padrão) se a chave nunca foi gravada,
  // igual ao Preferences do ESP32.
  float getFloat(const char* k, float d) { return tem(k) ? std::stof(dados()[chave(k)]) : d; }
  uint32_t getUInt(const char* k, uint32_t d) { return tem(k) ? std::stoul(dados()[chave(k)]) : d; }
  uint8_t getUChar(const char* k, uint8_t d) { return tem(k) ? (uint8_t)std::stoul(dados()[chave(k)]) : d; }
  bool getBool(const char* k, bool d) { return tem(k) ? dados()[chave(k)] == "1" : d; }
  // Escritores: gravam o valor como texto. Retornam 1 ("bytes gravados" diferente de zero = ok).
  size_t putFloat(const char* k, float v) { return put(k, std::to_string(v)); }
  size_t putUInt(const char* k, uint32_t v) { return put(k, std::to_string(v)); }
  size_t putUChar(const char* k, uint8_t v) { return put(k, std::to_string(v)); }
  size_t putBool(const char* k, bool v) { return put(k, v ? "1" : "0"); }

  // Armazenamento único, compartilhado por todas as instâncias (cada Preferences do firmware é
  // uma variável local que abre e fecha; o conteúdo precisa sobreviver entre elas).
  static std::map<std::string, std::string>& dados() {
    static std::map<std::string, std::string> m;
    return m;
  }

 private:
  // Chave completa no mapa: "namespace/chave".
  std::string chave(const char* k) { return ns_ + "/" + k; }
  bool tem(const char* k) { return dados().count(chave(k)) > 0; }
  size_t put(const char* k, const std::string& v) {
    dados()[chave(k)] = v;
    return 1;
  }
  std::string ns_;  // namespace aberto em begin()
};
