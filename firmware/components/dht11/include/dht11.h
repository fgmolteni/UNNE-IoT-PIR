#ifndef DHT11_H
#define DHT11_H

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t pin;
} dht11_t;

esp_err_t dht11_init(dht11_t *sensor, gpio_num_t pin);
esp_err_t dht11_read(const dht11_t *sensor, float *temperature_c, float *humidity_percent);

#endif
