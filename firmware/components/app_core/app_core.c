#include "app_core.h"

#include <stdio.h>

#include "board_config.h"
#include "dht11.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SENSOR_POLL_INTERVAL_MS 3000

static const char *TAG = "app_core";

void app_core_start(void) {
    dht11_t sensor;

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_show_boot());
    ESP_ERROR_CHECK(dht11_init(&sensor, BOARD_PIN_DHT11));

    ESP_LOGI(TAG, "Heltec WiFi LoRa 32 V2 ready");
    ESP_LOGI(TAG, "OLED on SDA=%d SCL=%d RST=%d VEXT=%d", BOARD_PIN_OLED_SDA, BOARD_PIN_OLED_SCL, BOARD_PIN_OLED_RST, BOARD_PIN_VEXT);
    ESP_LOGI(TAG, "DHT11 on GPIO %d", BOARD_PIN_DHT11);

    vTaskDelay(pdMS_TO_TICKS(1500));

    while (true) {
        float temperature_c = 0.0f;
        float humidity_percent = 0.0f;
        esp_err_t err = dht11_read(&sensor, &temperature_c, &humidity_percent);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "temperature=%.1fC humidity=%.1f%%", temperature_c, humidity_percent);
            ESP_ERROR_CHECK(display_show_sensor_data(temperature_c, humidity_percent));
        } else {
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK(display_show_status("DHT11 ERROR", "CHECK WIRING"));
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}
