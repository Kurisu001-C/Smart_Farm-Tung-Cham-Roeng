#pragma once
#include <Arduino.h>

/*****************************************************************************
 * Hardware Pinout (ESP32-S3-POE-ETH-8DI-8RO Waveshare)
 *****************************************************************************/
#define I2C_SDA_PIN         42
#define I2C_SCL_PIN         41
#define RS485_RX_PIN        18   // Hardware UART1 RX
#define RS485_TX_PIN        17   // Hardware UART1 TX
#define GPIO_PIN_RGB        38
#define GPIO_PIN_BUZZER     46

// TCA9554 Relay Expander
#define TCA9554_ADDRESS     0x20
#define TCA9554_INPUT_REG   0x00
#define TCA9554_OUTPUT_REG  0x01
#define TCA9554_CONFIG_REG  0x03

// PCF85063 RTC
#define PCF85063_ADDRESS    0x51

// Valves Allocation
#define VALVE_LEFT_CH       1    // Relay CH1
#define VALVE_RIGHT_CH      2    // Relay CH2

/*****************************************************************************
 * System & Network Configurations
 *****************************************************************************/
#define DEFAULT_AP_SSID     "ESP32-SMART-FARM"
#define DEFAULT_AP_PASS     "farm123456"

#define BLYNK_TEMPLATE_ID   "TMPL6ajFMsWqd"
#define BLYNK_TEMPLATE_NAME "New Template"
#define FIRMWARE_VERSION    "2.0.1"   // 2.0.1: manual valve respects Master, no dropped status sync, preset on empty zone starts today, growth stage chosen by caretaker (no auto-advance)

/*****************************************************************************
 * Enums & State Definitions
 *****************************************************************************/
enum ZoneMode {
  MODE_OFF = 0,
  MODE_AUTO = 1,
  MODE_MANUAL = 2
};

enum ZoneState {
  STATE_IDLE = 0,
  STATE_WATERING = 1,
  STATE_SOAKING = 2,
  STATE_FAULT = 3
};

// Soil Presets (FC & PWP physics)
enum SoilType {
  SOIL_SANDY_LOAM = 0, // ดินร่วนปนทราย
  SOIL_LOAM = 1,       // ดินร่วนมาตรฐาน
  SOIL_CLAY_LOAM = 2,   // ดินเหนียว / ดินร่วนเหนียว
  SOIL_COCOPEAT = 3,    // วัสดุปลูก / ขุยมะพร้าว
  SOIL_CUSTOM = 4       // กำหนดเองอิสระ
};

// Crop Group Presets (Physiological characteristics)
enum CropGroup {
  CROP_LEAFY = 0,       // ผักกินใบและผักสวนครัวทั่วไป
  CROP_SOLANACEAE = 1,  // พืชตระกูลมะเขือและพริก
  CROP_VINE_FRUIT = 2,  // พืชเถาเลื้อยและพืชผล
  CROP_HERB_AROMA = 3,  // สมุนไพรและผักกลิ่นแรง
  CROP_CUSTOM = 4       // กำหนดเองอิสระ
};

/*****************************************************************************
 * Growth Stage Profile (4 Stages)
 *****************************************************************************/
struct GrowthStage {
  int endDAP;                 // สิ้นสุดช่วงที่ DAP วันที่เท่าไร
  float moistureLow;          // เกณฑ์เริ่มรด (%)
  float moistureHigh;         // เกณฑ์หยุดรด (%)
  int pulseOnMinutes;         // เปิดวาล์ว (นาที)
  int soakMinutes;            // พักน้ำซึม (นาที)
  int maxDailyRuntimeMinutes; // เวลารดน้ำรวมสูงสุดต่อวัน (นาที)
};

/*****************************************************************************
 * Crop Profile & Zone Configuration
 *****************************************************************************/
#define ZONE_CONFIG_VERSION 3
#define STAGE_UNPLANTED -1  // สถานะไม่ได้ปลูก (พักแปลง / ไม่นับ DAP / พักระบบรดน้ำอัตโนมัติ)

struct ZoneConfig {
  uint16_t version;           // Storage version for migration check
  char cropName[32];
  SoilType soilType;
  CropGroup cropGroup;
  
  int startYear;
  int startMonth;
  int startDay;
  int dapOffset;
  
  // Custom Override / Default thresholds (ตัด EC ออก)
  float moistureLow;
  float moistureHigh;
  float moistureCritical;     // เกณฑ์วิกฤตแห้งจัด (PWP + buffer)
  int pulseOnMinutes;
  int soakMinutes;
  int maxDailyRuntimeMinutes;
  float maxSoilTemp;
  
  int manualDurationMinutes;  // ตั้งเวลาตัด Manual (นาที)
  
  // Growth Stages
  bool useGrowthStages;
  int selectedStage;          // ระยะปัจจุบัน: -1 = ไม่ได้ปลูก (STAGE_UNPLANTED), 0..3 = Stage 1..4
  GrowthStage stages[4];
};

/*****************************************************************************
 * Zone Runtime Data
 *****************************************************************************/
struct ZoneRuntime {
  uint8_t slaveID;
  ZoneMode mode;
  ZoneState state;
  
  // Sensor Readings (TH sensor: humidity + temperature only)
  float humidity;
  float temperature;
  bool sensorOnline;
  int failCount;
  unsigned long lastPollTime; // สำหรับแบ็คออฟ 10s เมื่อเซนเซอร์หลุด
  
  // History & Tracking for Safety
  float lastStableHum;
  unsigned long lastHumCheckTime;
  
  // Watering Cycle Timing
  unsigned long stateStartTime;
  unsigned long valveOpenStartTime; // Failsafe ตรวจวาล์วเปิดค้าง
  unsigned long lockoutUntilMs;     // No-moisture-rise lockout timer (60 mins)
  int dailyRuntimeSeconds;          // เวลารดสะสมวันนี้ (วินาที)
  int consecutivePulses;            // นับรอบ pulse ต่อเนื่องในรอบรด
  float humAtStartOfWatering;       // ความชื้นก่อนเริ่มรดเพื่อตรวจ No Moisture Rise
  
  // Manual Mode Tracking
  unsigned long manualStartTime;
  bool manualValveOpen;
  
  // Calculated DAP
  int currentDAP;
  int currentStageIndex;
  
  // Notification cooldowns (ms) for 5 Core Events
  unsigned long lastFaultNotify;
  unsigned long lastNoRiseNotify;
  unsigned long lastCritNotify;
  unsigned long lastTempHighNotify;
  unsigned long lastDailyLimitNotify;
};
