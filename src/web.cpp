#include "web.h"

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include "config.h"
#include "protocolo.h"

// Arquivos de src/web embutidos pelo board_build.embed_txtfiles (terminam em '\0').
extern const char index_html[] asm("_binary_src_web_index_html_start");
extern const char manifest_json[] asm("_binary_src_web_manifest_json_start");
extern const char icon_svg[] asm("_binary_src_web_icon_svg_start");

static WebServer http(HTTP_PORTA);
static WebSocketsServer ws(WS_PORTA);
static bool ativo = false;
static char ip[16] = "-";
static char buf[640];

void web_enviarConfig() {
  if (!ativo) return;
  size_t n = protocolo_config(buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

void web_evento(const char* nivel, const char* msg) {
  if (!ativo) return;
  size_t n = protocolo_evento(nivel, msg, buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

void web_enviarDados(const Status& s) {
  if (!ativo || ws.connectedClients() == 0) return;
  size_t n = protocolo_dados(s, millis(), buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

static void aoEventoWs(uint8_t cliente, WStype_t tipo, uint8_t* payload, size_t len) {
  // Ao conectar, ou se o comando pedir ("get"), manda o config só para quem pediu.
  if (tipo == WStype_CONNECTED || (tipo == WStype_TEXT && protocolo_comando((const char*)payload, len))) {
    char cfg[384];
    size_t n = protocolo_config(cfg, sizeof(cfg));
    ws.sendTXT(cliente, cfg, n);
  }
}

static void servir(const char* tipo, const char* conteudo) {
  http.sendHeader("Cache-Control", "no-cache");
  http.send_P(200, tipo, conteudo);
}

void web_iniciar() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_SENHA)) {
    Serial.println(F("[wifi] falha ao criar o Access Point"));
    return;
  }
  // Menos potência = menos pico de corrente (ajuda a não resetar com o motor) e
  // alcance de sobra para uma sala.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setSleep(false);
  strlcpy(ip, WiFi.softAPIP().toString().c_str(), sizeof(ip));

  http.on("/", HTTP_GET, [] { servir("text/html; charset=utf-8", index_html); });
  http.on("/index.html", HTTP_GET, [] { servir("text/html; charset=utf-8", index_html); });
  http.on("/manifest.json", HTTP_GET, [] { servir("application/manifest+json", manifest_json); });
  http.on("/icon.svg", HTTP_GET, [] { servir("image/svg+xml", icon_svg); });
  http.onNotFound([] {
    http.sendHeader("Location", String("http://") + ip + "/");
    http.send(302, "text/plain", "");
  });
  http.begin();

  ws.begin();
  ws.onEvent(aoEventoWs);
  ws.enableHeartbeat(5000, 2000, 2);  // derruba clientes que sumiram (celular saiu da rede)

  ativo = true;
  Serial.printf("[wifi] AP \"%s\" senha \"%s\" -> abra http://%s\n", WIFI_SSID, WIFI_SENHA, ip);
}

void web_atualizar() {
  if (!ativo) return;
  http.handleClient();
  ws.loop();
}

bool web_ativo() { return ativo; }
const char* web_ip() { return ip; }
int web_clientes() { return ativo ? ws.connectedClients() : 0; }
