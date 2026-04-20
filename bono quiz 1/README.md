# Bono Quiz 1 — Control de Motor DC con Puente H en ESP32

Control bidireccional de un motor DC usando un puente H con MOSFET (IRF9630 + IRLZ44N), PWM por LEDC, potenciometro ADC y display de 7 segmentos de 3 digitos.

## Que hace

El potenciometro controla la velocidad del motor de 0 a 100%. Los botones cambian la direccion de giro. El display muestra el porcentaje de potencia en tiempo real. Un LED verde indica giro a la derecha y un LED rojo indica giro a la izquierda. Al cambiar de direccion el motor frena 300ms antes de invertir para proteger el circuito.

## Pines

Boton derecha: GPIO 15
Boton izquierda: GPIO 35
LED verde: GPIO 14
LED rojo: GPIO 13
Segmentos A-G: GPIO 16, 17, 18, 19, 21, 22, 23
Digitos 0-2: GPIO 25, 33, 32
PMOS izquierdo (IRF9630): GPIO 4
NMOS izquierdo (IRLZ44N): GPIO 26
PMOS derecho (IRF9630): GPIO 5
NMOS derecho (IRLZ44N): GPIO 27
Potenciometro: GPIO 36 (ADC1 canal 6)

## Tareas FreeRTOS

tarea_display — refresca los 3 digitos por multiplexacion
tarea_adc — lee el potenciometro cada 20ms
tarea_botones — detecta flancos de bajada en los botones
tarea_motor — aplica PWM y maneja el cambio de direccion

## Compilar y flashear

Con PlatformIO:
pio run --target upload

Con ESP-IDF:
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor