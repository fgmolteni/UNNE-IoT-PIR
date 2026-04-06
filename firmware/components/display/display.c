#include "display.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_PAGE_COUNT)

static const char *TAG = "display";
static uint8_t s_buffer[OLED_BUFFER_SIZE];

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

static const glyph_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x62, 0x64, 0x08, 0x13, 0x23}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}},
    {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x2F, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3E, 0x49, 0x49, 0x49, 0x32}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x26, 0x49, 0x49, 0x49, 0x3E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *find_glyph(char character) {
    size_t count = sizeof(s_font) / sizeof(s_font[0]);
    for (size_t i = 0; i < count; ++i) {
        if (s_font[i].character == character) {
            return s_font[i].columns;
        }
    }
    return s_font[0].columns;
}

static esp_err_t send_command(uint8_t command) {
    uint8_t payload[2] = {0x00, command};
    return i2c_master_write_to_device(BOARD_I2C_PORT, OLED_I2C_ADDRESS, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

static esp_err_t send_data(const uint8_t *data, size_t length) {
    uint8_t packet[17];
    packet[0] = 0x40;

    while (length > 0) {
        size_t chunk = length > 16 ? 16 : length;
        memcpy(&packet[1], data, chunk);
        esp_err_t err = i2c_master_write_to_device(BOARD_I2C_PORT, OLED_I2C_ADDRESS, packet, chunk + 1, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            return err;
        }
        data += chunk;
        length -= chunk;
    }

    return ESP_OK;
}

static void buffer_draw_char(int x, int page, char character) {
    if (page < 0 || page >= OLED_PAGE_COUNT || x >= OLED_WIDTH) {
        return;
    }

    const uint8_t *glyph = find_glyph(character);
    size_t base = (size_t) page * OLED_WIDTH;
    for (int i = 0; i < 5; ++i) {
        int column = x + i;
        if (column >= 0 && column < OLED_WIDTH) {
            s_buffer[base + column] = glyph[i];
        }
    }

    int spacer = x + 5;
    if (spacer >= 0 && spacer < OLED_WIDTH) {
        s_buffer[base + spacer] = 0x00;
    }
}

static void buffer_draw_text(int x, int page, const char *text) {
    while (*text != '\0' && x < OLED_WIDTH) {
        char character = *text++;
        if (character >= 'a' && character <= 'z') {
            character = (char) (character - 32);
        }
        buffer_draw_char(x, page, character);
        x += 6;
    }
}

static esp_err_t flush_buffer(void) {
    for (uint8_t page = 0; page < OLED_PAGE_COUNT; ++page) {
        ESP_RETURN_ON_ERROR(send_command((uint8_t) (0xB0 + page)), TAG, "set page failed");
        ESP_RETURN_ON_ERROR(send_command(0x00), TAG, "set lower column failed");
        ESP_RETURN_ON_ERROR(send_command(0x10), TAG, "set upper column failed");
        ESP_RETURN_ON_ERROR(send_data(&s_buffer[page * OLED_WIDTH], OLED_WIDTH), TAG, "write page failed");
    }
    return ESP_OK;
}

esp_err_t display_clear(void) {
    memset(s_buffer, 0, sizeof(s_buffer));
    return flush_buffer();
}

esp_err_t display_init(void) {
    ESP_RETURN_ON_ERROR(send_command(0xAE), TAG, "display off failed");
    ESP_RETURN_ON_ERROR(send_command(0xD5), TAG, "clock command failed");
    ESP_RETURN_ON_ERROR(send_command(0x80), TAG, "clock value failed");
    ESP_RETURN_ON_ERROR(send_command(0xA8), TAG, "mux command failed");
    ESP_RETURN_ON_ERROR(send_command(0x3F), TAG, "mux value failed");
    ESP_RETURN_ON_ERROR(send_command(0xD3), TAG, "offset command failed");
    ESP_RETURN_ON_ERROR(send_command(0x00), TAG, "offset value failed");
    ESP_RETURN_ON_ERROR(send_command(0x40), TAG, "start line failed");
    ESP_RETURN_ON_ERROR(send_command(0x8D), TAG, "charge pump command failed");
    ESP_RETURN_ON_ERROR(send_command(0x14), TAG, "charge pump value failed");
    ESP_RETURN_ON_ERROR(send_command(0x20), TAG, "memory mode command failed");
    ESP_RETURN_ON_ERROR(send_command(0x00), TAG, "memory mode value failed");
    ESP_RETURN_ON_ERROR(send_command(0xA1), TAG, "segment remap failed");
    ESP_RETURN_ON_ERROR(send_command(0xC8), TAG, "scan direction failed");
    ESP_RETURN_ON_ERROR(send_command(0xDA), TAG, "com pins command failed");
    ESP_RETURN_ON_ERROR(send_command(0x12), TAG, "com pins value failed");
    ESP_RETURN_ON_ERROR(send_command(0x81), TAG, "contrast command failed");
    ESP_RETURN_ON_ERROR(send_command(0xCF), TAG, "contrast value failed");
    ESP_RETURN_ON_ERROR(send_command(0xD9), TAG, "precharge command failed");
    ESP_RETURN_ON_ERROR(send_command(0xF1), TAG, "precharge value failed");
    ESP_RETURN_ON_ERROR(send_command(0xDB), TAG, "vcom command failed");
    ESP_RETURN_ON_ERROR(send_command(0x40), TAG, "vcom value failed");
    ESP_RETURN_ON_ERROR(send_command(0xA4), TAG, "resume ram display failed");
    ESP_RETURN_ON_ERROR(send_command(0xA6), TAG, "normal display failed");
    ESP_RETURN_ON_ERROR(send_command(0x2E), TAG, "scroll disable failed");
    ESP_RETURN_ON_ERROR(send_command(0xAF), TAG, "display on failed");
    return display_clear();
}

esp_err_t display_show_status(const char *line1, const char *line2) {
    memset(s_buffer, 0, sizeof(s_buffer));
    if (line1 != NULL) {
        buffer_draw_text(0, 0, line1);
    }
    if (line2 != NULL) {
        buffer_draw_text(0, 2, line2);
    }
    return flush_buffer();
}

esp_err_t display_show_boot(void) {
    ESP_LOGI(TAG, "showing boot screen");
    return display_show_status("BOOTING", "FACENA-UNNE");
}

esp_err_t display_show_sensor_data(float temperature_c, float humidity_percent) {
    char temp_line[22];
    char hum_line[22];

    snprintf(temp_line, sizeof(temp_line), "TEMP: %.1f C", temperature_c);
    snprintf(hum_line, sizeof(hum_line), "HUM: %.1f %%", humidity_percent);

    memset(s_buffer, 0, sizeof(s_buffer));
    buffer_draw_text(0, 0, "DHT11 READY");
    buffer_draw_text(0, 2, temp_line);
    buffer_draw_text(0, 4, hum_line);
    return flush_buffer();
}

esp_err_t display_show_lora_tx(int tx_count, int chunks_ok, int chunks_total) {
    char line1[22];
    char line2[22];

    /*
     * Cada página del SSD1306 tiene 8px de alto.
     * buffer_draw_text(x, page, text) escribe en la página indicada.
     * Usamos pages 0, 2, 4, 6 para dejar espacio entre líneas.
     */
    snprintf(line1, sizeof(line1), "TX %d  %d/%d OK", tx_count, chunks_ok, chunks_total);
    snprintf(line2, sizeof(line2), "SF10 BW125");

    memset(s_buffer, 0, sizeof(s_buffer));
    buffer_draw_text(0, 0, "LORA TX");
    buffer_draw_text(0, 2, line1);
    buffer_draw_text(0, 4, line2);
    buffer_draw_text(0, 6, "915 MHZ");
    return flush_buffer();
}

esp_err_t display_show_lora_rx(int img_count, int16_t rssi, int8_t snr) {
    char line1[22];
    char line2[22];
    char line3[22];

    snprintf(line1, sizeof(line1), "IMG %d OK", img_count);

    /*
     * RSSI puede ser negativo (ej: -87). El font no tiene el signo '-'
     * así que lo manejamos con la lógica de snprintf y el font ya tiene '-'.
     */
    snprintf(line2, sizeof(line2), "RSSI %d DBM", (int)rssi);
    snprintf(line3, sizeof(line3), "SNR %d DB",   (int)snr);

    memset(s_buffer, 0, sizeof(s_buffer));
    buffer_draw_text(0, 0, "LORA RX");
    buffer_draw_text(0, 2, line1);
    buffer_draw_text(0, 4, line2);
    buffer_draw_text(0, 6, line3);
    return flush_buffer();
}

esp_err_t display_show_lora_waiting(void) {
    memset(s_buffer, 0, sizeof(s_buffer));
    buffer_draw_text(0, 0, "LORA RX");
    buffer_draw_text(0, 2, "WAITING");
    buffer_draw_text(0, 4, "SF10 BW125");
    buffer_draw_text(0, 6, "915 MHZ");
    return flush_buffer();
}

esp_err_t display_show_lora_rx_image(const uint8_t *img,
                                      int img_count,
                                      int16_t rssi,
                                      int8_t  snr,
                                      int     chunks_ok,
                                      int     chunks_total)
{
    /*
     * Renderizado de imagen 16x16 escalada x3 en la mitad izquierda.
     *
     * El SSD1306 organiza la memoria en páginas de 8 filas verticales.
     * s_buffer[page * 128 + col] = byte que representa 8 píxeles verticales
     * en la columna `col` de la página `page`.
     *
     * Para escalar x3: cada píxel de la imagen ocupa 3 columnas x 3 filas
     * en la pantalla. Como la imagen tiene 16 columnas x 16 filas:
     *   - Ancho renderizado: 16 x 3 = 48 px (columnas 0..47)
     *   - Alto renderizado:  16 x 3 = 48 px (páginas 0..5, filas 0..47)
     *
     * Para cada píxel (img_x, img_y):
     *   - pantalla_col_inicio = img_x * 3
     *   - pantalla_fila_inicio = img_y * 3
     * Las 3 filas de pantalla caen en hasta 2 páginas distintas.
     * Para cada fila de pantalla `pf` en [pf_ini, pf_ini+2]:
     *   - page = pf / 8
     *   - bit  = pf % 8
     *   Si el píxel es blanco (val > 127), setear ese bit en las 3 columnas.
     */
    memset(s_buffer, 0, sizeof(s_buffer));

    for (int img_y = 0; img_y < 16; img_y++) {
        for (int img_x = 0; img_x < 16; img_x++) {

            uint8_t val = img[img_y * 16 + img_x];
            if (val <= 127) continue;   /* negro — bits ya en 0 */

            /* Rango de filas de pantalla que ocupa este píxel */
            int pf_start = img_y * 3;

            for (int df = 0; df < 3; df++) {          /* 3 filas de pantalla */
                int pf   = pf_start + df;
                int page = pf / 8;
                int bit  = pf % 8;

                if (page >= OLED_PAGE_COUNT) continue;

                for (int dc = 0; dc < 3; dc++) {      /* 3 columnas de pantalla */
                    int col = img_x * 3 + dc;
                    if (col >= 48) continue;
                    s_buffer[page * OLED_WIDTH + col] |= (1 << bit);
                }
            }
        }
    }

    /* — Separador vertical entre imagen y texto (columna 52) — */
    for (int page = 0; page < OLED_PAGE_COUNT; page++) {
        s_buffer[page * OLED_WIDTH + 52] = 0xFF;
    }

    /* — Texto de estado en la mitad derecha (columna 56) — */
    char line1[16];
    char line2[16];
    char line3[16];
    char line4[16];

    snprintf(line1, sizeof(line1), "RX %d",   img_count);
    snprintf(line2, sizeof(line2), "%d/%d CHK", chunks_ok, chunks_total);
    snprintf(line3, sizeof(line3), "R%d",       (int)rssi);   /* RSSI */
    snprintf(line4, sizeof(line4), "S%d DB",    (int)snr);    /* SNR  */

    buffer_draw_text(56, 0, line1);
    buffer_draw_text(56, 2, line2);
    buffer_draw_text(56, 4, line3);
    buffer_draw_text(56, 6, line4);

    /*
     * Indicador de integridad: si chunks_ok < chunks_total
     * (imagen incompleta) escribimos "ERR" en la parte inferior
     * de la zona de imagen para que sea visible a simple vista.
     */
    if (chunks_ok < chunks_total) {
        buffer_draw_text(0, 6, "CORRUPT");
    }

    return flush_buffer();
}
