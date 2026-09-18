#include "network_manager.h"
#include "app_config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <lwip/dns.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

// ----------------------------------------------------------------- DNS

static void printNetworkInfo() {
    Serial.println("  Gateway : " + WiFi.gatewayIP().toString());
    Serial.println("  Subnet  : " + WiFi.subnetMask().toString());
    Serial.println("  DNS1    : " + WiFi.dnsIP(0).toString());
    Serial.println("  DNS2    : " + WiFi.dnsIP(1).toString());
    Serial.printf("  RSSI    : %d dBm\n", WiFi.RSSI());
}

// บาง router (หรือตอน DHCP ไม่แจก DNS) จะได้ DNS = 0.0.0.0 ทำให้ hostByName() ล้มทุกครั้ง
// ใส่ DNS สาธารณะเป็นตัวสำรองไว้ เพื่อให้โหนดยังทำงานต่อได้โดยไม่ต้องไปตั้ง router
static void ensureDnsServer() {
    bool needFallback = (WiFi.dnsIP(0) == IPAddress(0, 0, 0, 0));

    if (!needFallback) {
        // router แจก DNS มาแล้ว แต่ถ้ามันตอบไม่ได้ก็ต้องมีตัวสำรองใน slot ที่ 2
        if (WiFi.dnsIP(1) == IPAddress(0, 0, 0, 0)) {
            ip_addr_t backup;
            IP_ADDR4(&backup, 1, 1, 1, 1);
            dns_setserver(1, &backup);
            Serial.println("  -> added 1.1.1.1 as backup DNS");
        }
        return;
    }

    Serial.println("  -> DHCP ไม่ได้แจก DNS มา ใช้ 8.8.8.8 / 1.1.1.1 แทน");
    ip_addr_t primary, secondary;
    IP_ADDR4(&primary, 8, 8, 8, 8);
    IP_ADDR4(&secondary, 1, 1, 1, 1);
    dns_setserver(0, &primary);
    dns_setserver(1, &secondary);
}

static bool resolveCheck(const char *host) {
    IPAddress addr;
    if (WiFi.hostByName(host, addr)) {
        Serial.printf("  DNS OK: %s -> %s\n", host, addr.toString().c_str());
        return true;
    }
    Serial.printf("  DNS FAIL: %s\n", host);
    return false;
}

// ---------------------------------------------------------------- WiFi

bool connectWiFi() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);

    // Attempt to connect, if it fails, autoConnect will start AP
    // If user doesn't configure in time, it returns false
    bool res = wm.autoConnect("TurbidityNode_AP");

    if (!res) {
        Serial.println("Failed to connect to WiFi or hit timeout");
        return false;
    }

    Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());

    // Give the network stack time to get DNS from DHCP
    delay(3000);

    printNetworkInfo();
    ensureDnsServer();

    // ลองแปลงชื่อโดเมนจริงสักตัว ถ้าไม่ผ่านแปลว่าเน็ตวงนี้ออกอินเทอร์เน็ตไม่ได้ ไม่ใช่โค้ดมีปัญหา
    if (!resolveCheck("raw.githubusercontent.com")) {
        Serial.println("!! DNS ใช้งานไม่ได้ ตรวจสอบ: router แจก DNS มาหรือไม่ / วง WiFi นี้มีเน็ตจริงหรือไม่");
    }

    return true;
}

// ------------------------------------------------------------- Version

// แปลง "1.2.3" เป็นตัวเลขเดียวเพื่อเทียบว่าใหม่กว่าจริงหรือไม่ (กัน downgrade โดยไม่ตั้งใจ)
static uint32_t versionToNumber(const String &v) {
    int major = 0, minor = 0, patch = 0;
    sscanf(v.c_str(), "%d.%d.%d", &major, &minor, &patch);
    return ((uint32_t)major << 16) | ((uint32_t)minor << 8) | (uint32_t)patch;
}

// ------------------------------------------------------------- Config

bool syncConfig(bool &updateAvailable, String &newVersion) {
    updateAvailable = false;
    newVersion = "";

    bool success = false;
    int httpCode = 0;

    for (int attempt = 1; attempt <= CONFIG_FETCH_RETRIES; attempt++) {
        Serial.printf("Fetching config... (Attempt %d/%d)\n", attempt, CONFIG_FETCH_RETRIES);

        // สร้าง client ใหม่ทุกครั้ง เพราะ WiFiClientSecure ที่ปิดไปแล้วนำกลับมา handshake ซ้ำไม่น่าเชื่อถือ
        WiFiClientSecure client;
        client.setInsecure(); // Disable SSL certificate verification for simplicity

        HTTPClient http;
        http.setConnectTimeout(10000);
        http.setTimeout(10000);

        if (!http.begin(client, GITHUB_CONFIG_URL)) {
            Serial.println("http.begin() failed");
            delay(3000);
            continue;
        }

        httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            // อ่านผ่าน stream + filter เอาเฉพาะข้อมูลที่ต้องใช้ ประหยัด heap ขณะที่ TLS ยังเปิดอยู่
            StaticJsonDocument<192> filter;
            filter["version"] = true;
            filter["devices"][deviceMAC]["sleep_minutes"] = true;
            filter["devices"][deviceMAC]["tb_token"] = true;

            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(
                doc, http.getStream(), DeserializationOption::Filter(filter));

            if (!error) {
                String latestVersion = doc["version"] | CURRENT_VERSION;

                if (versionToNumber(latestVersion) > versionToNumber(CURRENT_VERSION)) {
                    updateAvailable = true;
                    newVersion = latestVersion;
                    Serial.println("Newer firmware available: " + latestVersion);
                } else if (latestVersion != CURRENT_VERSION) {
                    Serial.println("Config version (" + latestVersion + ") is not newer than "
                                   + CURRENT_VERSION + ", skipping OTA");
                }

                JsonVariant me = doc["devices"][deviceMAC];
                if (!me.isNull()) {
                    currentConfig.sleep_minutes = me["sleep_minutes"] | DEFAULT_SLEEP_MINUTES;
                    currentConfig.tb_token = me["tb_token"] | DEFAULT_TB_TOKEN;
                    Serial.println("Configuration synced successfully from GitHub");
                } else {
                    Serial.println("Device MAC not found in configuration, using fallbacks");
                }
                success = true;
            } else {
                Serial.printf("JSON Parse Error: %s\n", error.c_str());
            }

            http.end();
            break;
        }

        if (httpCode > 0) {
            Serial.printf("HTTP GET failed, error code: %d\n", httpCode);
        } else {
            Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();

        if (attempt < CONFIG_FETCH_RETRIES) {
            delay(3000); // รอแล้วลองใหม่ เผื่อ DNS/Network ช้า
        }
    }

    return success;
}

// ---------------------------------------------------------------- MQTT

bool isMqttConnected() {
    return mqttClient.connected();
}

void mqttLoop() {
    if (mqttClient.connected()) {
        mqttClient.loop();
    }
}

bool mqttConnect(const String &mac, const String &token) {
    mqttClient.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
    mqttClient.setKeepAlive(60);

    for (int retry = 0; retry < MQTT_CONNECT_RETRIES && !mqttClient.connected(); retry++) {
        Serial.print("Connecting to ThingsBoard... ");

        // Connect to ThingsBoard using MAC as client ID, and token as username
        if (mqttClient.connect(mac.c_str(), token.c_str(), NULL)) {
            Serial.println("Connected!");
            return true;
        }

        Serial.print("Failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" retrying in 2 seconds...");
        delay(2000);
    }

    Serial.println("Failed to connect to ThingsBoard.");
    return false;
}

void mqttDisconnect() {
    if (!mqttClient.connected()) {
        return;
    }

    // ดันข้อความสุดท้ายออกให้พ้น socket ก่อนปิด ไม่งั้น payload ของ GO_SLEEP จะหายไปเฉยๆ
    for (int i = 0; i < 5; i++) {
        mqttClient.loop();
        delay(MQTT_FLUSH_STEP_MS);
    }

    mqttClient.disconnect();
    delay(MQTT_FLUSH_STEP_MS); // ให้ DISCONNECT packet ได้ออกไปจริงก่อนปิดวิทยุ
}

static bool publishJson(const char *topic, JsonDocument &doc) {
    if (!mqttClient.connected()) {
        return false;
    }

    String payload;
    serializeJson(doc, payload);

    bool ok = mqttClient.publish(topic, payload.c_str());
    Serial.printf("MQTT %s -> %s : %s\n", ok ? "OK" : "FAILED", topic, payload.c_str());

    // publish() แค่เขียนลง socket ยังไม่ได้ออกอากาศ ต้องให้จังหวะ TCP ส่งจริงก่อน
    // และกันไม่ให้หลายข้อความตกลงมาที่ timestamp เดียวกันจน ThingsBoard ทับกันเอง
    mqttClient.loop();
    delay(MQTT_PUBLISH_GAP_MS);
    return ok;
}

// 4 ค่าพื้นฐานที่ติดไปกับทุก payload
static void addBaseFields(JsonDocument &doc, const char *state) {
    doc["mac"] = deviceMAC;
    doc["state"] = state;
    doc["version"] = CURRENT_VERSION;
    doc["sleep"] = currentConfig.sleep_minutes;
}

bool publishState(const char *state) {
    StaticJsonDocument<160> doc;
    addBaseFields(doc, state);
    return publishJson(TB_TELEMETRY_TOPIC, doc);
}

bool publishTelemetry(const char *state, float turbidity, float temperatureC, bool sensorOk) {
    StaticJsonDocument<192> doc;
    addBaseFields(doc, state);
    if (sensorOk) {
        doc["turbidity"] = turbidity;
        doc["temperature"] = temperatureC;
    }

    // ส่งขึ้น attributes ด้วย เพื่อให้หน้า Device แสดงสถานะล่าสุดได้โดยไม่ต้องเปิดกราฟ
    publishJson(TB_ATTRIBUTES_TOPIC, doc);
    return publishJson(TB_TELEMETRY_TOPIC, doc);
}

// ----------------------------------------------------------------- OTA

void performOTA(const String &version) {
    Serial.println("Starting OTA update to version " + version + "...");

    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate verification for simplicity

    String binUrl = String(GITHUB_OTA_URL_PREFIX) + version + "/firmware.bin";

    // ป้อน watchdog ระหว่างดาวน์โหลด/เขียน flash
    // ไม่งั้นไฟล์ใหญ่บนเน็ตช้าจะโดน WDT รีเซ็ตกลางคันจนเฟิร์มแวร์พัง
    Update.onProgress([](size_t current, size_t total) {
        esp_task_wdt_reset();
        static uint8_t lastPercent = 255;
        if (total > 0) {
            uint8_t percent = (uint8_t)((current * 100) / total);
            if (percent != lastPercent && (percent % 10) == 0) {
                Serial.printf("OTA progress: %u%%\n", percent);
                lastPercent = percent;
            }
        }
    });

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
    esp_task_wdt_reset();

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK"); // ปกติบอร์ดจะรีบูตไปก่อนถึงบรรทัดนี้
            break;
    }
}
