#ifndef MODBUS_SENSOR_H
#define MODBUS_SENSOR_H

#include <Arduino.h>

void initModbus();

// อ่านความขุ่น (NTU) และอุณหภูมิน้ำ (องศา C) ในคำสั่งเดียว คืน true เมื่อสำเร็จ
bool readTurbidity(float &turbidity, float &temperatureC);
uint8_t lastModbusError();

#endif
