/**
 * UNNE-IoT-PIR — Prueba TX imagen 16×16 por LoRa
 * Hardware : Heltec WiFi LoRa 32 V2
 * Framework: ESP-IDF + RadioLib (via Arduino-ESP32 component)
 *
 * Envía una imagen de 16×16 píxeles en escala de grises
 * como 2 paquetes LoRa con header de 4 bytes cada uno.
 *
 * Header por paquete:
 *   byte 0: chunk_idx       (índice del chunk, base 0)
 *   byte 1: total_chunks    (cantidad total de chunks)
 *   byte 2: payload_len_hi  (MSB del largo del payload de este chunk)
 *   byte 3: payload_len_lo  (LSB del largo del payload de este chunk)
 *   bytes 4..N: datos de imagen
 *
 * Parámetros LoRa:
 *   Frecuencia : 915.0 MHz  (banda ISM Argentina)
 *   BW         : 125 kHz
 *   SF         : 10
 *   CR         : 4/7
 *   Sync word  : 0x12       (privado, evita interferencia con TTN 0x34)
 *   Potencia   : 17 dBm
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* RadioLib se usa desde el wrapper Arduino-ESP32.
   Si usás ESP-IDF puro, reemplazá por el driver SX1276 directo. */
#include <RadioLib.h>

static const char *TAG = "LORA_TX";

/* ── Pines Heltec V2 ── */
#define LORA_NSS    18
#define LORA_DIO0   26
#define LORA_RESET  14
#define LORA_DIO1   35

/* ── Config LoRa ── */
#define LORA_FREQ   915.0f
#define LORA_BW     125.0f
#define LORA_SF     10
#define LORA_CR     7          /* 4/7 */
#define LORA_SYNC   0x12
#define LORA_PWR    17

/* ── Imagen ── */
#define IMG_W       16
#define IMG_H       16
#define IMG_SIZE    (IMG_W * IMG_H)   /* 256 bytes */

/*
 * CHUNK_SIZE: payload útil por paquete.
 * 251 = 255 (límite SX1276) - 4 (header).
 * Usamos 200 como valor conservador para mayor margen de ruido.
 * Con 256 bytes de imagen → ceil(256/200) = 2 paquetes.
 */
#define CHUNK_SIZE  200

/* ── Buffer de imagen ── */
static uint8_t img_buf[IMG_SIZE];

/* ──────────────────────────────────────────────
 * Genera imagen de prueba: tablero de ajedrez
 * 4×4 píxeles por celda, blanco=255 / negro=0
 * Fácil de verificar visualmente en el receptor.
 * ────────────────────────────────────────────── */
static void gen_checkerboard(uint8_t *buf)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            buf[y * IMG_W + x] = ((x / 4 + y / 4) % 2) ? 255 : 0;
        }
    }
}

/* ──────────────────────────────────────────────
 * Genera imagen de prueba: gradiente diagonal
 * Útil para verificar que el orden de bytes
 * se preserva correctamente en la transmisión.
 * ────────────────────────────────────────────── */
static void gen_gradient(uint8_t *buf)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            /* valor de 0 a 255 a lo largo de la diagonal */
            buf[y * IMG_W + x] = (uint8_t)(((x + y) * 255) / (IMG_W + IMG_H - 2));
        }
    }
}

/* ──────────────────────────────────────────────
 * Imprime la imagen por UART en formato CSV
 * para poder copiar y pegar en el script Python.
 * ────────────────────────────────────────────── */
static void print_image_csv(const uint8_t *buf)
{
    ESP_LOGI(TAG, "=== IMAGEN TX (CSV, fila por fila) ===");
    for (int y = 0; y < IMG_H; y++) {
        char line[IMG_W * 5]; /* máx "255, " × 16 = 80 chars */
        int pos = 0;
        for (int x = 0; x < IMG_W; x++) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "%3d%s", buf[y * IMG_W + x],
                            (x < IMG_W - 1) ? "," : "");
        }
        printf("%s\n", line);
    }
    ESP_LOGI(TAG, "=== FIN IMAGEN TX ===");
}

/* ──────────────────────────────────────────────
 * Transmite la imagen en chunks por LoRa
 * Retorna: número de chunks enviados exitosamente
 * ────────────────────────────────────────────── */
static int transmit_image(SX1276 &radio, const uint8_t *buf, uint16_t img_size)
{
    uint8_t total_chunks = (img_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int ok_count = 0;

    ESP_LOGI(TAG, "Iniciando transmisión: %d bytes → %d chunks", img_size, total_chunks);

    for (uint8_t i = 0; i < total_chunks; i++) {

        uint16_t offset = i * CHUNK_SIZE;
        uint16_t len    = (img_size - offset < CHUNK_SIZE)
                          ? (img_size - offset)
                          : CHUNK_SIZE;

        /* Armar paquete: 4 bytes header + payload */
        uint8_t packet[4 + CHUNK_SIZE];
        packet[0] = i;                        /* chunk_idx      */
        packet[1] = total_chunks;             /* total_chunks   */
        packet[2] = (uint8_t)(len >> 8);      /* payload_len_hi */
        packet[3] = (uint8_t)(len & 0xFF);    /* payload_len_lo */
        memcpy(packet + 4, buf + offset, len);

        int state = radio.transmit(packet, len + 4);

        if (state == RADIOLIB_ERR_NONE) {
            ESP_LOGI(TAG, "Chunk %d/%d OK  (payload %d bytes, tiempo TX: %.0f ms)",
                     i + 1, total_chunks, len,
                     radio.getLastTransmitTime());  /* ms */
            ok_count++;
        } else {
            ESP_LOGE(TAG, "Chunk %d/%d ERROR: código %d", i + 1, total_chunks, state);
        }

        /* Pausa mínima entre paquetes para que el receptor esté listo */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return ok_count;
}

/* ──────────────────────────────────────────────
 * Task principal
 * ────────────────────────────────────────────── */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== LORA TX IMAGE — Heltec V2 ===");

    /* ── Inicializar radio ── */
    SX1276 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1);

    ESP_LOGI(TAG, "Inicializando SX1276...");
    int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                            LORA_SYNC, LORA_PWR);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "Radio init FAILED: %d — verificá conexiones SPI", state);
        return;
    }
    ESP_LOGI(TAG, "Radio OK");

    /* ── Generar imagen de prueba ── */
    gen_checkerboard(img_buf);
    /* Alternativa: gen_gradient(img_buf); */

    print_image_csv(img_buf);

    /* ── Loop: transmitir cada 10 segundos ── */
    int tx_count = 0;
    while (1) {
        tx_count++;
        ESP_LOGI(TAG, "--- TX #%d ---", tx_count);

        int ok = transmit_image(radio, img_buf, IMG_SIZE);
        ESP_LOGI(TAG, "TX #%d completo: %d/%d chunks OK",
                 tx_count, ok,
                 (IMG_SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
