#include "modbus_sensor.h"
#include "app_config.h"
#include <Arduino.h>
#include <ModbusMaster.h>

static ModbusMaster node;
static uint8_t g_lastError = 0;

#if MODBUS_DE_RE_PIN >= 0
static void preTransmission() {
    digitalWrite(MODBUS_DE_RE_PIN, HIGH); // เข้าโหมดส่ง
}
static void postTransmission() {
    digitalWrite(MODBUS_DE_RE_PIN, LOW);  // กลับเข้าโหมดรับ
}
#endif

void initModbus() {
    // Initialize Serial2 for Modbus RTU Shield
    Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);

#if MODBUS_DE_RE_PIN >= 0
    pinMode(MODBUS_DE_RE_PIN, OUTPUT);
    digitalWrite(MODBUS_DE_RE_PIN, LOW);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
#endif

    // Communicate with Modbus slave ID
    node.begin(SENSOR_SLAVE_ID, Serial2);
}

bool readTurbidity(float &turbidity, float &temperatureC) {
    // เซนเซอร์ RS485 มักตอบพลาดครั้งแรกหลังจ่ายไฟ จึงลองซ้ำหลายรอบ
    for (int attempt = 1; attempt <= MODBUS_READ_RETRIES; attempt++) {
        node.clearResponseBuffer();
        g_lastError = node.readHoldingRegisters(SENSOR_REG_ADDR, SENSOR_REG_COUNT);

        if (g_lastError == node.ku8MBSuccess) {
            uint16_t rawTurbidity = node.getResponseBuffer(0);
            int16_t rawTemp = (int16_t)node.getResponseBuffer(1); // ติดลบได้ จึงต้องอ่านเป็น signed

            turbidity = (float)rawTurbidity * TURBIDITY_SCALE;
            temperatureC = (float)rawTemp * TEMPERATURE_SCALE;

            Serial.printf("Modbus OK (attempt %d): %.1f NTU, %.1f C\n",
                          attempt, turbidity, temperatureC);
            return true;
        }

        Serial.printf("Modbus Error (attempt %d/%d): %02X\n",
                      attempt, MODBUS_READ_RETRIES, g_lastError);
        if (attempt < MODBUS_READ_RETRIES) {
            delay(MODBUS_RETRY_DELAY_MS);
        }
    }
    return false;
}

uint8_t lastModbusError() {
    return g_lastError;
}
