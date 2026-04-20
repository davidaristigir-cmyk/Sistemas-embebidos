# Matrix Shooter — ESP32 + Matriz LED 6x6

Juego de esquivar balas en C con ESP-IDF sobre una matriz LED bicolor 6x6 controlada por multiplexacion de filas.

## Como jugar

La nave verde de 2 pixeles esta en la columna izquierda. Balas rojas vienen desde la derecha. Esquivalas con los botones de subir y bajar. Tienes 2 vidas. Cada 3 esquives el juego sube de nivel: las balas se vuelven mas rapidas y mas grandes. Al perder todas las vidas aparece Game Over. Presiona START para reiniciar.

## Pines

Filas: GPIO 16, 17, 18, 19, 21, 22
Columna verde (nave): GPIO 33
Columnas rojas (balas): GPIO 23, 25, 26, 27, 32
Boton subir: GPIO 14
Boton bajar: GPIO 5
Boton start: GPIO 13

## Compilar y flashear

Con PlatformIO:
pio run --target upload

Con ESP-IDF:
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

## Archivos

main/main.c — codigo fuente del juego