#pragma once
#include <Arduino.h>

void BlynkManager_Init();
void BlynkManager_Update();

void BlynkManager_SendTelemetry();
void BlynkManager_SendControlState();
void BlynkManager_SendZoneConfig(int zoneIdx);
void BlynkManager_SendStatus();

void BlynkManager_SendStatusData(int zoneIdx);
void BlynkManager_SendSensorData(int zoneIdx);
void BlynkManager_LogEvent(const char* eventCode, const char* desc);
