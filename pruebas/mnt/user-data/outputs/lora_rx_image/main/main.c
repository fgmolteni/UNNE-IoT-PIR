/**
 * UNNE-IoT-PIR — Prueba RX imagen 16×16 por LoRa
 * Hardware : Heltec WiFi LoRa 32 V2
 * Framework: ESP-IDF + RadioLib (via Arduino-ESP32 component)
 *
 * Recibe los chunks de imagen enviados por lora_tx_image,
 * reensambla la imagen en un buffer y vuelca los bytes
 * por UART (Serial) para reconstrucción en Python.
 *
 * Protocolo de header (mismo que TX):
 *   byte 0: chunk_idx
 *   byte 1: total_chunks
 *   byte 2: payload_len_hi
 *   byte 3: payload_len_lo
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <RadioLib.h>

static const char *TAG = "LORA_RX";

/* ── Pines Heltec V2 (mismos que TX) ── */
#define LORA_NSS    18
#define LORA_DIO0   26
#define LORA_RESET  14
#define LORA_DIO1   35

/* ── Config LoRa — DEBE ser idéntica al TX ── */
#define LORA_FREQ   915.0f
#define LORA_BW     125.0f
#define LORA_SF     10
#define LORA_CR     7
#define LORA_SYNC   0x12
#define LORA_PWR    17          /* no afecta al receptor, pero lo fijamos igual */

/* ── Imagen esperada ── */
#define IMG_W       16
#define IMG_H       16
#define IMG_SIZE    (IMG_W * IMG_H)
#define CHUNK_SIZE  200
#define MAX_CHUNKS  10          /* ceil(IMG_SIZE / CHUNK_SIZE) con margen */

/* ── Buffer de recepción ── */
static uint8_t img_buf[IMG_SIZE];
static bool    chunk_received[MAX_CHUNKS];
static uint8_t total_chunks_expected = 0;

/* ──────────────────────────────────────────────
 * Resetea el estado de recepción para la
 * siguiente imagen.
 * ────────────────────────────────────────────── */
static void reset_reception(void)
{
    memset(img_buf, 0, IMG_SIZE);
    memset(chunk_received, 0, sizeof(chunk_received));
    total_chunks_expected = 0;
    ESP_LOGI(TAG, "Recepción reseteada — esperando nueva imagen...");
}

/* ──────────────────────────────────────────────
 * Verifica si se recibieron todos los chunks
 * ────────────────────────────────────────────── */
static bool all_chunks_received(void)
{
    if (total_chunks_expected == 0) return false;
    for (int i = 0; i < total_chunks_expected; i++) {
        if (!chunk_received[i]) return false;
    }
    return true;
}

/* ──────────────────────────────────────────────
 * Vuelca la imagen completa por UART.
 * Formato: un entero por línea (0-255), fila a fila,
 * columna a columna. El script Python lo lee así.
 *
 * También imprime el CSV para verificación manual.
 * ────────────────────────────────────────────── */
static void dump_image_uart(const uint8_t *buf)
{
    /* ── Bloque delimitador — Python busca estas líneas ── */
    printf("IMG_START\n");
    for (int i = 0; i < IMG_SIZE; i++) {
        printf("%d\n", buf[i]);
    }
    printf("IMG_END\n");
    fflush(stdout);

    /* ── CSV para revisión manual ── */
    ESP_LOGI(TAG, "=== IMAGEN RX (CSV) ===");
    for (int y = 0; y < IMG_H; y++) {
        char line[IMG_W * 5];
        int pos = 0;
        for (int x = 0; x < IMG_W; x++) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "%3d%s", buf[y * IMG_W + x],
                            (x < IMG_W - 1) ? "," : "");
        }
        printf("%s\n", line);
    }
    ESP_LOGI(TAG, "=== FIN IMAGEN RX ===");
}

/* ──────────────────────────────────────────────
 * Procesa un paquete recibido.
 * Retorna true si la imagen quedó completa.
 * ────────────────────────────────────────────── */
static bool process_packet(const uint8_t *packet, int pkt_len,
                           float rssi, float snr)
{
    if (pkt_len < 4) {
        ESP_LOGW(TAG, "Paquete demasiado corto (%d bytes), descartado", pkt_len);
        return false;
    }

    uint8_t  chunk_idx    = packet[0];
    uint8_t  total_chunks = packet[1];
    uint16_t payload_len  = ((uint16_t)packet[2] << 8) | packet[3];
    const uint8_t *data   = packet + 4;

    /* Validaciones básicas */
    if (total_chunks == 0 || total_chunks > MAX_CHUNKS) {
        ESP_LOGW(TAG, "total_chunks inválido: %d", total_chunks);
        return false;
    }
    if (chunk_idx >= total_chunks) {
        ESP_LOGW(TAG, "chunk_idx %d >= total_chunks %d", chunk_idx, total_chunks);
        return false;
    }
    if (payload_len > CHUNK_SIZE) {
        ESP_LOGW(TAG, "payload_len inválido: %d", payload_len);
        return false;
    }

    /* Si es una nueva imagen (primer chunk o reinicio) */
    if (chunk_idx == 0) {
        reset_reception();
        total_chunks_expected = total_chunks;
    } else if (total_chunks_expected == 0) {
        /* Llegó un chunk sin haber recibido el primero antes */
        ESP_LOGW(TAG, "Chunk %d recibido sin chunk 0 — descartado (esperá el TX completo)", chunk_idx);
        return false;
    }

    /* Evitar escribir fuera del buffer */
    uint16_t offset = chunk_idx * CHUNK_SIZE;
    if (offset + payload_len > IMG_SIZE) {
        ESP_LOGW(TAG, "Offset %d + len %d > IMG_SIZE %d — overflow", offset, payload_len, IMG_SIZE);
        return false;
    }

    /* Copiar payload al buffer de imagen */
    memcpy(img_buf + offset, data, payload_len);
    chunk_received[chunk_idx] = true;

    ESP_LOGI(TAG, "Chunk %d/%d OK  payload=%d bytes  RSSI=%.1f dBm  SNR=%.1f dB",
             chunk_idx + 1, total_chunks, payload_len, rssi, snr);

    /* ¿Imagen completa? */
    if (all_chunks_received()) {
        ESP_LOGI(TAG, "✓ Imagen completa — volcando por UART...");
        dump_image_uart(img_buf);
        return true;
    }

    /* Mostrar chunks faltantes */
    char missing[64] = "Faltantes: ";
    int pos = strlen(missing);
    for (int i = 0; i < total_chunks_expected; i++) {
        if (!chunk_received[i])
            pos += snprintf(missing + pos, sizeof(missing) - pos, "%d ", i);
    }
    ESP_LOGI(TAG, "%s", missing);

    return false;
}

/* ──────────────────────────────────────────────
 * Task principal
 * ────────────────────────────────────────────── */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== LORA RX IMAGE — Heltec V2 ===");

    /* ── Inicializar radio ── */
    SX1276 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1);

    ESP_LOGI(TAG, "Inicializando SX1276...");
    int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                            LORA_SYNC, LORA_PWR);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "Radio init FAILED: %d", state);
        return;
    }
    ESP_LOGI(TAG, "Radio OK — escuchando en %.1f MHz SF%d BW%.0f sync=0x%02X",
             LORA_FREQ, LORA_SF, LORA_BW, LORA_SYNC);

    reset_reception();

    /* ── Buffer de recepción de paquete ── */
    uint8_t rx_buf[256];
    int     img_count = 0;

    /* ── Loop de recepción ── */
    while (1) {
        /* receive() bloqueante — espera hasta recibir o timeout.
           El 0 significa tamaño automático (usa el largo del paquete LoRa). */
        int recv_state = radio.receive(rx_buf, 0);

        if (recv_state == RADIOLIB_ERR_NONE) {
            int pkt_len = radio.getPacketLength();
            float rssi  = radio.getRSSI();
            float snr   = radio.getSNR();

            bool complete = process_packet(rx_buf, pkt_len, rssi, snr);

            if (complete) {
                img_count++;
                ESP_LOGI(TAG, "=== Imagen #%d recibida y volcada ===", img_count);
                /* Resetear para la siguiente imagen */
                reset_reception();
            }

        } else if (recv_state == RADIOLIB_ERR_RX_TIMEOUT) {
            /* Normal si no hay TX — se puede ignorar o usar para watchdog */
            ESP_LOGD(TAG, "RX timeout — sin señal");

        } else {
            ESP_LOGW(TAG, "RX error: código %d (CRC o corrupción)", recv_state);
        }
    }
}
