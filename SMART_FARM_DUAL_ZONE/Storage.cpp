#include "Storage.h"

Preferences prefs;

char wifiSSID[32] = "";
char wifiPass[64] = "";
char blynkAuth[34] = "";
char adminUser[32] = "admin";
char adminPass[32] = "admin";
char apPass[32] = DEFAULT_AP_PASS;
bool masterEnable = true;
bool mustChangeDefaultPass = true;

ZoneConfig configLeft;
ZoneConfig configRight;

bool Config_Validate(const ZoneConfig& cfg, String& outError) {
  if (cfg.moistureCritical < 0.0f || cfg.moistureCritical >= cfg.moistureLow) {
    outError = "Critical moisture must be >= 0 and < Low threshold";
    return false;
  }
  if (cfg.moistureLow >= cfg.moistureHigh || cfg.moistureHigh > 100.0f) {
    outError = "Low moisture must be < High threshold and <= 100%";
    return false;
  }
  if (cfg.pulseOnMinutes < 1 || cfg.pulseOnMinutes > 30) {
    outError = "Pulse ON time must be between 1 and 30 minutes";
    return false;
  }
  if (cfg.soakMinutes < 1 || cfg.soakMinutes > 120) {
    outError = "Soak time must be between 1 and 120 minutes";
    return false;
  }
  if (cfg.maxDailyRuntimeMinutes < 1 || cfg.maxDailyRuntimeMinutes > 600) {
    outError = "Max daily runtime must be between 1 and 600 minutes";
    return false;
  }
  if (cfg.manualDurationMinutes < 1 || cfg.manualDurationMinutes > 60) {
    outError = "Manual duration must be between 1 and 60 minutes";
    return false;
  }
  if (cfg.maxSoilTemp <= 0.0f || cfg.maxSoilTemp > 60.0f) {
    outError = "Max soil temperature must be between 1 and 60 C";
    return false;
  }

  if (cfg.useGrowthStages) {
    int prevEndDAP = 0;
    for (int i = 0; i < 4; i++) {
      if (cfg.stages[i].endDAP <= prevEndDAP) {
        outError = "Stage endDAP must be strictly strictly increasing (stage " + String(i + 1) + ")";
        return false;
      }
      prevEndDAP = cfg.stages[i].endDAP;

      if (cfg.stages[i].moistureLow < 0.0f || cfg.stages[i].moistureLow >= cfg.stages[i].moistureHigh || cfg.stages[i].moistureHigh > 100.0f) {
        outError = "Stage " + String(i + 1) + ": Low moisture must be < High threshold and <= 100%";
        return false;
      }
      if (cfg.stages[i].pulseOnMinutes < 1 || cfg.stages[i].pulseOnMinutes > 30) {
        outError = "Stage " + String(i + 1) + ": Pulse ON time must be between 1 and 30 minutes";
        return false;
      }
      if (cfg.stages[i].soakMinutes < 1 || cfg.stages[i].soakMinutes > 120) {
        outError = "Stage " + String(i + 1) + ": Soak time must be between 1 and 120 minutes";
        return false;
      }
      if (cfg.stages[i].maxDailyRuntimeMinutes < 1 || cfg.stages[i].maxDailyRuntimeMinutes > 600) {
        outError = "Stage " + String(i + 1) + ": Max daily runtime must be between 1 and 600 minutes";
        return false;
      }
    }
  }

  return true;
}

void Storage_ApplySoilPreset(ZoneConfig& cfg, SoilType soil) {
  cfg.soilType = soil;
  switch (soil) {
    case SOIL_SANDY_LOAM:
      cfg.moistureHigh = 45.0f;
      cfg.moistureLow  = 30.0f;
      cfg.moistureCritical = 15.0f;
      break;

    case SOIL_LOAM:
      cfg.moistureHigh = 60.0f;
      cfg.moistureLow  = 42.0f;
      cfg.moistureCritical = 22.0f;
      break;

    case SOIL_CLAY_LOAM:
      cfg.moistureHigh = 70.0f;
      cfg.moistureLow  = 52.0f;
      cfg.moistureCritical = 32.0f;
      break;

    case SOIL_COCOPEAT:
      cfg.moistureHigh = 75.0f;
      cfg.moistureLow  = 55.0f;
      cfg.moistureCritical = 35.0f;
      break;

    case SOIL_CUSTOM:
    default:
      break;
  }
}

void Storage_ApplyCropPreset(ZoneConfig& cfg, CropGroup crop) {
  cfg.cropGroup = crop;
  cfg.useGrowthStages = true;

  float baseLow = cfg.moistureLow;
  float baseHigh = cfg.moistureHigh;

  switch (crop) {
    case CROP_LEAFY:
      strncpy(cfg.cropName, "Leafy Greens", sizeof(cfg.cropName) - 1);
      cfg.pulseOnMinutes = 2;
      cfg.soakMinutes = 8;
      cfg.maxDailyRuntimeMinutes = 40;
      cfg.stages[0] = { 10, baseLow + 5, baseHigh, 2, 8, 30 };
      cfg.stages[1] = { 25, baseLow + 3, baseHigh, 2, 8, 45 };
      cfg.stages[2] = { 40, baseLow,     baseHigh, 3, 8, 50 };
      cfg.stages[3] = { 50, baseLow,     baseHigh, 2, 8, 40 };
      break;

    case CROP_SOLANACEAE:
      strncpy(cfg.cropName, "Chili & Tomato", sizeof(cfg.cropName) - 1);
      cfg.pulseOnMinutes = 3;
      cfg.soakMinutes = 10;
      cfg.maxDailyRuntimeMinutes = 60;
      cfg.stages[0] = { 15, baseLow + 5, baseHigh,     2, 10, 40 };
      cfg.stages[1] = { 45, baseLow,     baseHigh,     3, 10, 60 };
      cfg.stages[2] = { 85, baseLow,     baseHigh + 3, 4, 12, 80 };
      cfg.stages[3] = { 120,baseLow - 3, baseHigh - 2, 3, 10, 50 };
      break;

    case CROP_VINE_FRUIT:
      strncpy(cfg.cropName, "Melon & Vine", sizeof(cfg.cropName) - 1);
      cfg.pulseOnMinutes = 3;
      cfg.soakMinutes = 12;
      cfg.maxDailyRuntimeMinutes = 70;
      cfg.stages[0] = { 15, baseLow + 5, baseHigh,     2, 10, 40 };
      cfg.stages[1] = { 40, baseLow,     baseHigh,     3, 12, 60 };
      cfg.stages[2] = { 70, baseLow,     baseHigh + 5, 4, 15, 90 };
      cfg.stages[3] = { 85, baseLow - 8, baseHigh - 10, 2, 15, 30 };
      break;

    case CROP_HERB_AROMA:
      strncpy(cfg.cropName, "Herbs & Aroma", sizeof(cfg.cropName) - 1);
      cfg.pulseOnMinutes = 2;
      cfg.soakMinutes = 15;
      cfg.maxDailyRuntimeMinutes = 35;
      cfg.stages[0] = { 15, baseLow + 3, baseHigh,     2, 12, 30 };
      cfg.stages[1] = { 45, baseLow - 3, baseHigh - 3, 2, 15, 35 };
      cfg.stages[2] = { 80, baseLow - 5, baseHigh - 5, 2, 15, 40 };
      cfg.stages[3] = { 120,baseLow - 5, baseHigh - 5, 2, 15, 30 };
      break;

    case CROP_CUSTOM:
    default:
      cfg.useGrowthStages = false;
      break;
  }
}

void Storage_ApplyCombinedPreset(ZoneConfig& cfg, SoilType soil, CropGroup crop) {
  Storage_ApplySoilPreset(cfg, soil);
  Storage_ApplyCropPreset(cfg, crop);
}

void setDefaultConfig(ZoneConfig& cfg, const char* name) {
  cfg.version = ZONE_CONFIG_VERSION;
  strncpy(cfg.cropName, name, sizeof(cfg.cropName) - 1);
  cfg.soilType = SOIL_LOAM;
  cfg.cropGroup = CROP_LEAFY;
  cfg.selectedStage = 0;
  
  cfg.startYear = 2026;
  cfg.startMonth = 1;
  cfg.startDay = 1;
  cfg.dapOffset = 0;
  
  cfg.maxSoilTemp = 35.0f;
  cfg.manualDurationMinutes = 10;
  
  Storage_ApplyCombinedPreset(cfg, SOIL_LOAM, CROP_LEAFY);
}

void Storage_Init() {
  prefs.begin("smartfarm", false);
  
  String s_ssid = prefs.getString("ssid", "");
  String s_pass = prefs.getString("pass", "");
  String s_auth = prefs.getString("auth", "");
  String s_admin = prefs.getString("admUser", "admin");
  String s_admpw = prefs.getString("admPass", "admin");
  if (s_admpw == "admin1234" && !prefs.getBool("pwdChanged", false)) {
    s_admpw = "admin";
  }
  String s_appw  = prefs.getString("apPass", DEFAULT_AP_PASS);
  masterEnable = prefs.getBool("master", true);
  mustChangeDefaultPass = prefs.getBool("pwdChanged", false) == false;
  
  strncpy(wifiSSID, s_ssid.c_str(), sizeof(wifiSSID) - 1);
  strncpy(wifiPass, s_pass.c_str(), sizeof(wifiPass) - 1);
  strncpy(blynkAuth, s_auth.c_str(), sizeof(blynkAuth) - 1);
  strncpy(adminUser, s_admin.c_str(), sizeof(adminUser) - 1);
  strncpy(adminPass, s_admpw.c_str(), sizeof(adminPass) - 1);
  strncpy(apPass, s_appw.c_str(), sizeof(apPass) - 1);
  if (strlen(apPass) < 8) {
    strncpy(apPass, DEFAULT_AP_PASS, sizeof(apPass) - 1);
  }
  
  // Storage Version & Migration Check for ZoneConfig
  uint16_t storedVer = prefs.getUShort("cfgVer", 0);
  
  // Load Left Zone Config
  if (storedVer == ZONE_CONFIG_VERSION && prefs.getBytesLength("cfgLeft") == sizeof(ZoneConfig)) {
    prefs.getBytes("cfgLeft", &configLeft, sizeof(ZoneConfig));
  } else {
    Serial.printf("[Storage] Migrating / Init Left Config (Stored ver: %u, expected: %u)\n", storedVer, ZONE_CONFIG_VERSION);
    setDefaultConfig(configLeft, "Zone Left Crop");
    Storage_SaveZoneConfig(0, configLeft);
  }
  
  // Load Right Zone Config
  if (storedVer == ZONE_CONFIG_VERSION && prefs.getBytesLength("cfgRight") == sizeof(ZoneConfig)) {
    prefs.getBytes("cfgRight", &configRight, sizeof(ZoneConfig));
  } else {
    Serial.printf("[Storage] Migrating / Init Right Config (Stored ver: %u, expected: %u)\n", storedVer, ZONE_CONFIG_VERSION);
    setDefaultConfig(configRight, "Zone Right Crop");
    Storage_SaveZoneConfig(1, configRight);
  }

  prefs.putUShort("cfgVer", ZONE_CONFIG_VERSION);
}

void Storage_SaveNetwork(const char* ssid, const char* pass, const char* auth) {
  if (ssid && strlen(ssid) > 0) {
    strncpy(wifiSSID, ssid, sizeof(wifiSSID) - 1);
    prefs.putString("ssid", wifiSSID);
  }
  if (pass && strlen(pass) > 0) {
    strncpy(wifiPass, pass, sizeof(wifiPass) - 1);
    prefs.putString("pass", wifiPass);
  }
  if (auth && strlen(auth) > 0) {
    strncpy(blynkAuth, auth, sizeof(blynkAuth) - 1);
    prefs.putString("auth", blynkAuth);
  }
}

void Storage_SaveAdminAuth(const char* user, const char* pass) {
  if (user && strlen(user) > 0) {
    strncpy(adminUser, user, sizeof(adminUser) - 1);
    prefs.putString("admUser", adminUser);
  }
  if (pass && strlen(pass) > 0) {
    strncpy(adminPass, pass, sizeof(adminPass) - 1);
    prefs.putString("admPass", adminPass);
    mustChangeDefaultPass = false;
    prefs.putBool("pwdChanged", true);
  }
}

void Storage_SaveApPass(const char* pass) {
  if (pass && strlen(pass) >= 8) {
    strncpy(apPass, pass, sizeof(apPass) - 1);
    prefs.putString("apPass", apPass);
  } else if (!pass || strlen(pass) == 0) {
    strncpy(apPass, DEFAULT_AP_PASS, sizeof(apPass) - 1);
    prefs.putString("apPass", apPass);
  }
}

void Storage_SaveMasterEnable(bool en) {
  masterEnable = en;
  prefs.putBool("master", masterEnable);
}

bool Storage_SaveZoneConfig(int zoneIdx, const ZoneConfig& cfg) {
  String validationErr;
  ZoneConfig validatedCfg = cfg;
  validatedCfg.version = ZONE_CONFIG_VERSION;
  
  if (!Config_Validate(validatedCfg, validationErr)) {
    Serial.printf("[Storage ERROR] Config validation failed for Zone %d: %s\n", zoneIdx, validationErr.c_str());
    return false;
  }
  
  if (zoneIdx == 0) {
    configLeft = validatedCfg;
    prefs.putBytes("cfgLeft", &configLeft, sizeof(ZoneConfig));
  } else {
    configRight = validatedCfg;
    prefs.putBytes("cfgRight", &configRight, sizeof(ZoneConfig));
  }
  return true;
}

void Storage_SaveDailyRuntime(int leftSec, int rightSec, int dateInt) {
  prefs.putInt("rLeft", leftSec);
  prefs.putInt("rRight", rightSec);
  prefs.putInt("rDateInt", dateInt);
}

void Storage_LoadDailyRuntime(int& leftSec, int& rightSec, int& savedDateInt) {
  leftSec = prefs.getInt("rLeft", 0);
  rightSec = prefs.getInt("rRight", 0);
  savedDateInt = prefs.getInt("rDateInt", 0);
}

void Storage_FactoryReset() {
  prefs.clear();
  delay(100);
  ESP.restart();
}
