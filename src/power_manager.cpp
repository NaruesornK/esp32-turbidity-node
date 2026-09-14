#include "power_manager.h"
#include <Arduino.h>
#include <esp_sleep.h>

void goToSleep(int minutes) {
    if (minutes <= 0) minutes = 15; // Fallback
    
    Serial.printf("Going to sleep for %d minutes...\n", minutes);
    Serial.flush();
    
    uint64_t sleep_time_us = (uint64_t)minutes * 60 * 1000000;
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    esp_deep_sleep_start();
}
