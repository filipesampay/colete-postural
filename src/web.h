// Wi-Fi em modo Access Point + página do app (HTTP :80) + WebSocket (:81).
#pragma once

#include <stdint.h>

#include "app.h"

void web_iniciar();
void web_atualizar();

// {"t":"data",...} para todos os clientes (10 Hz)
void web_enviarDados(const Status& s);
// {"t":"config",...} para todos os clientes
void web_enviarConfig();
// {"t":"evt","nivel":...,"msg":...} para aparecer como aviso no app
void web_evento(const char* nivel, const char* msg);

bool web_ativo();
const char* web_ip();
int web_clientes();
