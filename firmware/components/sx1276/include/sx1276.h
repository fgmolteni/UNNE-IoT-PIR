#ifndef SX1276_H
#define SX1276_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

/* ─────────────────────────────────────────────────────────────────
 * Parámetros LoRa — se pasan a sx1276_init() dentro de este struct.
 * Todos los valores son los que se usan en la prueba TX/RX imagen.
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    /* Frecuencia en Hz, ej: 915000000 para 915 MHz */
    uint32_t frequency_hz;

    /*
     * Spreading Factor: 6–12.
     * SF10 = buen balance alcance/velocidad para esta prueba.
     * A mayor SF → más lento, más alcance, más consumo de airtime.
     */
    uint8_t spreading_factor;

    /*
     * Ancho de banda en kHz: 7, 10, 15, 20, 31, 41, 62, 125, 250, 500.
     * Usamos 125 kHz.
     */
    uint8_t bandwidth_khz;

    /*
     * Coding Rate: 5, 6, 7 u 8 (representa 4/5, 4/6, 4/7, 4/8).
     * Usamos 7 (= 4/7).
     */
    uint8_t coding_rate;

    /*
     * Sync word: 0x12 para redes privadas.
     * TTN usa 0x34. Si ponés 0x34 acá, tus paquetes van a la red pública.
     */
    uint8_t sync_word;

    /*
     * Potencia de TX en dBm: 2–17 con PA_BOOST (pin PA_BOOST del SX1276).
     * El Heltec V2 usa PA_BOOST, no RFO, así que el rango válido es 2–17.
     */
    int8_t tx_power_dbm;

} sx1276_config_t;

/* ─────────────────────────────────────────────────────────────────
 * Handle del dispositivo.
 * Se inicializa con sx1276_init() y se pasa a todas las funciones.
 * No modificar los campos directamente — son internos al driver.
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    spi_device_handle_t spi;   /* handle del bus SPI de ESP-IDF     */
    gpio_num_t          pin_rst;  /* GPIO de reset                  */
    gpio_num_t          pin_dio0; /* GPIO de interrupción DIO0       */
    sx1276_config_t     config;   /* copia de la config para reuso   */
} sx1276_t;

/* ─────────────────────────────────────────────────────────────────
 * Valores de retorno de sx1276_receive() para timeout.
 * ESP-IDF no tiene un código estándar para "sin datos todavía",
 * así que usamos ESP_ERR_TIMEOUT que ya existe en esp_err.h.
 * ───────────────────────────────────────────────────────────────── */
#define SX1276_ERR_TIMEOUT  ESP_ERR_TIMEOUT   /* sin paquete en el plazo */
#define SX1276_ERR_CRC      ESP_ERR_INVALID_CRC

/* ─────────────────────────────────────────────────────────────────
 * API pública
 * ───────────────────────────────────────────────────────────────── */

/*
 * sx1276_init — inicializa el bus SPI y configura el chip.
 *
 * Hace en orden:
 *   1. Reset del SX1276 por hardware (pulso bajo 10 ms)
 *   2. Inicializa bus SPI2 con los pines de board_config.h
 *   3. Agrega el SX1276 como dispositivo SPI (NSS automático)
 *   4. Verifica que el chip responda leyendo RegVersion (0x42 esperado)
 *   5. Escribe todos los registros de configuración LoRa
 *   6. Deja el chip en modo Standby listo para TX o RX
 *
 * Retorna ESP_OK si todo salió bien, otro código si hubo error.
 */
esp_err_t sx1276_init(sx1276_t *dev, const sx1276_config_t *config);

/*
 * sx1276_transmit — envía un buffer de bytes por LoRa.
 *
 * Carga el payload en la FIFO del SX1276, activa modo TX,
 * y espera polling sobre DIO0 hasta que TxDone se ponga en 1.
 * Timeout de 5 segundos (más que suficiente para SF10/BW125).
 *
 * len: máximo 255 bytes (límite físico del SX1276).
 *
 * Retorna ESP_OK cuando el paquete fue transmitido exitosamente.
 */
esp_err_t sx1276_transmit(sx1276_t *dev, const uint8_t *data, uint8_t len);

/*
 * sx1276_receive — espera y recibe un paquete LoRa.
 *
 * Pone el chip en modo RxSingle (espera exactamente un paquete).
 * Polling sobre DIO0 hasta RxDone o timeout.
 *
 * Parámetros de salida:
 *   buf    → buffer donde se copia el payload recibido
 *   len    → largo del payload recibido
 *   rssi   → RSSI en dBm (ej: -87)
 *   snr    → SNR en dB con signo (puede ser negativo)
 *
 * timeout_ms: milisegundos máximos de espera.
 *             Pasá 0 para espera indefinida.
 *
 * Retorna ESP_OK si llegó un paquete válido.
 * Retorna SX1276_ERR_TIMEOUT si venció el plazo sin paquete.
 * Retorna SX1276_ERR_CRC si el paquete llegó con error de CRC.
 */
esp_err_t sx1276_receive(sx1276_t *dev,
                         uint8_t  *buf,
                         uint8_t  *len,
                         int16_t  *rssi,
                         int8_t   *snr,
                         uint32_t  timeout_ms);

/*
 * sx1276_deinit — libera el bus SPI y el handle.
 *
 * Llamar antes de deshabilitar el chip o hacer deep sleep.
 */
void sx1276_deinit(sx1276_t *dev);

#endif /* SX1276_H */
