#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

bool connectWiFi();
bool syncConfigAndOTA();
bool sendTelemetry(float turbidity, String mac, String token);

#endif
