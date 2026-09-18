#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

// ปิด MQTT/WiFi ให้เรียบร้อยแล้วเข้า deep sleep (ฟังก์ชันนี้ไม่คืนค่ากลับ)
void goToSleep(int minutes);

// สาเหตุการรีบูตครั้งล่าสุด ใช้บอก ThingsBoard ว่าบอร์ดโดน watchdog รีเซ็ตหรือไม่
const char *resetReasonString();

// true เมื่อรีบูตครั้งล่าสุดเกิดจากการค้าง (watchdog / panic / brownout)
bool wasAbnormalReset();

#endif
