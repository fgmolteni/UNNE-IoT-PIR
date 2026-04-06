#ifndef LORA_IMAGE_TEST_H
#define LORA_IMAGE_TEST_H

/*
 * ─────────────────────────────────────────────────────────────────
 * MODO DE OPERACIÓN — cambiá esta línea antes de flashear
 *
 *   #define LORA_IMAGE_MODE_TX   → este Heltec transmite
 *   #define LORA_IMAGE_MODE_RX   → este Heltec recibe
 *
 * Flasheá con MODE_TX en el Heltec A y MODE_RX en el Heltec B.
 * ─────────────────────────────────────────────────────────────────
 */
#define LORA_IMAGE_MODE_TX 
/* #define LORA_IMAGE_MODE_RX */

/* Verificación: exactamente uno de los dos debe estar definido */
#if defined(LORA_IMAGE_MODE_TX) && defined(LORA_IMAGE_MODE_RX)
    #error "Definí solo uno: LORA_IMAGE_MODE_TX o LORA_IMAGE_MODE_RX"
    #endif
#if !defined(LORA_IMAGE_MODE_TX) && !defined(LORA_IMAGE_MODE_RX)
    #error "Definí uno: LORA_IMAGE_MODE_TX o LORA_IMAGE_MODE_RX"
#endif

/*
 * lora_image_test_start — punto de entrada único.
 *
 * Inicializa el SX1276 con los parámetros de la prueba y lanza
 * la FreeRTOS task correspondiente al modo definido arriba.
 *
 * Llamar desde app_core_start() en lugar del loop de DHT11/OLED,
 * o agregarlo como rama condicional con un #define en app_core.
 *
 * No retorna (la task corre indefinidamente).
 */
void lora_image_test_start(void);

#endif /* LORA_IMAGE_TEST_H */
