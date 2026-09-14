#include "modbus_sensor.h"
#include "app_config.h"
#include <Arduino.h>
#include <ModbusMaster.h>

ModbusMaster node;

void initModbus() {
    // Initialize Serial2 for Modbus RTU Shield
    Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    
    // Communicate with Modbus slave ID
    node.begin(SENSOR_SLAVE_ID, Serial2);
}

float readTurbidity() {
    uint8_t result;
    
    // The DFRobot Industrial Turbidity Sensor (SEN0671) uses Holding Register 0x0000 
    // Function code 03
    result = node.readHoldingRegisters(0x0000, 1);
    
    if (result == node.ku8MBSuccess) {
        uint16_t data = node.getResponseBuffer(0);
        return (float)data; 
    } else {
        Serial.printf("Modbus Error: %02X\n", result);
        return -999.0f;
    }
}
