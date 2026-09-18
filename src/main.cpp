#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "app_config.h"
#include "power_manager.h"
#include "modbus_sensor.h"
#include "network_manager.h"

// Initialize globals
DeviceConfig currentConfig = {DEFAULT_SLEEP_MINUTES, DEFAULT_TB_TOKEN};
String deviceMAC = "";

enum AppState {
    STATE_WIFI_CONNECT,
    STATE_SYNC_CONFIG,
    STATE_MQTT_CONNECT,
    STATE_READ_SENSOR,
    STATE_SEND_TELEMETRY,
    STATE_OTA_UPDATE,
    STATE_GO_SLEEP
};

static const char *stateName(AppState s) {
    switch (s) {
        case STATE_WIFI_CONNECT:   return "WIFI_CONNECT";
        case STATE_SYNC_CONFIG:    return "SYNC_CONFIG";
        case STATE_MQTT_CONNECT:   return "MQTT_CONNECT";
        case STATE_READ_SENSOR:    return "READ_SENSOR";
        case STATE_SEND_TELEMETRY: return "SEND_TELEMETRY";
        case STATE_OTA_UPDATE:     return "OTA_UPDATE";
        case STATE_GO_SLEEP:       return "GO_SLEEP";
        default:                   return "UNKNOWN";
    }
}

// ข้อมูลที่ต้องอยู่รอดข้าม deep sleep "และ" ข้ามการรีเซ็ตจาก watchdog
// ต้องใช้ RTC_NOINIT_ATTR เพราะ RTC_DATA_ATTR จะถูกตั้งค่าใหม่เมื่อรีเซ็ตแบบที่ไม่ใช่ deep sleep
#define RTC_MAGIC 0x54524231UL // "TRB1"

typedef struct {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t faultCount;    // จำนวนครั้งที่รีเซ็ตผิดปกติติดกัน
    uint32_t otaAttempts;   // จำนวนครั้งที่พยายาม OTA เวอร์ชันเดิม
    int32_t  sleepMinutes;  // ค่าล่าสุดที่ sync มาได้ ใช้ตอนที่ sync ไม่สำเร็จ
    char     lastState[20]; // state ที่กำลังทำอยู่ตอนบอร์ดค้าง
    char     otaVersion[16];
} RtcState;

RTC_NOINIT_ATTR static RtcState rtc;

static AppState currentState = STATE_WIFI_CONNECT;
static float turbidityValue = 0.0f;
static float temperatureValue = 0.0f;
static bool  sensorOk = false;
static bool  otaAvailable = false;
static String otaVersion = "";
static char  statusMsg[32] = "OK";
static char  faultStateAtBoot[20] = "";
static const char *resetReason = "UNKNOWN";

static void setState(AppState s) {
    currentState = s;
    // จดไว้ก่อนเข้า state เพื่อให้รู้ว่าค้างตรงไหน ถ้าโดน watchdog รีเซ็ต
    strncpy(rtc.lastState, stateName(s), sizeof(rtc.lastState) - 1);
    rtc.lastState[sizeof(rtc.lastState) - 1] = '\0';

    Serial.printf("\n[STATE] %s\n", stateName(s));
    if (isMqttConnected()) {
        publishState(stateName(s));
    }
}

// status ใช้ดูใน Serial อย่างเดียว ไม่ได้ส่งขึ้นคลาวด์ เพื่อให้ payload เหลือแค่ค่าที่จำเป็น
static void finishCycle(const char *status) {
    strncpy(statusMsg, status, sizeof(statusMsg) - 1);
    statusMsg[sizeof(statusMsg) - 1] = '\0';
    Serial.printf("Cycle result: %s\n", statusMsg);
    setState(STATE_GO_SLEEP);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    resetReason = resetReasonString();

    // เตรียมหน่วยความจำ RTC (ครั้งแรกหลังจ่ายไฟค่าจะเป็นขยะ จึงเช็กด้วย magic)
    if (rtc.magic != RTC_MAGIC) {
        memset(&rtc, 0, sizeof(rtc));
        rtc.magic = RTC_MAGIC;
        rtc.sleepMinutes = DEFAULT_SLEEP_MINUTES;
    }
    rtc.bootCount++;

    // ใช้ค่า sleep ล่าสุดที่เคย sync ได้ เผื่อรอบนี้โหลด config ไม่สำเร็จ
    currentConfig.sleep_minutes = rtc.sleepMinutes;

    if (wasAbnormalReset()) {
        rtc.faultCount++;
        strncpy(faultStateAtBoot, rtc.lastState, sizeof(faultStateAtBoot) - 1);
        faultStateAtBoot[sizeof(faultStateAtBoot) - 1] = '\0';
    } else {
        rtc.faultCount = 0;
        faultStateAtBoot[0] = '\0';
    }

    // ตั้งค่า Hardware Watchdog Timer ป้องกันบอร์ดค้าง (โหนดอยู่ไกล ต้องรีเซ็ตตัวเองได้)
    Serial.println("\nInitializing Watchdog Timer...");
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);

    Serial.println("\n--- Industrial Smart Turbidity Node ---");
    Serial.println("Version     : " + String(CURRENT_VERSION));
    Serial.printf("Boot count  : %u\n", rtc.bootCount);
    Serial.printf("Reset reason: %s\n", resetReason);
    if (faultStateAtBoot[0] != 0) {
        Serial.printf("!! Board hung at state: %s (fault #%u)\n", faultStateAtBoot, rtc.faultCount);
    }

    // Get ESP32 MAC address
    deviceMAC = WiFi.macAddress();
    deviceMAC.replace(":", "");
    Serial.println("Node MAC    : " + deviceMAC);

    // Initialize Modbus Shield
    initModbus();

    // กัน boot loop: ถ้าค้าง/รีเซ็ตผิดปกติติดกันหลายครั้ง ให้ข้ามรอบนี้ไปนอนก่อน
    // ไม่งั้นโหนดที่อยู่ไกลจะวนรีบูตไม่หยุดและกินไฟจนแบตหมด
    if (rtc.faultCount >= MAX_CONSECUTIVE_FAULTS) {
        Serial.printf("Too many consecutive faults (%u), skipping this cycle\n", rtc.faultCount);
        rtc.faultCount = 0;
        goToSleep(currentConfig.sleep_minutes);
    }

    setState(STATE_WIFI_CONNECT);
}

void loop() {
    // ป้อนอาหารสุนัขเฝ้าบ้าน (Reset Watchdog) ทุกรอบลูป
    esp_task_wdt_reset();
    mqttLoop();

    // Fail-safe ชั้นที่สอง: ตื่นนานผิดปกติแต่ยังไม่ค้างถึงขั้นโดน WDT ก็ให้ไปนอน
    if (millis() > MAX_AWAKE_MS && currentState != STATE_GO_SLEEP) {
        Serial.println("Max awake time exceeded, forcing sleep");
        finishCycle("AWAKE_TIMEOUT");
    }

    // Finite State Machine
    switch (currentState) {
        case STATE_WIFI_CONNECT:
            if (connectWiFi()) {
                setState(STATE_SYNC_CONFIG);
            } else {
                // ไม่มีเน็ตก็ส่งอะไรไม่ได้ ไปนอนรอรอบหน้า
                finishCycle("WIFI_FAIL");
            }
            break;

        case STATE_SYNC_CONFIG: {
            if (syncConfig(otaAvailable, otaVersion)) {
                rtc.sleepMinutes = currentConfig.sleep_minutes; // จำไว้ใช้รอบหน้าถ้า sync ไม่ผ่าน
                strncpy(statusMsg, "OK", sizeof(statusMsg));
            } else {
                // GitHub ล่ม/เน็ตอืด ไม่ใช่เหตุผลที่จะทิ้งค่าวัดทั้งรอบ ใช้ค่าเดิมแล้วไปต่อ
                Serial.println("Config sync failed, continuing with last known config");
                strncpy(statusMsg, "CONFIG_SYNC_FAIL", sizeof(statusMsg));
            }
            setState(STATE_MQTT_CONNECT);
            break;
        }

        case STATE_MQTT_CONNECT:
            if (mqttConnect(deviceMAC, currentConfig.tb_token)) {
                // ถ้ารอบก่อนโดน watchdog รีเซ็ต บอกไปใน state เลยว่าค้างตรงไหน
                // ใช้ key เดิม ไม่ต้องเพิ่มค่าใน payload
                if (faultStateAtBoot[0] != 0) {
                    char recovered[40];
                    snprintf(recovered, sizeof(recovered), "RECOVERED_FROM_%s", faultStateAtBoot);
                    publishState(recovered);
                }
                publishState(stateName(STATE_MQTT_CONNECT));
                setState(STATE_READ_SENSOR);
            } else {
                finishCycle("MQTT_FAIL");
            }
            break;

        case STATE_READ_SENSOR:
            sensorOk = readTurbidity(turbidityValue, temperatureValue);
            if (!sensorOk) {
                Serial.println("Sensor read failed after retries");
                snprintf(statusMsg, sizeof(statusMsg), "SENSOR_ERR_%02X", lastModbusError());
            }
            setState(STATE_SEND_TELEMETRY);
            break;

        case STATE_SEND_TELEMETRY: {
            // ส่งเสมอ แม้เซนเซอร์อ่านไม่ได้ จะได้รู้จากฝั่ง ThingsBoard ว่าโหนดยังมีชีวิตแต่เซนเซอร์มีปัญหา
            const char *state = sensorOk ? stateName(STATE_SEND_TELEMETRY) : statusMsg;
            if (!publishTelemetry(state, turbidityValue, temperatureValue, sensorOk)) {
                Serial.println("Telemetry send failed.");
            }
            setState(otaAvailable ? STATE_OTA_UPDATE : STATE_GO_SLEEP);
            break;
        }

        case STATE_OTA_UPDATE: {
            // ทำ OTA หลังส่งข้อมูลเสร็จแล้ว ค่าวัดของรอบนี้จะได้ไม่หายไปกับการรีบูต
            bool sameVersion = (otaVersion == String(rtc.otaVersion));
            if (sameVersion && rtc.otaAttempts >= MAX_OTA_ATTEMPTS) {
                Serial.println("OTA attempt limit reached for this version, skipping");
                publishState("OTA_SKIPPED");
                setState(STATE_GO_SLEEP);
                break;
            }

            strncpy(rtc.otaVersion, otaVersion.c_str(), sizeof(rtc.otaVersion) - 1);
            rtc.otaVersion[sizeof(rtc.otaVersion) - 1] = '\0';
            rtc.otaAttempts = sameVersion ? rtc.otaAttempts + 1 : 1;

            // แจ้งก่อนเริ่ม เพราะถ้า OTA สำเร็จบอร์ดจะรีบูตทันทีและไม่ได้ส่งอะไรอีก
            publishState("OTA_UPDATE");
            mqttDisconnect(); // คืน RAM ให้ TLS ของ OTA

            performOTA(otaVersion); // สำเร็จ = รีบูตเข้าเฟิร์มแวร์ใหม่ ไม่กลับมาที่บรรทัดถัดไป

            finishCycle("OTA_FAIL");
            break;
        }

        case STATE_GO_SLEEP:
            // setState() ส่ง state นี้ขึ้น ThingsBoard ไปแล้ว เหลือแค่ปิดการเชื่อมต่อแล้วหลับ
            goToSleep(currentConfig.sleep_minutes);
            break;
    }
}
