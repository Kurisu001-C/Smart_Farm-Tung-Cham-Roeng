#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>

#include "Config.h"
#include "Hardware.h"
#include "Storage.h"
#include "ModbusSensor.h"
#include "ZoneControl.h"
#include "BlynkManager.h"
#include "WebPortal.h"
#include "AppLog.h"

static unsigned long lastNTPAttempt = 0;
static bool ntpSynced = false;

void syncNTPtoRTC() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    // Timeoutสั้นลงเป็น 800ms ป้องกันการบล็อก
    if (getLocalTime(&timeinfo, 800)) {
      RTCDateTime dt;
      dt.year   = timeinfo.tm_year + 1900;
      dt.month  = timeinfo.tm_mon + 1;
      dt.day    = timeinfo.tm_mday;
      dt.hour   = timeinfo.tm_hour;
      dt.minute = timeinfo.tm_min;
      dt.second = timeinfo.tm_sec;
      RTC_SetTime(dt);
      ntpSynced = true;
      Serial.println("[NTP] Time synchronized and saved to RTC.");
      AppLog_Log("NTP", "Time synchronized and saved to RTC: %04d-%02d-%02d %02d:%02d:%02d",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
      Serial.println("[NTP] Sync attempt timed out, will retry in 60s.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Wait USB CDC port if connected
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 2000)) {
    delay(10);
  }
  delay(300);
  
  Serial.println("\n==============================================");
  Serial.println("  Smart Farm Dual Zone Controller (ESP32-S3)");
  Serial.println("  Firmware: " FIRMWARE_VERSION);
  Serial.println("==============================================");

  // Phase 1 Item 2: Initialize Task Watchdog (15 seconds)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = 15000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  if (esp_task_wdt_init(&twdt_config) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&twdt_config);
  }
#else
  esp_task_wdt_init(15, true);
#endif
  esp_task_wdt_add(NULL);

  // 1. Storage & Config
  Storage_Init();

  // 1.1 App Logging (Flash + RAM buffer)
  AppLog_Init();
  AppLog_Log("SYSTEM", "Dual Zone Farm Controller booted (v%s, Free Heap: %u B)", FIRMWARE_VERSION, ESP.getFreeHeap());

  // 2. Hardware (Relays, Expander, RTC)
  Hardware_Init();

  // 3. Modbus RS485 (Soil TH model)
  Modbus_Init();

  // 4. Zone Control State Machine
  ZoneControl_Init();

  // 5. Dual Wi-Fi (AP + STA)
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  WiFi.setSleep(false);                 // ปิด Modem Sleep ให้ Wi-Fi ปล่อยสัญญาณต่อเนื่อง
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // ปรับกำลังส่งสูงสุด 19.5 dBm

  if (strlen(apPass) < 8) {
    strncpy(apPass, DEFAULT_AP_PASS, sizeof(apPass) - 1);
  }

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  bool apStarted = false;
  for (int retry = 0; retry < 3; retry++) {
    if (WiFi.softAP(DEFAULT_AP_SSID, apPass, 1, 0, 4)) {
      apStarted = true;
      break;
    }
    delay(200);
  }

  if (!apStarted) {
    Serial.println("[WiFi AP] Failed with password, starting Open AP fallback...");
    WiFi.softAP(DEFAULT_AP_SSID, NULL, 1, 0, 4);
  }

  Serial.printf("[WiFi AP] Started: %s (IP: %s)\n", DEFAULT_AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("[WiFi AP] Password: %s\n", apPass);
  AppLog_Log("WIFI", "SoftAP started: %s (IP: %s)", DEFAULT_AP_SSID, WiFi.softAPIP().toString().c_str());

  // RGB indicator (เขียว/ฟ้า = AP และระบบพร้อมทำงาน)
  pinMode(GPIO_PIN_RGB, OUTPUT);
  neopixelWrite(GPIO_PIN_RGB, 30, 0, 30);

  if (strlen(wifiSSID) > 0) {
    Serial.printf("[WiFi STA] Connecting to %s...\n", wifiSSID);
    WiFi.begin(wifiSSID, wifiPass);
    unsigned long staWait = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - staWait < 4000)) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi STA] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      AppLog_Log("WIFI", "STA connected to %s (IP: %s)", wifiSSID, WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WiFi STA] Not connected immediately. Stopping scan to keep SoftAP fast.");
      WiFi.disconnect(false); // ปิดการสแกนคลื่นชั่วคราว เพื่อไม่ให้กวน SoftAP
    }
  }

  // 6. Web Portal
  WebPortal_Init();

  // 7. Blynk MQTT Manager
  BlynkManager_Init();
}

static unsigned long lastStaRetry = 0;
static bool staConnecting = false;
static unsigned long staConnectStart = 0;

static void updateStaReconnect() {
  if (strlen(wifiSSID) == 0) return;
  
  if (WiFi.status() == WL_CONNECTED) {
    if (staConnecting) {
      AppLog_Log("WIFI", "STA reconnected to %s (IP: %s)", wifiSSID, WiFi.localIP().toString().c_str());
    }
    staConnecting = false;
    return;
  }
  
  unsigned long now = millis();
  
  // หากกำลังพยายามเชื่อมต่อ ให้รอผลไม่เกิน 5 วินาที
  if (staConnecting) {
    if (now - staConnectStart >= 5000UL) {
      staConnecting = false;
      Serial.println("[WiFi STA] Reconnect timeout. Pausing scan to protect SoftAP.");
      WiFi.disconnect(false); // ตัดการสแกนคลื่น คืนช่องสัญญาณ 100% ให้ SoftAP
      lastStaRetry = now;
    }
    return;
  }
  
  // ลองเชื่อมต่อใหม่ทุกๆ 60 วินาที
  if (now - lastStaRetry >= 60000UL) {
    lastStaRetry = now;
    staConnecting = true;
    staConnectStart = now;
    Serial.printf("[WiFi STA] Periodic reconnect to %s...\n", wifiSSID);
    WiFi.begin(wifiSSID, wifiPass);
  }
}

void loop() {
  // Feed Task Watchdog
  esp_task_wdt_reset();

  // Update Wi-Fi Web Server
  WebPortal_Update();

  // App Logging periodic flush (1 hour / buffer threshold)
  AppLog_Update();

  // Non-blocking Wi-Fi STA Maintenance
  updateStaReconnect();

  // Update Core Zone Control & Sensors
  ZoneControl_Update();

  // Update Blynk Cloud Sync (MQTT)
  BlynkManager_Update();

  // Phase 1 Item 4: NTP Sync with backoff (60s if not synced, 6 hours if synced)
  unsigned long ntpInterval = ntpSynced ? 21600000UL : 60000UL;
  if (WiFi.status() == WL_CONNECTED && (millis() - lastNTPAttempt > ntpInterval)) {
    lastNTPAttempt = millis();
    syncNTPtoRTC();
  }
}
