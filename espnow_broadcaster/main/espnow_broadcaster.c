// main/espnow_broadcaster.c
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_timer.h"  // ★ ต้องมี

static const char* TAG = "ESP_NOW_BROADCASTER";

/* ส่ง broadcast ถึงทุกเครื่อง */
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
#define CHANNEL 1  // ★ ให้ตั้งเท่ากันทุกบอร์ด

typedef struct __attribute__((packed)) {
    char     sender_id[20];
    char     message[180];
    uint8_t  message_type;   // 1=Info, 2=Command, 3=Alert
    uint8_t  group_id;       // 0=All, 1=Group1, 2=Group2
    uint32_t sequence_num;
    uint32_t timestamp_ms;
} broadcast_data_t;

static uint32_t sequence_counter = 0;

/* --- callback แบบใหม่ใน IDF v5.x --- */
static void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    ESP_LOGI(TAG, "Send status: %s", status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    // โดยปกติฝั่ง Broadcaster ไม่ค่อยมีคนตอบกลับ (ยกเว้น Receiver ส่ง ACK กลับ)
    if (!data || len <= 0) return;

    if (info && info->src_addr) {
        ESP_LOGI(TAG, "📥 Reply from %02X:%02X:%02X:%02X:%02X:%02X",
                 info->src_addr[0], info->src_addr[1], info->src_addr[2],
                 info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    }

    broadcast_data_t rx = {0};
    memcpy(&rx, data, len < (int)sizeof(rx) ? len : (int)sizeof(rx));
    ESP_LOGI(TAG, "   Reply msg=\"%s\" type=%u group=%u seq=%" PRIu32 " ts=%" PRIu32 "ms",
             rx.message, rx.message_type, rx.group_id, rx.sequence_num, rx.timestamp_ms);
}

/* --- Wi-Fi STA & lock channel --- */
static void wifi_init_for_espnow(uint8_t ch) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (ch >= 1 && ch <= 13) {
        wifi_config_t sta = {0};
        sta.sta.channel = ch;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA started (channel=%u)", ch);
}

/* --- ESP-NOW init + add broadcast peer --- */
static void espnow_init_and_add_broadcast_peer(uint8_t ch) {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = ch;         // ★ ให้ตรงกับ Wi-Fi
    peer.encrypt = false;

    esp_err_t er = esp_now_add_peer(&peer);
    if (er == ESP_ERR_ESPNOW_EXIST) {
        ESP_ERROR_CHECK(esp_now_del_peer(BROADCAST_MAC));
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    } else {
        ESP_ERROR_CHECK(er);
    }
    ESP_LOGI(TAG, "ESP-NOW ready & broadcast peer added");
}

static void send_broadcast(const char* message, uint8_t msg_type, uint8_t group_id) {
    broadcast_data_t tx = {0};
    snprintf(tx.sender_id, sizeof(tx.sender_id), "%s", "MASTER_001");
    snprintf(tx.message, sizeof(tx.message), "%s", message);
    tx.message_type = msg_type;
    tx.group_id     = group_id;
    tx.sequence_num = ++sequence_counter;
    tx.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    ESP_LOGI(TAG, "📡 TX: type=%u group=%u seq=%" PRIu32 " msg=\"%s\"",
             tx.message_type, tx.group_id, tx.sequence_num, tx.message);

    esp_err_t er = esp_now_send(BROADCAST_MAC, (const uint8_t*)&tx, sizeof(tx));
    if (er != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(er));
    }
}

void app_main(void) {
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_for_espnow(CHANNEL);
    espnow_init_and_add_broadcast_peer(CHANNEL);

    // log MAC ตัวเอง
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "📍 Broadcaster MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int i = 0;
    while (1) {
        switch (i % 4) {
            case 0: send_broadcast("General announcement to all devices", 1, 0); break;
            case 1: send_broadcast("Command for Group 1 devices",        2, 1); break;
            case 2: send_broadcast("Alert for Group 2 devices",          3, 2); break;
            case 3: send_broadcast("Status update for all groups",       1, 0); break;
        }
        i++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}