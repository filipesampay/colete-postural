// Simulador: NVS em memória (sobrevive a "!reboot" dentro da mesma execução).
#pragma once

#include <stdint.h>

#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char* ns, bool readOnly = false) {
    ns_ = ns;
    return true;
  }
  void end() {}
  float getFloat(const char* k, float d) { return tem(k) ? std::stof(dados()[chave(k)]) : d; }
  uint32_t getUInt(const char* k, uint32_t d) { return tem(k) ? std::stoul(dados()[chave(k)]) : d; }
  uint8_t getUChar(const char* k, uint8_t d) { return tem(k) ? (uint8_t)std::stoul(dados()[chave(k)]) : d; }
  bool getBool(const char* k, bool d) { return tem(k) ? dados()[chave(k)] == "1" : d; }
  size_t putFloat(const char* k, float v) { return put(k, std::to_string(v)); }
  size_t putUInt(const char* k, uint32_t v) { return put(k, std::to_string(v)); }
  size_t putUChar(const char* k, uint8_t v) { return put(k, std::to_string(v)); }
  size_t putBool(const char* k, bool v) { return put(k, v ? "1" : "0"); }

  static std::map<std::string, std::string>& dados() {
    static std::map<std::string, std::string> m;
    return m;
  }

 private:
  std::string chave(const char* k) { return ns_ + "/" + k; }
  bool tem(const char* k) { return dados().count(chave(k)) > 0; }
  size_t put(const char* k, const std::string& v) {
    dados()[chave(k)] = v;
    return 1;
  }
  std::string ns_;
};
