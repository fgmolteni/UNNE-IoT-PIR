# UNNE-IoT-PIR Firmware

Firmware para `Heltec WiFi LoRa 32 V2` en `ESP-IDF puro`.

## Modos de operacion

### Modo sensor (por defecto)

Inicializa OLED y DHT11, muestra temperatura y humedad cada 3 s.

### Modo prueba LoRa (`APP_MODE_LORA_TEST`)

Inicializa el radio SX1276 y lanza `lora_image_test`. Transmite o recibe
una imagen 16×16 sobre LoRa con protocolo ACK por chunk y CRC-8.

Para activar:
1. Definir `APP_MODE_LORA_TEST` en `app_core.c` (o via `CMakeLists.txt`).
2. En `components/lora_image_test/include/lora_image_test.h` descomentar
   exactamente uno de `LORA_IMAGE_MODE_TX` / `LORA_IMAGE_MODE_RX`.
3. Compilar y flashear cada placa con el modo correcto.

## Estructura

```text
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    ├── app_core/          — orquestador principal
    ├── board/             — pines, Vext, I2C init
    ├── dht11/             — driver 1-wire DHT11
    ├── display/           — driver SSD1306 (I2C) + pantallas
    ├── sx1276/            — driver SX1276 LoRa (SPI)
    └── lora_image_test/   — prueba TX/RX imagen 16x16
```

## Pines

| Periferico | GPIO |
|------------|------|
| OLED SDA   | 4    |
| OLED SCL   | 15   |
| OLED RST   | 16   |
| Vext       | 21   |
| DHT11 DATA | 13   |
| LoRa SCK   | 5    |
| LoRa MOSI  | 27   |
| LoRa MISO  | 19   |
| LoRa NSS   | 18   |
| LoRa RST   | 14   |
| LoRa DIO0  | 26   |

## Requisitos

- ESP-IDF 5.x
- Python configurado por ESP-IDF
- Cable serie para flasheo/monitor (115200 baud, flash 8 MB)

## Comandos

```bash
idf.py set-target esp32   # primera vez
idf.py build
idf.py flash monitor
idf.py monitor            # solo monitor serie
idf.py fullclean          # limpiar artefactos
idf.py reconfigure        # tras cambios en sdkconfig o componentes
```

## VS Code

- Instalar la extension `Espressif IDF`.
- Abrir la carpeta `firmware/` como workspace.
- Ejecutar `ESP-IDF: Configure ESP-IDF extension`.
- Usar los botones de la barra inferior para build, flash y monitor.

## Salida esperada — modo sensor

```
I app_core: Heltec V2 sensor display ready
I app_core: DHT11 on GPIO 13
I app_core: temperature=24.0C humidity=58.0%
```

## Salida esperada — modo prueba LoRa (TX)

```
I lora_img_test: Modo: TRANSMISOR (TX)
I lora_img_test: TX: esperando 5s para que el RX este listo...
I lora_img_test: TX iniciando: session=1  256 bytes -> 2 chunks
I lora_img_test:   Chunk 1/2 enviado (session=1 plen=200 CRC=0x??)
I lora_img_test: TX: ACK chunk=0 session=1 RSSI=-65 dBm ✓
I lora_img_test: TX #1 completo: 2/2 chunks OK
```

## Parametros LoRa

- Frecuencia: 915 MHz (banda ISM Argentina)
- SF10 / BW 125 kHz / CR 4/7
- Sync word: 0x12 (privado, sin interferencia con TTN)
- Potencia TX: 17 dBm (PA_BOOST)
