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
#include "esp_timer.h"  // สำหรับ esp_timer_get_time()

static const char* TAG = "ESP_NOW_RECEIVER";

// กำหนด ID และ Group ของ Node นี้
#define MY_NODE_ID "NODE_001"
#define MY_GROUP_ID 1  // เปลี่ยนเป็น 1 หรือ 2 ตาม Group

// MAC ของ Broadcaster (ใส่ MAC จริงของ Master) 
static uint8_t broadcaster_mac[6] = {0x94, 0xB5, 0x55, 0xF8, 0x30, 0xF4};

// โครงสร้างข้อมูลเหมือน Broadcaster
typedef struct {
    char sender_id[20];
    char message[180];
    uint8_t message_type;  // 1=Info, 2=Command, 3=Alert
    uint8_t group_id;      // 0=All, 1=Group1, 2=Group2
    uint32_t sequence_num;
    uint32_t timestamp;
} broadcast_data_t;

// เก็บ sequence number ที่รับล่าสุด (ป้องกันการรับซ้ำ)
static uint32_t last_sequence = 0;

// Forward declaration ของ send_reply
void send_reply(const uint8_t* target_mac, const char* reply_message);

// Callback เมื่อรับข้อมูล Broadcast (ปรับรูปแบบ v5.x)
void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    const uint8_t *mac_addr = recv_info->src_addr;
    broadcast_data_t *recv_data = (broadcast_data_t*)data;

    // ตรวจสอบ sequence number (ป้องกันการรับซ้ำ)
    if (recv_data->sequence_num <= last_sequence) {
        ESP_LOGW(TAG, "⚠️  Duplicate message ignored (seq: %lu)", recv_data->sequence_num);
        return;
    }
    last_sequence = recv_data->sequence_num;

    // ตรวจสอบว่าข้อความนี้สำหรับเราหรือไม่
    bool for_me = (recv_data->group_id == 0) || (recv_data->group_id == MY_GROUP_ID);

    if (!for_me) {
        ESP_LOGI(TAG, "📋 Message for Group %d (not for me)", recv_data->group_id);
        return;
    }

    // แสดงข้อมูลที่รับ
    ESP_LOGI(TAG, "📥 Received from %s:", recv_data->sender_id);
    ESP_LOGI(TAG, "   📨 Message: %s", recv_data->message);

    // แสดงประเภทข้อความ
    const char* msg_type_str;
    switch (recv_data->message_type) {
        case 1: msg_type_str = "INFO"; break;
        case 2: msg_type_str = "COMMAND"; break;
        case 3: msg_type_str = "ALERT"; break;
        default: msg_type_str = "UNKNOWN"; break;
    }
    ESP_LOGI(TAG, "   🏷️  Type: %s", msg_type_str);
    ESP_LOGI(TAG, "   👥 Group: %d", recv_data->group_id);
    ESP_LOGI(TAG, "   📊 Sequence: %lu", recv_data->sequence_num);

    // ประมวลผลตามประเภทข้อความ
    if (recv_data->message_type == 2) { // COMMAND
        ESP_LOGI(TAG, "🔧 Processing command...");
        // ใส่การประมวลผล Command ที่นี่

        // ส่งตอบกลับ
        send_reply(mac_addr, "Command received and processed");
    } else if (recv_data->message_type == 3) { // ALERT
        ESP_LOGW(TAG, "🚨 ALERT RECEIVED: %s", recv_data->message);
        // ใส่การจัดการ Alert ที่นี่
    }

    ESP_LOGI(TAG, "--------------------------------");
}

// ฟังก์ชันส่งตอบกลับไปยัง Broadcaster
void send_reply(const uint8_t* target_mac, const char* reply_message) {
    broadcast_data_t reply_data;

    strcpy(reply_data.sender_id, MY_NODE_ID);
    strncpy(reply_data.message, reply_message, sizeof(reply_data.message) - 1);
    reply_data.message[sizeof(reply_data.message)-1] = '\0';
    reply_data.message_type = 1; // Info
    reply_data.group_id = MY_GROUP_ID;
    reply_data.sequence_num = 0; // Reply ไม่ต้องใช้ sequence
    reply_data.timestamp = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "📤 Sending reply: %s", reply_message);
    esp_now_send(target_mac, (uint8_t*)&reply_data, sizeof(reply_data));
}

// Callback เมื่อส่งข้อมูลเสร็จ (ปรับรูปแบบ v5.x)
void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    ESP_LOGI(TAG, "Reply sent: %s", (status == ESP_NOW_SEND_SUCCESS) ? "✅" : "❌");
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
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    // เพิ่ม Broadcaster เป็น Peer (สำหรับส่ง Reply)
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, broadcaster_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    ESP_LOGI(TAG, "ESP-NOW Receiver initialized");
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_espnow();

    // แสดง Node Info
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "📍 Node ID: %s", MY_NODE_ID);
    ESP_LOGI(TAG, "📍 Group ID: %d", MY_GROUP_ID);
    ESP_LOGI(TAG, "📍 MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "🎯 ESP-NOW Receiver ready - Waiting for broadcasts...");

    // Receiver จะทำงานใน callback
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
