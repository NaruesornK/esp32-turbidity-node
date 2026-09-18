#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

bool connectWiFi();

// ดึง config.json จาก GitHub มาใช้ ถ้าล้มเหลวจะใช้ค่า fallback แล้วไปต่อ (ไม่ทิ้งรอบการวัด)
// updateAvailable / newVersion จะบอกว่ามีเฟิร์มแวร์ใหม่รออยู่หรือไม่
bool syncConfig(bool &updateAvailable, String &newVersion);

// --- MQTT / ThingsBoard ---
bool mqttConnect(const String &mac, const String &token);
bool isMqttConnected();
void mqttLoop();
void mqttDisconnect();

// ทุก payload ส่ง 4 ค่าพื้นฐานเหมือนกันหมด: mac, state, version, sleep
bool publishState(const char *state);

// ค่าพื้นฐาน 4 ตัว + ค่าวัดจากเซนเซอร์
bool publishTelemetry(const char *state, float turbidity, float temperatureC, bool sensorOk);

void performOTA(const String &version);

#endif
