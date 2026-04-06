# UNNE-IoT-PIR Firmware

Firmware minimo para `Heltec WiFi LoRa 32 V2` migrado a `ESP-IDF puro` y enfocado en sensado con pantalla OLED.

## Objetivo actual

Esta primera iteracion deja listo el firmware base para trabajar sin Arduino ni PlatformIO.

- OLED SSD1306 integrada en la Heltec V2
- Sensor DHT11
- Estructura modular con componentes ESP-IDF
- Logs por puerto serie con `ESP_LOGI`

Esta version no incluye integracion LoRa ni TTN.

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

## VS Code

- Instalar la extension `Espressif IDF`.
- Abrir directamente la carpeta `firmware/` como workspace en VS Code.
- Ejecutar `ESP-IDF: Configure ESP-IDF extension` y dejar que la extension configure `IDF_PATH`, Python y herramientas.
- Seleccionar el target `esp32` cuando la extension lo pida.
- Usar `ESP-IDF: Build your project`, `Flash your project` y `Monitor your device` o la barra inferior de la extension.

## Salida esperada

```text
I app_core: Heltec V2 sensor display ready
I app_core: DHT11 on GPIO 13
I app_core: temperature=24.0C humidity=58.0%
```

La OLED muestra:

- pantalla de arranque `BOOTING / FACENA-UNNE`
- luego temperatura y humedad

## Notas

- Este firmware reemplaza la base Arduino/PlatformIO previa.
- El pin del DHT11 se dejo en `GPIO 13` para evitar conflicto con el bus I2C de la OLED.
- El firmware actual queda limitado a OLED + DHT11.
