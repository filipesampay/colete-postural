// Motor vibratório (PWM via LEDC no GPIO25) + LED da placa (GPIO2) acendendo junto.
// Tudo sem delay(): chamar motor_atualizar() a cada loop.
#pragma once

#include <stdint.h>

void motor_iniciar();
void motor_atualizar(uint32_t agoraMs);

// Alerta: pulsos 300 ms ligado / 700 ms desligado enquanto estiver ativo.
void motor_alerta(bool ativo);
// Sequência curta (confirmação, teste). Tem prioridade sobre o alerta.
void motor_pulsos(uint8_t quantidade, uint16_t ligadoMs, uint16_t desligadoMs);
void motor_intensidade(uint8_t pct);

bool motor_ligado();
