#pragma once
#include <Arduino.h>
#include "Config.h"

extern ZoneRuntime zoneLeft;
extern ZoneRuntime zoneRight;

void ZoneControl_Init();
void ZoneControl_Update();

void ZoneControl_SetMode(int zoneIdx, ZoneMode newMode);
void ZoneControl_SetManualSwitch(int zoneIdx, bool openValve);
void ZoneControl_ResetDailyRuntime();

// Helper to get active thresholds based on DAP/Growth Stages
void ZoneControl_GetActiveParams(int zoneIdx, float& low, float& high, int& pulseM, int& soakM, int& maxDailyM);
