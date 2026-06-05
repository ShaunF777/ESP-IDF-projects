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
#include "esp_sleep.h"
#include "clrc663_idf.h"

static const char *TAG = "main";

// ── Pin map – ESP32-C3 Super Mini ────────────────────────────────────────────
#define SPI_SCK     4
#define SPI_MISO    5
#define SPI_MOSI    6
#define SPI_CS      7

// ── LPCD wakeup pin ──────────────────────────────────────────────────────────
#define LPCD_IRQ_PIN    3   // GPIO wired to CLRC663 IRQ pin

// ── Tag layout ────────────────────────────────────────────────────────────────
#define USER_STRINGS        3
#define STRING_SIZE         8
static const uint8_t string_first_block[USER_STRINGS] = { 0, 2, 4 };

// ── Default configuration ─────────────────────────────────────────────────────
#define DEFAULT_FLEET_ID    "KMG005"
static const uint8_t default_mac_addr[ESP_NOW_ETH_ALEN] = {0x84, 0xF7, 0x03, 0x32, 0x24, 0xF4};

// ─────────────────────────────────────────────────────────────────────────────

static void load_configuration(char *fleet_id, uint8_t *mac_addr)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("config", NVS_READWRITE, &h));

    size_t len = 17;
    if (nvs_get_str(h, "fleet_id", fleet_id, &len) != ESP_OK) {
        strlcpy(fleet_id, DEFAULT_FLEET_ID, 17);
        nvs_set_str(h, "fleet_id", fleet_id);
        ESP_LOGI(TAG, "NVS: fleet_id defaulted to %s", fleet_id);
    }

    len = ESP_NOW_ETH_ALEN;
    if (nvs_get_blob(h, "mac_addr", mac_addr, &len) != ESP_OK) {
        memcpy(mac_addr, default_mac_addr, ESP_NOW_ETH_ALEN);
        nvs_set_blob(h, "mac_addr", mac_addr, ESP_NOW_ETH_ALEN);
        ESP_LOGI(TAG, "NVS: mac_addr defaulted");
    }

    nvs_commit(h);
    nvs_close(h);
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Send Transmission %s",
             status == ESP_NOW_SEND_SUCCESS ? "Ok" : "FAILED");
}

static void espnow_init(const uint8_t *mac_addr)
{
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
    memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "Send Transmission ready");
}

static void send_payload(const char *fleet_id, const char *user_id, const uint8_t *mac_addr)
{
    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"FleetID\":\"%s\",\"UserID\":\"%s\"}",
             fleet_id, user_id);

    esp_err_t ret = esp_now_send(mac_addr,
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
    // ── Load configuration from NVS ───────────────────────────────────────────
    ESP_ERROR_CHECK(nvs_flash_init());
    char     fleet_id[17];
    uint8_t  mac_addr[ESP_NOW_ETH_ALEN];
    load_configuration(fleet_id, mac_addr);

    // ── ESP-NOW init ──────────────────────────────────────────────────────────
    espnow_init(mac_addr);

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

    // ── Single poll attempt ───────────────────────────────────────────────────
    uint8_t data[STRING_SIZE + 1] = {0};
    if (clrc663_iso15693_read_block_pair(&reader, NULL, string_first_block[0], data)) {
        ESP_LOGI(TAG, "String 0: \"%s\"", (char *)data);
        send_payload(fleet_id, (const char *)data, mac_addr);
        vTaskDelay(pdMS_TO_TICKS(20));  // allow send callback to fire
    } else {
        ESP_LOGW(TAG, "No tag found");
    }

    // ── Shutdown Wi-Fi, arm LPCD and enter deep sleep ────────────────────────
    esp_now_deinit();
    esp_wifi_stop();
    setup_lpcd(&reader);
    esp_sleep_enable_ext0_wakeup(LPCD_IRQ_PIN, 0);  // wake on IRQ low
    ESP_LOGI(TAG, "Entering deep sleep - wake on tag detect");
    esp_deep_sleep_start();
}
