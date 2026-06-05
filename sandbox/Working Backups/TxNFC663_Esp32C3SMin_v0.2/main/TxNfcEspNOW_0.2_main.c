/*
 * Project : Battery-Powered RFID Node Network
 * Device  : ESP32-C3 Super Mini + NXP CLRC663 (SPI)
 * Version : 0.2
 *
 * Reads User String 0 (blocks 0+1) from an ISO 15693 tag and sends a
 * JSON payload over ESP-NOW:
 *   { "FleetID": "KMG005", "UserID": "01234567" }
 *
 * Tag memory layout (NXP ICODE SLI – 4 bytes per native block):
 *   User string 0  ->  blocks 0+1
 *   User string 1  ->  blocks 2+3
 *   User string 2  ->  blocks 4+5
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "clrc663_idf.h"

static const char *TAG = "main";

// ── Pin map – ESP32-C3 Super Mini ────────────────────────────────────────────
#define SPI_SCK     4
#define SPI_MISO    5
#define SPI_MOSI    6
#define SPI_CS      7

// ── Poll interval ─────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS    2000

// ── Tag layout ────────────────────────────────────────────────────────────────
#define USER_STRINGS        3
#define STRING_SIZE         8
static const uint8_t string_first_block[USER_STRINGS] = { 0, 2, 4 };

// ── ESP-NOW ───────────────────────────────────────────────────────────────────
#define FLEET_ID            "KMG005"
static const uint8_t broadcast_addr[ESP_NOW_ETH_ALEN] = {0x84, 0xF7, 0x03, 0x32, 0x24, 0xF4};

// ─────────────────────────────────────────────────────────────────────────────

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Send Transmission %s",
             status == ESP_NOW_SEND_SUCCESS ? "Ok" : "FAILED");
}

static void espnow_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    esp_now_peer_info_t peer = {
        .channel = 0,
        .ifidx   = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, broadcast_addr, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "Send Transmission ready");
}

static void send_payload(const char *user_id)
{
    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"FleetID\":\"%s\",\"UserID\":\"%s\"}",
             FLEET_ID, user_id);

    esp_err_t ret = esp_now_send(broadcast_addr,
                                 (const uint8_t *)payload,
                                 strlen(payload));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sent: %s", payload);
    } else {
        ESP_LOGW(TAG, "Send Transmission failed: %s", esp_err_to_name(ret));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void app_main(void)
{
    // ── ESP-NOW init ──────────────────────────────────────────────────────────
    espnow_init();

    // ── SPI bus init ──────────────────────────────────────────────────────────
    spi_bus_config_t buscfg = {
        .mosi_io_num   = SPI_MOSI,
        .miso_io_num   = SPI_MISO,
        .sclk_io_num   = SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // ── CLRC663 init ──────────────────────────────────────────────────────────
    clrc663_t reader;
    ESP_ERROR_CHECK(clrc663_init(SPI2_HOST, SPI_CS, &reader));
    clrc663_iso15693_init(&reader);

    ESP_LOGI(TAG, "ISO15693 tag ready – polling every %d ms", POLL_INTERVAL_MS);

    while (1) {
        bool any_read = false;

        for (uint8_t s = 0; s < USER_STRINGS; s++) {
            uint8_t data[STRING_SIZE + 1] = {0};

            if (clrc663_iso15693_read_block_pair(&reader, NULL,
                                                 string_first_block[s], data)) {
                ESP_LOGI(TAG, "String %d: \"%s\"", s, (char *)data);

                // Send ESP-NOW payload for String 0 (UserID)
                if (s == 0) {
                    send_payload((const char *)data);
                }

                any_read = true;
            } else {
                ESP_LOGW(TAG, "String %d: read failed", s);
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (!any_read) {
            ESP_LOGW(TAG, "No tag found");
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
