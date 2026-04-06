#include "dht11.h"

#include <stdint.h>

#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT11_START_SIGNAL_MS 20
#define DHT11_RESPONSE_TIMEOUT_US 200
#define DHT11_BIT_TIMEOUT_US 150

static esp_err_t wait_for_level(gpio_num_t pin, int level, uint32_t timeout_us, uint32_t *pulse_width_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if ((uint32_t) (esp_timer_get_time() - start) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (pulse_width_us != NULL) {
        *pulse_width_us = (uint32_t) (esp_timer_get_time() - start);
    }

    return ESP_OK;
}

esp_err_t dht11_init(dht11_t *sensor, gpio_num_t pin) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor->pin = pin;
    ESP_ERROR_CHECK(gpio_reset_pin(pin));
    ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_level(pin, 1));
    return ESP_OK;
}

esp_err_t dht11_read(const dht11_t *sensor, float *temperature_c, float *humidity_percent) {
    if (sensor == NULL || temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[5] = {0};

    ESP_ERROR_CHECK(gpio_set_direction(sensor->pin, GPIO_MODE_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(sensor->pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_level(sensor->pin, 0));
    vTaskDelay(pdMS_TO_TICKS(DHT11_START_SIGNAL_MS));
    ESP_ERROR_CHECK(gpio_set_level(sensor->pin, 1));
    esp_rom_delay_us(30);
    ESP_ERROR_CHECK(gpio_set_direction(sensor->pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(sensor->pin, GPIO_PULLUP_ONLY));

    if (wait_for_level(sensor->pin, 1, DHT11_RESPONSE_TIMEOUT_US, NULL) != ESP_OK ||
        wait_for_level(sensor->pin, 0, DHT11_RESPONSE_TIMEOUT_US, NULL) != ESP_OK ||
        wait_for_level(sensor->pin, 1, DHT11_RESPONSE_TIMEOUT_US, NULL) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    for (int byte_index = 0; byte_index < 5; ++byte_index) {
        for (int bit_index = 0; bit_index < 8; ++bit_index) {
            uint32_t high_time_us = 0;
            if (wait_for_level(sensor->pin, 0, DHT11_BIT_TIMEOUT_US, NULL) != ESP_OK) {
                return ESP_ERR_TIMEOUT;
            }
            if (wait_for_level(sensor->pin, 1, DHT11_BIT_TIMEOUT_US, &high_time_us) != ESP_OK) {
                return ESP_ERR_TIMEOUT;
            }

            data[byte_index] <<= 1;
            if (high_time_us > 40) {
                data[byte_index] |= 1;
            }
        }
    }

    uint8_t checksum = (uint8_t) (data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    *humidity_percent = (float) data[0] + ((float) data[1] / 100.0f);
    *temperature_c = (float) data[2] + ((float) data[3] / 100.0f);
    return ESP_OK;
}
