/*
 * lora_image_test.c
 *
 * Prueba de transmisión de imagen 16×16 por LoRa.
 * Usa el driver sx1276 (C puro, ESP-IDF spi_master).
 *
 * Protocolo de fragmentación (header de 4 bytes por paquete):
 *
 *   ┌────────────┬──────────────┬──────────────┬─────────────┬────────────────┐
 *   │ chunk_idx  │ total_chunks │ payload_len  │ payload_len │ datos imagen   │
 *   │  (1 byte)  │   (1 byte)   │   hi (1B)    │   lo (1B)   │ (≤200 bytes)  │
 *   └────────────┴──────────────┴──────────────┴─────────────┴────────────────┘
 *
 * Con IMG_SIZE=256 bytes y CHUNK_PAYLOAD=200 → 2 paquetes por imagen.
 */

#include "lora_image_test.h"
#include "sx1276.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lora_img_test";

/* ─────────────────────────────────────────────────────────────────
 * Parámetros de imagen
 * ───────────────────────────────────────────────────────────────── */
#define IMG_W        16
#define IMG_H        16
#define IMG_SIZE     (IMG_W * IMG_H)   /* 256 bytes */

/*
 * CHUNK_PAYLOAD: bytes de imagen por paquete.
 *
 * El SX1276 admite hasta 255 bytes por paquete. Con 4 bytes de header
 * quedan 251 bytes útiles. Usamos 200 como valor conservador:
 *   - Más margen frente al ruido (menos bits expuestos)
 *   - Con 256 bytes → ceil(256/200) = 2 paquetes exactos
 */
#define CHUNK_PAYLOAD  200
#define MAX_CHUNKS     ((IMG_SIZE + CHUNK_PAYLOAD - 1) / CHUNK_PAYLOAD)  /* 2 */
#define PACKET_SIZE    (4 + CHUNK_PAYLOAD)   /* header + payload máx */

/* ─────────────────────────────────────────────────────────────────
 * Parámetros LoRa para la prueba
 * ───────────────────────────────────────────────────────────────── */
static const sx1276_config_t LORA_CFG = {
    .frequency_hz    = 915000000,  /* 915 MHz — banda ISM Argentina */
    .spreading_factor = 10,        /* SF10: buen balance alcance/velocidad */
    .bandwidth_khz   = 125,        /* 125 kHz */
    .coding_rate     = 7,          /* 4/7 */
    .sync_word       = 0x12,       /* privado — no interfiere con TTN (0x34) */
    .tx_power_dbm    = 17,         /* máximo con PA_BOOST en Heltec V2 */
};

/* ─────────────────────────────────────────────────────────────────
 * Buffers estáticos — no en stack para evitar stack overflow en tasks
 * ───────────────────────────────────────────────────────────────── */
static uint8_t s_img_buf[IMG_SIZE];
static uint8_t s_packet_buf[PACKET_SIZE];

/* Buffers y estado exclusivos del receptor */
#ifdef LORA_IMAGE_MODE_RX
static uint8_t s_rx_buf[IMG_SIZE];
static bool    s_chunk_received[MAX_CHUNKS];
static uint8_t s_total_chunks_expected;
#endif

/* ─────────────────────────────────────────────────────────────────
 * Generadores de imagen de prueba
 * ───────────────────────────────────────────────────────────────── */

/*
 * Tablero de ajedrez 4×4 píxeles por celda.
 * Blanco=255, Negro=0.
 * Ventaja: fácil de verificar visualmente — si un byte se corrompió,
 * se nota inmediatamente en la visualización Python.
 */
static void image_gen_checkerboard(uint8_t *buf)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            buf[y * IMG_W + x] = ((x / 4 + y / 4) % 2) ? 255 : 0;
        }
    }
}

#ifdef LORA_IMAGE_MODE_TX
/*
 * Gradiente diagonal: valores de 0 a 255 a lo largo de la diagonal.
 * Útil para verificar que el rango dinámico completo se preserva
 * y que el orden de bytes no se invierte en TX/RX.
 * Solo se compila en modo TX donde se genera la imagen.
 */
static void image_gen_gradient(uint8_t *buf)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            buf[y * IMG_W + x] = (uint8_t)(((x + y) * 255) / (IMG_W + IMG_H - 2));
        }
    }
}
#endif /* LORA_IMAGE_MODE_TX */

/*
 * Imprime la imagen por UART en formato CSV (fila a fila).
 * Sirve para copiar y pegar en un script Python si no se usa
 * el protocolo IMG_START/IMG_END.
 */
static void image_print_csv(const uint8_t *buf, const char *label)
{
    ESP_LOGI(TAG, "=== %s (CSV 16x16) ===", label);
    for (int y = 0; y < IMG_H; y++) {
        /* Buffer local para armar la línea: "255,255,...,255\n" */
        char line[IMG_W * 5];
        int  pos = 0;
        for (int x = 0; x < IMG_W; x++) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "%3d%s",
                            buf[y * IMG_W + x],
                            (x < IMG_W - 1) ? "," : "");
        }
        printf("%s\n", line);
    }
    ESP_LOGI(TAG, "=== FIN %s ===", label);
}

/* ─────────────────────────────────────────────────────────────────
 * TX — Fragmentación y transmisión
 * ───────────────────────────────────────────────────────────────── */

/*
 * Arma y envía todos los chunks de una imagen.
 * Retorna el número de chunks enviados exitosamente.
 *
 * Secuencia por chunk:
 *   1. Calcular offset y largo del payload de este chunk
 *   2. Armar el paquete: [idx | total | len_hi | len_lo | datos...]
 *   3. Llamar sx1276_transmit()
 *   4. Esperar 500 ms para que el receptor vuelva al modo escucha
 *      antes del próximo paquete
 */
static int transmit_image(sx1276_t *radio, const uint8_t *img, uint16_t img_size)
{
    uint8_t total = (uint8_t)((img_size + CHUNK_PAYLOAD - 1) / CHUNK_PAYLOAD);
    int     ok    = 0;

    ESP_LOGI(TAG, "TX iniciando: %d bytes → %d chunks", img_size, total);

    for (uint8_t i = 0; i < total; i++) {

        uint16_t offset = (uint16_t)i * CHUNK_PAYLOAD;
        uint16_t plen   = (img_size - offset < CHUNK_PAYLOAD)
                          ? (img_size - offset)
                          : CHUNK_PAYLOAD;

        /* Armar paquete en el buffer estático */
        s_packet_buf[0] = i;                       /* chunk_idx      */
        s_packet_buf[1] = total;                   /* total_chunks   */
        s_packet_buf[2] = (uint8_t)(plen >> 8);    /* payload_len_hi */
        s_packet_buf[3] = (uint8_t)(plen & 0xFF);  /* payload_len_lo */
        memcpy(s_packet_buf + 4, img + offset, plen);

        esp_err_t err = sx1276_transmit(radio, s_packet_buf, (uint8_t)(4 + plen));

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  Chunk %d/%d OK  (%d bytes payload)", i + 1, total, plen);
            ok++;
        } else {
            ESP_LOGE(TAG, "  Chunk %d/%d ERROR: %s", i + 1, total, esp_err_to_name(err));
        }

        /*
         * Pausa entre chunks: le da tiempo al receptor de salir del
         * modo RxSingle, procesar el paquete y volver a llamar
         * sx1276_receive() antes de que llegue el siguiente.
         * 500 ms es conservador; se puede bajar a 300 ms si funciona.
         */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return ok;
}

/*
 * Task TX — corre en loop, transmite cada TX_INTERVAL_MS.
 */
#define TX_INTERVAL_MS  10000   /* 10 segundos entre transmisiones */

static void lora_image_tx_task(void *arg)
{
    sx1276_t *radio = (sx1276_t *)arg;
    int tx_count    = 0;

    /* Generar imagen de prueba una sola vez */
    image_gen_checkerboard(s_img_buf);
    /* Alternativa: image_gen_gradient(s_img_buf); */

    image_print_csv(s_img_buf, "IMAGEN TX");

    /*
     * Espera inicial antes de la primera transmisión.
     * Le da tiempo al receptor de inicializarse completamente
     * (boot + sx1276_init + display) antes de que llegue el chunk 0.
     * Sin este delay, el chunk 0 de la primera imagen siempre se pierde.
     */
    ESP_LOGI(TAG, "TX: esperando 5s para que el RX esté listo...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        tx_count++;
        int64_t t0 = esp_timer_get_time();

        ESP_LOGI(TAG, "━━━ TX #%d ━━━", tx_count);
        int ok    = transmit_image(radio, s_img_buf, IMG_SIZE);
        int total = MAX_CHUNKS;

        int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "TX #%d completo: %d/%d chunks OK  (%lld ms total)",
                 tx_count, ok, total, (long long)elapsed_ms);

        /* Actualizar OLED con el resultado del TX */
        display_show_lora_tx(tx_count, ok, total);

        /* Esperar hasta el próximo ciclo */
        vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
    }
}

/* ─────────────────────────────────────────────────────────────────
 * RX — Recepción y reensamblado
 * Solo se compila en modo RX.
 * ───────────────────────────────────────────────────────────────── */
#ifdef LORA_IMAGE_MODE_RX

/*
 * Vuelca la imagen completa por UART usando el protocolo
 * IMG_START / IMG_END que lee el script Python.
 *
 * Formato: un entero por línea (0–255), fila a fila, columna a columna.
 * El script Python busca exactamente estas cadenas como delimitadores.
 */
static void dump_image_uart(const uint8_t *buf)
{
    printf("IMG_START\n");
    for (int i = 0; i < IMG_SIZE; i++) {
        printf("%d\n", buf[i]);
    }
    printf("IMG_END\n");
    fflush(stdout);

    /* También volcamos el CSV para verificación manual en el monitor */
    image_print_csv(buf, "IMAGEN RX");
}

/*
 * Resetea el estado del receptor para la siguiente imagen.
 * Se llama al recibir un chunk_idx==0 (nueva imagen) o
 * después de ensamblar una imagen completa.
 */
static void rx_reset(void)
{
    memset(s_rx_buf, 0, IMG_SIZE);
    memset(s_chunk_received, 0, sizeof(s_chunk_received));
    s_total_chunks_expected = 0;
}

/*
 * Verifica si se recibieron todos los chunks esperados.
 */
static bool rx_is_complete(void)
{
    if (s_total_chunks_expected == 0) return false;
    for (int i = 0; i < s_total_chunks_expected; i++) {
        if (!s_chunk_received[i]) return false;
    }
    return true;
}

/*
 * Procesa un paquete recibido: valida el header, copia el payload
 * en la posición correcta del buffer y verifica si la imagen está completa.
 *
 * Retorna true si la imagen quedó completa con este chunk.
 */
static bool rx_process_packet(const uint8_t *pkt, uint8_t pkt_len,
                               int16_t rssi, int8_t snr)
{
    /* Validar largo mínimo */
    if (pkt_len < 5) {
        ESP_LOGW(TAG, "RX: paquete demasiado corto (%d bytes)", pkt_len);
        return false;
    }

    uint8_t  chunk_idx    = pkt[0];
    uint8_t  total_chunks = pkt[1];
    uint16_t payload_len  = ((uint16_t)pkt[2] << 8) | pkt[3];
    const uint8_t *data   = pkt + 4;

    /* Validaciones de header */
    if (total_chunks == 0 || total_chunks > MAX_CHUNKS) {
        ESP_LOGW(TAG, "RX: total_chunks inválido: %d", total_chunks);
        return false;
    }
    if (chunk_idx >= total_chunks) {
        ESP_LOGW(TAG, "RX: chunk_idx %d >= total %d", chunk_idx, total_chunks);
        return false;
    }
    if (payload_len == 0 || payload_len > CHUNK_PAYLOAD) {
        ESP_LOGW(TAG, "RX: payload_len inválido: %d", payload_len);
        return false;
    }

    /*
     * Si es el primer chunk de una nueva imagen:
     * resetear el estado de recepción.
     * Si no es el primero y aún no recibimos el chunk 0:
     * descartamos porque no sabemos cuántos chunks esperar.
     */
    if (chunk_idx == 0) {
        if (s_total_chunks_expected != 0) {
            ESP_LOGW(TAG, "RX: nueva imagen antes de completar la anterior — reseteando");
        }
        rx_reset();
        s_total_chunks_expected = total_chunks;
    } else if (s_total_chunks_expected == 0) {
        ESP_LOGW(TAG, "RX: chunk %d recibido sin chunk 0 previo — descartado", chunk_idx);
        return false;
    }

    /* Verificar que el payload no se sale del buffer */
    uint16_t offset = (uint16_t)chunk_idx * CHUNK_PAYLOAD;
    if (offset + payload_len > IMG_SIZE) {
        ESP_LOGW(TAG, "RX: overflow — offset %d + len %d > %d",
                 offset, payload_len, IMG_SIZE);
        return false;
    }

    /* Copiar datos al buffer de imagen */
    memcpy(s_rx_buf + offset, data, payload_len);
    s_chunk_received[chunk_idx] = true;

    ESP_LOGI(TAG, "  Chunk %d/%d OK  payload=%d bytes  RSSI=%d dBm  SNR=%d dB",
             chunk_idx + 1, total_chunks, payload_len, rssi, snr);

    /* Mostrar chunks faltantes si la imagen no está completa */
    if (!rx_is_complete()) {
        char missing[64];
        int  pos = snprintf(missing, sizeof(missing), "  Faltantes: ");
        for (int i = 0; i < s_total_chunks_expected; i++) {
            if (!s_chunk_received[i]) {
                pos += snprintf(missing + pos, sizeof(missing) - pos, "%d ", i);
            }
        }
        ESP_LOGI(TAG, "%s", missing);
        return false;
    }

    return true;
}

/*
 * Task RX — espera paquetes en loop, reensambla y vuelca.
 */
#define RX_TIMEOUT_MS  15000   /* 15 s por chunk — más que TX_INTERVAL + tiempo TX */

static void lora_image_rx_task(void *arg)
{
    sx1276_t *radio  = (sx1276_t *)arg;
    int img_count    = 0;
    uint8_t rx_data[PACKET_SIZE];
    uint8_t rx_len   = 0;
    int16_t rssi     = 0;
    int8_t  snr      = 0;

    rx_reset();
    ESP_LOGI(TAG, "RX listo — esperando imagen en %.0f MHz SF%d BW%d kHz",
             (float)LORA_CFG.frequency_hz / 1e6,
             LORA_CFG.spreading_factor,
             LORA_CFG.bandwidth_khz);

    /* Mostrar pantalla de espera inicial */
    display_show_lora_waiting();

    while (1) {
        esp_err_t err = sx1276_receive(radio, rx_data, &rx_len,
                                       &rssi, &snr, RX_TIMEOUT_MS);

        if (err == ESP_OK) {
            bool complete = rx_process_packet(rx_data, rx_len, rssi, snr);

            if (complete) {
                img_count++;
                ESP_LOGI(TAG, "━━━ Imagen #%d completa — volcando por UART ━━━", img_count);
                dump_image_uart(s_rx_buf);
                /* Renderizar imagen en OLED con estado de integridad */
                display_show_lora_rx_image(s_rx_buf, img_count, rssi, snr,
                                           s_total_chunks_expected,
                                           s_total_chunks_expected);
                rx_reset();
            }

        } else if (err == SX1276_ERR_TIMEOUT) {
            /*
             * Timeout normal si el TX todavía no empezó o está en la pausa
             * entre transmisiones. No es un error — seguimos esperando.
             */
            ESP_LOGD(TAG, "RX timeout — sin señal, reintentando...");

        } else if (err == SX1276_ERR_CRC) {
            /*
             * El paquete llegó pero con error de CRC: corrupción en el canal.
             * Incrementar un contador de errores sería útil en pruebas de
             * largo alcance. Por ahora solo logueamos y seguimos.
             */
            ESP_LOGW(TAG, "RX: error de CRC — paquete descartado");

        } else {
            ESP_LOGE(TAG, "RX: error inesperado: %s", esp_err_to_name(err));
        }
    }
}

#endif /* LORA_IMAGE_MODE_RX */

/* ─────────────────────────────────────────────────────────────────
 * Handle estático del radio — vive durante toda la ejecución
 * ───────────────────────────────────────────────────────────────── */
static sx1276_t s_radio;

/* ─────────────────────────────────────────────────────────────────
 * lora_image_test_start — punto de entrada público
 * ───────────────────────────────────────────────────────────────── */
void lora_image_test_start(void)
{
#ifdef LORA_IMAGE_MODE_TX
    ESP_LOGI(TAG, "Modo: TRANSMISOR (TX)");
#else
    ESP_LOGI(TAG, "Modo: RECEPTOR (RX)");
#endif

    /*
     * Orden de inicialización:
     *   1. Radio primero — sx1276_init() hace un reset por hardware (GPIO 14
     *      bajo 10 ms) que puede generar un glitch en Vext y apagar la OLED
     *      si el display ya estaba encendido.
     *   2. Display después — una vez que Vext se estabilizó post-reset,
     *      inicializamos la OLED y mostramos el estado.
     */

    /* 1. Inicializar el radio */
    esp_err_t err = sx1276_init(&s_radio, &LORA_CFG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sx1276_init falló: %s — abortando", esp_err_to_name(err));
        return;
    }

    /* Pausa para que Vext se estabilice completamente post-reset del SX1276 */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 2. Inicializar el display */
    esp_err_t disp_err = display_init();
    if (disp_err != ESP_OK) {
        ESP_LOGW(TAG, "display_init falló: %s — continuando sin OLED",
                 esp_err_to_name(disp_err));
    } else {
        display_show_boot();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /*
     * Lanzar la task FreeRTOS correspondiente al modo.
     *
     * Stack de 4096 bytes: suficiente para los buffers estáticos más
     * el overhead de las funciones de log y SPI.
     * Prioridad 5: por encima de la idle task (0) y de la mayoría de
     * tareas del sistema, pero sin bloquear el watchdog (prioridad 1).
     */
#ifdef LORA_IMAGE_MODE_TX
    xTaskCreate(lora_image_tx_task, "lora_tx", 4096, &s_radio, 5, NULL);
#else
    xTaskCreate(lora_image_rx_task, "lora_rx", 4096, &s_radio, 5, NULL);
#endif
}
