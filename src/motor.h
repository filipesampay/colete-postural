// Motor vibratório (PWM via LEDC no GPIO25) + LED da placa (GPIO2) acendendo junto.
// Tudo sem delay(): chamar motor_atualizar() a cada passo da tarefa de controle.
//
// Papel no sistema: única saída do colete. Dois usos que convivem:
//   alerta    - pulsos 300 ms ligado / 700 ms desligado enquanto a postura estiver em alerta
//   sequência - pulsos curtos de aviso (boot, calibração começou/concluiu/recusada, teste)
// A sequência tem prioridade: se começa durante o alerta, o alerta espera ela terminar.
// Roda em: tarefa de controle (e no setup, antes dela existir). Nunca chamar do loop: o loop
// pede o teste do motor por app_testarMotor (main.cpp).
//
// Documentação: Documentação/referencia/motor.md e Documentação/06-motor-e-nvs.md
#pragma once

#include <stdint.h>

// Configura o PWM (LEDC, 20 kHz, 8 bits) e o LED, com o motor desligado.
void motor_iniciar();
// Avança as fases da sequência e do alerta. agoraMs = millis(). Chamar a cada passo.
void motor_atualizar(uint32_t agoraMs);

// Alerta: pulsos 300 ms ligado / 700 ms desligado enquanto estiver ativo.
// Pode ser chamado a cada passo com o mesmo valor: só age quando o valor muda.
void motor_alerta(bool ativo);
// Sequência curta (confirmação, teste). Tem prioridade sobre o alerta.
// quantidade pulsos de ligadoMs ligado + desligadoMs desligado. Começa ligado na hora e
// substitui uma sequência que estiver em andamento.
void motor_pulsos(uint8_t quantidade, uint16_t ligadoMs, uint16_t desligadoMs);
// Potência do motor em % (0–100) = duty do PWM. Se estiver ligado, aplica na hora.
void motor_intensidade(uint8_t pct);

// true se o motor está ligado neste instante (vai para o app no campo "motor").
bool motor_ligado();
