#include "board_config.h"

#include "esp_rom_sys.h"

esp_err_t board_enable_vext(bool enable) {
    ESP_ERROR_CHECK(gpio_reset_pin(BOARD_PIN_VEXT));
    ESP_ERROR_CHECK(gpio_set_direction(BOARD_PIN_VEXT, GPIO_MODE_OUTPUT));
    return gpio_set_level(BOARD_PIN_VEXT, enable ? 0 : 1);
}

esp_err_t board_i2c_init(void) {
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_PIN_OLED_SDA,
        .scl_io_num = BOARD_PIN_OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQUENCY_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(BOARD_I2C_PORT, &config);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(BOARD_I2C_PORT, config.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }

    return err;
}

esp_err_t board_oled_reset(void) {
    ESP_ERROR_CHECK(gpio_reset_pin(BOARD_PIN_OLED_RST));
    ESP_ERROR_CHECK(gpio_set_direction(BOARD_PIN_OLED_RST, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_PIN_OLED_RST, 0));
    esp_rom_delay_us(10000);
    ESP_ERROR_CHECK(gpio_set_level(BOARD_PIN_OLED_RST, 1));
    esp_rom_delay_us(10000);
    return ESP_OK;
}

esp_err_t board_init(void) {
    ESP_ERROR_CHECK(board_enable_vext(true));
    ESP_ERROR_CHECK(board_i2c_init());
    return board_oled_reset();
}
