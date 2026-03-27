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
