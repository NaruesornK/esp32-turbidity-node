#include <Arduino.h>
#include <WiFi.h>
#include "app_config.h"
#include "power_manager.h"
#include "modbus_sensor.h"
#include "network_manager.h"

// Initialize globals
DeviceConfig currentConfig = {DEFAULT_SLEEP_MINUTES, DEFAULT_TB_TOKEN};
String deviceMAC = "";

enum AppState {
    STATE_WIFI_CONNECT,
    STATE_SYNC_CONFIG_OTA,
    STATE_READ_SENSOR,
    STATE_SEND_TELEMETRY,
    STATE_GO_SLEEP
};

AppState currentState = STATE_WIFI_CONNECT;
float turbidityValue = -999.0f;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Industrial Smart Turbidity Node ---");
    Serial.println("Version: " + String(CURRENT_VERSION));
    
    // Get ESP32 MAC address
    deviceMAC = WiFi.macAddress();
    deviceMAC.replace(":", "");
    Serial.println("Node MAC: " + deviceMAC);
    
    // Initialize Modbus Shield
    initModbus();
}

void loop() {
    // Finite State Machine
    switch (currentState) {
        case STATE_WIFI_CONNECT:
            Serial.println("\n[STATE] Connecting to WiFi...");
            if (connectWiFi()) {
                currentState = STATE_SYNC_CONFIG_OTA;
            } else {
                // Fail-safe: jump to sleep if WiFi fails
                currentState = STATE_GO_SLEEP;
            }
            break;
            
        case STATE_SYNC_CONFIG_OTA:
            Serial.println("\n[STATE] Syncing Config & Checking OTA...");
            if (syncConfigAndOTA()) {
                currentState = STATE_READ_SENSOR;
            } else {
                currentState = STATE_GO_SLEEP;
            }
            break;
            
        case STATE_READ_SENSOR:
            Serial.println("\n[STATE] Reading Modbus Sensor...");
            turbidityValue = readTurbidity();
            currentState = STATE_SEND_TELEMETRY;
            break;
            
        case STATE_SEND_TELEMETRY:
            Serial.println("\n[STATE] Sending Telemetry to ThingsBoard...");
            if (turbidityValue != -999.0f) {
                if (!sendTelemetry(turbidityValue, deviceMAC, currentConfig.tb_token)) {
                    Serial.println("Telemetry send failed.");
                    currentState = STATE_GO_SLEEP;
                } else {
                    currentState = STATE_GO_SLEEP;
                }
            } else {
                Serial.println("Skipping telemetry due to sensor error.");
                currentState = STATE_GO_SLEEP;
            }
            break;
            
        case STATE_GO_SLEEP:
            Serial.println("\n[STATE] Going to Sleep...");
            goToSleep(currentConfig.sleep_minutes);
            break;
    }
}