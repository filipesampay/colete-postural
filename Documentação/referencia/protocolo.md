# Módulo `protocolo`

> Código: `src/protocolo.h`, `src/protocolo.cpp` · Relacionados: [07 — Protocolo e interfaces](../07-protocolo-e-interfaces.md), [web](web.md), [08 — App web](../08-app-web.md)

## Papel no sistema

Monta e interpreta o JSON trocado com o app web pelo WebSocket. Fica separado de `web.cpp` de propósito:
não depende do Wi-Fi, então o simulador no PC (`sim/sim_main.cpp`) usa **exatamente este código**, e o
`tools/sim_web.py` liga a página real (`src/web/index.html`) a ele. Assim o protocolo é testado sem placa.

A definição resumida das mensagens fica no comentário do topo de `src/protocolo.h`; as tabelas completas,
com tipos e unidades, estão em [07 — Protocolo e interfaces](../07-protocolo-e-interfaces.md).

## Dependências

- Usa: ArduinoJson 7 (`JsonDocument`, `serializeJson`, `deserializeJson`), API `app_*` ([main](main.md)),
  `estadoNome` ([posture](posture.md)), `modoNome`, `modoDeTexto`, `configPadrao` ([settings](settings.md)),
  `web_evento` ([web](web.md)), `FIRMWARE_VERSAO` ([config](config.md)).
- É usado por: `web.cpp` (ESP32) e `sim/sim_main.cpp` (PC).
- Roda em: loop.

## Funções

### `size_t protocolo_dados(const Status& s, uint32_t uptimeMs, char* out, size_t tam)`
- **O que faz:** escreve `{"t":"data",...}` em `out` com um campo por membro de `Status` (mesmos nomes)
  e `uptime`.
- **Parâmetros:** `s` — foto do colete; `uptimeMs` — `millis()` do envio; `out`/`tam` — buffer de saída.
- **Retorno:** bytes escritos.
- **Observações:** ângulos e porcentagens são arredondados para 1 casa (`r1`): menos bytes no Wi-Fi e no
  CSV que o app grava. No pior caso a mensagem tem ~360 bytes; `web.cpp` usa um buffer de 640.
- **Chamada por:** `web_enviarDados` (10 Hz) e o simulador.

### `size_t protocolo_config(char* out, size_t tam)`
- **O que faz:** escreve `{"t":"config",...}` com os parâmetros, os modos dos flex, `calibrado` e `versao`.
  Lê com `app_config()` e `app_calibracao()`.
- **Chamada por:** `web_enviarConfig` (para todos) e `aoEventoWs` (só para quem conectou ou pediu `get`).

### `size_t protocolo_evento(const char* nivel, const char* msg, char* out, size_t tam)`
- **O que faz:** escreve `{"t":"evt","nivel":...,"msg":...}`. O app mostra como aviso (toast).
- **Chamada por:** `web_evento`.

### `bool protocolo_comando(const char* payload, size_t len)`
- **O que faz:** interpreta um comando do app e executa:

  | `cmd` | Ação | Resposta |
  |---|---|---|
  | `calib` | `app_calibrar(ORIGEM_APP)` | eventos da calibração, depois config |
  | `motor_test` | `app_testarMotor()` | — |
  | `get` | — | retorna `true`: `web.cpp` manda o config só para quem pediu |
  | `set` | `aplicarSet` | config para todos + evento "Parâmetros salvos" |
  | `reset` | `app_definirConfig(configPadrao())` | config para todos + evento "Parâmetros padrão restaurados" |
  | outro / sem `cmd` | — | evento de erro "comando desconhecido" |
  | JSON inválido | — | evento de erro "comando inválido" |

- **Retorno:** `true` só para `get`.
- **Chamada por:** `aoEventoWs` (texto recebido no WebSocket) e o simulador (`!ws {...}`).

### `static void aplicarSet(JsonDocument& d)`
- **O que faz:** parte da config atual e troca só os campos presentes **com o tipo certo**
  (`limiteGraus`, `histerese`, `tempoMs`, `limiteFlex`, `intensidade` numéricos; `flexModo`, `flex2Modo`
  texto). O app pode mandar só o que mudou. Depois chama `app_definirConfig`, que prende os valores nas
  faixas, salva na NVS e envia o config para todos.
- **Observações:** `is<float>()` aceita inteiros também. `tempoMs` negativo vira 0; `intensidade` é presa
  em 0–100. Texto inválido em `flexModo`/`flex2Modo` é ignorado sem aviso de erro.

### `static float r1(float v)`
- **O que faz:** arredonda para 1 casa decimal.

## Armadilhas e cuidados

- **Mudou um campo?** Atualize juntos: este arquivo, o comentário do topo de `protocolo.h`,
  `src/web/index.html` (quem lê), `tools/mock_server.py` (imitação em Python) e
  [07](../07-protocolo-e-interfaces.md).
- **Tamanho:** se `{"t":"data"}` crescer muito, confira o `buf[640]` de `web.cpp` e o `wsBuf[640]` de
  `sim/sim_main.cpp`; o config (~180 bytes no pior caso) usa um buffer de 384 bytes em `aoEventoWs`.
- **"Parâmetros salvos" sai mesmo se nada mudou** (por exemplo, só um `flexModo` com texto inválido).
- `protocolo_config` liga uma referência `const Config&` ao valor temporário devolvido por `app_config()`;
  é válido em C++ (a vida do temporário é estendida), mas parece um erro à primeira vista.
