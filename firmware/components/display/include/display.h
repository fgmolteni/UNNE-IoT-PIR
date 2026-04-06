#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"

esp_err_t display_init(void);
esp_err_t display_clear(void);
esp_err_t display_show_boot(void);
esp_err_t display_show_status(const char *line1, const char *line2);
esp_err_t display_show_sensor_data(float temperature_c, float humidity_percent);

/*
 * display_show_lora_tx — pantalla del transmisor LoRa.
 *
 * Muestra:
 *   Línea 0: "LORA TX"
 *   Línea 1: estado del último envío  (ej: "TX #3  2/2 OK")
 *   Línea 2: parámetros fijos         (ej: "SF10 BW125")
 *   Línea 3: frecuencia               (ej: "915 MHZ")
 */
esp_err_t display_show_lora_tx(int tx_count, int chunks_ok, int chunks_total);

/*
 * display_show_lora_rx — pantalla del receptor LoRa.
 *
 * Muestra:
 *   Línea 0: "LORA RX"
 *   Línea 1: imágenes recibidas       (ej: "IMG #2 OK")
 *   Línea 2: RSSI del último paquete  (ej: "RSSI -87 DBM")
 *   Línea 3: SNR del último paquete   (ej: "SNR 8 DB")
 */
esp_err_t display_show_lora_rx(int img_count, int16_t rssi, int8_t snr);

/*
 * display_show_lora_waiting — pantalla de espera del receptor.
 * Se muestra mientras no llega ningún paquete.
 */
esp_err_t display_show_lora_waiting(void);

/*
 * display_show_lora_rx_image — renderiza la imagen recibida en la OLED.
 *
 * Divide la pantalla en dos mitades:
 *   - Izquierda (0..47px):  imagen 16x16 escalada x3, pixel > 127 = blanco.
 *   - Derecha  (68..127px): estado de la recepción.
 *
 * Si un chunk se perdió, esa zona de la imagen aparece en negro.
 * img debe ser un buffer de 256 bytes (16x16), fila a fila.
 */
esp_err_t display_show_lora_rx_image(const uint8_t *img,
                                      int img_count,
                                      int16_t rssi,
                                      int8_t  snr,
                                      int     chunks_ok,
                                      int     chunks_total);

#endif
