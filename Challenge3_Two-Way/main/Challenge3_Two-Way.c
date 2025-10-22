#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "esp_timer.h"
#include <inttypes.h>

static const char* TAG = "ESP_NOW_CHAT";

// MAC Address ของอีกตัว (ต้องเปลี่ยนตามของจริง)
static uint8_t partner_mac[6] = {0x94,0xB5,0x55,0xF8,0x30,0xF4};

// โครงสร้างข้อความแชท
typedef struct __attribute__((packed)) {
    char sender_name[20];
    char message[200];
    uint32_t msg_id;
    bool is_ack;
} chat_message_t;

// Callback เมื่อส่งข้อมูลเสร็จ
void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    (void) info; // info contains TX meta; unused for now
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "✅ Message delivered successfully");
    } else {
        ESP_LOGE(TAG, "❌ Failed to deliver message");
    }
}

// Callback เมื่อรับข้อมูล
// Keep a simple message counter and track last seen IDs
static uint32_t last_received_id = 0;
static uint32_t message_counter = 0;

void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!info || !data || len < (int)sizeof(chat_message_t)) return;
    const uint8_t *mac_addr = info->src_addr;

    chat_message_t recv_msg;
    memcpy(&recv_msg, data, sizeof(recv_msg));

    ESP_LOGI(TAG, "📥 From=%s id=%" PRIu32 " ack=%d: %s",
             recv_msg.sender_name, recv_msg.msg_id, recv_msg.is_ack, recv_msg.message);

    // If it's an ACK for our message, log and ignore
    if (recv_msg.is_ack) {
    ESP_LOGI(TAG, "🔔 Received ACK for msg_id=%" PRIu32 " from %s", recv_msg.msg_id, recv_msg.sender_name);
        return;
    }

    // If new message, send ACK back
    if (recv_msg.msg_id > last_received_id) {
        last_received_id = recv_msg.msg_id;

        chat_message_t ack = {0};
        strncpy(ack.sender_name, "ESP32_B", sizeof(ack.sender_name)-1);
        strncpy(ack.message, "ACK: Received", sizeof(ack.message)-1);
        ack.msg_id = recv_msg.msg_id;
        ack.is_ack = true;

        // ensure peer exists
        if (!esp_now_is_peer_exist(mac_addr)) {
            esp_now_peer_info_t p = {0};
            memcpy(p.peer_addr, mac_addr, 6);
            p.channel = 0;
            p.encrypt = false;
            esp_err_t er = esp_now_add_peer(&p);
            if (er != ESP_OK && er != ESP_ERR_ESPNOW_EXIST) {
                ESP_LOGE(TAG, "add_peer failed: %d", er);
                return;
            }
        }

        esp_err_t er = esp_now_send(mac_addr, (const uint8_t*)&ack, sizeof(ack));
    if (er == ESP_OK) ESP_LOGI(TAG, "📤 Sent ACK for id=%" PRIu32, ack.msg_id);
    else ESP_LOGE(TAG, "❌ Failed to send ACK: %d", er);
    } else {
        ESP_LOGW(TAG, "Duplicate/old msg id=%" PRIu32 " <= last=%" PRIu32 ", ignored", recv_msg.msg_id, last_received_id);
    }
}

// ฟังก์ชันเริ่มต้น WiFi และ ESP-NOW
void init_espnow(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    
    // เพิ่ม Peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, partner_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
    
    ESP_LOGI(TAG, "ESP-NOW bidirectional communication initialized");
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_espnow();
    
    chat_message_t send_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    strncpy(send_msg.sender_name, "ESP32_B", sizeof(send_msg.sender_name)-1);

    // simple loop: send a chat message every 5s and wait for ACKs
    while (1) {
        message_counter++;
        snprintf(send_msg.message, sizeof(send_msg.message), "Hello from ESP32_B #%" PRIu32, message_counter);
        send_msg.msg_id = message_counter;
        send_msg.is_ack = false;

        esp_err_t er = esp_now_send(partner_mac, (uint8_t*)&send_msg, sizeof(send_msg));
        if (er == ESP_OK) ESP_LOGI(TAG, "📤 Sent msg id=%" PRIu32, send_msg.msg_id);
        else ESP_LOGE(TAG, "❌ Send failed: %d", er);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
}