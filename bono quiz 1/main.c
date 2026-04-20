#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"

// ---- PINES ----
#define BTN_DERECHA     15
#define BTN_IZQUIERDA   35
#define LED_VERDE       14
#define LED_ROJO        13

#define SEG_A   16
#define SEG_B   17
#define SEG_C   18
#define SEG_D   19
#define SEG_E   21
#define SEG_F   22
#define SEG_G   23

#define DIG_0   25
#define DIG_1   33
#define DIG_2   32

#define PMOS_IZQ    4
#define NMOS_IZQ    26
#define PMOS_DER    5
#define NMOS_DER    27

#define POTENCIOMETRO   ADC1_CHANNEL_6

// ---- PWM ----
#define PWM_FREQ        500
#define PWM_RES         LEDC_TIMER_8_BIT
#define PWM_MODO        LEDC_HIGH_SPEED_MODE
#define PWM_TIMER       LEDC_TIMER_0
#define CANAL_IZQ       LEDC_CHANNEL_0
#define CANAL_DER       LEDC_CHANNEL_1
#define PWM_MAX         255

// ---- TIEMPOS ----
#define MS_FRENADO      300
#define MS_DISPLAY      2
#define MS_BOTON        20
#define MS_MOTOR        10

typedef enum { DERECHA = 0, IZQUIERDA } direccion_t;

// PMOS: 1=ON  NMOS: 0=ON
#define NMOS_ON  0
#define NMOS_OFF 1

// ---- VARIABLES ----
static volatile int porcentaje = 0;
static volatile int pwm_actual = 0;
static volatile direccion_t dir_actual    = DERECHA;
static volatile direccion_t dir_pedida    = DERECHA;
static volatile bool cambio_pendiente     = false;

// ---- DISPLAY (anodo comun: 0=encendido) ----
static const uint8_t tabla[10][7] = {
    {0,0,0,0,0,0,1}, // 0
    {1,0,0,1,1,1,1}, // 1
    {0,0,1,0,0,1,0}, // 2
    {0,0,0,0,1,1,0}, // 3
    {1,0,0,1,1,0,0}, // 4
    {0,1,0,0,1,0,0}, // 5
    {0,1,0,0,0,0,0}, // 6
    {0,0,0,1,1,1,1}, // 7
    {0,0,0,0,0,0,0}, // 8
    {0,0,0,0,1,0,0}  // 9
};

// ---- INIT GPIO ----
static void init_gpio(void)
{
    gpio_config_t salidas = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL<<LED_VERDE)|(1ULL<<LED_ROJO)|
            (1ULL<<SEG_A)|(1ULL<<SEG_B)|(1ULL<<SEG_C)|(1ULL<<SEG_D)|
            (1ULL<<SEG_E)|(1ULL<<SEG_F)|(1ULL<<SEG_G)|
            (1ULL<<DIG_0)|(1ULL<<DIG_1)|(1ULL<<DIG_2)|
            (1ULL<<NMOS_IZQ)|(1ULL<<NMOS_DER)
    };
    gpio_config(&salidas);

    gpio_config_t entradas = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<BTN_DERECHA)|(1ULL<<BTN_IZQUIERDA)
    };
    gpio_config(&entradas);
}

// ---- INIT ADC ----
static void init_adc(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(POTENCIOMETRO, ADC_ATTEN_DB_11);
}

// ---- INIT PWM ----
static void init_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = PWM_MODO,
        .timer_num       = PWM_TIMER,
        .duty_resolution = PWM_RES,
        .freq_hz         = PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = PWM_MODO,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .gpio_num   = PMOS_IZQ,
        .channel    = CANAL_IZQ
    };
    ledc_channel_config(&ch);
    ch.gpio_num = PMOS_DER;
    ch.channel  = CANAL_DER;
    ledc_channel_config(&ch);
}

// ---- DISPLAY ----
static void apagar_digitos(void)
{
    gpio_set_level(DIG_0, 1);
    gpio_set_level(DIG_1, 1);
    gpio_set_level(DIG_2, 1);
}

static void mostrar_digito(int pos, int numero)
{
    apagar_digitos();
    gpio_set_level(SEG_A, tabla[numero][0]);
    gpio_set_level(SEG_B, tabla[numero][1]);
    gpio_set_level(SEG_C, tabla[numero][2]);
    gpio_set_level(SEG_D, tabla[numero][3]);
    gpio_set_level(SEG_E, tabla[numero][4]);
    gpio_set_level(SEG_F, tabla[numero][5]);
    gpio_set_level(SEG_G, tabla[numero][6]);
    if (pos == 0) gpio_set_level(DIG_0, 0);
    if (pos == 1) gpio_set_level(DIG_1, 0);
    if (pos == 2) gpio_set_level(DIG_2, 0);
}

static void tarea_display(void *arg)
{
    int pos = 0;
    while (1) {
        int c = porcentaje / 100;
        int d = (porcentaje / 10) % 10;
        int u = porcentaje % 10;
        if (pos == 0) mostrar_digito(0, c);
        if (pos == 1) mostrar_digito(1, d);
        if (pos == 2) mostrar_digito(2, u);
        if (++pos > 2) pos = 0;
        vTaskDelay(pdMS_TO_TICKS(MS_DISPLAY));
    }
}

// ---- LEDS ----
static void actualizar_leds(direccion_t dir)
{
    gpio_set_level(LED_VERDE, dir == DERECHA ? 1 : 0);
    gpio_set_level(LED_ROJO,  dir == DERECHA ? 0 : 1);
}

// ---- MOTOR ----
static void motor_apagado(void)
{
    ledc_set_duty(PWM_MODO, CANAL_IZQ, 0); ledc_update_duty(PWM_MODO, CANAL_IZQ);
    ledc_set_duty(PWM_MODO, CANAL_DER, 0); ledc_update_duty(PWM_MODO, CANAL_DER);
    gpio_set_level(NMOS_IZQ, NMOS_OFF);
    gpio_set_level(NMOS_DER, NMOS_OFF);
}

static void girar_derecha(int pwm)
{
    motor_apagado();
    gpio_set_level(NMOS_DER, NMOS_ON);
    ledc_set_duty(PWM_MODO, CANAL_IZQ, pwm);
    ledc_update_duty(PWM_MODO, CANAL_IZQ);
}

static void girar_izquierda(int pwm)
{
    motor_apagado();
    gpio_set_level(NMOS_IZQ, NMOS_ON);
    ledc_set_duty(PWM_MODO, CANAL_DER, pwm);
    ledc_update_duty(PWM_MODO, CANAL_DER);
}

static void tarea_motor(void *arg)
{
    while (1) {
        if (cambio_pendiente) {
            motor_apagado();
            vTaskDelay(pdMS_TO_TICKS(MS_FRENADO));
            dir_actual = dir_pedida;
            actualizar_leds(dir_actual);
            cambio_pendiente = false;
        }
        if (pwm_actual <= 0) {
            motor_apagado();
        } else {
            if (dir_actual == DERECHA) girar_derecha(pwm_actual);
            else                       girar_izquierda(pwm_actual);
        }
        vTaskDelay(pdMS_TO_TICKS(MS_MOTOR));
    }
}

// ---- ADC ----
static void tarea_adc(void *arg)
{
    while (1) {
        int raw = adc1_get_raw(POTENCIOMETRO);
        if (raw < 0)    raw = 0;
        if (raw > 4095) raw = 4095;
        porcentaje = (raw * 100) / 4095;
        pwm_actual = (porcentaje * PWM_MAX) / 100;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---- BOTONES ----
static void tarea_botones(void *arg)
{
    bool ant_der = true, ant_izq = true;
    while (1) {
        bool der = gpio_get_level(BTN_DERECHA);
        bool izq = gpio_get_level(BTN_IZQUIERDA);
        if (ant_der && !der) {
            dir_pedida = DERECHA;
            if (dir_pedida != dir_actual) cambio_pendiente = true;
        }
        if (ant_izq && !izq) {
            dir_pedida = IZQUIERDA;
            if (dir_pedida != dir_actual) cambio_pendiente = true;
        }
        ant_der = der;
        ant_izq = izq;
        vTaskDelay(pdMS_TO_TICKS(MS_BOTON));
    }
}

// ---- MAIN ----
void app_main(void)
{
    init_gpio();
    init_adc();
    init_pwm();
    dir_actual = dir_pedida = DERECHA;
    cambio_pendiente = false;
    actualizar_leds(dir_actual);
    motor_apagado();
    apagar_digitos();

    xTaskCreate(tarea_display, "display", 2048, NULL, 1, NULL);
    xTaskCreate(tarea_adc,     "adc",     2048, NULL, 1, NULL);
    xTaskCreate(tarea_botones, "botones", 2048, NULL, 1, NULL);
    xTaskCreate(tarea_motor,   "motor",   2048, NULL, 1, NULL);
}