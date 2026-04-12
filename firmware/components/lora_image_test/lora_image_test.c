/*
 * lora_image_test.c
 *
 * Prueba de transmisión de imagen 16×16 por LoRa.
 * Usa el driver sx1276 (C puro, ESP-IDF spi_master).
 *
 * Protocolo ACK con header extendido (6 bytes por paquete TX):
 *
 *   ┌────────────┬──────────────┬──────────────┬─────────────┬──────────┬──────────┬────────────────┐
 *   │ session_id │  chunk_idx   │ total_chunks │ payload_len │  rsv     │  crc8    │ datos imagen   │
 *   │  (1 byte)  │   (1 byte)   │   (1 byte)   │  (1 byte)   │ (1 byte) │ (1 byte) │ (≤200 bytes)  │
 *   └────────────┴──────────────┴──────────────┴─────────────┴──────────┴──────────┴────────────────┘
 *
 * Paquete ACK del RX (3 bytes): [0xAC | session_id | chunk_idx]
 *
 * Con IMG_SIZE=256 bytes y CHUNK_PAYLOAD=200 → 2 paquetes por imagen.
 * El TX espera ACK por cada chunk antes de enviar el siguiente.
 * Si no llega ACK en ACK_TIMEOUT_MS → reintenta hasta MAX_RETRIES.
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

/* ─────────────────────────────────────────────────────────────────
 * Protocolo ACK
 * ───────────────────────────────────────────────────────────────── */
#define HEADER_LEN_ACK   6        /* session | idx | total | len | rsv | crc8 */
#define ACK_LEN          3        /* 0xAC | session_id | chunk_idx            */
#define ACK_MAGIC        0xAC
#define ACK_TIMEOUT_MS   2000     /* ms esperando ACK antes de reintentar     */
#define MAX_RETRIES      5        /* reintentos por chunk antes de abortar    */
#define ACK_TX_DELAY_MS  60       /* pausa antes de transmitir ACK (RX→TX)   */

#define PACKET_SIZE    (HEADER_LEN_ACK + CHUNK_PAYLOAD)   /* header extendido + payload */

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
static uint8_t s_packet_buf[HEADER_LEN_ACK + CHUNK_PAYLOAD]; /* header extendido */
static uint8_t s_session_id = 0;   /* incrementa con cada imagen TX          */

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
 * CRC-8 (polinomio 0x07, Dallas/Maxim) — protege el payload
 * ───────────────────────────────────────────────────────────────── */
static uint8_t crc8_calc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

/* ─────────────────────────────────────────────────────────────────
 * wait_ack — espera ACK del RX con timeout.
 * Retorna true si llegó ACK válido (session y chunk coinciden).
 * ───────────────────────────────────────────────────────────────── */
static bool wait_ack(sx1276_t *radio, uint8_t session_id, uint8_t chunk_idx)
{
    uint8_t buf[16];
    uint8_t rx_len = 0;
    int16_t rssi   = 0;
    int8_t  snr    = 0;

    /* Usamos ACK_TIMEOUT_MS dividido en ventanas de 300 ms para
     * no bloquear el watchdog y poder inspeccionar paquetes basura */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ACK_TIMEOUT_MS);

    while (xTaskGetTickCount() < deadline) {
        esp_err_t err = sx1276_receive(radio, buf, &rx_len, &rssi, &snr, 300);

        if (err == ESP_OK && rx_len == ACK_LEN &&
            buf[0] == ACK_MAGIC &&
            buf[1] == session_id &&
            buf[2] == chunk_idx)
        {
            ESP_LOGI(TAG, "TX: ACK chunk=%d session=%d RSSI=%d dBm ✓",
                     chunk_idx, session_id, rssi);
            return true;
        }

        if (err == ESP_OK)
            ESP_LOGW(TAG, "TX: paquete extraño (%d bytes) esperando ACK", rx_len);
    }

    ESP_LOGW(TAG, "TX: timeout ACK chunk=%d session=%d", chunk_idx, session_id);
    return false;
}

/* ─────────────────────────────────────────────────────────────────
 * TX — Fragmentación y transmisión
 * ───────────────────────────────────────────────────────────────── */

/*
 * transmit_image — envía todos los chunks con confirmación ACK.
 *
 * Header extendido (6 bytes):
 *   [0] session_id   — ID de esta imagen (evita chunks de imagen anterior)
 *   [1] chunk_idx
 *   [2] total_chunks
 *   [3] payload_len  (≤ CHUNK_PAYLOAD, siempre < 256)
 *   [4] 0x00         — reservado
 *   [5] crc8         — CRC8 del payload
 *
 * Por cada chunk: transmite → espera ACK → si no llega, reintenta
 * hasta MAX_RETRIES antes de abortar la imagen.
 */
static int transmit_image(sx1276_t *radio, const uint8_t *img, uint16_t img_size)
{
    uint8_t total = (uint8_t)((img_size + CHUNK_PAYLOAD - 1) / CHUNK_PAYLOAD);
    int     ok    = 0;

    s_session_id++;   /* nueva sesión por cada imagen */

    ESP_LOGI(TAG, "TX iniciando: session=%d  %d bytes → %d chunks",
             s_session_id, img_size, total);

    for (uint8_t i = 0; i < total; i++) {

        uint16_t offset = (uint16_t)i * CHUNK_PAYLOAD;
        uint16_t plen   = (img_size - offset < CHUNK_PAYLOAD)
                          ? (img_size - offset)
                          : CHUNK_PAYLOAD;

        uint8_t chk = crc8_calc(img + offset, plen);

        /* Construir paquete */
        s_packet_buf[0] = s_session_id;
        s_packet_buf[1] = i;
        s_packet_buf[2] = total;
        s_packet_buf[3] = (uint8_t)plen;
        s_packet_buf[4] = 0x00;    /* reservado */
        s_packet_buf[5] = chk;
        memcpy(s_packet_buf + HEADER_LEN_ACK, img + offset, plen);

        uint8_t pkt_len = HEADER_LEN_ACK + (uint8_t)plen;
        bool    ack_ok  = false;

        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {

            if (attempt > 0)
                ESP_LOGW(TAG, "  Reintento %d chunk=%d session=%d",
                         attempt, i, s_session_id);

            esp_err_t err = sx1276_transmit(radio, s_packet_buf, pkt_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "  sx1276_transmit error: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            ESP_LOGI(TAG, "  Chunk %d/%d enviado (session=%d plen=%d CRC=0x%02X)",
                     i + 1, total, s_session_id, (int)plen, chk);

            /* Pequeña pausa para que el RX procese y cambie a TX */
            vTaskDelay(pdMS_TO_TICKS(200));

            ack_ok = wait_ack(radio, s_session_id, i);
            if (ack_ok) { ok++; break; }
        }

        if (!ack_ok) {
            ESP_LOGE(TAG, "  Chunk %d falló tras %d intentos — abortando imagen",
                     i, MAX_RETRIES);
            break;
        }
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

/* session_id de la imagen en curso (RX) */
static uint8_t s_rx_session_id = 0;

/*
 * send_ack — transmite el ACK al TX.
 * 3 bytes: [0xAC | session_id | chunk_idx]
 */
static void send_ack(sx1276_t *radio, uint8_t session_id, uint8_t chunk_idx)
{
    uint8_t ack[ACK_LEN] = { ACK_MAGIC, session_id, chunk_idx };
    vTaskDelay(pdMS_TO_TICKS(ACK_TX_DELAY_MS));
    esp_err_t err = sx1276_transmit(radio, ack, ACK_LEN);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "  ACK enviado → session=%d chunk=%d ✓", session_id, chunk_idx);
    else
        ESP_LOGE(TAG, "  Error enviando ACK: %s", esp_err_to_name(err));
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
 * rx_process_packet — valida header extendido, CRC y session_id.
 *
 * Header esperado (6 bytes):
 *   [0] session_id
 *   [1] chunk_idx
 *   [2] total_chunks
 *   [3] payload_len
 *   [4] reservado
 *   [5] crc8
 *
 * Retorna true si la imagen quedó completa con este chunk.
 * El radio se pasa para poder enviar el ACK desde aquí.
 */
static bool rx_process_packet(sx1276_t *radio,
                               const uint8_t *pkt, uint8_t pkt_len,
                               int16_t rssi, int8_t snr)
{
    /* Validar largo mínimo para el header extendido */
    if (pkt_len < HEADER_LEN_ACK + 1) {
        ESP_LOGW(TAG, "RX: paquete muy corto (%d bytes) — descartado", pkt_len);
        return false;
    }

    /* ── Decodificar header extendido ── */
    uint8_t  session_id   = pkt[0];
    uint8_t  chunk_idx    = pkt[1];
    uint8_t  total_chunks = pkt[2];
    uint8_t  payload_len  = pkt[3];
    /* pkt[4] reservado */
    uint8_t  rx_crc       = pkt[5];
    const uint8_t *data   = pkt + HEADER_LEN_ACK;

    /* ── Validaciones básicas de header ── */
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
    if (pkt_len < HEADER_LEN_ACK + payload_len) {
        ESP_LOGW(TAG, "RX: paquete truncado (esperado=%d recibido=%d)",
                 HEADER_LEN_ACK + payload_len, pkt_len);
        return false;
    }

    /* ── Validar CRC8 ── */
    uint8_t calc_crc = crc8_calc(data, payload_len);
    if (calc_crc != rx_crc) {
        ESP_LOGE(TAG, "RX: CRC error chunk=%d session=%d "
                      "(rx=0x%02X calc=0x%02X) — no ACK",
                 chunk_idx, session_id, rx_crc, calc_crc);
        return false;   /* sin ACK → TX reintentará */
    }

    /* ── Detectar sesión nueva ── */
    if (chunk_idx == 0) {
        if (s_total_chunks_expected != 0)
            ESP_LOGW(TAG, "RX: nueva sesión=%d antes de completar sesión=%d",
                     session_id, s_rx_session_id);
        rx_reset();
        s_rx_session_id         = session_id;
        s_total_chunks_expected = total_chunks;
        ESP_LOGI(TAG, "RX: nueva sesión=%d, esperando %d chunks",
                 session_id, total_chunks);

    } else if (session_id != s_rx_session_id) {
        /* Chunk de sesión diferente y no es chunk 0 — descartar sin ACK */
        ESP_LOGW(TAG, "RX: chunk %d de sesión=%d pero activa sesión=%d — descartado",
                 chunk_idx, session_id, s_rx_session_id);
        return false;

    } else if (s_total_chunks_expected == 0) {
        ESP_LOGW(TAG, "RX: chunk %d recibido sin chunk 0 previo — descartado", chunk_idx);
        return false;
    }

    /* ── Chunk duplicado: re-enviar ACK sin re-copiar datos ── */
    if (s_chunk_received[chunk_idx]) {
        ESP_LOGW(TAG, "RX: chunk %d duplicado — re-enviando ACK", chunk_idx);
        send_ack(radio, session_id, chunk_idx);
        return false;
    }

    /* ── Verificar que el payload no desborde el buffer ── */
    uint16_t offset = (uint16_t)chunk_idx * CHUNK_PAYLOAD;
    if (offset + payload_len > IMG_SIZE) {
        ESP_LOGW(TAG, "RX: overflow offset=%d len=%d > %d", offset, payload_len, IMG_SIZE);
        return false;
    }

    /* ── Copiar datos y marcar chunk ── */
    memcpy(s_rx_buf + offset, data, payload_len);
    s_chunk_received[chunk_idx] = true;

    ESP_LOGI(TAG, "  Chunk %d/%d OK  session=%d payload=%d CRC=0x%02X  RSSI=%d SNR=%d",
             chunk_idx + 1, total_chunks, session_id,
             payload_len, rx_crc, rssi, snr);

    /* ── Enviar ACK ── */
    send_ack(radio, session_id, chunk_idx);

    /* ── ¿Imagen completa? ── */
    if (!rx_is_complete()) {
        char missing[64]; int pos = snprintf(missing, sizeof(missing), "  Faltantes: ");
        for (int i = 0; i < s_total_chunks_expected; i++)
            if (!s_chunk_received[i])
                pos += snprintf(missing + pos, sizeof(missing) - pos, "%d ", i);
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
            bool complete = rx_process_packet(radio, rx_data, rx_len, rssi, snr);

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
