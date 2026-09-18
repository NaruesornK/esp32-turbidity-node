#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

#define CURRENT_VERSION "1.0.0"

// Hardware Config
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17
// SEN0710 ออกจากโรงงานมาที่ 4800 8N1 (เปลี่ยนได้ที่ register 0x07D1 ถ้าเคยตั้งไว้)
#define MODBUS_BAUD_RATE 4800
#define SENSOR_SLAVE_ID 1

// ขา DE/RE ของชิลด์ RS485 ตั้งเป็น -1 ถ้าชิลด์เป็นแบบ auto-direction (IOXESP32 ปกติเป็นแบบนี้)
// ถ้าอ่านเซนเซอร์ไม่ได้ (Error E2 ตลอด) ให้ลองใส่เลขขา DE/RE ตามสเปกชิลด์
#define MODBUS_DE_RE_PIN -1

// Register map ของ DFRobot SEN0710 (RS485 Industrial Turbidity Sensor 0-1000 NTU)
// 0x0000 = ความขุ่น (16-bit unsigned), 0x0001 = อุณหภูมิ (16-bit signed) อ่านได้ในคำสั่งเดียว
#define SENSOR_REG_ADDR 0x0000
#define SENSOR_REG_COUNT 2
#define TURBIDITY_SCALE 0.1f        // ค่าที่อ่านได้คูณ 10 มาแล้ว (0x0D2E = 337.4 NTU)
#define TEMPERATURE_SCALE 0.1f      // 0x00DB = 21.9 องศา
#define MODBUS_READ_RETRIES 3
#define MODBUS_RETRY_DELAY_MS 300   // สเปกบังคับให้เว้นจังหวะ poll มากกว่า 200ms

// GitHub URLs
#define GITHUB_CONFIG_URL "https://raw.githubusercontent.com/NaruesornK/esp32-turbidity-node/master/config.json"
#define GITHUB_OTA_URL_PREFIX "https://github.com/NaruesornK/esp32-turbidity-node/releases/download/v"

// ThingsBoard
#define THINGSBOARD_SERVER "thingsboard.lesyslab.com"
#define THINGSBOARD_PORT 1883
#define TB_TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define TB_ATTRIBUTES_TOPIC "v1/devices/me/attributes"

// Default fallbacks
#define DEFAULT_SLEEP_MINUTES 15
#define DEFAULT_TB_TOKEN "DEFAULT_TOKEN"

// Watchdog / Fail-safe
// WDT ต้องยาวกว่าช่วงที่ block นานที่สุด (WiFiManager portal 60s + ช่วง connect)
#define WDT_TIMEOUT_SECONDS 180
// กันตื่นค้าง: ถ้าทำงานเกินเวลานี้ให้บังคับไปนอนทันที
#define MAX_AWAKE_MS (5UL * 60UL * 1000UL)
// ถ้ารีเซ็ตผิดปกติติดกันเกินนี้ ให้ข้ามรอบทำงานแล้วไปนอนเลย (กัน boot loop ตอนอยู่ไกล)
#define MAX_CONSECUTIVE_FAULTS 3
// จำกัดจำนวนครั้งที่พยายาม OTA เวอร์ชันเดิม (กันวน OTA ล้มเหลวไม่สิ้นสุด)
#define MAX_OTA_ATTEMPTS 2

#define WIFI_PORTAL_TIMEOUT_S 60
#define CONFIG_FETCH_RETRIES 3
#define MQTT_CONNECT_RETRIES 3
// เว้นจังหวะหลัง publish ให้ TCP ส่งจริง และให้แต่ละข้อความได้ timestamp คนละค่า
#define MQTT_PUBLISH_GAP_MS 60
#define MQTT_FLUSH_STEP_MS 100

struct DeviceConfig {
    int sleep_minutes;
    String tb_token;
};

extern DeviceConfig currentConfig;
extern String deviceMAC;

#endif
