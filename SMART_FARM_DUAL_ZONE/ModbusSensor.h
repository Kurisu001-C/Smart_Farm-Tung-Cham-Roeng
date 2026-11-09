#pragma once
#include <Arduino.h>
#include "Config.h"

void Modbus_Init();
bool Modbus_ReadSoilSensor(uint8_t slaveID, float& humidity, float& temperature);
