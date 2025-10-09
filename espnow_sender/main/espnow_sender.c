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

static const char *TAG = "ESP_NOW_SENDER";

/* === ตั้ง MAC ของ Receiver ให้ตรงกับเครื่องปลายทาง ===
   ตัวอย่างที่คุณให้: 94:B5:55:F8:5D:50 */
static uint8_t receiver_mac[6] = { 0x94, 0xB5, 0x55, 0xF6, 0xF6, 0x40 };

/* === Payload === */
typedef struct {
    char  message[200];
    int   counter;
    float sensor_value;
} esp_now_data_t;

/* === Callback ส่ง (รูปแบบใหม่ใน IDF v5.x) ===
   หลีกเลี่ยงการอ้างฟิลด์ภายใน info ที่ต่างกันระหว่างเวอร์ชัน  */
static void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "✅ SENT ok");
    } else {
        ESP_LOGE(TAG, "❌ SENT fail");
    }
    // ถ้าต้องการดีบักเพิ่มเติมค่อย printf ระดับ DEBUG โดยไม่แตะฟิลด์ที่ไม่การันตี
    if (info) {
        ESP_LOGD(TAG, "(tx info available)");
    } else {
        ESP_LOGD(TAG, "(tx info NULL)");
    }
}

/* === Init Wi-Fi (STA) สำหรับ ESP-NOW === */
static void wifi_init_for_espnow(uint8_t channel /*1–13*/)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // ล็อก channel ให้ตรงกับฝั่งรับ (แนะนำตั้งทั้งคู่ให้เท่ากัน)
    if (channel >= 1 && channel <= 13) {
        wifi_config_t sta_cfg = { 0 };
        sta_cfg.sta.channel = channel;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started (STA), channel=%u", channel);
}

/* === Init ESP-NOW + เพิ่ม peer === */
static void espnow_init_and_add_peer(uint8_t channel)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = channel;      // ให้ตรงกับที่ตั้งไว้
    peer.encrypt = false;

    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_LOGI(TAG, "ESP-NOW initialized & peer added");
}

void app_main(void)
{
    // เตรียม NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    const uint8_t CHANNEL = 1;   // ปรับให้ตรงกับ Receiver ของคุณ
    wifi_init_for_espnow(CHANNEL);
    espnow_init_and_add_peer(CHANNEL);

    ESP_LOGI(TAG, "🚀 ESP-NOW Sender started");

    esp_now_data_t send_data = {0};
    int counter = 0;

    while (1) {
        // เตรียม payload
        snprintf(send_data.message, sizeof(send_data.message),
                 "Hello from Sender! Time: %d", counter);
        send_data.counter      = counter;
        send_data.sensor_value = 25.5f + (float)(counter % 10);

        // ส่ง unicast
        esp_err_t res = esp_now_send(receiver_mac,
                                     (const uint8_t *)&send_data,
                                     sizeof(send_data));

        if (res == ESP_OK) {
            ESP_LOGI(TAG, "📤 Sending: '%s' (ctr=%d, sensor=%.2f)",
                     send_data.message, send_data.counter, send_data.sensor_value);
        } else {
            ESP_LOGE(TAG, "❌ esp_now_send err=%d", res);
        }

        counter++;
        vTaskDelay(pdMS_TO_TICKS(2000)); // ส่งทุก 2 วินาที
    }
}