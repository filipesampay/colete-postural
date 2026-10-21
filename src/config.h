// Pinagem e valores padrão do colete.
//
// O firmware atende as DUAS pinagens existentes ao mesmo tempo (nenhum pino conflita):
//   - montagem.pdf / GUIA.md: MPU 21/22, flex 34, motor 25, botão 27 (flex 2 reserva no 35)
//   - README do BioSpine V3 (João): MPU 21/22, flex esq. 34, flex dir. 35, FSR 32, LED 2
// O que não estiver ligado é detectado e ignorado (ver comando "diag").
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- Pinos
#define PIN_I2C_SDA 21    // MPU-6050 SDA
#define PIN_I2C_SCL 22    // MPU-6050 SCL
#define PIN_FLEX    34    // ADC1 (só entrada). 3V3 -> flex -> D34 -> 10k -> GND
#define PIN_FLEX2   35    // ADC1 (só entrada). 2º flex (opcional, detectado sozinho)
#define PIN_FSR     32    // ADC1. Sensor de pressão do BioSpine V3 (opcional, só telemetria)
#define PIN_MOTOR   25    // -> 1k -> base do NPN
#define PIN_BUTTON  27    // para GND, INPUT_PULLUP (pressionado = LOW)
#define PIN_LED     2     // LED azul da placa: acende junto com o motor

// ---------------------------------------------------------------- MPU-6050
#define MPU_ADDR_A      0x68  // AD0 solto ou em GND (padrão do GY-521)
#define MPU_ADDR_B      0x69  // AD0 em 3V3
#define MPU_I2C_HZ      100000  // 100 kHz: mais tolerante a fio longo no colete
#define MPU_FILTRO_GIRO 0.98f // filtro complementar: peso do giroscópio
#define MPU_REINICIO_MS 2000  // tenta reconectar o MPU se cair
#define GIRO_PARADO_DPS 5.0f  // variação máx. do giroscópio (°/s) para considerar o sensor parado
#define GIRO_OFFSET_MAX 25.0f // offset de giroscópio plausível (°/s); acima disso é movimento

// ---------------------------------------------------------------- Sensores analógicos
#define ADC_MIN_VALIDO   100   // abaixo disso: flex solto
#define ADC_MAX_VALIDO   4000  // acima disso: falta o resistor de 10 kΩ
#define ADC_RUIDO_MAX    300   // amplitude máxima na janela da média (pino solto oscila mais)
#define ADC_MEDIA_N      10    // média móvel (amostras)
#define FLEX_LIGA_MS     2000  // tempo válido contínuo para considerar o flex presente
#define FLEX_DESLIGA_MS  1000  // tempo inválido contínuo para considerar o flex ausente
#define FSR_LIMIAR       200   // FSR: leitura estável acima disso por FLEX_LIGA_MS = sensor existe
// Pino analógico solto "copia" a tensão do canal lido antes (capacitor do ADC). Leitura do FSR
// parecida com a do flex 1 é tratada como esse fantasma.
#define ADC_FANTASMA_DIF 300

// ---------------------------------------------------------------- Motor (LEDC)
#define MOTOR_LEDC_CANAL 0
#define MOTOR_LEDC_FREQ  20000  // 20 kHz: fora da faixa audível
#define MOTOR_LEDC_BITS  8

#define MOTOR_ALERTA_ON_MS   300
#define MOTOR_ALERTA_OFF_MS  700
#define MOTOR_TESTE_MS       1000
#define MOTOR_CONFIRMA_MS    120

// ---------------------------------------------------------------- Tempos (ms)
#define PERIODO_MPU_MS       5     // 200 Hz
#define PERIODO_ADC_MS       10    // 100 Hz (média de 10 = janela de 100 ms)
#define PERIODO_POSTURA_MS   20    // 50 Hz
#define PERIODO_ENVIO_MS     100   // 10 Hz para o app e o plotter
#define CALIBRACAO_ESPERA_MS 1500  // depois do clique: tempo para a pessoa se ajeitar
#define CALIBRACAO_MS        1000  // média de 1 s
#define CALIBRACAO_GIRO_MAX  30.0f // °/s: mexeu mais que isso durante a média = calibração recusada
#define MOTOR_ERRO_MS        600   // pulso longo: calibração recusada
#define BOTAO_DEBOUNCE_MS    50

// ---------------------------------------------------------------- Wi-Fi
#define WIFI_SSID  "BioSpine"
#define WIFI_SENHA "colete123"     // mínimo de 8 caracteres
#define HTTP_PORTA 80
#define WS_PORTA   81

// ---------------------------------------------------------------- Padrões (ajustáveis por serial/app)
#define PADRAO_LIMITE_GRAUS  20.0f
#define PADRAO_HISTERESE     3.0f
#define PADRAO_TEMPO_MS      3000
#define PADRAO_LIMITE_FLEX   15.0f
#define PADRAO_INTENSIDADE   100

#define FIRMWARE_VERSAO "1.0.0"
