#include "network_manager.h"
#include "app_config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool connectWiFi() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(60); // 60 seconds timeout
    
    // Attempt to connect, if it fails, autoConnect will start AP
    // If user doesn't configure in 60s, it returns false
    bool res = wm.autoConnect("TurbidityNode_AP");
    
    if(!res) {
        Serial.println("Failed to connect to WiFi or hit timeout");
        return false;
    }
    
    Serial.println("WiFi connected");
    return true;
}

void performOTA(String version) {
    Serial.println("Starting OTA update to version " + version + "...");
    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate verification for simplicity
    
    String binUrl = String(GITHUB_OTA_URL_PREFIX) + version + "/firmware.bin";
    
    t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
    switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            break;
    }
}

bool syncConfigAndOTA() {
    bool success = false;
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, GITHUB_CONFIG_URL);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                String latestVersion = doc["version"] | CURRENT_VERSION;
                
                // Compare versions (simple string comparison)
                if (latestVersion != CURRENT_VERSION) {
                    Serial.println("New firmware version found on GitHub! Updating...");
                    performOTA(latestVersion);
                }
                
                if (doc["devices"].containsKey(deviceMAC)) {
                    currentConfig.sleep_minutes = doc["devices"][deviceMAC]["sleep_minutes"] | DEFAULT_SLEEP_MINUTES;
                    currentConfig.tb_token = doc["devices"][deviceMAC]["tb_token"].as<String>();
                    Serial.println("Configuration synced successfully from GitHub");
                } else {
                    Serial.println("Device MAC not found in configuration, using fallbacks");
                }
                success = true; // Configuration processed successfully
            } else {
                Serial.printf("JSON Parse Error: %s\n", error.c_str());
            }
        } else {
            Serial.printf("HTTP GET failed, error code: %d\n", httpCode);
        }
    } else {
        Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return success;
}

bool sendTelemetry(float turbidity, String mac, String token) {
    mqttClient.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
    
    int retry = 0;
    while (!mqttClient.connected() && retry < 3) {
        Serial.print("Connecting to ThingsBoard... ");
        
        // Connect to ThingsBoard using MAC as client ID, and token as username
        if (mqttClient.connect(mac.c_str(), token.c_str(), NULL)) {
            Serial.println("Connected!");
        } else {
            Serial.print("Failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" retrying in 2 seconds...");
            delay(2000);
            retry++;
        }
    }
    
    if (!mqttClient.connected()) {
        Serial.println("Failed to connect to ThingsBoard after 3 retries.");
        return false;
    }
    
    // สร้าง JSON Payload
    DynamicJsonDocument doc(256);
    doc["turbidity"] = turbidity;
    doc["mac"] = mac;
    doc["wifi"] = WiFi.RSSI();
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("Publishing telemetry: " + payload);
    
    bool success = mqttClient.publish("v1/devices/me/telemetry", payload.c_str());
    if (success) {
        Serial.println("Telemetry published successfully");
    } else {
        Serial.println("Failed to publish telemetry");
    }
    
    mqttClient.disconnect();
    return success;
}
