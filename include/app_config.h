#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

#define CURRENT_VERSION "1.0.1"

// Hardware Config
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17
#define MODBUS_BAUD_RATE 9600
#define SENSOR_SLAVE_ID 1

// GitHub URLs
#define GITHUB_CONFIG_URL "https://raw.githubusercontent.com/NaruesornK/esp32-turbidity-node/master/config.json"
#define GITHUB_OTA_URL_PREFIX "https://github.com/NaruesornK/esp32-turbidity-node/releases/download/v"

// ThingsBoard
#define THINGSBOARD_SERVER "thingsboard.lesyslab.com"
#define THINGSBOARD_PORT 1883

// Default fallbacks
#define DEFAULT_SLEEP_MINUTES 15
#define DEFAULT_TB_TOKEN "DEFAULT_TOKEN"

struct DeviceConfig {
    int sleep_minutes;
    String tb_token;
};

extern DeviceConfig currentConfig;
extern String deviceMAC;

#endif
