#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"

esp_err_t display_init(void);
esp_err_t display_clear(void);
esp_err_t display_show_boot(void);
esp_err_t display_show_status(const char *line1, const char *line2);
esp_err_t display_show_sensor_data(float temperature_c, float humidity_percent);

#endif
