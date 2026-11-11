#include "BlynkManager.h"
#include "Config.h"
#include "Storage.h"
#include "ZoneControl.h"
#include "Hardware.h"
#include "AppLog.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>
#include <esp_idf_version.h>

/*****************************************************************************
 * ISRG Root X1 CA Certificate (Let's Encrypt / Blynk Cloud TLS)
 * Valid until: 2035-06-04
 *****************************************************************************/
static const char BLYNK_ROOT_CA[] = 
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

/*****************************************************************************
 * Internal State & Queue Definitions
 *****************************************************************************/
struct DownlinkItem {
  char topic[64];
  char payload[192];
};

static esp_mqtt_client_handle_t mqttClient = NULL;
static QueueHandle_t downlinkQueue = NULL;
static bool mqttConnected = false;
static bool needFullSync = false;
static bool needReconnect = false;

static char brokerHost[64] = "blynk.cloud";
static int brokerPort = 8883;

// Quota & Rate Limiting (< 2400 msgs/day)
static int dailyMessageCount = 0;
static int lastCountDay = -1;
static bool quotaWarnSent = false;
static unsigned long lastTelemetrySync = 0;
static unsigned long lastStatusSync = 0;
static unsigned long lastThrottlePublishMs = 0;

// ถ้าสั่งส่งสถานะถี่เกิน (ภายใน 500ms) จะไม่ทิ้ง แต่ตั้งธงไว้ แล้วส่งตามใน BlynkManager_Update()
// (เดิมถูกทิ้งเงียบ ทำให้ State_L/R ค้างค่าเก่าได้นานถึง ~2 นาที)
static const unsigned long PUBLISH_THROTTLE_MS = 500UL;
static bool pendingStatusSync = false;   // ต้องส่ง ControlState + Status + Telemetry
static bool pendingSensorSync = false;   // ต้องส่ง Telemetry อย่างเดียว

static float prevHumL = -99.0f, prevTempL = -99.0f;
static float prevHumR = -99.0f, prevTempR = -99.0f;

static bool canSendMessage() {
  RTCDateTime now;
  if (RTC_GetTime(now)) {
    if (now.day != lastCountDay) {
      lastCountDay = now.day;
      dailyMessageCount = 0;
      quotaWarnSent = false;
    }
  }
  return (dailyMessageCount < 2400);
}

static void countMsg() {
  dailyMessageCount++;
}

static const char* stateToString(ZoneState st) {
  switch (st) {
    case STATE_IDLE:     return "IDLE";
    case STATE_WATERING: return "WATERING";
    case STATE_SOAKING:  return "SOAKING";
    case STATE_FAULT:    return "FAULT";
    default:             return "UNKNOWN";
  }
}

/*****************************************************************************
 * MQTT Event Callback (Runs in esp_mqtt background task)
 *****************************************************************************/
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      mqttConnected = true;
      Serial.println("[Blynk MQTT] Connected to Cloud broker over TLS!");
      AppLog_Log("BLYNK", "Connected to Blynk Cloud MQTT broker over TLS");
      // 1. Subscribe to all downlinks
      esp_mqtt_client_subscribe(mqttClient, "downlink/#", 0);
      // 2. Publish device info
      {
        String info = "{\"tmpl\":\"" BLYNK_TEMPLATE_ID "\",\"ver\":\"" FIRMWARE_VERSION "\"}";
        esp_mqtt_client_publish(mqttClient, "info/mcu", info.c_str(), info.length(), 0, 0);
      }
      needFullSync = true;
      break;

    case MQTT_EVENT_DISCONNECTED:
      mqttConnected = false;
      Serial.println("[Blynk MQTT] Disconnected from Cloud broker.");
      AppLog_Log("BLYNK", "Disconnected from Blynk Cloud broker");
      break;

    case MQTT_EVENT_DATA:
      if (downlinkQueue != NULL) {
        DownlinkItem item;
        int tLen = (event->topic_len < (int)sizeof(item.topic) - 1) ? event->topic_len : (sizeof(item.topic) - 1);
        memcpy(item.topic, event->topic, tLen);
        item.topic[tLen] = '\0';

        int dLen = (event->data_len < (int)sizeof(item.payload) - 1) ? event->data_len : (sizeof(item.payload) - 1);
        memcpy(item.payload, event->data, dLen);
        item.payload[dLen] = '\0';

        xQueueSend(downlinkQueue, &item, 0); // Non-blocking push
      }
      break;

    case MQTT_EVENT_ERROR:
      Serial.println("[Blynk MQTT] Transport/TLS error occurred.");
      break;

    default:
      break;
  }
}

/*****************************************************************************
 * MQTT Client Lifecycle
 *****************************************************************************/
static void startMqttClient() {
  if (mqttClient != NULL) {
    esp_mqtt_client_stop(mqttClient);
    esp_mqtt_client_destroy(mqttClient);
    mqttClient = NULL;
    mqttConnected = false;
  }

  if (strlen(blynkAuth) == 0) {
    Serial.println("[Blynk MQTT] Auth Token is empty, skipping connection.");
    return;
  }

  esp_mqtt_client_config_t mqtt_cfg = {};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  mqtt_cfg.broker.address.hostname = brokerHost;
  mqtt_cfg.broker.address.port = brokerPort;
  mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
  mqtt_cfg.broker.verification.certificate = BLYNK_ROOT_CA;
  mqtt_cfg.credentials.username = "device";
  mqtt_cfg.credentials.authentication.password = blynkAuth;
#else
  mqtt_cfg.host = brokerHost;
  mqtt_cfg.port = brokerPort;
  mqtt_cfg.transport = MQTT_TRANSPORT_OVER_SSL;
  mqtt_cfg.cert_pem = BLYNK_ROOT_CA;
  mqtt_cfg.username = "device";
  mqtt_cfg.password = blynkAuth;
#endif

  mqttClient = esp_mqtt_client_init(&mqtt_cfg);
  if (mqttClient != NULL) {
    esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqttClient);
    Serial.printf("[Blynk MQTT] Client initiated -> connecting to %s:%d\n", brokerHost, brokerPort);
  } else {
    Serial.println("[Blynk MQTT] Failed to initialize MQTT client struct!");
  }
}

static void parseRedirectURI(const char* uri) {
  String s = uri;
  s.trim();
  if (s.startsWith("mqtts://")) s = s.substring(8);
  else if (s.startsWith("mqtt://")) s = s.substring(7);

  int colon = s.indexOf(':');
  int port = 8883;
  String host = s;
  if (colon != -1) {
    host = s.substring(0, colon);
    port = s.substring(colon + 1).toInt();
    if (port <= 0) port = 8883;
  }
  int slash = host.indexOf('/');
  if (slash != -1) host = host.substring(0, slash);

  Serial.printf("[Blynk MQTT] GeoDNS redirect detected -> New host: %s:%d\n", host.c_str(), port);
  strncpy(brokerHost, host.c_str(), sizeof(brokerHost) - 1);
  brokerPort = port;
  needReconnect = true;
}

/*****************************************************************************
 * Uplink Batches (via batch_ds)
 *****************************************************************************/
void BlynkManager_SendTelemetry() {
  if (!mqttConnected || !canSendMessage() || mqttClient == NULL) return;

  StaticJsonDocument<512> doc;
  doc["Humidity_L"]     = serialized(String(zoneLeft.humidity, 1));
  doc["Temp_L"]         = serialized(String(zoneLeft.temperature, 1));
  doc["State_L"]        = stateToString(zoneLeft.state);
  doc["SensorOnline_L"] = zoneLeft.sensorOnline ? 1 : 0;

  doc["Humidity_R"]     = serialized(String(zoneRight.humidity, 1));
  doc["Temp_R"]         = serialized(String(zoneRight.temperature, 1));
  doc["State_R"]        = stateToString(zoneRight.state);
  doc["SensorOnline_R"] = zoneRight.sensorOnline ? 1 : 0;

  String output;
  serializeJson(doc, output);
  esp_mqtt_client_publish(mqttClient, "batch_ds", output.c_str(), output.length(), 0, 0);
  countMsg();
  lastThrottlePublishMs = millis();
}

void BlynkManager_SendControlState() {
  if (!mqttConnected || !canSendMessage() || mqttClient == NULL) return;

  StaticJsonDocument<384> doc;
  doc["Master"]  = masterEnable ? 1 : 0;
  doc["Mode_L"]  = (int)zoneLeft.mode;
  doc["Valve_L"] = Relay_GetChannel(VALVE_LEFT_CH) ? 1 : 0;
  doc["Mode_R"]  = (int)zoneRight.mode;
  doc["Valve_R"] = Relay_GetChannel(VALVE_RIGHT_CH) ? 1 : 0;

  String output;
  serializeJson(doc, output);
  esp_mqtt_client_publish(mqttClient, "batch_ds", output.c_str(), output.length(), 0, 0);
  countMsg();
  lastThrottlePublishMs = millis();
}

void BlynkManager_SendStatus() {
  if (!mqttConnected || !canSendMessage() || mqttClient == NULL) return;

  StaticJsonDocument<512> doc;
  doc["DAP_L"]       = (configLeft.selectedStage == STAGE_UNPLANTED) ? 0 : zoneLeft.currentDAP;
  doc["DailyTime_L"] = zoneLeft.dailyRuntimeSeconds / 60;
  doc["Stage_L"]     = (configLeft.selectedStage == STAGE_UNPLANTED) ? 0 : (configLeft.selectedStage + 1);
  doc["DAP_R"]       = (configRight.selectedStage == STAGE_UNPLANTED) ? 0 : zoneRight.currentDAP;
  doc["DailyTime_R"] = zoneRight.dailyRuntimeSeconds / 60;
  doc["Stage_R"]     = (configRight.selectedStage == STAGE_UNPLANTED) ? 0 : (configRight.selectedStage + 1);
  doc["MsgQuota"]    = dailyMessageCount;

  String output;
  serializeJson(doc, output);
  esp_mqtt_client_publish(mqttClient, "batch_ds", output.c_str(), output.length(), 0, 0);
  countMsg();
  lastThrottlePublishMs = millis();
}

void BlynkManager_SendZoneConfig(int zoneIdx) {
  if (!mqttConnected || !canSendMessage() || mqttClient == NULL) return;

  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  String sfx = (zoneIdx == 0) ? "_L" : "_R";

  int presetCode = 100 + (((int)cfg.cropGroup % 4) * 10) + (((int)cfg.soilType % 4) * 2) + (cfg.useGrowthStages ? 1 : 0);

  StaticJsonDocument<640> doc;
  doc["Crop" + sfx]      = (int)cfg.cropGroup;
  doc["Soil" + sfx]      = (int)cfg.soilType;
  doc["UseStages" + sfx] = cfg.useGrowthStages ? 1 : 0;
  doc["Preset" + sfx]    = presetCode;
  doc["MoistLow" + sfx]  = serialized(String(cfg.moistureLow, 1));
  doc["MoistHigh" + sfx] = serialized(String(cfg.moistureHigh, 1));
  doc["MoistCrit" + sfx] = serialized(String(cfg.moistureCritical, 1));
  doc["Pulse" + sfx]     = cfg.pulseOnMinutes;
  doc["Soak" + sfx]      = cfg.soakMinutes;
  doc["MaxDaily" + sfx]  = cfg.maxDailyRuntimeMinutes;
  doc["MaxTemp" + sfx]   = serialized(String(cfg.maxSoilTemp, 1));
  doc["ManualCut" + sfx] = cfg.manualDurationMinutes;

  String output;
  serializeJson(doc, output);
  esp_mqtt_client_publish(mqttClient, "batch_ds", output.c_str(), output.length(), 0, 0);
  countMsg();
  lastThrottlePublishMs = millis();
}

static void sendFullStatusNow() {
  pendingStatusSync = false;
  pendingSensorSync = false;            // Telemetry รวมอยู่ในชุดนี้แล้ว
  BlynkManager_SendControlState();
  BlynkManager_SendStatus();
  BlynkManager_SendTelemetry();
}

void BlynkManager_SendStatusData(int zoneIdx) {
  (void)zoneIdx;
  if (millis() - lastThrottlePublishMs > PUBLISH_THROTTLE_MS) sendFullStatusNow();
  else pendingStatusSync = true;        // ส่งตามทีหลัง ไม่ทิ้ง
}

void BlynkManager_SendSensorData(int zoneIdx) {
  (void)zoneIdx;
  if (millis() - lastThrottlePublishMs > PUBLISH_THROTTLE_MS) {
    pendingSensorSync = false;
    BlynkManager_SendTelemetry();
  } else {
    pendingSensorSync = true;
  }
}

/** ส่งข้อมูลที่ค้างไว้จากการ throttle เมื่อพ้นช่วง 500ms แล้ว (เรียกจาก BlynkManager_Update) */
static void flushPendingSync() {
  if (!mqttConnected) return;
  if (!pendingStatusSync && !pendingSensorSync) return;
  if (millis() - lastThrottlePublishMs <= PUBLISH_THROTTLE_MS) return;

  if (pendingStatusSync) {
    sendFullStatusNow();
  } else {
    pendingSensorSync = false;
    BlynkManager_SendTelemetry();
  }
}

/*****************************************************************************
 * High-Priority Events Dispatch (event/{EVENTCODE})
 *****************************************************************************/
void BlynkManager_LogEvent(const char* eventCode, const char* desc) {
  if (!mqttConnected || mqttClient == NULL) return;
  if (dailyMessageCount >= 2400) return; // Hard budget safety

  String topic = "event/" + String(eventCode);
  String payload = (desc != NULL && strlen(desc) > 0) ? String(desc) : "";
  esp_mqtt_client_publish(mqttClient, topic.c_str(), payload.c_str(), payload.length(), 0, 0);
  countMsg();
  Serial.printf("[Blynk Event] Published: %s -> %s\n", topic.c_str(), payload.c_str());
}

/*****************************************************************************
 * Inbound Downlink Processing (Non-blocking queue execution)
 *****************************************************************************/
static void resetZoneRuntimeFromBlynk(int zoneIdx) {
  if (zoneIdx == 0) {
    zoneLeft.dailyRuntimeSeconds = 0;
    zoneLeft.lockoutUntilMs = 0;
    zoneLeft.consecutivePulses = 0;
  } else {
    zoneRight.dailyRuntimeSeconds = 0;
    zoneRight.lockoutUntilMs = 0;
    zoneRight.consecutivePulses = 0;
  }
  RTCDateTime now;
  int nowDateInt = 0;
  if (RTC_GetTime(now)) {
    nowDateInt = now.year * 10000 + now.month * 100 + now.day;
  }
  Storage_SaveDailyRuntime(zoneLeft.dailyRuntimeSeconds, zoneRight.dailyRuntimeSeconds, nowDateInt);
  AppLog_Log("RESET", "Zone %s daily watering runtime reset to 0 min from Blynk.", (zoneIdx == 0) ? "Left" : "Right");
  BlynkManager_SendStatus();
}

/**
 * เริ่มรอบปลูกใหม่วันนี้ใน cfg/zn (ยังไม่บันทึก NVS): วันเริ่มปลูก = วันนี้, DAP = 0, ระยะที่ 1
 * ถ้า RTC ยังไม่มีเวลาที่ถูกต้อง จะคงวันเริ่มปลูกเดิมไว้ และคืนค่า false
 */
static bool startCropCycleToday(ZoneConfig& cfg, ZoneRuntime& zn) {
  RTCDateTime now;
  bool timeOk = RTC_GetTime(now) && now.year >= 2024;
  if (timeOk) {
    cfg.startYear = now.year;
    cfg.startMonth = now.month;
    cfg.startDay = now.day;
  }
  cfg.dapOffset = 0;
  cfg.selectedStage = 0;
  zn.currentDAP = 0;
  zn.currentStageIndex = 0;
  return timeOk;
}

static void resetZoneDapFromBlynk(int zoneIdx) {
  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  if (!startCropCycleToday(cfg, zn)) {
    AppLog_Log("WARN", "Zone %s DAP reset: RTC time invalid, start date unchanged.", (zoneIdx == 0) ? "Left" : "Right");
  }
  Storage_SaveZoneConfig(zoneIdx, cfg);
  AppLog_Log("CROP", "Zone %s crop cycle reset from Blynk (DAP=0, Stage=1).", (zoneIdx == 0) ? "Left" : "Right");
  BlynkManager_SendStatus();
}

static void applyZonePresetFromBlynk(int zoneIdx, SoilType soil, CropGroup crop, bool useStages) {
  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  bool wasUnplanted = (cfg.selectedStage == STAGE_UNPLANTED);
  Storage_ApplyCombinedPreset(cfg, soil, crop);
  cfg.useGrowthStages = useStages;
  cfg.dapOffset = 0;
  if (wasUnplanted) {
    // แปลงว่าง → ตั้งสูตร = เริ่มปลูกวันนี้
    // (เดิมเปลี่ยนเป็น "ปลูกแล้ว" แต่ใช้วันเริ่มปลูกเก่า ทำให้ DAP สูงผิดและเด้งไประยะ 4 ทันที)
    startCropCycleToday(cfg, zn);
  }
  Storage_SaveZoneConfig(zoneIdx, cfg);
  AppLog_Log("CROP", "Zone %s preset updated from Blynk (Crop=%d, Soil=%d, UseStages=%d)",
             (zoneIdx == 0) ? "Left" : "Right", (int)crop, (int)soil, useStages ? 1 : 0);
  BlynkManager_SendZoneConfig(zoneIdx);
  BlynkManager_SendStatus();
}

static void processConfigField(int zoneIdx, const String& field, float val) {
  ZoneConfig tmp = (zoneIdx == 0) ? configLeft : configRight;

  if (field.equalsIgnoreCase("Crop")) {
    int c = constrain((int)val, 0, 3);
    applyZonePresetFromBlynk(zoneIdx, tmp.soilType, (CropGroup)c, tmp.useGrowthStages);
    return;
  }
  if (field.equalsIgnoreCase("Soil")) {
    int s = constrain((int)val, 0, 3);
    applyZonePresetFromBlynk(zoneIdx, (SoilType)s, tmp.cropGroup, tmp.useGrowthStages);
    return;
  }
  if (field.equalsIgnoreCase("UseStages")) {
    applyZonePresetFromBlynk(zoneIdx, tmp.soilType, tmp.cropGroup, ((int)val == 1));
    return;
  }
  if (field.equalsIgnoreCase("Preset")) {
    int code = (int)val;
    if (code >= 100 && code < 140) {
      int rem = code - 100;
      int crop = constrain(rem / 10, 0, 3);
      int soil = constrain((rem % 10) / 2, 0, 3);
      bool useStg = (rem % 2) == 1;
      applyZonePresetFromBlynk(zoneIdx, (SoilType)soil, (CropGroup)crop, useStg);
    }
    return;
  }
  if (field.equalsIgnoreCase("ResetRuntime")) {
    resetZoneRuntimeFromBlynk(zoneIdx);
    return;
  }
  if (field.equalsIgnoreCase("ResetDAP")) {
    resetZoneDapFromBlynk(zoneIdx);
    return;
  }

  if (field.equalsIgnoreCase("MoistLow")) tmp.moistureLow = val;
  else if (field.equalsIgnoreCase("MoistHigh")) tmp.moistureHigh = val;
  else if (field.equalsIgnoreCase("MoistCrit")) tmp.moistureCritical = val;
  else if (field.equalsIgnoreCase("Pulse")) tmp.pulseOnMinutes = (int)val;
  else if (field.equalsIgnoreCase("Soak")) tmp.soakMinutes = (int)val;
  else if (field.equalsIgnoreCase("MaxDaily")) tmp.maxDailyRuntimeMinutes = (int)val;
  else if (field.equalsIgnoreCase("MaxTemp")) tmp.maxSoilTemp = val;
  else if (field.equalsIgnoreCase("ManualCut")) tmp.manualDurationMinutes = (int)val;
  else return;

  String err;
  if (Config_Validate(tmp, err)) {
    if (zoneIdx == 0) configLeft = tmp;
    else configRight = tmp;
    Storage_SaveZoneConfig(zoneIdx, tmp);
    BlynkManager_SendZoneConfig(zoneIdx);
    Serial.printf("[Blynk Config] Zone %d updated %s = %.1f\n", zoneIdx, field.c_str(), val);
  } else {
    Serial.printf("[Blynk Config REJECTED] Zone %d invalid %s: %s\n", zoneIdx, field.c_str(), err.c_str());
  }
}

static void processZoneStageOrCode(int zoneIdx, int st) {
  ZoneConfig& cfg = (zoneIdx == 0) ? configLeft : configRight;
  ZoneRuntime& zn = (zoneIdx == 0) ? zoneLeft : zoneRight;
  const char* zName = (zoneIdx == 0) ? "Left" : "Right";

  if (st == 98) {
    resetZoneRuntimeFromBlynk(zoneIdx);
    return;
  }
  if (st == 99) {
    resetZoneDapFromBlynk(zoneIdx);
    return;
  }
  if (st >= 100 && st < 140) {
    int rem = st - 100;
    int crop = constrain(rem / 10, 0, 3);
    int soil = constrain((rem % 10) / 2, 0, 3);
    bool useStg = (rem % 2) == 1;
    applyZonePresetFromBlynk(zoneIdx, (SoilType)soil, (CropGroup)crop, useStg);
    return;
  }

  if (st == 0) {
    cfg.selectedStage = STAGE_UNPLANTED;
    zn.currentStageIndex = STAGE_UNPLANTED;
    zn.currentDAP = 0;
    Storage_SaveZoneConfig(zoneIdx, cfg);
    BlynkManager_SendStatus();
    AppLog_Log("STAGE", "Zone %s set to UNPLANTED (ไม่ได้ปลูก / พักแปลง) from Blynk. DAP stopped.", zName);
  } else if (st >= 1 && st <= 4) {
    cfg.selectedStage = st - 1;
    zn.currentStageIndex = st - 1;
    Storage_SaveZoneConfig(zoneIdx, cfg);
    BlynkManager_SendStatus();
    AppLog_Log("STAGE", "Zone %s Stage set to %d from Blynk", zName, st);
    Serial.printf("[Blynk] Zone %s Stage set to %d\n", zName, st);
  }
}

static void processDownlinkItem(const String& topic, const String& val) {
  Serial.printf("[Blynk Downlink] %s = %s\n", topic.c_str(), val.c_str());

  if (topic == "downlink/redirect") {
    parseRedirectURI(val.c_str());
    return;
  }

  if (topic == "downlink/reboot") {
    Serial.println("[Blynk Downlink] Remote reboot command acknowledged.");
    delay(500);
    ESP.restart();
    return;
  }

  if (!topic.startsWith("downlink/ds/")) return;

  String ds = topic.substring(12); // Trim "downlink/ds/"
  
  // JSON fallback for compatibility
  if (val.startsWith("{")) {
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, val);
    if (!err) {
      if (doc.containsKey("master")) Storage_SaveMasterEnable(doc["master"] == 1);
      if (doc.containsKey("L_mode")) ZoneControl_SetMode(0, (ZoneMode)doc["L_mode"].as<int>());
      if (doc.containsKey("R_mode")) ZoneControl_SetMode(1, (ZoneMode)doc["R_mode"].as<int>());
      if (doc.containsKey("L_valve")) ZoneControl_SetManualSwitch(0, doc["L_valve"] == 1);
      if (doc.containsKey("R_valve")) ZoneControl_SetManualSwitch(1, doc["R_valve"] == 1);
      if (doc.containsKey("resetRuntime")) resetZoneRuntimeFromBlynk(doc["resetRuntime"].as<int>());
      if (doc.containsKey("resetDap")) resetZoneDapFromBlynk(doc["resetDap"].as<int>());
      if (doc.containsKey("presetZone")) {
        int z = doc["presetZone"].as<int>();
        int c = doc["crop"] | 0;
        int s = doc["soil"] | 1;
        bool u = (doc["useStages"] | 0) == 1;
        applyZonePresetFromBlynk(z, (SoilType)s, (CropGroup)c, u);
      }
      BlynkManager_SendControlState();
      return;
    }
  }

  // Master Switch
  if (ds.equalsIgnoreCase("Master") || ds.equalsIgnoreCase("V0")) {
    Storage_SaveMasterEnable(val.toInt() == 1);
    BlynkManager_SendControlState();
    return;
  }

  // Zone Left Controls
  if (ds.equalsIgnoreCase("Mode_L") || ds.equalsIgnoreCase("L_mode") || ds.equalsIgnoreCase("V4")) {
    int m = val.toInt();
    if (m >= 98) {
      processZoneStageOrCode(0, m);
    } else {
      ZoneControl_SetMode(0, (ZoneMode)constrain(m, 0, 2));
      BlynkManager_SendControlState();
    }
    return;
  }
  if (ds.equalsIgnoreCase("Valve_L") || ds.equalsIgnoreCase("L_valve") || ds.equalsIgnoreCase("V5")) {
    ZoneControl_SetManualSwitch(0, val.toInt() == 1);
    BlynkManager_SendControlState();
    return;
  }
  if (ds.equalsIgnoreCase("Stage_L") || ds.equalsIgnoreCase("L_stage") || ds.equalsIgnoreCase("V16")) {
    processZoneStageOrCode(0, val.toInt());
    return;
  }
  if (ds.equalsIgnoreCase("DAP_L") || ds.equalsIgnoreCase("V6")) {
    int v = val.toInt();
    if (v == 0) resetZoneDapFromBlynk(0);
    else if (v >= 98) processZoneStageOrCode(0, v);
    return;
  }
  if (ds.equalsIgnoreCase("DailyTime_L") || ds.equalsIgnoreCase("V7")) {
    int v = val.toInt();
    if (v == 0) resetZoneRuntimeFromBlynk(0);
    else if (v >= 98) processZoneStageOrCode(0, v);
    return;
  }
  if (ds.equalsIgnoreCase("V8"))  { processConfigField(0, "Crop", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V9"))  { processConfigField(0, "Soil", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V10")) { processConfigField(0, "UseStages", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V11")) { resetZoneRuntimeFromBlynk(0); return; }
  if (ds.equalsIgnoreCase("V12")) { resetZoneDapFromBlynk(0); return; }
  if (ds.equalsIgnoreCase("V15")) { processConfigField(0, "Preset", val.toFloat()); return; }

  // Zone Right Controls
  if (ds.equalsIgnoreCase("Mode_R") || ds.equalsIgnoreCase("R_mode") || ds.equalsIgnoreCase("V24")) {
    int m = val.toInt();
    if (m >= 98) {
      processZoneStageOrCode(1, m);
    } else {
      ZoneControl_SetMode(1, (ZoneMode)constrain(m, 0, 2));
      BlynkManager_SendControlState();
    }
    return;
  }
  if (ds.equalsIgnoreCase("Valve_R") || ds.equalsIgnoreCase("R_valve") || ds.equalsIgnoreCase("V25")) {
    ZoneControl_SetManualSwitch(1, val.toInt() == 1);
    BlynkManager_SendControlState();
    return;
  }
  if (ds.equalsIgnoreCase("Stage_R") || ds.equalsIgnoreCase("R_stage") || ds.equalsIgnoreCase("V36")) {
    processZoneStageOrCode(1, val.toInt());
    return;
  }
  if (ds.equalsIgnoreCase("DAP_R") || ds.equalsIgnoreCase("V26")) {
    int v = val.toInt();
    if (v == 0) resetZoneDapFromBlynk(1);
    else if (v >= 98) processZoneStageOrCode(1, v);
    return;
  }
  if (ds.equalsIgnoreCase("DailyTime_R") || ds.equalsIgnoreCase("V27")) {
    int v = val.toInt();
    if (v == 0) resetZoneRuntimeFromBlynk(1);
    else if (v >= 98) processZoneStageOrCode(1, v);
    return;
  }
  if (ds.equalsIgnoreCase("V28")) { processConfigField(1, "Crop", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V29")) { processConfigField(1, "Soil", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V30")) { processConfigField(1, "UseStages", val.toFloat()); return; }
  if (ds.equalsIgnoreCase("V31")) { resetZoneRuntimeFromBlynk(1); return; }
  if (ds.equalsIgnoreCase("V32")) { resetZoneDapFromBlynk(1); return; }
  if (ds.equalsIgnoreCase("V35")) { processConfigField(1, "Preset", val.toFloat()); return; }

  // Left Zone Configurations
  if (ds.endsWith("_L") || ds.endsWith("_l")) {
    String field = ds.substring(0, ds.length() - 2);
    processConfigField(0, field, val.toFloat());
    return;
  }

  // Right Zone Configurations
  if (ds.endsWith("_R") || ds.endsWith("_r")) {
    String field = ds.substring(0, ds.length() - 2);
    processConfigField(1, field, val.toFloat());
    return;
  }
}

/*****************************************************************************
 * Main Manager API
 *****************************************************************************/
void BlynkManager_Init() {
  if (downlinkQueue == NULL) {
    downlinkQueue = xQueueCreate(20, sizeof(DownlinkItem));
  }
}

void BlynkManager_Update() {
  // 1. Maintain Connection
  if (WiFi.status() == WL_CONNECTED && strlen(blynkAuth) > 0) {
    if (mqttClient == NULL || needReconnect) {
      needReconnect = false;
      startMqttClient();
    }
  }

  // 2. Process Dequeued Downlinks
  DownlinkItem item;
  while (downlinkQueue != NULL && xQueueReceive(downlinkQueue, &item, 0) == pdTRUE) {
    processDownlinkItem(String(item.topic), String(item.payload));
  }

  // 2.1 ส่งสถานะที่ค้างจากการ throttle
  flushPendingSync();

  // 3. Initial Full Sync after connect
  if (needFullSync && mqttConnected) {
    needFullSync = false;
    Serial.println("[Blynk MQTT] Executing initial batch synchronization...");
    BlynkManager_SendControlState();
    BlynkManager_SendZoneConfig(0);
    BlynkManager_SendZoneConfig(1);
    BlynkManager_SendTelemetry();
    BlynkManager_SendStatus();
  }

  unsigned long now = millis();

  // Smart Circuit Breaker: If messages reach 2000/day, warn user and slow down
  if (dailyMessageCount >= 2000 && !quotaWarnSent) {
    quotaWarnSent = true;
    AppLog_Log("WARN", "Blynk daily quota limit reached (2000 msgs). Auto-throttling active.");
    BlynkManager_LogEvent("daily_limit", "Warning: Message count reached 2000/day! Auto-throttling to conserve Blynk quota.");
  }

  // 4. Smart Deadband Telemetry (Normally 60s check / 120s heartbeat, slowed to 300s if quota > 2000)
  unsigned long telemetryInterval = (dailyMessageCount >= 2000) ? 300000UL : 60000UL;
  unsigned long heartbeatInterval = (dailyMessageCount >= 2000) ? 600000UL : 120000UL;

  if (now - lastTelemetrySync >= telemetryInterval) {
    bool changed = false;

    if (zoneLeft.sensorOnline) {
      if (fabs(zoneLeft.humidity - prevHumL) >= 0.5f || fabs(zoneLeft.temperature - prevTempL) >= 0.5f) {
        prevHumL = zoneLeft.humidity;
        prevTempL = zoneLeft.temperature;
        changed = true;
      }
    }
    if (zoneRight.sensorOnline) {
      if (fabs(zoneRight.humidity - prevHumR) >= 0.5f || fabs(zoneRight.temperature - prevTempR) >= 0.5f) {
        prevHumR = zoneRight.humidity;
        prevTempR = zoneRight.temperature;
        changed = true;
      }
    }

    if (changed || (now - lastTelemetrySync >= heartbeatInterval)) {
      lastTelemetrySync = now;
      BlynkManager_SendTelemetry();
    }
  }

  // 5. Periodic Status Sync (Every 5 minutes)
  if (now - lastStatusSync >= 300000UL) {
    lastStatusSync = now;
    BlynkManager_SendStatus();
  }
}
