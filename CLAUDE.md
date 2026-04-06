# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

IoT rural fire-prevention system (UNNE-IoT-PIR). This branch contains the ESP-IDF pure firmware for Heltec WiFi LoRa 32 V2. The `mqtt_service/` and `dashboard/` directories have been removed from this branch — the firmware is the active focus.

## Build Commands

All commands run from `firmware/`:

```bash
idf.py set-target esp32   # first time only
idf.py build
idf.py flash monitor
idf.py monitor            # serial monitor only
idf.py fullclean          # wipe build artifacts
idf.py reconfigure        # after sdkconfig or component changes
```

Monitor baud rate: 115200. Flash size: 8 MB.

## Firmware Architecture

`main/main.c` calls `app_core_start()` and nothing else.

`app_core` (`components/app_core/`) is the single orchestrator. It calls `board_init()`, then either:

- **Default mode** (no extra `#define`): initializes display + DHT11, shows boot screen, then polls DHT11 every 3 s and updates the OLED.
- **LoRa test mode** (`APP_MODE_LORA_TEST` defined): skips DHT11/OLED and calls `lora_image_test_start()`, which launches a FreeRTOS task and suspends `app_main`.

**Component responsibilities:**
- `board/` — pin definitions (all GPIO in `board_config.h`), Vext enable, I2C init, OLED reset
- `display/` — SSD1306 driver over I2C; boot screen and sensor data rendering
- `dht11/` — 1-wire protocol driver
- `sx1276/` — SX1276 LoRa radio driver over SPI2/HSPI
- `lora_image_test/` — TX/RX image test using `sx1276`; mode selected by commenting/uncommenting `LORA_IMAGE_MODE_TX` / `LORA_IMAGE_MODE_RX` in `lora_image_test.h`

**Pin assignments** (from `board_config.h`):
- OLED: SDA=4, SCL=15, RST=16, Vext=21
- DHT11: GPIO 13
- LoRa SPI: SCK=5, MOSI=27, MISO=19, NSS=18, RST=14, DIO0=26

## Switching Firmware Modes

To activate LoRa test mode, define `APP_MODE_LORA_TEST` (e.g., in `app_core.c` or via `sdkconfig`/`CMakeLists.txt`). To select TX or RX, edit `firmware/components/lora_image_test/include/lora_image_test.h` and uncomment exactly one of `LORA_IMAGE_MODE_TX` / `LORA_IMAGE_MODE_RX` before flashing.

## Coding Conventions (C / ESP-IDF)

- 4-space indentation, opening brace on same line as function/control statement.
- Include order: local header first, then C standard headers, then ESP-IDF headers.
- Function prefix per component: `board_`, `display_`, `dht11_`, `app_core_`, `sx1276_`.
- `static const char *TAG` per file for logging.
- Public APIs return `esp_err_t`; use `ESP_ERROR_CHECK` for unrecoverable failures, `ESP_RETURN_ON_ERROR` to propagate errors.
- DHT11 read failures are recoverable — log with `ESP_LOGW` and show an error screen; do not abort.

## Verification

After any firmware change: `idf.py build` from `firmware/`. Use `idf.py flash monitor` when hardware behavior matters.
