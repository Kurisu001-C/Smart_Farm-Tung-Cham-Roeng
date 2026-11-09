#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

extern Preferences prefs;

extern char wifiSSID[32];
extern char wifiPass[64];
extern char blynkAuth[34];
extern char adminUser[32];
extern char adminPass[32];
extern char apPass[32];
extern bool masterEnable;
extern bool mustChangeDefaultPass;

extern ZoneConfig configLeft;
extern ZoneConfig configRight;

void Storage_Init();
void Storage_SaveNetwork(const char* ssid, const char* pass, const char* auth);
void Storage_SaveMasterEnable(bool en);
bool Storage_SaveZoneConfig(int zoneIdx, const ZoneConfig& cfg);
void Storage_SaveDailyRuntime(int leftSec, int rightSec, int dateInt);
void Storage_LoadDailyRuntime(int& leftSec, int& rightSec, int& savedDateInt);
void Storage_SaveAdminAuth(const char* user, const char* pass);
void Storage_SaveApPass(const char* pass);
void Storage_FactoryReset();

// Central Configuration Validation
bool Config_Validate(const ZoneConfig& cfg, String& outError);

// Preset Application Helpers
void Storage_ApplySoilPreset(ZoneConfig& cfg, SoilType soil);
void Storage_ApplyCropPreset(ZoneConfig& cfg, CropGroup crop);
void Storage_ApplyCombinedPreset(ZoneConfig& cfg, SoilType soil, CropGroup crop);
