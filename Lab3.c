#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"

/* ============================================================
   PINES
   ============================================================ */
#define PIN_LM35        ADC1_CHANNEL_0
#define PIN_LDR         ADC1_CHANNEL_3
#define PIN_CALEFACTOR  GPIO_NUM_25
#define PIN_MOTOR_IN1   GPIO_NUM_26
#define PIN_MOTOR_IN2   GPIO_NUM_27
#define PIN_MOTOR_IN3   GPIO_NUM_14
#define PIN_MOTOR_IN4   GPIO_NUM_12
#define PIN_LED1        GPIO_NUM_32
#define PIN_LED2        GPIO_NUM_33

/* ============================================================
   LEDC
   ============================================================ */
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CH_LED1      LEDC_CHANNEL_0
#define LEDC_CH_LED2      LEDC_CHANNEL_1
#define LEDC_FREQ_HZ      5000
#define LEDC_RESOLUTION   LEDC_TIMER_8_BIT

/* ============================================================
   UART
   ============================================================ */
#define UART_NUM        UART_NUM_0
#define UART_BUF_SIZE   256
#define UART_BAUD       115200

/* ============================================================
   SECUENCIA HALF-STEP MOTOR
   ============================================================ */
static const uint8_t STEP_SEQ[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

/* ============================================================
   VARIABLES GLOBALES
   ============================================================ */
static volatile float g_temp_control = 25.0f;
static volatile float g_temp_actual  = 0.0f;
static volatile int   g_pct_luz      = 0;
static volatile int   g_step_freq    = 0;
static volatile int   g_motor_dir    = 1;
static volatile int   g_calefactor   = 0;

/* ============================================================
   ADC
   ============================================================ */
static void init_adc(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(PIN_LM35, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(PIN_LDR,  ADC_ATTEN_DB_11);
}

static float leer_temperatura(void) {
    uint32_t suma = 0;
    for (int i = 0; i < 4; i++) {
        suma += adc1_get_raw(PIN_LM35);
    }
    float voltaje_mv = ((suma / 4) * 3300.0f) / 4095.0f;
    return voltaje_mv / 10.0f;
}

static int leer_luz_pct(void) {
    return (int)((adc1_get_raw(PIN_LDR) * 100.0f) / 4095.0f);
}

/* ============================================================
   LEDC
   ============================================================ */
static void init_ledc(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch0 = {
        .gpio_num   = PIN_LED1,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_LED1,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch0);

    ledc_channel_config_t ch1 = {
        .gpio_num   = PIN_LED2,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_LED2,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch1);
}

static void set_leds(int pct) {
    uint32_t duty = (uint32_t)((pct / 100.0f) * 255);
    ledc_set_duty(LEDC_MODE, LEDC_CH_LED1, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH_LED1);
    ledc_set_duty(LEDC_MODE, LEDC_CH_LED2, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH_LED2);
}

/* ============================================================
   MOTOR
   ============================================================ */
static void motor_paso(int idx) {
    gpio_set_level(PIN_MOTOR_IN1, STEP_SEQ[idx][0]);
    gpio_set_level(PIN_MOTOR_IN2, STEP_SEQ[idx][1]);
    gpio_set_level(PIN_MOTOR_IN3, STEP_SEQ[idx][2]);
    gpio_set_level(PIN_MOTOR_IN4, STEP_SEQ[idx][3]);
}

static void motor_off(void) {
    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);
    gpio_set_level(PIN_MOTOR_IN3, 0);
    gpio_set_level(PIN_MOTOR_IN4, 0);
}

/* ============================================================
   TAREA MOTOR
   ============================================================ */
static void tarea_motor(void *pv) {
    int idx = 0;
    while (1) {
        int freq = g_step_freq;

        if (freq == 0) {
            motor_off();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int delay_ms;
        if (freq <= 100)      delay_ms = 10;
        else if (freq <= 300) delay_ms = 7;
        else                  delay_ms = 5;

        if (g_motor_dir == 1)
            idx = (idx + 7) % 8;
        else
            idx = (idx + 1) % 8;

        motor_paso(idx);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ============================================================
   TAREA TEMPERATURA
   ============================================================ */
static void tarea_temperatura(void *pv) {
    while (1) {
        float T  = leer_temperatura();
        float Tc = g_temp_control;
        g_temp_actual = T;

        if (T >= (Tc - 1.0f) && T <= (Tc + 1.0f)) {
            g_calefactor = 0;
            g_step_freq  = 0;
        } else if (T < (Tc - 1.0f)) {
            g_calefactor = 1;
            g_motor_dir  = 1;
            g_step_freq  = 100;
        } else if (T > (Tc + 1.0f) && T < (Tc + 3.0f)) {
            g_calefactor = 0;
            g_motor_dir  = -1;
            g_step_freq  = 100;
        } else if (T >= (Tc + 3.0f) && T <= (Tc + 5.0f)) {
            g_calefactor = 0;
            g_motor_dir  = -1;
            g_step_freq  = 300;
        } else {
            g_calefactor = 0;
            g_motor_dir  = -1;
            g_step_freq  = 600;
        }

        gpio_set_level(PIN_CALEFACTOR, g_calefactor);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ============================================================
   TAREA ILUMINACION
   ============================================================ */
static void tarea_iluminacion(void *pv) {
    while (1) {
        int ni = leer_luz_pct();
        g_pct_luz = ni;

        int brillo = 0;
        if      (ni < 20)             brillo = 100;
        else if (ni >= 20 && ni < 30) brillo = 80;
        else if (ni >= 30 && ni < 40) brillo = 60;
        else if (ni >= 40 && ni < 60) brillo = 50;
        else if (ni >= 60 && ni < 80) brillo = 30;
        else                          brillo = 0;

        set_leds(brillo);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ============================================================
   TAREA UART
   ============================================================ */
static void tarea_uart(void *pv) {
    uint8_t buf[UART_BUF_SIZE];
    while (1) {
        char msg[128];
        snprintf(msg, sizeof(msg),
            "Tc=%.1f | T=%.1f | Luz=%d%%\r\n",
            g_temp_control, g_temp_actual, g_pct_luz);
        uart_write_bytes(UART_NUM, msg, strlen(msg));

        int len = uart_read_bytes(UART_NUM, buf,
                                  sizeof(buf) - 1,
                                  pdMS_TO_TICKS(100));
        if (len > 0) {
            buf[len] = '\0';
            char *ptr = strstr((char *)buf, "TC:");
            if (ptr != NULL) {
                float nuevo = atof(ptr + 3);
                if (nuevo > 0 && nuevo < 100) {
                    g_temp_control = nuevo;
                    char ack[64];
                    snprintf(ack, sizeof(ack),
                        ">>> Tc = %.1f C OK\r\n",
                        g_temp_control);
                    uart_write_bytes(UART_NUM, ack, strlen(ack));
                } else {
                    uart_write_bytes(UART_NUM,
                        ">>> ERROR: valor entre 1 y 99\r\n", 31);
                }
            } else {
                uart_write_bytes(UART_NUM,
                    ">>> ERROR: usa TC:XX ejemplo TC:25\r\n", 36);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ============================================================
   APP MAIN
   ============================================================ */
void app_main(void) {
    esp_task_wdt_deinit();  // Deshabilita el watchdog

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_CALEFACTOR) |
                        (1ULL << PIN_MOTOR_IN1)  |
                        (1ULL << PIN_MOTOR_IN2)  |
                        (1ULL << PIN_MOTOR_IN3)  |
                        (1ULL << PIN_MOTOR_IN4),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);

    init_adc();
    init_ledc();

    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &uart_cfg);
    uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    xTaskCreatePinnedToCore(tarea_motor, "motor", 4096, NULL, 5, NULL, 1);
    xTaskCreate(tarea_temperatura, "temp",  4096, NULL, 4, NULL);
    xTaskCreate(tarea_iluminacion, "luz",   4096, NULL, 3, NULL);
    xTaskCreate(tarea_uart,        "uart",  8192, NULL, 2, NULL);
}
