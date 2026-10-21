// Servidor do app web: Access Point próprio, página embutida na flash (HTTP) e WebSocket.
// O JSON em si é montado/interpretado por protocolo.cpp; aqui fica só o transporte.
// Bibliotecas: WebServer (vem no core) + WebSocketsServer (links2004). Evita a ESPAsyncWebServer,
// que tem forks e versões confusas.
// Roda no loop. Documentação: Documentação/referencia/web.md
#include "web.h"

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include "config.h"
#include "protocolo.h"

// Arquivos de src/web embutidos pelo board_build.embed_txtfiles (terminam em '\0').
// O linker cria um símbolo _binary_<caminho>_start para cada arquivo listado no platformio.ini.
extern const char index_html[] asm("_binary_src_web_index_html_start");
extern const char manifest_json[] asm("_binary_src_web_manifest_json_start");
extern const char icon_svg[] asm("_binary_src_web_icon_svg_start");

static WebServer http(HTTP_PORTA);  // serve a página (porta 80)
static WebSocketsServer ws(WS_PORTA);  // dados e comandos do app (porta 81)
static bool ativo = false;  // AP no ar e servidores iniciados
static char ip[16] = "-";   // IP do AP em texto (para a serial e o redirecionamento)
static char buf[640];       // mensagem de saída (dados, config, evento); só o loop usa

// Config para todos (depois de set/reset/calibração).
void web_enviarConfig() {
  if (!ativo) return;
  size_t n = protocolo_config(buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

// Aviso para todos os clientes.
void web_evento(const char* nivel, const char* msg) {
  if (!ativo) return;
  size_t n = protocolo_evento(nivel, msg, buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

// Dados a 10 Hz. Sem cliente conectado, nem monta o JSON (economiza CPU).
void web_enviarDados(const Status& s) {
  if (!ativo || ws.connectedClients() == 0) return;
  size_t n = protocolo_dados(s, millis(), buf, sizeof(buf));
  ws.broadcastTXT(buf, n);
}

// Callback da biblioteca para cada evento do WebSocket (conexão, texto recebido, queda...).
// Texto recebido = comando do app (protocolo_comando). Outros tipos são ignorados.
static void aoEventoWs(uint8_t cliente, WStype_t tipo, uint8_t* payload, size_t len) {
  // Ao conectar, ou se o comando pedir ("get"), manda o config só para quem pediu.
  if (tipo == WStype_CONNECTED || (tipo == WStype_TEXT && protocolo_comando((const char*)payload, len))) {
    // Buffer local na pilha (o config cabe com folga em 384 bytes).
    char cfg[384];
    size_t n = protocolo_config(cfg, sizeof(cfg));
    ws.sendTXT(cliente, cfg, n);
  }
}

// Responde um arquivo embutido. "no-cache": depois de gravar um firmware novo, o celular não
// fica com a página antiga.
static void servir(const char* tipo, const char* conteudo) {
  http.sendHeader("Cache-Control", "no-cache");
  http.send_P(200, tipo, conteudo);
}

// Sobe o Access Point e os dois servidores. Chamada uma vez, no setup.
void web_iniciar() {
  WiFi.persistent(false);  // não grava a config do Wi-Fi na flash a cada boot
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_SENHA)) {
    Serial.println(F("[wifi] falha ao criar o Access Point"));
    return;
  }
  // Menos potência = menos pico de corrente (ajuda a não resetar com o motor) e
  // alcance de sobra para uma sala.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setSleep(false);  // sem economia de energia no rádio: menos atraso nos dados do app
  strlcpy(ip, WiFi.softAPIP().toString().c_str(), sizeof(ip));

  // Rotas: a página, o manifest (para "Adicionar à tela inicial") e o ícone.
  http.on("/", HTTP_GET, [] { servir("text/html; charset=utf-8", index_html); });
  http.on("/index.html", HTTP_GET, [] { servir("text/html; charset=utf-8", index_html); });
  http.on("/manifest.json", HTTP_GET, [] { servir("application/manifest+json", manifest_json); });
  http.on("/icon.svg", HTTP_GET, [] { servir("image/svg+xml", icon_svg); });
  // Qualquer outro caminho (ex.: /favicon.ico, endereço digitado errado) volta para a página.
  http.onNotFound([] {
    http.sendHeader("Location", String("http://") + ip + "/");
    http.send(302, "text/plain", "");
  });
  http.begin();

  ws.begin();
  ws.onEvent(aoEventoWs);
  // Ping a cada 5 s; sem resposta em 2 s, duas vezes seguidas, o cliente é desconectado.
  ws.enableHeartbeat(5000, 2000, 2);  // derruba clientes que sumiram (celular saiu da rede)

  ativo = true;
  Serial.printf("[wifi] AP \"%s\" senha \"%s\" -> abra http://%s\n", WIFI_SSID, WIFI_SENHA, ip);
}

// Atende os pedidos HTTP pendentes e o WebSocket (é aqui que aoEventoWs é chamado).
void web_atualizar() {
  if (!ativo) return;
  http.handleClient();
  ws.loop();
}

bool web_ativo() { return ativo; }
const char* web_ip() { return ip; }
int web_clientes() { return ativo ? ws.connectedClients() : 0; }
