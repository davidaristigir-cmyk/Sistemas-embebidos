#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Tamaño matriz
#define ANCHO_MATRIZ 6
#define ALTO_MATRIZ 6

// Niveles
#define FILA_ACTIVA   1
#define FILA_INACTIVA 0

#define COL_ACTIVA   0
#define COL_INACTIVA 1

// Colores
#define APAGADO 0
#define ROJO    1
#define VERDE   2

// Tiempos
#define REFRESCO_FILA_MS   1
#define REFRESCO_FRAME_MS  4
#define TIEMPO_JUEGO_MS    300
#define DEBOUNCE_BTN_MS   120
#define DEBOUNCE_START_MS 200

#define VIDAS_INICIALES 2

// Dificultad
static int esquives = 0;
static int nivel = 1;
static int velocidad_bala = 300;
static int tam_bala = 1;

// Pines filas
#define FILA_1 16
#define FILA_2 17
#define FILA_3 18
#define FILA_4 19
#define FILA_5 21
#define FILA_6 22

// Columnas rojas
#define COL_R_2 23
#define COL_R_3 25
#define COL_R_4 26
#define COL_R_5 27
#define COL_R_6 32

// Columna verde
#define COL_G_1 33

// Botones
#define BTN_SUBIR  14
#define BTN_BAJAR  5
#define BTN_START  13

static const int pines_filas[ALTO_MATRIZ] = {
    FILA_1, FILA_2, FILA_3, FILA_4, FILA_5, FILA_6
};

static const int pines_columnas_rojas[5] = {
    COL_R_2, COL_R_3, COL_R_4, COL_R_5, COL_R_6
};

static volatile uint8_t matriz[ALTO_MATRIZ][ANCHO_MATRIZ];

static volatile int nave_y = 2;
static volatile int bala_x = 5;
static volatile int bala_y = 3;

static volatile int vidas = VIDAS_INICIALES;
static volatile bool juego_iniciado = false;
static volatile bool juego_terminado = false;

void limpiar_matriz(void) {
    for (int y = 0; y < ALTO_MATRIZ; y++)
        for (int x = 0; x < ANCHO_MATRIZ; x++)
            matriz[y][x] = APAGADO;
}

void poner_pixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < ANCHO_MATRIZ && y >= 0 && y < ALTO_MATRIZ)
        matriz[y][x] = color;
}

void dibujar_bala(void) {
    int mitad = tam_bala / 2;
    for (int i = 0; i < tam_bala; i++) {
        int y = bala_y - mitad + i;
        if (y >= 0 && y < ALTO_MATRIZ)
            poner_pixel(bala_x, y, ROJO);
    }
}

void apagar_matriz(void) {
    for (int i = 0; i < ALTO_MATRIZ; i++)
        gpio_set_level(pines_filas[i], FILA_INACTIVA);
    for (int i = 0; i < 5; i++)
        gpio_set_level(pines_columnas_rojas[i], COL_INACTIVA);
    gpio_set_level(COL_G_1, COL_INACTIVA);
}

void refrescar_matriz_una_vez(void) {
    for (int fila = 0; fila < ALTO_MATRIZ; fila++) {
        apagar_matriz();
        for (int col = 0; col < ANCHO_MATRIZ; col++) {
            if (matriz[fila][col] == VERDE && col == 0)
                gpio_set_level(COL_G_1, COL_ACTIVA);
            if (matriz[fila][col] == ROJO && col >= 1)
                gpio_set_level(pines_columnas_rojas[col - 1], COL_ACTIVA);
        }
        gpio_set_level(pines_filas[fila], FILA_ACTIVA);
        vTaskDelay(pdMS_TO_TICKS(REFRESCO_FILA_MS));
    }
    apagar_matriz();
}

void refrescar_matriz_tiempo(int ms_total) {
    int ciclos = ms_total / REFRESCO_FRAME_MS;
    if (ciclos < 1) ciclos = 1;
    for (int i = 0; i < ciclos; i++)
        refrescar_matriz_una_vez();
}

void dibujar_juego(void) {
    limpiar_matriz();
    if (!juego_terminado) {
        poner_pixel(0, nave_y, VERDE);
        if (nave_y + 1 < ALTO_MATRIZ)
            poner_pixel(0, nave_y + 1, VERDE);
        dibujar_bala();
    }
}

void pantalla_inicio(void) {
    limpiar_matriz();
    poner_pixel(0, nave_y, VERDE);
    poner_pixel(0, nave_y + 1, VERDE);
}

void pantalla_game_over(void) {
    limpiar_matriz();
    poner_pixel(1, 0, ROJO);
    poner_pixel(4, 0, ROJO);
    poner_pixel(1, 1, ROJO);
    poner_pixel(4, 1, ROJO);
    poner_pixel(1, 4, ROJO);
    poner_pixel(1, 5, ROJO);
    poner_pixel(2, 3, ROJO);
    poner_pixel(3, 3, ROJO);
    poner_pixel(4, 4, ROJO);
    poner_pixel(4, 5, ROJO);
    refrescar_matriz_tiempo(800);
}

void generar_bala(void) {
    bala_x = 5;
    bala_y = rand() % 6;
}

void reiniciar_juego(void) {
    nave_y = 2;
    vidas = VIDAS_INICIALES;
    juego_terminado = false;
    juego_iniciado = true;
    esquives = 0;
    nivel = 1;
    velocidad_bala = 300;
    tam_bala = 1;
    generar_bala();
}

void mover_nave_arriba(void) {
    if (nave_y > 0) nave_y--;
}

void mover_nave_abajo(void) {
    if (nave_y < ALTO_MATRIZ - 2) nave_y++;
}

void mover_bala(void) {
    bala_x--;
}

void verificar_colision(void) {
    if (bala_x == 1) {
        if (bala_y == nave_y || bala_y == nave_y + 1) {
            vidas--;
            if (vidas <= 0)
                juego_terminado = true;
        } else {
            esquives++;
            if (esquives % 3 == 0) {
                nivel++;
                if (tam_bala < 3) tam_bala++;
                velocidad_bala -= 50;
                if (velocidad_bala < 100) velocidad_bala = 100;
            }
        }
        generar_bala();
    }
    if (bala_x < 1)
        generar_bala();
}

int boton_presionado(int pin) {
    return gpio_get_level(pin) == 0;
}

void configurar_salidas(void) {
    uint64_t mascara =
        (1ULL << FILA_1)|(1ULL << FILA_2)|(1ULL << FILA_3)|
        (1ULL << FILA_4)|(1ULL << FILA_5)|(1ULL << FILA_6)|
        (1ULL << COL_R_2)|(1ULL << COL_R_3)|(1ULL << COL_R_4)|
        (1ULL << COL_R_5)|(1ULL << COL_R_6)|(1ULL << COL_G_1);
    gpio_config_t config = {
        .pin_bit_mask = mascara,
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&config);
    apagar_matriz();
}

void configurar_botones(void) {
    uint64_t mascara = (1ULL << BTN_SUBIR)|(1ULL << BTN_BAJAR)|(1ULL << BTN_START);
    gpio_config_t config = {
        .pin_bit_mask = mascara,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&config);
}

void app_main(void) {
    configurar_salidas();
    configurar_botones();
    limpiar_matriz();

    while (1) {
        if (!juego_iniciado) {
            pantalla_inicio();
            refrescar_matriz_tiempo(30);
            if (boton_presionado(BTN_START)) {
                reiniciar_juego();
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_START_MS));
            }
            continue;
        }
        if (juego_terminado) {
            pantalla_game_over();
            if (boton_presionado(BTN_START)) {
                reiniciar_juego();
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_START_MS));
            }
            continue;
        }
        int tiempo = 0;
        while (tiempo < velocidad_bala) {
            if (boton_presionado(BTN_SUBIR)) {
                mover_nave_arriba();
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_BTN_MS));
            }
            if (boton_presionado(BTN_BAJAR)) {
                mover_nave_abajo();
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_BTN_MS));
            }
            dibujar_juego();
            refrescar_matriz_tiempo(10);
            tiempo += 10;
        }
        mover_bala();
        verificar_colision();
    }
}
