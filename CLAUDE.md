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

## LoRa Image Test — Protocol

`lora_image_test` implements a chunked TX/RX protocol with ACK and CRC-8:

- **TX packet header (6 bytes):** `[session_id | chunk_idx | total_chunks | payload_len | reserved | crc8]`
- **ACK packet (3 bytes):** `[0xAC | session_id | chunk_idx]`
- **CRC-8:** polynomial 0x07 (Dallas/Maxim) over the payload.
- `session_id` increments per image; lets the RX discard stale chunks from previous sessions.
- Key constants: `ACK_TIMEOUT_MS=2000`, `ACK_TX_DELAY_MS=60`, `MAX_RETRIES=5`.
- If CRC fails, RX does **not** send ACK — silence triggers a TX retry.
- Duplicate chunk: RX re-sends ACK without re-copying data.

**Init order in `lora_image_test_start()` — critical:**
```c
sx1276_init();          // radio first — its hardware reset pulses GPIO 14 low,
                        // which glitches Vext and would kill the OLED if it were
                        // already initialized
vTaskDelay(50 ms);      // wait for Vext to stabilize after reset
display_init();         // display after, with stable Vext
display_show_boot();
vTaskDelay(500 ms);
```
Reversing this order causes the OLED to silently fail (SSD1306 discards I2C writes without power, no error returned).

## Gateway

`gateway/` contains configuration and tooling for the RPi4 + RAK5146 SPI gateway:

- Hardware: Raspberry Pi 4 + RAK5146 (SX1302 + 2× SX1250), Pi HAT 40-pin.
- Software: `sx1302_hal` v2.1.0 compiled from source (RAKWireless install script is incompatible with RPi OS Lite).
- Frequency plan: AU915 sub-band 2 (916.8–918.2 MHz uplink). Gateway EUI: `0016C001FF1E02BA`.
- Reset pin: **GPIO 17** (RESET_B). GPIO 18 = power enable. The Semtech reference uses GPIO 23 — wrong for RAK5146 on Pi HAT.
- GPIO control: `pinctrl` (sysfs `/sys/class/gpio/` is absent in kernel 6.12+).
- STTS751 temperature sensor is unpopulated on this RAK5146 variant; `loragw_hal.c` is patched to downgrade the fatal error to a warning and return 25 °C.
- SSH: key `gateway/.ssh/rpi-gateway` (ed25519, private key in `.gitignore`). Connect with `ssh -i gateway/.ssh/rpi-gateway gabi@rpi-gateway`.
- Packet forwarder runs as systemd service `lora_pkt_fwd` (autostart enabled). Forwards to `localhost:1700`.

## Image Pipeline (planned — not yet in repo)

Future components for fire-detection image processing on the node:

- **OV7670 + AL422B FIFO** — camera capture. Data bus D0–D7: GPIOs 34,35,36,39,22,23,17,32. Control via PCF8574 I/O expander (WRST, RRST pins). Expected output: QVGA 320×240 (to be confirmed with resolution test).
- **Etapa 0 — BOX downscale** — averages 20×15 source blocks → 16×16 output. Uses fixed reciprocal (`×14318 >> 22` for /300) instead of division. Accumulators: `uint32_t acc_r[16]`, `acc_g[16]` (channel B unused).
- **Etapa 1 — R−G index** — computes `max(R−G, 0)` per pixel. Buffer: `static int16_t s_idx[16][16]` — must be `int16_t`, not `uint8_t`, because the DWT step produces sums up to 680 which overflow uint8. Fire detection threshold: R−G > 50. Fused into the Etapa 0 loop (no separate pass).
- **Etapa 2 — compression** — two options under evaluation: DWT Haar 2D (transmits LL+LH+HL subbands, 192 bytes, PSNR ~38 dB) or 4-bit quantization (128 bytes, 16 levels). Decision pending hardware validation of Etapa 1. Both fit in one LoRa packet at SF10/125 kHz.

## Coding Conventions (C / ESP-IDF)

- 4-space indentation, opening brace on same line as function/control statement.
- Include order: local header first, then C standard headers, then ESP-IDF headers.
- Function prefix per component: `board_`, `display_`, `dht11_`, `app_core_`, `sx1276_`.
- `static const char *TAG` per file for logging.
- Public APIs return `esp_err_t`; use `ESP_ERROR_CHECK` for unrecoverable failures, `ESP_RETURN_ON_ERROR` to propagate errors.
- DHT11 read failures are recoverable — log with `ESP_LOGW` and show an error screen; do not abort.

## Verification

After any firmware change: `idf.py build` from `firmware/`. Use `idf.py flash monitor` when hardware behavior matters.
