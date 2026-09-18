#include "power_manager.h"
#include "network_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>

const char *resetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW_RESET";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_EXT:      return "EXT_RESET";
        default:               return "UNKNOWN";
    }
}

bool wasAbnormalReset() {
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

void goToSleep(int minutes) {
    if (minutes <= 0) minutes = 15; // Fallback

    // ปิดการเชื่อมต่อให้เรียบร้อยก่อนหลับ ไม่งั้นวิทยุยังกินไฟตอนเข้า sleep
    mqttDisconnect();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    Serial.printf("Going to sleep for %d minutes...\n", minutes);
    Serial.flush();

    uint64_t sleep_time_us = (uint64_t)minutes * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    esp_deep_sleep_start();
}
