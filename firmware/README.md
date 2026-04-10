# UNNE-IoT-PIR Firmware

Firmware minimo para `Heltec WiFi LoRa 32 V2` migrado a `ESP-IDF puro`.

## Objetivo actual

Esta primera iteracion deja listo el firmware base para trabajar sin Arduino ni PlatformIO.

- OLED SSD1306 integrada en la Heltec V2
- Sensor DHT11
- Estructura modular con componentes ESP-IDF
- Logs por puerto serie con `ESP_LOGI`

TTN y LoRaWAN quedan para la segunda etapa de la migracion.

## Estructura

```text
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    ├── app_core/
    ├── board/
    ├── dht11/
    └── display/
```

## Pines usados

- OLED SDA: GPIO 4
- OLED SCL: GPIO 15
- OLED RST: GPIO 16
- Vext: GPIO 21
- DHT11 DATA: GPIO 13

## Requisitos

- ESP-IDF 5.x
- Python configurado por ESP-IDF
- Cable serie para flasheo/monitor

## Compilar

```bash
cd firmware
idf.py set-target esp32
idf.py build
```

## Flashear y monitorear

```bash
cd firmware
idf.py flash monitor
```

## Salida esperada

```text
I app_core: Heltec WiFi LoRa 32 V2 ready
I app_core: DHT11 on GPIO 13
I app_core: temperature=24.0C humidity=58.0%
```

La OLED muestra:

- pantalla de arranque `BOOTING / FACENA-UNNE`
- luego temperatura y humedad

## Notas

- Este firmware reemplaza la base Arduino/PlatformIO previa.
- El pin del DHT11 se dejo en `GPIO 13` para evitar conflicto con el bus I2C de la OLED.
- La siguiente fase agregara TTN sobre esta base ESP-IDF.
