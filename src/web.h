// Wi-Fi em modo Access Point + página do app (HTTP :80) + WebSocket (:81).
//
// Tudo roda no loop (as bibliotecas WebServer e WebSocketsServer são atendidas por
// web_atualizar). A tarefa de controle nunca chama web_*: um envio pode travar por segundos.
// No simulador (sim/sim_main.cpp) estas funções são trocadas por versões que imprimem no terminal.
// Documentação: Documentação/referencia/web.md e Documentação/07-protocolo-e-interfaces.md
#pragma once

#include <stdint.h>

#include "app.h"

// Cria o AP "BioSpine", registra as rotas HTTP e abre o WebSocket. Se o AP falhar, o colete
// segue funcionando sem app (web_ativo() = false e todas as funções web_* viram nada).
void web_iniciar();
// Atende HTTP e WebSocket (chamar a cada volta do loop).
void web_atualizar();

// {"t":"data",...} para todos os clientes (10 Hz)
void web_enviarDados(const Status& s);
// {"t":"config",...} para todos os clientes
void web_enviarConfig();
// {"t":"evt","nivel":...,"msg":...} para aparecer como aviso no app
void web_evento(const char* nivel, const char* msg);

// true se o Access Point subiu.
bool web_ativo();
// IP do Access Point ("192.168.4.1"), ou "-" se não subiu.
const char* web_ip();
// Quantos apps estão conectados no WebSocket agora.
int web_clientes();
