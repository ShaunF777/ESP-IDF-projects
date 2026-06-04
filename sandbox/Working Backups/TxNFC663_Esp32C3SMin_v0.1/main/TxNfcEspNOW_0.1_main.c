/*
 * Project : Battery-Powered RFID Node Network
 * Device  : ESP32-C3 Super Mini + NXP CLRC663 (SPI)
 * Focus   : ISO 15693 user-block read / write
 *
 * Tag memory layout (NXP ICODE SLI – 4 bytes per native block):
 *   User string 0  ->  blocks 0+1  (8 ASCII bytes from PLC)
 *   User string 1  ->  blocks 2+3
 *   User string 2  ->  blocks 4+5
 *
 * Main loop:
 *   1. Init SPI + CLRC663 (once)
 *   2. Read hardware UID
 *   3. Read all 3 user strings (each = 2 x 4-byte blocks)
 *   4. Optionally write a user string
 *   5. Wait, then repeat
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "clrc663_idf.h"

static const char *TAG = "main";

// ── Pin map – ESP32-C3 Super Mini ────────────────────────────────────────────
#define SPI_SCK     4
#define SPI_MISO    5
#define SPI_MOSI    6
#define SPI_CS      7

// ── Poll interval ─────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS    2000

// ── 3 user strings, each spanning 2 native blocks ────────────────────────────
#define USER_STRINGS        3
#define STRING_SIZE         8                        // bytes per user string

// first native block for each user string
static const uint8_t string_first_block[USER_STRINGS] = { 0, 2, 4 };

// ─────────────────────────────────────────────────────────────────────────────

void app_main(void)
{
    // ── 1. SPI bus init (once) ────────────────────────────────────────────────
    spi_bus_config_t buscfg = {
        .mosi_io_num   = SPI_MOSI,
        .miso_io_num   = SPI_MISO,
        .sclk_io_num   = SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // ── 2. CLRC663 init (once) ────────────────────────────────────────────────
    clrc663_t reader;
    ESP_ERROR_CHECK(clrc663_init(SPI2_HOST, SPI_CS, &reader));
    clrc663_iso15693_init(&reader);

    ESP_LOGI(TAG, "CLRC663 ready – polling every %d ms", POLL_INTERVAL_MS);

    while (1) {
        // ── 3. Read UID ───────────────────────────────────────────────────────
        uint8_t uid[8] = {0};
        if (clrc663_iso15693_read_uid(&reader, uid) == 0) {
            ESP_LOGW(TAG, "No tag found");
            goto next;
        }

        ESP_LOGI(TAG, "UID: %02X%02X%02X%02X%02X%02X%02X%02X",
                 uid[0], uid[1], uid[2], uid[3],
                 uid[4], uid[5], uid[6], uid[7]);

        // ── 4. Read all 3 user strings ────────────────────────────────────────
        for (uint8_t s = 0; s < USER_STRINGS; s++) {
            uint8_t data[STRING_SIZE + 1] = {0};    // +1 for null terminator

            vTaskDelay(pdMS_TO_TICKS(20));

            if (clrc663_iso15693_read_block_pair(&reader, uid,
                                                 string_first_block[s], data)) {
                ESP_LOGI(TAG, "String %d: \"%s\"", s, (char *)data);
            } else {
                ESP_LOGW(TAG, "String %d: read failed (blocks %d+%d)",
                         s, string_first_block[s], string_first_block[s] + 1);
            }
        }

        // ── 5. Example write – uncomment and set your payload to use ─────────
        /*
        const char *new_value = "12345678";          // must be exactly 8 chars
        uint8_t write_buf[STRING_SIZE];
        memcpy(write_buf, new_value, STRING_SIZE);

        if (clrc663_iso15693_write_block_pair(&reader, uid, 0, write_buf)) {
            ESP_LOGI(TAG, "String 0 written: \"%s\"", new_value);
        } else {
            ESP_LOGW(TAG, "String 0 write failed");
        }
        */

next:
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
