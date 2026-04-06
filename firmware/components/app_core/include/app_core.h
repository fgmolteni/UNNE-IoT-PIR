#ifndef APP_CORE_H
#define APP_CORE_H

/*
 * ─────────────────────────────────────────────────────────────────
 * MODO DE APLICACIÓN
 *
 * Descomentá UNO de los siguientes para seleccionar qué corre
 * el firmware al arrancar:
 *
 *   APP_MODE_LORA_TEST → prueba TX/RX imagen 16×16 por LoRa
 *                        (el modo TX/RX se controla en lora_image_test.h)
 *
 * Si ninguno está definido → comportamiento original: DHT11 + OLED.
 * ─────────────────────────────────────────────────────────────────
 */
#define APP_MODE_LORA_TEST

void app_core_start(void);

#endif
