#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_FREQUENCY_HZ 400000

#define BOARD_PIN_VEXT GPIO_NUM_21
#define BOARD_PIN_OLED_SDA GPIO_NUM_4
#define BOARD_PIN_OLED_SCL GPIO_NUM_15
#define BOARD_PIN_OLED_RST GPIO_NUM_16
#define BOARD_PIN_DHT11 GPIO_NUM_13

/* ── SX1276 LoRa (bus SPI2 / HSPI) ── */
#define BOARD_SPI_HOST      SPI2_HOST
#define BOARD_PIN_LORA_SCK  GPIO_NUM_5
#define BOARD_PIN_LORA_MOSI GPIO_NUM_27
#define BOARD_PIN_LORA_MISO GPIO_NUM_19
#define BOARD_PIN_LORA_NSS  GPIO_NUM_18   /* chip select, activo bajo */
#define BOARD_PIN_LORA_RST  GPIO_NUM_14   /* reset, activo bajo       */
#define BOARD_PIN_LORA_DIO0 GPIO_NUM_26   /* TX/RX done interrupt     */

esp_err_t board_init(void);
esp_err_t board_enable_vext(bool enable);
esp_err_t board_i2c_init(void);
esp_err_t board_oled_reset(void);

#endif
