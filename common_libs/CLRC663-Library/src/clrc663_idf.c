/*
 * clrc663_idf.c
 *
 * ESP-IDF port of the CLRC663 NFC frontend driver.
 *
 * Supports:
 *   - ISO 15693 UID read  (inventory, addressed)
 *   - ISO 15693 user-block read  (non-addressed, CMD 0x20)
 *   - ISO 15693 user-block write (non-addressed, CMD 0x21)
 *
 * Block reads and writes use NON-ADDRESSED mode (no UID in frame).
 * This is valid when only one tag is in the field and removes the
 * need for a prior UID read and per-command protocol re-initialisation,
 * which was the main source of intermittent timing errors.
 */

#include "clrc663_idf.h"
#include "mfrc630_def.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ISO15693 tag";

#define ISO15693_CMD_READ_SINGLE_BLOCK  0x20
#define ISO15693_CMD_WRITE_SINGLE_BLOCK 0x21

// Non-addressed: bit 1 = high data rate. No UID field.
#define ISO15693_FLAGS_NONADDRESSED     0x02

// Addressed: bit 1 = high data rate, bit 5 = address flag (UID follows).
#define ISO15693_FLAGS_ADDRESSED        0x22

#define RESPONSE_TIMEOUT_MS             50

// ── Internal helpers ──────────────────────────────────────────────────────────

static uint8_t read_reg(clrc663_t *dev, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) | 0x01), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_transmit(dev->spi, &t);
    return rx[1];
}

static void write_reg(clrc663_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(reg << 1), value };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    spi_device_transmit(dev->spi, &t);
}

static void flush_fifo(clrc663_t *dev)
{
    write_reg(dev, MFRC630_REG_FIFOCONTROL, 1 << 4);
}

static uint8_t fifo_length(clrc663_t *dev)
{
    return read_reg(dev, MFRC630_REG_FIFOLENGTH);
}

static void write_fifo(clrc663_t *dev, const uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
        write_reg(dev, MFRC630_REG_FIFODATA, data[i]);
}

static void read_fifo(clrc663_t *dev, uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
        buf[i] = read_reg(dev, MFRC630_REG_FIFODATA);
}

static void cmd_idle(clrc663_t *dev)
{
    write_reg(dev, MFRC630_REG_COMMAND, MFRC630_CMD_IDLE);
}

static void cmd_transceive(clrc663_t *dev, const uint8_t *data, uint8_t len)
{
    cmd_idle(dev);
    flush_fifo(dev);
    write_fifo(dev, data, len);
    write_reg(dev, MFRC630_REG_COMMAND, MFRC630_CMD_TRANSCEIVE);
}

static void clear_irqs(clrc663_t *dev)
{
    write_reg(dev, MFRC630_REG_IRQ0, (uint8_t)~(1 << 7));
    write_reg(dev, MFRC630_REG_IRQ1, (uint8_t)~(1 << 7));
}

static void apply_iso15693_protocol(clrc663_t *dev)
{
    flush_fifo(dev);
    write_reg(dev, MFRC630_REG_FIFODATA, MFRC630_PROTO_ISO15693_1_OF_4_SSC);
    write_reg(dev, MFRC630_REG_FIFODATA, MFRC630_PROTO_ISO15693_1_OF_4_SSC);
    write_reg(dev, MFRC630_REG_COMMAND,  MFRC630_CMD_LOADPROTOCOL);
    vTaskDelay(pdMS_TO_TICKS(5));

    const uint8_t recom[] = MFRC630_RECOM_15693_ID1_SSC26;
    for (uint8_t i = 0; i < sizeof(recom); i++)
        write_reg(dev, MFRC630_REG_DRVMOD + i, recom[i]);

    cmd_idle(dev);
    flush_fifo(dev);
    clear_irqs(dev);
}

static uint8_t wait_for_response(clrc663_t *dev)
{
    write_reg(dev, MFRC630_REG_IRQ0EN,
              MFRC630_IRQ0EN_RX_IRQEN | MFRC630_IRQ0EN_ERR_IRQEN);
    write_reg(dev, MFRC630_REG_IRQ1EN, MFRC630_IRQ1EN_TIMER1_IRQEN);

    write_reg(dev, MFRC630_REG_T1CONTROL,
              MFRC630_TCONTROL_CLK_211KHZ | MFRC630_TCONTROL_START_TX_END);
    write_reg(dev, MFRC630_REG_T1RELOADHI,     0x29);
    write_reg(dev, MFRC630_REG_T1RELOADLO,     0x8E);
    write_reg(dev, MFRC630_REG_T1COUNTERVALHI, 0x29);
    write_reg(dev, MFRC630_REG_T1COUNTERVALLO, 0x8E);

    uint8_t irq0 = 0, irq1 = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RESPONSE_TIMEOUT_MS);

    while (xTaskGetTickCount() < deadline) {
        irq0 = read_reg(dev, MFRC630_REG_IRQ0);
        irq1 = read_reg(dev, MFRC630_REG_IRQ1);
        if (irq0 & (MFRC630_IRQ0_RX_IRQ | MFRC630_IRQ0_ERR_IRQ)) break;
        if (irq1 & MFRC630_IRQ1_TIMER1_IRQ)                        break;
        vTaskDelay(1);
    }

    cmd_idle(dev);
    write_reg(dev, MFRC630_REG_IRQ0EN, MFRC630_IRQ0EN_CLEAR);
    write_reg(dev, MFRC630_REG_IRQ1EN, MFRC630_IRQ1EN_CLEAR);
    return irq0;
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t clrc663_init(spi_host_device_t host, gpio_num_t cs_pin, clrc663_t *dev)
{
    dev->cs_pin = cs_pin;
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode           = 0,
        .spics_io_num   = cs_pin,
        .queue_size     = 1,
    };
    esp_err_t ret = spi_bus_add_device(host, &devcfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    clrc663_reset(dev);
    return ESP_OK;
}

void clrc663_reset(clrc663_t *dev)
{
    write_reg(dev, MFRC630_REG_COMMAND, MFRC630_CMD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));
}

uint8_t clrc663_read_reg(clrc663_t *dev, uint8_t reg)               { return read_reg(dev, reg); }
void    clrc663_write_reg(clrc663_t *dev, uint8_t reg, uint8_t val) { write_reg(dev, reg, val); }

void clrc663_iso15693_init(clrc663_t *dev)
{
    apply_iso15693_protocol(dev);

    write_reg(dev, MFRC630_REG_T0CONTROL,   0x98);
    write_reg(dev, MFRC630_REG_T1CONTROL,   0x92);
    write_reg(dev, MFRC630_REG_T0RELOADHI,  0x18);
    write_reg(dev, MFRC630_REG_T0RELOADLO,  0x86);
    write_reg(dev, MFRC630_REG_T1RELOADHI,  0x00);
    write_reg(dev, MFRC630_REG_T1RELOADLO,  0x00);
    write_reg(dev, MFRC630_REG_FIFOCONTROL, 0x90);
    write_reg(dev, MFRC630_REG_WATERLEVEL,  0xFE);

    ESP_LOGI(TAG, "ISO 15693 protocol initialised");
}

// ── UID read (still uses addressed inventory, kept for v0.1 compatibility) ────

uint8_t clrc663_iso15693_read_uid(clrc663_t *dev, uint8_t *uid)
{
    apply_iso15693_protocol(dev);

    uint8_t cmd[4] = {
        MFRC630_ISO15693_FLAGS,
        MFRC630_ISO15693_INVENTORY,
        0x00, 0x00
    };
    cmd_transceive(dev, cmd, 4);
    clear_irqs(dev);

    uint8_t irq0 = wait_for_response(dev);
    if (irq0 & MFRC630_IRQ0_ERR_IRQ) {
        ESP_LOGW(TAG, "UID read: error IRQ (irq0=0x%02X)", irq0);
        return 0;
    }

    uint8_t flen = fifo_length(dev);
    if (flen < MFRC630_ISO15693_UID_LENGTH) {
        ESP_LOGW(TAG, "UID read: FIFO too short (%d bytes)", flen);
        return 0;
    }

    uint8_t raw[10];
    uint8_t read_len = flen < 10 ? flen : 10;
    read_fifo(dev, raw, read_len);

    uint8_t uid_len = read_len - 2;
    memcpy(dev->uid_lsb, &raw[2], uid_len);
    for (uint8_t i = 0; i < uid_len; i++)
        uid[i] = raw[uid_len + 1 - i];

    ESP_LOGI(TAG, "UID (%d bytes): %02X%02X%02X%02X%02X%02X%02X%02X",
             uid_len,
             uid[0], uid[1], uid[2], uid[3],
             uid[4], uid[5], uid[6], uid[7]);

    return uid_len;
}

// ── Block read – NON-ADDRESSED (no UID required) ──────────────────────────────

bool clrc663_iso15693_read_block(clrc663_t *dev, const uint8_t *uid,
                                 uint8_t block_num, uint8_t *data)
{
    (void)uid;  // not used in non-addressed mode

    // Frame: [flags(1), CMD(1), block_num(1)] = 3 bytes
    uint8_t frame[3] = {
        ISO15693_FLAGS_NONADDRESSED,
        ISO15693_CMD_READ_SINGLE_BLOCK,
        block_num
    };

    cmd_transceive(dev, frame, sizeof(frame));
    clear_irqs(dev);

    uint8_t irq0 = wait_for_response(dev);
    if (irq0 & MFRC630_IRQ0_ERR_IRQ) {
        ESP_LOGW(TAG, "read_block %d: error IRQ (irq0=0x%02X)", block_num, irq0);
        return false;
    }

    uint8_t flen = fifo_length(dev);
    if (flen < 2) {
        ESP_LOGW(TAG, "read_block %d: FIFO too short (%d)", block_num, flen);
        return false;
    }

    uint8_t raw[10];
    uint8_t read_len = flen < 10 ? flen : 10;
    read_fifo(dev, raw, read_len);

    if (raw[0] & 0x01) {
        ESP_LOGW(TAG, "read_block %d: tag returned error flag", block_num);
        return false;
    }

    // Response (no Option_flag): [resp_flags(1), data(4)]
    uint8_t data_len = read_len - 1;
    memcpy(data, &raw[1], data_len);
    if (data_len < 4)
        memset(data + data_len, 0, 4 - data_len);

    ESP_LOGI(TAG, "Block %d: \"%.4s\"", block_num, (char *)data);
    return true;
}

bool clrc663_iso15693_read_block_pair(clrc663_t *dev, const uint8_t *uid,
                                      uint8_t first_block, uint8_t *data)
{
    // Apply protocol once for the pair — not before every single block
    apply_iso15693_protocol(dev);

    if (!clrc663_iso15693_read_block(dev, uid, first_block, data))
        return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!clrc663_iso15693_read_block(dev, uid, first_block + 1, data + 4))
        return false;
    return true;
}

// ── Block write – NON-ADDRESSED ───────────────────────────────────────────────

bool clrc663_iso15693_write_block(clrc663_t *dev, const uint8_t *uid,
                                  uint8_t block_num, const uint8_t *data)
{
    (void)uid;

    // Frame: [flags(1), CMD(1), block_num(1), data(4)] = 7 bytes
    uint8_t frame[7];
    frame[0] = ISO15693_FLAGS_NONADDRESSED;
    frame[1] = ISO15693_CMD_WRITE_SINGLE_BLOCK;
    frame[2] = block_num;
    memcpy(&frame[3], data, 4);

    cmd_transceive(dev, frame, sizeof(frame));
    clear_irqs(dev);

    uint8_t irq0 = wait_for_response(dev);
    if (irq0 & MFRC630_IRQ0_ERR_IRQ) {
        ESP_LOGW(TAG, "write_block %d: error IRQ (irq0=0x%02X)", block_num, irq0);
        return false;
    }

    uint8_t flen = fifo_length(dev);
    if (flen < 1) {
        ESP_LOGW(TAG, "write_block %d: no response", block_num);
        return false;
    }

    uint8_t resp_flags;
    read_fifo(dev, &resp_flags, 1);
    if (resp_flags & 0x01) {
        ESP_LOGW(TAG, "write_block %d: tag returned error flag", block_num);
        return false;
    }

    ESP_LOGI(TAG, "Block %d written OK", block_num);
    return true;
}

bool clrc663_iso15693_write_block_pair(clrc663_t *dev, const uint8_t *uid,
                                       uint8_t first_block, const uint8_t *data)
{
    apply_iso15693_protocol(dev);

    if (!clrc663_iso15693_write_block(dev, uid, first_block, data))
        return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!clrc663_iso15693_write_block(dev, uid, first_block + 1, data + 4))
        return false;
    return true;
}
