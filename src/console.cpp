// Interpretador de comandos da serial: diag, calib, get, set, motor, plot, reset, help e os
// comandos do BioSpine V3. Também gera as saídas contínuas (plot e JSON do BioSpine).
// Roda no loop. Documentação: Documentação/referencia/console.md
#include "console.h"

#include <Arduino.h>
#include <ctype.h>

#include "config.h"
#include "motor.h"
#include "sensors.h"
#include "settings.h"
#include "web.h"

static char linha[256];          // linha sendo digitada (até o Enter)
static uint8_t tam = 0;          // caracteres já guardados em "linha"
static bool plot = false;        // "plot on": imprime os dados a 10 Hz para o Serial Plotter
static bool streamJson = false;  // START/STOP do BioSpine V3
static uint32_t ultimoJson = 0;  // millis() do último JSON contínuo (1 por segundo)

// Emojis em UTF-8 escritos byte a byte (não dependem da codificação do editor).
static const char* SIM = "\xE2\x9C\x85";   // ✅
static const char* NAO = "\xE2\x9D\x8C";   // ❌

// Usado pelos eventos da tarefa de controle (drenarEventos) e pelas próprias funções app_*.
void console_evento(const char* tag, const char* msg) { Serial.printf("[%s] %s\n", tag, msg); }

// Resposta no formato que o programa do BioSpine V3 espera ler na serial.
void console_respostaJson(bool ok, const char* msg) {
  Serial.printf("{\"success\":%s,\"message\":\"%s\"}\n", ok ? "true" : "false", msg);
}

// ------------------------------------------------------------------ saídas

// Comando "help" (também "ajuda" e "?").
static void ajuda() {
  Serial.println(F("Comandos:"));
  Serial.println(F("  diag                 testa cada componente"));
  Serial.println(F("  calib                calibra a postura correta (igual ao botão)"));
  Serial.println(F("  get                  mostra os parâmetros"));
  Serial.println(F("  set limite 20        graus (2-60)"));
  Serial.println(F("  set histerese 3      graus / pontos % (menor que os limites)"));
  Serial.println(F("  set tempo 3000       ms em má postura antes de vibrar"));
  Serial.println(F("  set flexlim 15       % de dobra do flex (2-90)"));
  Serial.println(F("  set motor 100        intensidade do motor (%)"));
  Serial.println(F("  set flex on|off|auto   set flex2 on|off|auto"));
  Serial.println(F("  motor                testa o motor (1 s)"));
  Serial.println(F("  plot on|off          saída para o Serial Plotter"));
  Serial.println(F("  reset                volta os parâmetros ao padrão (mantém a calibração)"));
  Serial.println(F("  START | STOP | STATUS | CALIBRATE   (compatível com o BioSpine V3)"));
}

// Texto da situação de um flex para o "get": desligado / ativo / não detectado /
// "detectado, recalibre" (ligado depois da calibração: ainda não tem referência).
static const char* descreverSensor(ModoSensor modo, bool detectado, bool ativo) {
  if (modo == MODO_OFF) return "desligado";
  if (ativo) return "ativo";
  if (!detectado) return "não detectado";
  return "detectado, recalibre para usar";
}

// Comando "get" (e o eco depois de "set"/"reset"): parâmetros, modo dos flex e calibração.
static void mostrarConfig() {
  Config c = app_config();
  Calibracao cal = app_calibracao();
  Status s = app_status();
  Serial.printf("limite ....... %.1f°\n", c.params.limiteGraus);
  Serial.printf("histerese .... %.1f\n", c.params.histerese);
  Serial.printf("tempo ........ %lu ms\n", (unsigned long)c.params.tempoMs);
  Serial.printf("flexlim ...... %.1f %%\n", c.params.limiteFlex);
  Serial.printf("motor ........ %u %%\n", c.params.intensidade);
  Serial.printf("flex ......... %s (%s)\n", modoNome(c.flexModo), descreverSensor(c.flexModo, s.flexDetectado, s.flexAtivo));
  Serial.printf("flex2 ........ %s (%s)\n", modoNome(c.flex2Modo),
                descreverSensor(c.flex2Modo, s.flex2Detectado, s.flex2Ativo));
  if (cal.valida) {
    Serial.printf("calibração ... salva (flex0 %.0f, flex2_0 %.0f)\n", cal.flex0, cal.flex20);
  } else {
    Serial.println(F("calibração ... pendente: aperte o botão com a postura correta"));
  }
}

// Comando "diag": testa cada componente e diz, em linguagem de montagem, o que conferir quando
// algo falha (regra: ❌ no diag = problema de fio, não de código). Pulsa o motor por 1 s.
// Lê sensores() direto, sem trava: é só diagnóstico, um valor de um passo atrás não importa.
static void diag() {
  const Leituras& L = sensores();
  Status s = app_status();

  // Varredura do barramento I2C: mostra todos os endereços que responderam.
  uint8_t achados[8];
  int n = sensores_scanI2C(achados, 8);
  Serial.print(F("I2C scan ........ "));
  if (!n) {
    Serial.printf("nada %s (SDA/SCL trocados ou VCC/GND solto?)\n", NAO);
  } else {
    for (int i = 0; i < n; i++) Serial.printf("0x%02X ", achados[i]);
    bool temMpu = false;
    for (int i = 0; i < n; i++) temMpu |= achados[i] == MPU_ADDR_A || achados[i] == MPU_ADDR_B;
    Serial.println(temMpu ? SIM : NAO);
  }

  // MPU: ângulos atuais, endereço e WHO_AM_I (identifica clones) e se o giroscópio tem offset.
  if (L.mpuOk) {
    Serial.printf("MPU6050 ......... pitch %.1f° roll %.1f° desvio %.1f° %s (0x%02X, WHO_AM_I 0x%02X%s)\n", s.pitch,
                  s.roll, s.desvio, SIM, L.mpuEndereco, L.mpuWhoAmI,
                  L.giroCalibrado ? "" : ", giro sem offset: deixe parado 2 s");
  } else {
    Serial.printf("MPU6050 ......... não responde %s\n", NAO);
  }

  // Flex 1: a leitura média já diz qual é o defeito (faixas de ADC_MIN_VALIDO/ADC_MAX_VALIDO).
  Serial.printf("Flex (GPIO34) ... %d %s (dobre o sensor e veja o valor mudar)\n", (int)L.flex.media,
                L.flex.detectado ? SIM : NAO);
  if (!L.flex.detectado) {
    if (L.flex.media > ADC_MAX_VALIDO) {
      Serial.println(F("                  ~4095: falta o resistor de 10 kΩ. O colete segue só com o MPU."));
    } else if (L.flex.media < ADC_MIN_VALIDO) {
      Serial.println(F("                  ~0: flex solto ou quebrado. O colete segue só com o MPU."));
    } else {
      Serial.println(F("                  leitura instável: mau contato? O colete segue só com o MPU."));
    }
  }
  // Flex 2: desligado por padrão (o GPIO35 solto parece um sensor); diz como ligar.
  Config c = app_config();
  if (c.flex2Modo == MODO_OFF) {
    Serial.printf("Flex 2 (GPIO35) . %d desligado (se houver 2º flex: set flex2 auto)\n", (int)L.flex2.media);
  } else {
    Serial.printf("Flex 2 (GPIO35) . %d %s\n", (int)L.flex2.media,
                  L.flex2.detectado ? "detectado ✅" : "não detectado");
  }
  // FSR: só fica "detectado" depois de ler pressão estável uma vez.
  Serial.printf("FSR (GPIO32) .... %d %s\n", (int)L.fsr.media,
                L.fsr.detectado ? "detectado ✅" : "não detectado (normal se não houver FSR; aperte-o para detectar)");
  // Botão: sempre ✅ (não há como saber se está ligado sem apertar); "apertado" parado = curto.
  Serial.printf("Botão (GPIO27) .. %s %s (aperte para testar: calibra)\n",
                digitalRead(PIN_BUTTON) == LOW ? "apertado" : "solto", SIM);
  // Motor: quem confirma é a pessoa (sentir a vibração / ver o LED azul).
  Serial.println(F("Motor (GPIO25) .. pulsando 1 s — sentiu? (o LED azul da placa acende junto)"));
  app_testarMotor();
  if (web_ativo()) {
    Serial.printf("Wi-Fi AP ........ %s @ %s %s (%d no app)\n", WIFI_SSID, web_ip(), SIM, web_clientes());
  } else {
    Serial.printf("Wi-Fi AP ........ falhou %s\n", NAO);
  }
  Serial.printf("Calibração ...... %s\n", s.calibrado ? "salva ✅" : "pendente ⚠ (aperte o botão)");
}

// Uma linha JSON no formato do BioSpine V3 (STATUS e o envio contínuo do START).
// Mapeamento: angle = desvio, flex_left = flex 1, flex_right = flex 2, pressure = FSR,
// status = GOOD/WARNING/BAD (ok/atenção/alerta, mesma ordem do enum Estado).
static void jsonBioSpine(const Status& s) {
  static const char* nomes[] = {"GOOD", "WARNING", "BAD"};
  Serial.printf(
      "{\"angle\":%.1f,\"flex_left\":%d,\"flex_right\":%d,\"pressure\":%d,\"status\":\"%s\",\"alert\":%s,"
      "\"calibrated\":%s}\n",
      s.desvio, s.flex, s.flex2, s.fsr, nomes[s.estado], s.estado == ESTADO_ALERTA ? "true" : "false",
      s.calibrado ? "true" : "false");
}

// Saídas contínuas, chamadas pelo loop a 10 Hz.
//   plot: "nome:valor,..." (formato do Serial Plotter do Arduino e do tools/grafico_serial.py).
//         "alerta" vale 5 ou 0 só para aparecer como degrau visível na mesma escala dos graus.
//   streamJson: 1 linha JSON por segundo (formato do BioSpine V3, ligado pelo START).
void console_enviarDados(const Status& s, uint32_t agora) {
  if (plot) {
    Params p = app_config().params;
    Serial.printf("desvio:%.1f,limite:%.1f,pitch:%.1f,roll:%.1f,flexPct:%.1f,limiteFlex:%.1f,alerta:%d\n", s.desvio,
                  p.limiteGraus, s.pitch, s.roll, s.flexPct, p.limiteFlex, s.estado == ESTADO_ALERTA ? 5 : 0);
  }
  if (streamJson && agora - ultimoJson >= 1000) {
    ultimoJson = agora;
    jsonBioSpine(s);
  }
}

// ------------------------------------------------------------------ set

// "set <nome> <valor>": altera um parâmetro na cópia da config e manda aplicar.
// Aqui só se converte o texto; as faixas (limite 2-60 etc.) são impostas em app_definirConfig
// por validarParams. Valor não numérico vira 0 (atof) e depois é preso na faixa.
static void comandoSet(char* nome, char* valor) {
  if (!nome || !valor) {
    Serial.println(F("uso: set <limite|histerese|tempo|flexlim|motor|flex|flex2> <valor>"));
    return;
  }
  Config c = app_config();
  float v = atof(valor);
  if (!strcmp(nome, "limite")) {
    c.params.limiteGraus = v;
  } else if (!strcmp(nome, "histerese")) {
    c.params.histerese = v;
  } else if (!strcmp(nome, "tempo")) {
    c.params.tempoMs = v < 0 ? 0 : (uint32_t)v;
  } else if (!strcmp(nome, "flexlim")) {
    c.params.limiteFlex = v;
  } else if (!strcmp(nome, "motor")) {
    c.params.intensidade = (uint8_t)constrain((int)v, 0, 100);
  } else if (!strcmp(nome, "flex") || !strcmp(nome, "flex2")) {
    ModoSensor m;
    if (!modoDeTexto(valor, &m)) {
      Serial.println(F("use on, off ou auto"));
      return;
    }
    if (!strcmp(nome, "flex")) {
      c.flexModo = m;
    } else {
      c.flex2Modo = m;
    }
  } else {
    Serial.printf("parâmetro desconhecido: %s\n", nome);
    return;
  }
  app_definirConfig(c);  // valida, salva na NVS e avisa o app
  Serial.println(F("salvo."));
  mostrarConfig();
}

// ------------------------------------------------------------------ interpretador

// Executa uma linha completa. Os comandos do BioSpine V3 são comparados antes, exatamente como
// chegam (maiúsculos); o resto é convertido para minúsculas e quebrado em até 3 palavras.
// Atenção: modifica "cmd" (tolower e strtok escrevem na própria string).
static void executar(char* cmd) {
  // Comandos do BioSpine V3 (maiúsculos, respostas JSON)
  if (!strcmp(cmd, "START")) {
    streamJson = true;
    console_respostaJson(true, "Monitoring started");
    return;
  }
  if (!strcmp(cmd, "STOP")) {
    streamJson = false;
    console_respostaJson(true, "Monitoring stopped");
    return;
  }
  if (!strcmp(cmd, "STATUS")) {
    jsonBioSpine(app_status());
    return;
  }
  if (!strcmp(cmd, "CALIBRATE")) {
    // Sucesso: a resposta JSON sai no fim da calibração (evento EVT_JSON_CALIB_OK em main.cpp).
    if (!app_calibrar(ORIGEM_SERIAL_JSON)) console_respostaJson(false, "MPU6050 not found");
    return;
  }

  // Comandos do colete: palavra + até 2 argumentos separados por espaço.
  for (char* p = cmd; *p; p++) *p = tolower(*p);
  char* palavra = strtok(cmd, " ");
  if (!palavra) return;
  char* a1 = strtok(NULL, " ");
  char* a2 = strtok(NULL, " ");

  if (!strcmp(palavra, "help") || !strcmp(palavra, "ajuda") || !strcmp(palavra, "?")) {
    ajuda();
  } else if (!strcmp(palavra, "diag")) {
    diag();
  } else if (!strcmp(palavra, "calib") || !strcmp(palavra, "calibrar")) {
    app_calibrar(ORIGEM_SERIAL);
  } else if (!strcmp(palavra, "get")) {
    mostrarConfig();
  } else if (!strcmp(palavra, "set")) {
    comandoSet(a1, a2);
  } else if (!strcmp(palavra, "motor")) {
    app_testarMotor();
    Serial.println(F("motor 1 s"));
  } else if (!strcmp(palavra, "plot")) {
    // "plot" ou "plot on" liga; só "plot off" desliga.
    plot = !(a1 && !strcmp(a1, "off"));
    if (!plot) Serial.println(F("plot desligado"));
  } else if (!strcmp(palavra, "status")) {
    jsonBioSpine(app_status());
  } else if (!strcmp(palavra, "reset")) {
    // Volta os parâmetros e os modos dos flex ao padrão. A calibração fica (outras chaves da NVS, que o reset não regrava).
    app_definirConfig(configPadrao());
    Serial.println(F("parâmetros padrão restaurados."));
    mostrarConfig();
  } else {
    Serial.printf("comando desconhecido: %s (digite help)\n", palavra);
  }
}

// Fim do setup: dica de uso e, se ainda não houver calibração, o lembrete (sem ela não vibra).
void console_iniciar() {
  Serial.println(F("Digite 'help' para os comandos ou 'diag' para testar o hardware."));
  if (!app_calibracao().valida) {
    Serial.println(F("Calibração pendente: vista o colete, fique com a postura correta e aperte o botão."));
  }
}

// Junta os caracteres recebidos em "linha" e executa quando chega \r ou \n (aceita os dois
// finais de linha; linha vazia é ignorada). Linha maior que 255 caracteres: o excesso é cortado.
void console_atualizar(uint32_t agora) {
  (void)agora;
  // Lê sem bloquear (readStringUntil travaria o loop até 1 s).
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (tam) {
        linha[tam] = 0;
        tam = 0;
        executar(linha);
      }
    } else if (tam < sizeof(linha) - 1) {
      linha[tam++] = ch;
    }
  }
}
