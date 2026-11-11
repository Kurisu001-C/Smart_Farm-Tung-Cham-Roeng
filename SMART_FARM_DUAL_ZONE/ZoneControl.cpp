#include "ZoneControl.h"
#include "Hardware.h"
#include "Storage.h"
#include "ModbusSensor.h"
#include "BlynkManager.h"
#include "AppLog.h"

ZoneRuntime zoneLeft;
ZoneRuntime zoneRight;

static unsigned long lastSensorPollTimeLeft = 0;
static unsigned long lastSensorPollTimeRight = 0;
static unsigned long lastSecondTick = 0;
static unsigned long lastNvsSaveMs = 0;
static int lastSavedLeftSec = -1;
static int lastSavedRightSec = -1;
static int lastSavedDateInt = -1;

// RTC Fallback tracking
static RTCDateTime lastGoodTime;
static unsigned long lastGoodTimeMillis = 0;
static bool hasGoodTime = false;

void ZoneControl_GetActiveParams(int zoneIdx, float& low, float& high, int& pulseM, int& soakM, int& maxDailyM) {
  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  
  if (cfg.selectedStage == STAGE_UNPLANTED) {
    zn.currentStageIndex = STAGE_UNPLANTED;
    low = 0.0f;
    high = 0.0f;
    pulseM = cfg.pulseOnMinutes;
    soakM = cfg.soakMinutes;
    maxDailyM = cfg.maxDailyRuntimeMinutes;
    return;
  }

  if (cfg.useGrowthStages) {
    int stageIdx = constrain(cfg.selectedStage, 0, 3);
    zn.currentStageIndex = stageIdx;
    low = cfg.stages[stageIdx].moistureLow;
    high = cfg.stages[stageIdx].moistureHigh;
    pulseM = cfg.stages[stageIdx].pulseOnMinutes;
    soakM = cfg.stages[stageIdx].soakMinutes;
    maxDailyM = cfg.stages[stageIdx].maxDailyRuntimeMinutes;
  } else {
    low = cfg.moistureLow;
    high = cfg.moistureHigh;
    pulseM = cfg.pulseOnMinutes;
    soakM = cfg.soakMinutes;
    maxDailyM = cfg.maxDailyRuntimeMinutes;
  }
}

static void applyValveHardware(int zoneIdx, bool open) {
  uint8_t valvePin = (zoneIdx == 0) ? VALVE_LEFT_CH : VALVE_RIGHT_CH;
  bool wasOpen = Relay_GetChannel(valvePin);
  Relay_SetChannel(valvePin, open);
  
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  if (open) {
    if (zn.valveOpenStartTime == 0) {
      zn.valveOpenStartTime = millis();
    }
  } else {
    zn.valveOpenStartTime = 0;
  }

  if (wasOpen != open) {
    BlynkManager_SendControlState();
  }
}

void ZoneControl_Init() {
  zoneLeft.slaveID = 1;
  zoneLeft.mode = MODE_AUTO;
  zoneLeft.state = STATE_IDLE;
  zoneLeft.sensorOnline = false;
  zoneLeft.failCount = 0;
  zoneLeft.dailyRuntimeSeconds = 0;
  zoneLeft.consecutivePulses = 0;
  zoneLeft.manualValveOpen = false;
  zoneLeft.lockoutUntilMs = 0;
  zoneLeft.valveOpenStartTime = 0;
  
  zoneRight.slaveID = 2;
  zoneRight.mode = MODE_AUTO;
  zoneRight.state = STATE_IDLE;
  zoneRight.sensorOnline = false;
  zoneRight.failCount = 0;
  zoneRight.dailyRuntimeSeconds = 0;
  zoneRight.consecutivePulses = 0;
  zoneRight.manualValveOpen = false;
  zoneRight.lockoutUntilMs = 0;
  zoneRight.valveOpenStartTime = 0;

  // Stagger initial polling timers so both sensors never block in the same loop cycle
  lastSensorPollTimeLeft = millis();
  lastSensorPollTimeRight = millis() + 1000UL;

  // Restore daily runtimes if same YYYYMMDD date
  RTCDateTime now;
  if (RTC_GetTime(now)) {
    lastGoodTime = now;
    lastGoodTimeMillis = millis();
    hasGoodTime = true;
    
    int nowDateInt = now.year * 10000 + now.month * 100 + now.day;
    int rL, rR, sDateInt;
    Storage_LoadDailyRuntime(rL, rR, sDateInt);
    if (sDateInt == nowDateInt) {
      zoneLeft.dailyRuntimeSeconds = rL;
      zoneRight.dailyRuntimeSeconds = rR;
    }
    lastSavedDateInt = nowDateInt;
    lastSavedLeftSec = zoneLeft.dailyRuntimeSeconds;
    lastSavedRightSec = zoneRight.dailyRuntimeSeconds;
  }
  
  applyValveHardware(0, false);
  applyValveHardware(1, false);
}

void ZoneControl_SetMode(int zoneIdx, ZoneMode newMode) {
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  if (zn.mode != newMode) {
    zn.mode = newMode;
    applyValveHardware(zoneIdx, false);
    zn.manualValveOpen = false;
    zn.state = STATE_IDLE;
    zn.stateStartTime = millis();
    zn.lockoutUntilMs = 0; // ปลดล็อกเมื่อผู้ใช้เปลี่ยนโหมด
    const char* zName = (zoneIdx == 0) ? "Left" : "Right";
    const char* mName = (newMode == MODE_AUTO) ? "AUTO" : ((newMode == MODE_MANUAL) ? "MANUAL" : "OFF");
    AppLog_Log("MODE", "Zone %s mode changed to %s", zName, mName);
    if (newMode == MODE_MANUAL) {
      zn.manualStartTime = millis();
    }
    BlynkManager_SendStatusData(zoneIdx);
  }
}

void ZoneControl_SetManualSwitch(int zoneIdx, bool openValve) {
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;

  // Safety: ระบบหลักปิดอยู่ → ไม่ยอมเปิดน้ำ (แต่สั่งปิดน้ำได้เสมอ)
  if (openValve && !masterEnable) {
    AppLog_Log("VALVE", "Zone %s manual open REJECTED: master system is disabled", (zoneIdx == 0) ? "Left" : "Right");
    return;
  }

  if (zn.mode == MODE_MANUAL) {
    zn.manualValveOpen = openValve;
    applyValveHardware(zoneIdx, openValve);
    zn.state = openValve ? STATE_WATERING : STATE_IDLE;
    zn.stateStartTime = millis();
    zn.manualStartTime = millis();
    const char* zName = (zoneIdx == 0) ? "Left" : "Right";
    AppLog_Log("VALVE", "Zone %s Manual valve %s", zName, openValve ? "OPENED" : "CLOSED");
    BlynkManager_SendStatusData(zoneIdx);
  }
}

void ZoneControl_ResetDailyRuntime() {
  zoneLeft.dailyRuntimeSeconds = 0;
  zoneRight.dailyRuntimeSeconds = 0;
  RTCDateTime now;
  if (hasGoodTime) {
    int dateInt = lastGoodTime.year * 10000 + lastGoodTime.month * 100 + lastGoodTime.day;
    Storage_SaveDailyRuntime(0, 0, dateInt);
    lastSavedLeftSec = 0;
    lastSavedRightSec = 0;
    lastSavedDateInt = dateInt;
  }
  AppLog_Log("RESET", "Daily watering runtimes reset to 0 for new day");
  BlynkManager_SendStatusData(0);
  BlynkManager_SendStatusData(1);
}

static void updateSingleZone(int zoneIdx, const RTCDateTime& now, bool timeValid) {
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  const char* zoneName = (zoneIdx == 0) ? "Left" : "Right";
  unsigned long nowMs = millis();

  // Update DAP if time is available
  if (timeValid) {
    if (cfg.selectedStage == STAGE_UNPLANTED) {
      zn.currentDAP = 0; // ไม่นับวันที่ปลูกเมื่ออยู่ในสถานะไม่ได้ปลูก
    } else {
      zn.currentDAP = CalculateDAP(cfg.startYear, cfg.startMonth, cfg.startDay, 0, now);
      // ระยะการเจริญเติบโต "ผู้ดูแลเป็นคนเลือก" (Web Portal / Blynk) — เฟิร์มแวร์ไม่เลื่อนระยะเอง
      // DAP ยังนับต่อไปเพื่อแสดงผล และให้แอปแนะนำได้ว่าตามวันปลูกควรอยู่ระยะไหน (stages[i].endDAP)
      // (เดิม 2.0.0 เลื่อนระยะตาม DAP ทุกวินาที ทำให้ค่าที่ผู้ดูแลเลือกถูกเขียนทับทันที)
    }
  } else {
    if (cfg.selectedStage == STAGE_UNPLANTED) {
      zn.currentDAP = 0;
    }
  }
  
  float low, high;
  int pulseM, soakM, maxDailyM;
  ZoneControl_GetActiveParams(zoneIdx, low, high, pulseM, soakM, maxDailyM);

  // Phase 1 Item 2: Failsafe Independent Valve Cutoff
  // If valve is open longer than max(pulseM, manualDurationMinutes) + 2 minutes -> Force close immediately
  int maxAllowedMinutes = (pulseM > cfg.manualDurationMinutes ? pulseM : cfg.manualDurationMinutes) + 2;
  unsigned long maxAllowedMs = (unsigned long)maxAllowedMinutes * 60000UL;
  if ((zn.state == STATE_WATERING || zn.manualValveOpen) && zn.valveOpenStartTime > 0) {
    if (nowMs - zn.valveOpenStartTime >= maxAllowedMs) {
      applyValveHardware(zoneIdx, false);
      zn.state = STATE_FAULT;
      zn.manualValveOpen = false;
      Serial.printf("[FAILSAFE] %s Zone valve open exceeded limit (%d min)! Emergency cutoff.\n", zoneName, maxAllowedMinutes);
      AppLog_Log("FAILSAFE", "%s Zone valve open exceeded limit (%d min)! Emergency cutoff.", zoneName, maxAllowedMinutes);
      BlynkManager_LogEvent("sensor_fault", (String(zoneName) + " Zone: Valve open exceeded safety limit! Emergency cutoff.").c_str());
      BlynkManager_SendStatusData(zoneIdx);
      return;
    }
  }

  // 1. Safety Checks & Notifications
  if (zn.sensorOnline) {
    // Soil Temperature High
    if (zn.temperature > cfg.maxSoilTemp && (nowMs - zn.lastTempHighNotify > 1800000UL)) {
      char msg[64];
      snprintf(msg, sizeof(msg), "%s Zone: Soil temp is %.1f C (Exceeded %.1f C)", zoneName, zn.temperature, cfg.maxSoilTemp);
      AppLog_Log("WARN", "%s Zone Soil temp is %.1f C (Exceeded %.1f C)", zoneName, zn.temperature, cfg.maxSoilTemp);
      BlynkManager_LogEvent("soil_temp_high", msg);
      zn.lastTempHighNotify = nowMs;
    }
    
    // Moisture Critical
    if (zn.humidity < cfg.moistureCritical && (zn.mode == MODE_OFF || !masterEnable || (zn.dailyRuntimeSeconds >= maxDailyM * 60))) {
      if (nowMs - zn.lastCritNotify > 1800000UL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s Zone: Critical moisture %.1f%% but cannot water!", zoneName, zn.humidity);
        AppLog_Log("WARN", "%s Zone Critical moisture %.1f%% but cannot water!", zoneName, zn.humidity);
        BlynkManager_LogEvent("moisture_critical", msg);
        zn.lastCritNotify = nowMs;
      }
    }
  }
    
  // 2. Control Mode State Machine
  if (zn.mode == MODE_OFF) {
    applyValveHardware(zoneIdx, false);
    zn.state = STATE_IDLE;
    return;
  }
  
  if (zn.mode == MODE_MANUAL) {
    // Safety: ปิดระบบหลักระหว่างเปิดน้ำมือ → ปิดวาล์วทันที (เดิมวาล์วเปิดค้างได้)
    if (!masterEnable && zn.manualValveOpen) {
      applyValveHardware(zoneIdx, false);
      zn.manualValveOpen = false;
      zn.state = STATE_IDLE;
      AppLog_Log("VALVE", "%s Zone manual valve closed (master system disabled)", zoneName);
      BlynkManager_SendStatusData(zoneIdx);
      return;
    }

    // Check Manual Duration Auto-cutoff (Always using millis)
    if (zn.manualValveOpen) {
      unsigned long elapsedSec = (nowMs - zn.stateStartTime) / 1000UL;
      if (elapsedSec >= (unsigned long)(cfg.manualDurationMinutes * 60)) {
        applyValveHardware(zoneIdx, false);
        zn.manualValveOpen = false;
        zn.state = STATE_IDLE;
        AppLog_Log("VALVE", "%s Zone manual valve closed (reached %d min limit)", zoneName, cfg.manualDurationMinutes);
        BlynkManager_SendStatusData(zoneIdx);
      }
    }
    
    // Check Idle in Manual Mode > 2 Hours -> Auto return to AUTO
    if (!zn.manualValveOpen && (nowMs - zn.manualStartTime >= 7200000UL)) {
      Serial.printf("[MANUAL TIMEOUT] %s Zone left in Manual > 2h. Auto returning to AUTO.\n", zoneName);
      AppLog_Log("MODE", "%s Zone left in Manual > 2h. Auto returning to AUTO.", zoneName);
      ZoneControl_SetMode(zoneIdx, MODE_AUTO);
    }
    return;
  }
  
  if (zn.mode == MODE_AUTO) {
    // If zone is unplanted, suspend auto watering
    if (cfg.selectedStage == STAGE_UNPLANTED) {
      applyValveHardware(zoneIdx, false);
      zn.state = STATE_IDLE;
      return;
    }

    // Sensor Fault Protection
    if (!zn.sensorOnline) {
      applyValveHardware(zoneIdx, false);
      zn.state = STATE_FAULT;
      if (nowMs - zn.lastFaultNotify > 1800000UL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s Zone: Sensor FAULT! Auto watering suspended.", zoneName);
        AppLog_Log("FAIL", "%s Zone Sensor FAULT! Auto watering suspended.", zoneName);
        BlynkManager_LogEvent("sensor_fault", msg);
        zn.lastFaultNotify = nowMs;
      }
      return;
    }
    
    // Master Enable check
    if (!masterEnable) {
      applyValveHardware(zoneIdx, false);
      zn.state = STATE_IDLE;
      return;
    }
    
    // Max Daily Runtime Reached
    if (zn.dailyRuntimeSeconds >= (maxDailyM * 60)) {
      applyValveHardware(zoneIdx, false);
      zn.state = STATE_IDLE;
      if (nowMs - zn.lastDailyLimitNotify > 1800000UL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s Zone: Reached max daily runtime %d min.", zoneName, maxDailyM);
        AppLog_Log("WARN", "%s Zone reached max daily runtime %d min.", zoneName, maxDailyM);
        BlynkManager_LogEvent("daily_limit", msg);
        zn.lastDailyLimitNotify = nowMs;
      }
      return;
    }
    
    // Phase 1 Item 3: Check No-moisture-rise Lockout
    if (zn.lockoutUntilMs > 0) {
      if (nowMs < zn.lockoutUntilMs) {
        applyValveHardware(zoneIdx, false);
        zn.state = STATE_IDLE;
        return; // ห้ามเริ่มรดระหว่างติด lockout
      } else {
        zn.lockoutUntilMs = 0; // ครบเวลา lockout ปลดล็อกอัตโนมัติ
        AppLog_Log("INFO", "%s Zone 60m lockout expired. Auto control resumed.", zoneName);
      }
    }
    
    // State Transitions
    switch (zn.state) {
      case STATE_FAULT:
      case STATE_IDLE:
        applyValveHardware(zoneIdx, false);
        if (zn.humidity < low) {
          // Start Watering Cycle
          zn.state = STATE_WATERING;
          zn.stateStartTime = nowMs;
          zn.humAtStartOfWatering = zn.humidity;
          zn.consecutivePulses = 1;
          applyValveHardware(zoneIdx, true);
          AppLog_Log("VALVE", "%s Zone AUTO watering started (Hum: %.1f%% < Low: %.1f%%, Pulse: %dm, Target: %.1f%%)",
                     zoneName, zn.humidity, low, pulseM, high);
          BlynkManager_SendStatusData(zoneIdx);
        }
        break;
        
      case STATE_WATERING:
        applyValveHardware(zoneIdx, true);
        // Cutoff if humidity reached High
        if (zn.humidity >= high) {
          applyValveHardware(zoneIdx, false);
          zn.state = STATE_IDLE;
          zn.consecutivePulses = 0;
          AppLog_Log("VALVE", "%s Zone reached high target (%.1f%% >= %.1f%%). Valve CLOSED.", zoneName, zn.humidity, high);
          BlynkManager_SendStatusData(zoneIdx);
          break;
        }
        // Pulse ON timeout (millis)
        if ((nowMs - zn.stateStartTime) >= (unsigned long)(pulseM * 60000UL)) {
          applyValveHardware(zoneIdx, false);
          zn.state = STATE_SOAKING;
          zn.stateStartTime = nowMs;
          AppLog_Log("VALVE", "%s Zone pulse #%d (%dm) ended. Valve CLOSED, SOAK %dm started.", zoneName, zn.consecutivePulses, pulseM, soakM);
          BlynkManager_SendStatusData(zoneIdx);
        }
        break;
        
      case STATE_SOAKING:
        applyValveHardware(zoneIdx, false);
        // Soak timeout (millis)
        if ((nowMs - zn.stateStartTime) >= (unsigned long)(soakM * 60000UL)) {
          // Phase 1 Item 3: Check No Moisture Rise condition (3 pulses and rise < 2%)
          if (zn.consecutivePulses >= 3 && (zn.humidity - zn.humAtStartOfWatering < 2.0f)) {
            zn.state = STATE_IDLE;
            zn.consecutivePulses = 0;
            zn.lockoutUntilMs = nowMs + (60UL * 60000UL); // Lockout for 60 minutes
            AppLog_Log("WARN", "%s Zone Moisture didn't rise after 3 pulses! Locked for 60m.", zoneName);
            if (nowMs - zn.lastNoRiseNotify > 1800000UL) {
              char msg[64];
              snprintf(msg, sizeof(msg), "%s Zone: Moisture didn't rise after 3 pulses! Locked for 60m.", zoneName);
              BlynkManager_LogEvent("no_moisture_rise", msg);
              zn.lastNoRiseNotify = nowMs;
            }
            BlynkManager_SendStatusData(zoneIdx);
            break;
          }
          
          if (zn.humidity < high) {
            // Next Pulse
            zn.state = STATE_WATERING;
            zn.stateStartTime = nowMs;
            zn.consecutivePulses++;
            applyValveHardware(zoneIdx, true);
            AppLog_Log("VALVE", "%s Zone starting pulse #%d (Hum: %.1f%% < High: %.1f%%). Valve OPENED.", zoneName, zn.consecutivePulses, zn.humidity, high);
          } else {
            // Finished Cycle
            zn.state = STATE_IDLE;
            zn.consecutivePulses = 0;
            AppLog_Log("INFO", "%s Zone watering cycle completed successfully (Hum: %.1f%%).", zoneName, zn.humidity);
          }
          BlynkManager_SendStatusData(zoneIdx);
        }
        break;
    }
  }
}

void ZoneControl_Update() {
  unsigned long nowMs = millis();
  
  // Phase 1 Item 6: Modbus polling with backoff (2s if online, 10s if offline)
  // Staggered polling (offset by at least 1000ms) to prevent two RS485 queries blocking the same loop cycle
  unsigned long pollIntervalLeft = zoneLeft.sensorOnline ? 2000UL : 10000UL;
  if (nowMs - lastSensorPollTimeLeft >= pollIntervalLeft) {
    lastSensorPollTimeLeft = nowMs;
    float hum, temp;
    if (Modbus_ReadSoilSensor(zoneLeft.slaveID, hum, temp)) {
      zoneLeft.humidity = hum;
      zoneLeft.temperature = temp;
      zoneLeft.failCount = 0;
      if (!zoneLeft.sensorOnline) {
        zoneLeft.sensorOnline = true;
        AppLog_Log("SENSOR", "Zone Left (ID: 1) restored ONLINE (Hum: %.1f%%, Temp: %.1f C)", hum, temp);
      }
    } else {
      zoneLeft.failCount++;
      if (zoneLeft.failCount >= 5 && zoneLeft.sensorOnline) {
        zoneLeft.sensorOnline = false;
        AppLog_Log("SENSOR", "Zone Left (ID: 1) went OFFLINE (5 timeouts)", zoneLeft.slaveID);
      }
    }
  }
  
  unsigned long pollIntervalRight = zoneRight.sensorOnline ? 2000UL : 10000UL;
  if ((nowMs - lastSensorPollTimeRight >= pollIntervalRight) && (nowMs - lastSensorPollTimeLeft >= 800UL)) {
    lastSensorPollTimeRight = nowMs;
    float hum, temp;
    if (Modbus_ReadSoilSensor(zoneRight.slaveID, hum, temp)) {
      zoneRight.humidity = hum;
      zoneRight.temperature = temp;
      zoneRight.failCount = 0;
      if (!zoneRight.sensorOnline) {
        zoneRight.sensorOnline = true;
        AppLog_Log("SENSOR", "Zone Right (ID: 2) restored ONLINE (Hum: %.1f%%, Temp: %.1f C)", hum, temp);
      }
    } else {
      zoneRight.failCount++;
      if (zoneRight.failCount >= 5 && zoneRight.sensorOnline) {
        zoneRight.sensorOnline = false;
        AppLog_Log("SENSOR", "Zone Right (ID: 2) went OFFLINE (5 timeouts)", zoneRight.slaveID);
      }
    }
  }
  
  // Second Timer Ticks: Runtime Accumulation & State Machine Update
  if (nowMs - lastSecondTick >= 1000UL) {
    lastSecondTick = nowMs;
    
    if (zoneLeft.state == STATE_WATERING) zoneLeft.dailyRuntimeSeconds++;
    if (zoneRight.state == STATE_WATERING) zoneRight.dailyRuntimeSeconds++;
    
    // Phase 1 Item 1: Read RTC or calculate fallback
    RTCDateTime currentNow;
    bool timeValid = false;
    
    if (RTC_GetTime(currentNow)) {
      lastGoodTime = currentNow;
      lastGoodTimeMillis = nowMs;
      hasGoodTime = true;
      timeValid = true;
    } else if (hasGoodTime) {
      // Fallback: estimate time using millis elapsed
      unsigned long elapsedSec = (nowMs - lastGoodTimeMillis) / 1000UL;
      currentNow = lastGoodTime;
      // Add seconds roughly for day tracking
      currentNow.second += (elapsedSec % 60);
      if (currentNow.second >= 60) {
        currentNow.second %= 60;
        currentNow.minute++;
      }
      timeValid = true;
    }

    // Phase 1 Item 7: Midnight Reset Check with YYYYMMDD
    if (timeValid && currentNow.year >= 2024) {
      int nowDateInt = currentNow.year * 10000 + currentNow.month * 100 + currentNow.day;
      if (lastSavedDateInt != -1 && nowDateInt != lastSavedDateInt) {
        AppLog_Log("RESET", "Midnight transition detected: %04d-%02d-%02d", currentNow.year, currentNow.month, currentNow.day);
        ZoneControl_ResetDailyRuntime();
        lastSavedDateInt = nowDateInt;
      }
    }
    
    // Hourly Status Heartbeat (Logs once every 60 minutes)
    static unsigned long lastHeartbeatMs = 0;
    if (nowMs - lastHeartbeatMs >= 3600000UL) {
      lastHeartbeatMs = nowMs;
      const char* stL = (zoneLeft.state == STATE_WATERING) ? "WATERING" : ((zoneLeft.state == STATE_SOAKING) ? "SOAKING" : ((zoneLeft.state == STATE_FAULT) ? "FAULT" : "IDLE"));
      const char* stR = (zoneRight.state == STATE_WATERING) ? "WATERING" : ((zoneRight.state == STATE_SOAKING) ? "SOAKING" : ((zoneRight.state == STATE_FAULT) ? "FAULT" : "IDLE"));
      char stageL[20], stageR[20];
      if (configLeft.selectedStage == STAGE_UNPLANTED) strcpy(stageL, "Unplanted");
      else snprintf(stageL, sizeof(stageL), "St:%d, DAP:%d", configLeft.selectedStage + 1, zoneLeft.currentDAP);
      if (configRight.selectedStage == STAGE_UNPLANTED) strcpy(stageR, "Unplanted");
      else snprintf(stageR, sizeof(stageR), "St:%d, DAP:%d", configRight.selectedStage + 1, zoneRight.currentDAP);

      AppLog_Log("STATUS", "Left: %.1f%%, %.1f C [%s, %s, Run:%dm] | Right: %.1f%%, %.1f C [%s, %s, Run:%dm] | Heap: %u B",
                 zoneLeft.humidity, zoneLeft.temperature, stL, stageL, zoneLeft.dailyRuntimeSeconds / 60,
                 zoneRight.humidity, zoneRight.temperature, stR, stageR, zoneRight.dailyRuntimeSeconds / 60,
                 ESP.getFreeHeap());
    }

    // Phase 1 Item 8: Reduce NVS writes - periodic save every 5 mins only if value changed
    if (nowMs - lastNvsSaveMs >= 300000UL) {
      lastNvsSaveMs = nowMs;
      if (timeValid && currentNow.year >= 2024) {
        int nowDateInt = currentNow.year * 10000 + currentNow.month * 100 + currentNow.day;
        if (zoneLeft.dailyRuntimeSeconds != lastSavedLeftSec || zoneRight.dailyRuntimeSeconds != lastSavedRightSec || nowDateInt != lastSavedDateInt) {
          Storage_SaveDailyRuntime(zoneLeft.dailyRuntimeSeconds, zoneRight.dailyRuntimeSeconds, nowDateInt);
          lastSavedLeftSec = zoneLeft.dailyRuntimeSeconds;
          lastSavedRightSec = zoneRight.dailyRuntimeSeconds;
          lastSavedDateInt = nowDateInt;
        }
      }
    }
    
    // Update zone state machines ALWAYS, even if RTC failed
    updateSingleZone(0, currentNow, timeValid);
    updateSingleZone(1, currentNow, timeValid);
  }
}
