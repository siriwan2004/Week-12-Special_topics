#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_now.h"

static const char* TAG = "ESP_NOW_RECEIVER";

/* โครงสร้างข้อมูลต้องตรงกับฝั่งส่ง */
typedef struct {
    char  message[200];
    int   counter;
    float sensor_value;
} esp_now_data_t;

/* พิมพ์ MAC สวย ๆ */
static void print_mac(const uint8_t m[6]) {
    ESP_LOGI(TAG, "From %02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* ==== CALLBACK แบบใหม่ใน IDF v5.x ====
   ใช้เฉพาะ src_addr ที่การันตีได้ หลีกเลี่ยง rssi/channel */
static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!data || len <= 0) return;

    if (info && info->src_addr) {
        print_mac(info->src_addr);
    } else {
        ESP_LOGI(TAG, "From: (unknown src)");
    }
    ESP_LOGI(TAG, "📥 Recv len=%d", len);

    esp_now_data_t pkt = {0};
    int n = len < (int)sizeof(pkt) ? len : (int)sizeof(pkt);
    memcpy(&pkt, data, n);

    ESP_LOGI(TAG, "📨 Message: %s", pkt.message);
    ESP_LOGI(TAG, "🔢 Counter: %d", pkt.counter);
    ESP_LOGI(TAG, "🌡️  Sensor: %.2f", pkt.sensor_value);
    ESP_LOGI(TAG, "--------------------------------");
}

/* เริ่ม Wi-Fi โหมด STA และ (ถ้าต้องการ) ล็อก channel ให้ตรงกับ Sender */
static void wifi_init_for_espnow(uint8_t channel /*1–13 หรือ 0 ไม่ล็อก*/) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (channel >= 1 && channel <= 13) {
        wifi_config_t sta_cfg = {0};
        sta_cfg.sta.channel = channel;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA started (channel=%u)", channel);
}

/* แสดง MAC ของตัวเอง (เอาไปใส่ฝั่ง Sender) */
static void print_my_mac(void) {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "📍 My MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "⚠️  Copy this MAC to Sender code");
}

void app_main(void) {
    // เตรียม NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    const uint8_t CHANNEL = 1;  // ★ ให้ตรงกับ Sender (หรือใช้ 0 ถ้าไม่ล็อก)
    wifi_init_for_espnow(CHANNEL);
    print_my_mac();

    // เริ่ม ESP-NOW + ลงทะเบียน callback แบบใหม่
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_LOGI(TAG, "ESP-NOW ready to receive…");

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }