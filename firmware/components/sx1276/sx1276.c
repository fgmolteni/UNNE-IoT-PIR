/*
 * sx1276.c — Driver SX1276 en C puro para ESP-IDF
 *
 * Comunicación: SPI full-duplex, modo 0 (CPOL=0, CPHA=0), MSB primero.
 * Protocolo de registro:
 *   Escritura: byte 0 = 0x80 | addr,  byte 1 = valor
 *   Lectura:   byte 0 = 0x00 | addr,  byte 1 = 0x00 (dummy), respuesta en byte 1
 */

#include "sx1276.h"
#include "board_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sx1276";

/* ─────────────────────────────────────────────────────────────────
 * Mapa de registros del SX1276 (LoRa mode)
 * Solo los que usamos — el datasheet completo tiene ~112 registros.
 * ───────────────────────────────────────────────────────────────── */
#define REG_FIFO              0x00  /* FIFO de datos TX/RX                      */
#define REG_OP_MODE           0x01  /* Modo de operación (Sleep/Standby/TX/RX)  */
#define REG_FRF_MSB           0x06  /* Frecuencia byte alto                     */
#define REG_FRF_MID           0x07  /* Frecuencia byte medio                    */
#define REG_FRF_LSB           0x08  /* Frecuencia byte bajo                     */
#define REG_PA_CONFIG         0x09  /* Config amplificador de potencia           */
#define REG_OCP               0x0B  /* Over-current protection                  */
#define REG_LNA               0x0C  /* Config LNA (amplif. bajo ruido en RX)    */
#define REG_FIFO_ADDR_PTR     0x0D  /* Puntero de lectura/escritura en FIFO     */
#define REG_FIFO_TX_BASE_ADDR 0x0E  /* Dirección base TX en FIFO                */
#define REG_FIFO_RX_BASE_ADDR 0x0F  /* Dirección base RX en FIFO                */
#define REG_FIFO_RX_CURR_ADDR 0x10  /* Dirección del último paquete recibido    */
#define REG_IRQ_FLAGS         0x12  /* Flags de interrupción                    */
#define REG_RX_NB_BYTES       0x13  /* Bytes recibidos en el último paquete     */
#define REG_PKT_SNR_VALUE     0x19  /* SNR del último paquete (Q0.25 dB)        */
#define REG_PKT_RSSI_VALUE    0x1A  /* RSSI del último paquete                  */
#define REG_MODEM_CONFIG1     0x1D  /* BW + CR + cabecera implícita/explícita   */
#define REG_MODEM_CONFIG2     0x1E  /* SF + CRC                                 */
#define REG_SYMB_TIMEOUT_LSB  0x1F  /* Timeout en símbolos para RxSingle        */
#define REG_PREAMBLE_MSB      0x20  /* Largo del preámbulo (byte alto)          */
#define REG_PREAMBLE_LSB      0x21  /* Largo del preámbulo (byte bajo)          */
#define REG_PAYLOAD_LENGTH    0x22  /* Largo del payload (modo cabecera implíc.)*/
#define REG_MODEM_CONFIG3     0x26  /* LDO, AGC automático                      */
#define REG_FREQ_ERROR_MSB    0x28  /* Error de frecuencia                      */
#define REG_RSSI_WIDEBAND     0x2C  /* RSSI de banda ancha                      */
#define REG_DETECTION_OPTIM   0x31  /* Optimización para SF6                    */
#define REG_DETECTION_THRESH  0x37  /* Umbral de detección para SF6             */
#define REG_SYNC_WORD         0x39  /* Sync word LoRa                           */
#define REG_DIO_MAPPING1      0x40  /* Mapeo de pines DIO0–DIO3                 */
#define REG_VERSION           0x42  /* Versión del chip: 0x12 para SX1276       */
#define REG_PA_DAC            0x4D  /* Config DAC del PA                        */

/* ── Valores de RegOpMode ── */
#define MODE_LONG_RANGE       0x80  /* Bit 7 = 1 → LoRa (no FSK)               */
#define MODE_SLEEP            0x00
#define MODE_STDBY            0x01
#define MODE_TX               0x03
#define MODE_RX_CONTINUOUS    0x05
#define MODE_RX_SINGLE        0x06

/* ── Bits de RegIrqFlags ── */
#define IRQ_TX_DONE           0x08
#define IRQ_PAYLOAD_CRC_ERROR 0x20
#define IRQ_RX_DONE           0x40

/* ── Versión esperada del chip ── */
#define SX1276_VERSION        0x12

/* ── Velocidad SPI: 10 MHz es seguro y más que suficiente ── */
#define SPI_CLOCK_HZ          (10 * 1000 * 1000)

/* ─────────────────────────────────────────────────────────────────
 * Operaciones SPI primitivas
 *
 * spi_write_reg: escribe un byte en un registro.
 * spi_read_reg:  lee un byte de un registro.
 *
 * El protocolo es siempre de 2 bytes:
 *   TX[0] = dirección (con bit 7 = W/R)
 *   TX[1] = dato (escritura) o 0x00 (lectura, dummy)
 *   RX[1] = dato leído (solo para lectura)
 * ───────────────────────────────────────────────────────────────── */
static esp_err_t spi_write_reg(spi_device_handle_t spi, uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { 0x80 | addr, val };
    spi_transaction_t t = {
        .length    = 16,        /* bits totales: 2 bytes × 8 = 16 */
        .tx_buffer = tx,
        .rx_buffer = NULL,      /* no nos importa lo que devuelve en escritura */
    };
    return spi_device_transmit(spi, &t);
}

static esp_err_t spi_read_reg(spi_device_handle_t spi, uint8_t addr, uint8_t *val)
{
    uint8_t tx[2] = { 0x00 | addr, 0x00 };
    uint8_t rx[2] = { 0x00, 0x00 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(spi, &t);
    if (err == ESP_OK) {
        *val = rx[1];   /* el dato leído está en el segundo byte de respuesta */
    }
    return err;
}

/*
 * spi_write_burst — escribe un bloque de bytes a la FIFO (addr=0x00).
 *
 * La FIFO del SX1276 se escribe con una transacción SPI donde el primer
 * byte es la dirección de escritura (0x80) y los siguientes son los datos.
 * No hace falta mandar la dirección antes de cada byte — el puntero de
 * la FIFO se incrementa automáticamente con cada byte de la transacción.
 */
static esp_err_t spi_write_burst(spi_device_handle_t spi, uint8_t addr,
                                 const uint8_t *data, uint8_t len)
{
    /* Buffer temporal: 1 byte de dirección + hasta 255 bytes de datos */
    uint8_t buf[256];
    buf[0] = 0x80 | addr;
    memcpy(buf + 1, data, len);

    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = buf,
        .rx_buffer = NULL,
    };
    return spi_device_transmit(spi, &t);
}

/*
 * spi_read_burst — lee un bloque de bytes de la FIFO.
 */
/* Buffers estáticos para burst — evita 512 bytes en el stack por llamada */
static uint8_t s_burst_tx[256];
static uint8_t s_burst_rx[256];

static esp_err_t spi_read_burst(spi_device_handle_t spi, uint8_t addr,
                                uint8_t *data, uint8_t len)
{
    memset(s_burst_tx, 0, len + 1);
    memset(s_burst_rx, 0, len + 1);
    uint8_t *tx = s_burst_tx;
    uint8_t *rx = s_burst_rx;
    tx[0] = 0x00 | addr;

    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(spi, &t);
    if (err == ESP_OK) {
        memcpy(data, rx + 1, len);  /* saltamos el primer byte (echo de dirección) */
    }
    return err;
}


/* ─────────────────────────────────────────────────────────────────
 * Helpers de modo
 * ───────────────────────────────────────────────────────────────── */
static esp_err_t set_mode(sx1276_t *dev, uint8_t mode)
{
    /*
     * El bit 7 de RegOpMode debe estar siempre en 1 para mantenernos
     * en modo LoRa (si se pone en 0, el chip cambia a FSK/OOK).
     */
    return spi_write_reg(dev->spi, REG_OP_MODE, MODE_LONG_RANGE | mode);
}

/* ─────────────────────────────────────────────────────────────────
 * Reset por hardware
 *
 * Según el datasheet del SX1276 (sección 7.2):
 *   - Poner RST en bajo mínimo 100 µs
 *   - Esperar 5 ms después de soltar RST antes de usar el chip
 * Usamos 10 ms en bajo y 10 ms de espera para tener margen.
 * ───────────────────────────────────────────────────────────────── */
static esp_err_t hardware_reset(gpio_num_t pin_rst)
{
    ESP_ERROR_CHECK(gpio_reset_pin(pin_rst));
    ESP_ERROR_CHECK(gpio_set_direction(pin_rst, GPIO_MODE_OUTPUT));

    gpio_set_level(pin_rst, 0);
    esp_rom_delay_us(10000);   /* 10 ms en bajo */
    gpio_set_level(pin_rst, 1);
    esp_rom_delay_us(10000);   /* 10 ms de espera post-reset */

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * Conversión de parámetros a valores de registro
 * ───────────────────────────────────────────────────────────────── */

/*
 * Frecuencia → 3 bytes Frf (Msb, Mid, Lsb)
 *
 * Fórmula del datasheet:
 *   Frf = frequency_hz / (32e6 / 2^19)
 *       = frequency_hz × 524288 / 32000000
 *
 * Con 32 MHz de XTAL y divisor de síntesis de 2^19 = 524288.
 * La resolución es 32e6/2^19 ≈ 61 Hz por paso.
 */
static uint32_t freq_to_frf(uint32_t frequency_hz)
{
    /* Usamos aritmética de 64 bits para no perder precisión */
    return (uint32_t)(((uint64_t)frequency_hz << 19) / 32000000UL);
}

/*
 * BW en kHz → bits [7:4] de RegModemConfig1
 *
 * El SX1276 codifica el BW en 4 bits según esta tabla del datasheet:
 */
static uint8_t bw_to_bits(uint8_t bw_khz)
{
    switch (bw_khz) {
        case 7:   return 0x00;
        case 10:  return 0x10;
        case 15:  return 0x20;
        case 20:  return 0x30;
        case 31:  return 0x40;
        case 41:  return 0x50;
        case 62:  return 0x60;
        case 125: return 0x70;
        case 250: return 0x80;
        case 500: return 0x90;
        default:
            ESP_LOGW(TAG, "BW %d kHz no válido, usando 125 kHz", bw_khz);
            return 0x70;
    }
}

/*
 * Coding Rate → bits [3:1] de RegModemConfig1
 *
 * El valor que pasamos (5,6,7,8) representa el denominador de 4/x.
 * La codificación del registro es CR-4 en los bits [3:1]:
 *   CR 4/5 → 0x02
 *   CR 4/6 → 0x04
 *   CR 4/7 → 0x06
 *   CR 4/8 → 0x08
 */
static uint8_t cr_to_bits(uint8_t cr)
{
    if (cr < 5 || cr > 8) cr = 7;
    return (uint8_t)((cr - 4) << 1);
}

/* ─────────────────────────────────────────────────────────────────
 * sx1276_init
 * ───────────────────────────────────────────────────────────────── */
esp_err_t sx1276_init(sx1276_t *dev, const sx1276_config_t *config)
{
    esp_err_t err;

    /* Guardar config en el handle para reuso en TX/RX */
    dev->config   = *config;
    dev->pin_rst  = BOARD_PIN_LORA_RST;
    dev->pin_dio0 = BOARD_PIN_LORA_DIO0;

    /* ── 1. Reset hardware ── */
    ESP_LOGI(TAG, "Reset hardware SX1276...");
    ESP_ERROR_CHECK(hardware_reset(dev->pin_rst));

    /* ── 2. Configurar DIO0 como entrada (polling, no interrupción) ── */
    gpio_reset_pin(dev->pin_dio0);
    gpio_set_direction(dev->pin_dio0, GPIO_MODE_INPUT);

    /* ── 3. Inicializar bus SPI2 ── */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = BOARD_PIN_LORA_MOSI,
        .miso_io_num   = BOARD_PIN_LORA_MISO,
        .sclk_io_num   = BOARD_PIN_LORA_SCK,
        .quadwp_io_num = -1,    /* no se usa QSPI */
        .quadhd_io_num = -1,
        .max_transfer_sz = 256, /* máximo payload SX1276 + 1 byte de dirección */
    };

    err = spi_bus_initialize(BOARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        /*
         * ESP_ERR_INVALID_STATE significa que el bus ya fue inicializado
         * (ej: por un init anterior). No es un error real.
         */
        ESP_LOGE(TAG, "spi_bus_initialize falló: %s", esp_err_to_name(err));
        return err;
    }

    /* ── 4. Agregar SX1276 como dispositivo SPI ── */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode           = 0,              /* CPOL=0, CPHA=0 */
        .spics_io_num   = BOARD_PIN_LORA_NSS,
        .queue_size     = 1,
        .pre_cb         = NULL,
        .post_cb        = NULL,
    };

    err = spi_bus_add_device(BOARD_SPI_HOST, &dev_cfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device falló: %s", esp_err_to_name(err));
        return err;
    }

    /* ── 5. Verificar versión del chip ── */
    uint8_t version = 0;
    ESP_ERROR_CHECK(spi_read_reg(dev->spi, REG_VERSION, &version));
    if (version != SX1276_VERSION) {
        ESP_LOGE(TAG, "SX1276 no responde. RegVersion=0x%02X (esperado 0x%02X)",
                 version, SX1276_VERSION);
        ESP_LOGE(TAG, "Verificá conexiones SPI: SCK=%d MOSI=%d MISO=%d NSS=%d",
                 BOARD_PIN_LORA_SCK, BOARD_PIN_LORA_MOSI,
                 BOARD_PIN_LORA_MISO, BOARD_PIN_LORA_NSS);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "SX1276 detectado (RegVersion=0x%02X)", version);

    /* ── 6. Poner en Sleep para poder cambiar a modo LoRa ──
     *
     * El bit MODE_LONG_RANGE solo se puede cambiar en modo Sleep.
     * Si intentamos ponerlo en Standby directamente, el chip lo ignora.
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_OP_MODE,
                                  MODE_LONG_RANGE | MODE_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ── 7. Configurar frecuencia ── */
    uint32_t frf = freq_to_frf(config->frequency_hz);
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FRF_MSB, (frf >> 16) & 0xFF));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FRF_MID, (frf >>  8) & 0xFF));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FRF_LSB, (frf      ) & 0xFF));
    ESP_LOGI(TAG, "Frecuencia: %u Hz (Frf=0x%06X)", (unsigned)config->frequency_hz, (unsigned)frf);

    /* ── 8. Potencia TX ──
     *
     * El Heltec V2 usa el pin PA_BOOST del SX1276 (no el pin RFO).
     * Con PA_BOOST:
     *   RegPaConfig = 0x80 | (potencia - 2)
     *   El bit 7 = 1 selecciona PA_BOOST.
     *   Los bits [3:0] van de 0 a 15, que representan 2 a 17 dBm.
     *
     * Para 17 dBm: 0x80 | (17-2) = 0x80 | 0x0F = 0x8F
     */
    int8_t pwr = config->tx_power_dbm;
    if (pwr < 2)  pwr = 2;
    if (pwr > 17) pwr = 17;
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_PA_CONFIG,
                                  0x80 | (uint8_t)(pwr - 2)));
    ESP_LOGI(TAG, "Potencia TX: %d dBm", pwr);

    /* ── 9. RegModemConfig1: BW + CR + cabecera explícita ──
     *
     * Bits [7:4] = BW
     * Bits [3:1] = CR
     * Bit  [0]   = 0 → cabecera explícita (incluye largo del payload en el paquete)
     *              1 → cabecera implícita (largo fijo, configurado en REG_PAYLOAD_LENGTH)
     * Usamos cabecera explícita para mayor flexibilidad.
     */
    uint8_t mcfg1 = bw_to_bits(config->bandwidth_khz) |
                    cr_to_bits(config->coding_rate)    |
                    0x00; /* cabecera explícita */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_MODEM_CONFIG1, mcfg1));

    /* ── 10. RegModemConfig2: SF + CRC habilitado ──
     *
     * Bits [7:4] = SF (6–12)
     * Bit  [2]   = 1 → CRC habilitado (el receptor verifica integridad)
     * Bit  [1:0] = timeout MSB para RxSingle (lo configuramos aparte)
     */
    uint8_t mcfg2 = (uint8_t)(config->spreading_factor << 4) | 0x04; /* CRC on */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_MODEM_CONFIG2, mcfg2));

    /* ── 11. RegModemConfig3: LDO + AGC automático ──
     *
     * Para SF >= 10 con BW 125 kHz, el símbolo es lo suficientemente largo
     * como para necesitar el Low Data Rate Optimize (LDO).
     * El datasheet dice: si T_symbol > 16 ms → activar LDO.
     * Con SF10/BW125: T_symbol = 2^10 / 125000 = 8.19 ms → no obligatorio,
     * pero lo activamos igual como práctica segura (bit 3 = 1).
     * Bit 2 = 1 → AGC automático (el chip ajusta la ganancia del LNA solo).
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_MODEM_CONFIG3, 0x0C));

    /* ── 12. Preámbulo ──
     *
     * Largo estándar de 8 símbolos. El receptor necesita al menos 6 para
     * sincronizar. Usamos 8 como mínimo recomendado.
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_PREAMBLE_MSB, 0x00));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_PREAMBLE_LSB, 0x08));

    /* ── 13. Sync word ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_SYNC_WORD, config->sync_word));

    /* ── 14. Optimizaciones para detección ──
     *
     * Para SF > 6 (nuestro caso), estos registros tienen valores fijos
     * definidos en el datasheet (tabla 42).
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_DETECTION_OPTIM,  0xC3));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_DETECTION_THRESH, 0x0A));

    /* ── 15. Over-current protection: 100 mA ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_OCP, 0x2B));

    /* ── 16. LNA: máxima ganancia + boost de 150% en corriente ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_LNA, 0x23));

    /* ── 17. Fijar direcciones base de FIFO ──
     *
     * La FIFO del SX1276 es de 256 bytes compartida entre TX y RX.
     * Ponemos ambas bases en 0x00: TX usa los primeros bytes,
     * RX va leyendo desde donde empieza el paquete recibido.
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FIFO_TX_BASE_ADDR, 0x00));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FIFO_RX_BASE_ADDR, 0x00));

    /* ── 18. Pasar a Standby (listo para usar) ── */
    ESP_ERROR_CHECK(set_mode(dev, MODE_STDBY));
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "SX1276 inicializado OK — SF%d BW%dkHz CR4/%d sync=0x%02X",
             config->spreading_factor,
             config->bandwidth_khz,
             config->coding_rate,
             config->sync_word);

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * sx1276_transmit
 * ───────────────────────────────────────────────────────────────── */
esp_err_t sx1276_transmit(sx1276_t *dev, const uint8_t *data, uint8_t len)
{
    esp_err_t err;

    if (len == 0 || len > 255) {
        ESP_LOGE(TAG, "TX: largo inválido %d (debe ser 1–255)", len);
        return ESP_ERR_INVALID_ARG;
    }

    /* ── 1. Standby ── */
    ESP_ERROR_CHECK(set_mode(dev, MODE_STDBY));

    /* ── 2. Limpiar flags de IRQ ──
     *
     * Escribiendo 0xFF en RegIrqFlags se limpian todos los bits pendientes.
     * Importante hacerlo antes de TX para no leer un TxDone viejo.
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_IRQ_FLAGS, 0xFF));

    /* ── 3. DIO0 → TxDone ──
     *
     * RegDioMapping1 bits [7:6] = 01 → DIO0 indica TxDone
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_DIO_MAPPING1, 0x40));

    /* ── 4. Apuntar FIFO al inicio y cargar datos ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FIFO_ADDR_PTR, 0x00));
    ESP_ERROR_CHECK(spi_write_burst(dev->spi, REG_FIFO, data, len));

    /* ── 5. Configurar largo del payload ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_PAYLOAD_LENGTH, len));

    /* ── 6. Activar modo TX ── */
    ESP_ERROR_CHECK(set_mode(dev, MODE_TX));

    /* ── 7. Esperar TxDone por polling en DIO0 ──
     *
     * DIO0 se pone en alto cuando TxDone está en RegIrqFlags.
     * Timeout de 5 segundos: para SF10/BW125 un paquete de 204 bytes
     * tarda ~370 ms, así que 5 s es más que suficiente.
     */
    int64_t t_start = esp_timer_get_time();
    int64_t timeout_us = 5000000LL; /* 5 segundos en µs */

    while (!gpio_get_level(dev->pin_dio0)) {
        if ((esp_timer_get_time() - t_start) > timeout_us) {
            ESP_LOGE(TAG, "TX timeout — DIO0 nunca se puso en alto");
            set_mode(dev, MODE_STDBY);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* ── 8. Verificar que TxDone está en el registro (doble check) ── */
    uint8_t irq = 0;
    err = spi_read_reg(dev->spi, REG_IRQ_FLAGS, &irq);
    if (err != ESP_OK || !(irq & IRQ_TX_DONE)) {
        ESP_LOGE(TAG, "TX: TxDone no está en IrqFlags (0x%02X)", irq);
        set_mode(dev, MODE_STDBY);
        return ESP_FAIL;
    }

    /* ── 9. Limpiar flags y volver a Standby ── */
    spi_write_reg(dev->spi, REG_IRQ_FLAGS, 0xFF);
    set_mode(dev, MODE_STDBY);

    int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
    ESP_LOGI(TAG, "TX OK — %d bytes en %lld ms", len, (long long)elapsed_ms);

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * sx1276_receive
 * ───────────────────────────────────────────────────────────────── */
esp_err_t sx1276_receive(sx1276_t *dev,
                         uint8_t  *buf,
                         uint8_t  *len,
                         int16_t  *rssi,
                         int8_t   *snr,
                         uint32_t  timeout_ms)
{
    /* ── 1. Limpiar flags y configurar DIO0 → RxDone ──
     *
     * RegDioMapping1 bits [7:6] = 00 → DIO0 indica RxDone
     */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_IRQ_FLAGS, 0xFF));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_DIO_MAPPING1, 0x00));

    /* ── 2. Apuntar FIFO a la base RX ── */
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FIFO_ADDR_PTR,
                                  0x00)); /* la base RX es 0x00 */

    /* ── 3. Activar modo RxSingle ──
     *
     * RxSingle: el chip escucha hasta recibir un paquete completo,
     * luego vuelve automáticamente a Standby.
     * RxContinuous: escucha indefinidamente (útil para gateway, más consumo).
     * Para esta prueba usamos RxSingle.
     */
    ESP_ERROR_CHECK(set_mode(dev, MODE_RX_SINGLE));

    /* ── 4. Esperar RxDone en DIO0 ── */
    int64_t t_start  = esp_timer_get_time();
    int64_t t_limit  = (timeout_ms == 0)
                       ? INT64_MAX
                       : t_start + (int64_t)timeout_ms * 1000;

    while (!gpio_get_level(dev->pin_dio0)) {
        if (esp_timer_get_time() > t_limit) {
            set_mode(dev, MODE_STDBY);
            return SX1276_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* ── 5. Leer flags de IRQ ── */
    uint8_t irq = 0;
    ESP_ERROR_CHECK(spi_read_reg(dev->spi, REG_IRQ_FLAGS, &irq));
    spi_write_reg(dev->spi, REG_IRQ_FLAGS, 0xFF); /* limpiar */

    if (irq & IRQ_PAYLOAD_CRC_ERROR) {
        ESP_LOGW(TAG, "RX: error de CRC (IrqFlags=0x%02X)", irq);
        set_mode(dev, MODE_STDBY);
        return SX1276_ERR_CRC;
    }

    if (!(irq & IRQ_RX_DONE)) {
        ESP_LOGW(TAG, "RX: DIO0 alto pero RxDone no está en IrqFlags (0x%02X)", irq);
        set_mode(dev, MODE_STDBY);
        return ESP_FAIL;
    }

    /* ── 6. Leer largo del payload recibido ── */
    uint8_t rx_len = 0;
    ESP_ERROR_CHECK(spi_read_reg(dev->spi, REG_RX_NB_BYTES, &rx_len));

    /* ── 7. Apuntar FIFO a donde empieza el paquete recibido ── */
    uint8_t fifo_rx_addr = 0;
    ESP_ERROR_CHECK(spi_read_reg(dev->spi, REG_FIFO_RX_CURR_ADDR, &fifo_rx_addr));
    ESP_ERROR_CHECK(spi_write_reg(dev->spi, REG_FIFO_ADDR_PTR, fifo_rx_addr));

    /* ── 8. Leer datos de la FIFO ── */
    ESP_ERROR_CHECK(spi_read_burst(dev->spi, REG_FIFO, buf, rx_len));
    *len = rx_len;

    /* ── 9. RSSI y SNR ──
     *
     * SNR: el registro guarda Q0.25 dB (entero con signo dividido por 4).
     *   snr_dB = (int8_t)RegPktSnrValue / 4
     *   Guardamos el valor crudo / 4 como int8_t.
     *
     * RSSI: fórmula del datasheet para 915 MHz (HF port):
     *   RSSI = -157 + RegPktRssiValue
     *   Para 433 MHz sería -164 + RegPktRssiValue.
     */
    uint8_t raw_snr  = 0;
    uint8_t raw_rssi = 0;
    spi_read_reg(dev->spi, REG_PKT_SNR_VALUE,  &raw_snr);
    spi_read_reg(dev->spi, REG_PKT_RSSI_VALUE, &raw_rssi);

    *snr  = (int8_t)raw_snr / 4;
    *rssi = -157 + (int16_t)raw_rssi;

    set_mode(dev, MODE_STDBY);

    int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
    ESP_LOGI(TAG, "RX OK — %d bytes  RSSI=%d dBm  SNR=%d dB  en %lld ms",
             rx_len, *rssi, *snr, (long long)elapsed_ms);

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * sx1276_deinit
 * ───────────────────────────────────────────────────────────────── */
void sx1276_deinit(sx1276_t *dev)
{
    set_mode(dev, MODE_SLEEP);
    spi_bus_remove_device(dev->spi);
    spi_bus_free(BOARD_SPI_HOST);
    ESP_LOGI(TAG, "SX1276 liberado");
}
