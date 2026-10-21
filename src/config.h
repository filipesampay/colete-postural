// Pinagem, tempos e valores padrão do colete (só #define, sem lógica). Incluído por quase todo o firmware.
// Documentação: Documentação/referencia/config.md (ATENÇÃO: VALIDACAO.md cita linhas deste arquivo)
// O firmware atende as DUAS pinagens existentes ao mesmo tempo (nenhum pino conflita):
//   - montagem.pdf / GUIA.md: MPU 21/22, flex 34, motor 25, botão 27 (flex 2 reserva no 35)
//   - README do BioSpine V3 (João): MPU 21/22, flex esq. 34, flex dir. 35, FSR 32, LED 2
// O que não estiver ligado é detectado e ignorado (ver comando "diag").
#pragma once

#include <stdint.h>  // uint8_t, uint32_t
// ---------------------------------------------------------------- Pinos
// Analógicos só no ADC1 (GPIO 32-39): o ADC2 não funciona com o Wi-Fi ligado.
#define PIN_I2C_SDA 21    // MPU-6050 SDA
#define PIN_I2C_SCL 22    // MPU-6050 SCL
#define PIN_FLEX    34    // ADC1 (só entrada). 3V3 -> flex -> D34 -> 10k -> GND (leitura CAI ao dobrar)
#define PIN_FLEX2   35    // ADC1 (só entrada). 2º flex (opcional; desligado por padrão, ver settings.cpp)
#define PIN_FSR     32    // ADC1. Sensor de pressão do BioSpine V3 (opcional, só telemetria)
#define PIN_MOTOR   25    // -> 1k -> base do NPN (o GPIO não aguenta os ~60 mA do motor)
#define PIN_BUTTON  27    // para GND, INPUT_PULLUP (pressionado = LOW)
#define PIN_LED     2     // LED azul da placa: acende junto com o motor
// ---------------------------------------------------------------- MPU-6050
// Endereço I2C, escala, filtro e reconexão do sensor de inclinação (driver em mpu.cpp).
#define MPU_ADDR_A      0x68  // AD0 solto ou em GND (padrão do GY-521)
#define MPU_ADDR_B      0x69  // AD0 em 3V3
#define MPU_I2C_HZ      100000  // 100 kHz: mais tolerante a fio longo no colete
#define MPU_FILTRO_GIRO 0.98f // filtro complementar: peso do giroscópio
#define MPU_REINICIO_MS 2000  // tenta reconectar o MPU se cair
#define GIRO_PARADO_DPS 5.0f  // variação máx. do giroscópio (°/s) para considerar o sensor parado
#define GIRO_OFFSET_MAX 25.0f // offset de giroscópio plausível (°/s); acima disso é movimento
// ---------------------------------------------------------------- Sensores analógicos
// Flex 1, flex 2 e FSR: ADC de 12 bits (0..4095), média móvel e detecção de sensor presente (filters.h).
#define ADC_MIN_VALIDO   100   // abaixo disso: flex solto
#define ADC_MAX_VALIDO   4000  // acima disso: falta o resistor de 10 kΩ
#define ADC_RUIDO_MAX    300   // amplitude máxima na janela da média (pino solto oscila mais)
#define ADC_MEDIA_N      10    // média móvel (amostras)
#define FLEX_LIGA_MS     2000  // tempo válido contínuo para considerar o flex presente (vale também p/ flex 2 e FSR)
#define FLEX_DESLIGA_MS  1000  // tempo inválido contínuo para considerar o flex ausente
#define FSR_LIMIAR       200   // FSR: leitura estável acima disso por FLEX_LIGA_MS = sensor existe
// Pino analógico solto "copia" a tensão do canal lido antes (capacitor do ADC). Leitura do FSR
// parecida com a do flex 1 é tratada como esse fantasma.
#define ADC_FANTASMA_DIF 300   // diferença mínima (ADC) entre FSR e flex 1 para o FSR contar como real
// ---------------------------------------------------------------- Motor (LEDC)
// Motor vibratório por PWM (periférico LEDC do ESP32); os padrões de pulso ficam em motor.cpp, sem delay().
#define MOTOR_LEDC_CANAL 0      // canal LEDC (core 2.x; no core 3.x o canal vem do pino)
#define MOTOR_LEDC_FREQ  20000  // 20 kHz: fora da faixa audível
#define MOTOR_LEDC_BITS  8      // resolução do duty: 0..255
// Durações (ms):
#define MOTOR_ALERTA_ON_MS   300   // alerta: tempo ligado de cada pulso
#define MOTOR_ALERTA_OFF_MS  700   // alerta: pausa entre pulsos (menos incômodo que vibrar direto)
#define MOTOR_TESTE_MS       1000  // comando "motor", botão "Testar motor" do app e diag
#define MOTOR_CONFIRMA_MS    120   // pulso curto de aviso: boot, início e fim da calibração
// ---------------------------------------------------------------- Tempos (ms)
// Períodos de cada etapa da tarefa de controle e do envio do loop (agendamento por millis(), sem delay()).
#define PERIODO_MPU_MS       5     // 200 Hz
#define PERIODO_ADC_MS       10    // 100 Hz (média de 10 = janela de 100 ms)
#define PERIODO_POSTURA_MS   20    // 50 Hz (cálculo do desvio e da detecção)
#define PERIODO_ENVIO_MS     100   // 10 Hz para o app e o plotter
#define CALIBRACAO_ESPERA_MS 1500  // depois do clique: tempo para a pessoa se ajeitar
#define CALIBRACAO_MS        1000  // média de 1 s
#define CALIBRACAO_GIRO_MAX  30.0f // °/s: mexeu mais que isso durante a média = calibração recusada
#define MOTOR_ERRO_MS        600   // pulso longo: calibração recusada
#define BOTAO_DEBOUNCE_MS    50    // o botão precisa ficar estável esse tempo para contar (filtra o repique)
// ---------------------------------------------------------------- Wi-Fi
// Access Point próprio: o celular conecta direto no colete (rede sem internet). IP fixo 192.168.4.1.
#define WIFI_SSID  "BioSpine"      // nome da rede criada pelo colete
#define WIFI_SENHA "colete123"     // mínimo de 8 caracteres
#define HTTP_PORTA 80  // página do app: http://192.168.4.1
#define WS_PORTA   81  // WebSocket do app: ws://192.168.4.1:81/

// ---------------------------------------------------------------- Padrões (ajustáveis por serial/app)
// Valores de fábrica: usados no primeiro boot, no comando "reset" e no "Restaurar padrão" do app.
// Os ajustados pela serial/app ficam na NVS (settings.cpp) e valem mais que estes.
// Folgados de propósito: funcionam sem ajuste fino. Faixas aceitas em validarParams() (posture.cpp).
#define PADRAO_LIMITE_GRAUS  20.0f  // limite de desvio (°)
#define PADRAO_HISTERESE     3.0f   // histerese (° para o MPU, pontos % para o flex)
#define PADRAO_TEMPO_MS      3000   // tempo mínimo em má postura antes de vibrar (ms)
#define PADRAO_LIMITE_FLEX   15.0f  // limite da curvatura do flex (% de queda da leitura)
#define PADRAO_INTENSIDADE   100    // intensidade do motor em toda vibração (% do duty)

// Versão mostrada no boot (serial) e enviada ao app no {"t":"config"}.
#define FIRMWARE_VERSAO "1.0.0"
