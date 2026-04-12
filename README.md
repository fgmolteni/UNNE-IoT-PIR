# UNNE-IoT-PIR - Sistema IoT de Prevencion de Incendios Rurales

Esta rama contiene el firmware de la placa `Heltec WiFi LoRa 32 V2` en `ESP-IDF puro`.

## Estado actual

- Estructura ESP-IDF modular con componentes independientes
- OLED SSD1306 con driver propio (I2C)
- Sensor DHT11
- Radio LoRa SX1276 con driver propio (SPI)
- Prueba de transmision de imagen 16×16 por LoRa con protocolo ACK y CRC-8

## Modos de firmware

**Modo sensor** (por defecto): inicializa OLED + DHT11, muestra temperatura y humedad en pantalla cada 3 s.

**Modo prueba LoRa** (`APP_MODE_LORA_TEST`): lanza `lora_image_test`, que transmite o recibe una imagen 16×16 por LoRa con confirmacion de chunks. Para activarlo, definir `APP_MODE_LORA_TEST` en `app_core.c` y seleccionar TX o RX en `lora_image_test.h`.

## Directorios relevantes

- `firmware/` — firmware ESP-IDF puro para la Heltec V2
- `docs/` — documentacion del proyecto y diagramas

## Compilar y flashear

```bash
cd firmware
idf.py set-target esp32   # primera vez
idf.py build
idf.py flash monitor
```

## Historial de la rama

- Migracion de PlatformIO/Arduino a ESP-IDF puro
- Incorporacion de driver SX1276 y prueba de imagen por LoRa
