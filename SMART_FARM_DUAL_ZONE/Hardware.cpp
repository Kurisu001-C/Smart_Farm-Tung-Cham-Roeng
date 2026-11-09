#include "Hardware.h"
#include "Config.h"
#include <Wire.h>
#include <time.h>

static uint8_t tcaOutputByte = 0x00;

static uint8_t decToBcd(int val) {
  return (uint8_t)((val / 10 * 16) + (val % 10));
}

static int bcdToDec(uint8_t val) {
  return (int)((val / 16 * 10) + (val % 16));
}

static bool TCA9554_Write(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(TCA9554_ADDRESS);
  Wire.write(reg);
  Wire.write(data);
  return (Wire.endTransmission() == 0);
}

void Hardware_Init() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(50);
  
  // Phase 1 Item 5: Write OUTPUT_REG = 0x00 first, then CONFIG_REG = 0x00 to prevent relay glitch at boot
  tcaOutputByte = 0x00;
  TCA9554_Write(TCA9554_OUTPUT_REG, tcaOutputByte);
  TCA9554_Write(TCA9554_CONFIG_REG, 0x00);
  
  RTC_Init();
}

void Relay_SetChannel(uint8_t ch, bool state) {
  if (ch < 1 || ch > 8) return;
  if (state) {
    tcaOutputByte |= (1 << (ch - 1));
  } else {
    tcaOutputByte &= ~(1 << (ch - 1));
  }
  TCA9554_Write(TCA9554_OUTPUT_REG, tcaOutputByte);
}

void Relay_SetAll(bool state) {
  tcaOutputByte = state ? 0xFF : 0x00;
  TCA9554_Write(TCA9554_OUTPUT_REG, tcaOutputByte);
}

bool Relay_GetChannel(uint8_t ch) {
  if (ch < 1 || ch > 8) return false;
  return (tcaOutputByte & (1 << (ch - 1))) != 0;
}

void Relay_EmergencyShutdown() {
  tcaOutputByte = 0x00;
  TCA9554_Write(TCA9554_OUTPUT_REG, tcaOutputByte);
}

bool RTC_Init() {
  Wire.beginTransmission(PCF85063_ADDRESS);
  Wire.write(0x00); // Control 1
  Wire.write(0x00); // Normal mode
  return (Wire.endTransmission() == 0);
}

bool RTC_GetTime(RTCDateTime& dt) {
  Wire.beginTransmission(PCF85063_ADDRESS);
  Wire.write(0x04); // Start at Seconds register
  if (Wire.endTransmission() != 0) return false;
  
  Wire.requestFrom((uint8_t)PCF85063_ADDRESS, (uint8_t)7);
  if (Wire.available() < 7) return false;
  
  dt.second = bcdToDec(Wire.read() & 0x7F);
  dt.minute = bcdToDec(Wire.read() & 0x7F);
  dt.hour   = bcdToDec(Wire.read() & 0x3F);
  dt.day    = bcdToDec(Wire.read() & 0x3F);
  Wire.read(); // Skip Weekday
  dt.month  = bcdToDec(Wire.read() & 0x1F);
  dt.year   = bcdToDec(Wire.read()) + 2000;
  
  return true;
}

bool RTC_SetTime(const RTCDateTime& dt) {
  uint8_t buf[7];
  buf[0] = decToBcd(dt.second);
  buf[1] = decToBcd(dt.minute);
  buf[2] = decToBcd(dt.hour);
  buf[3] = decToBcd(dt.day);
  buf[4] = 0; // weekday
  buf[5] = decToBcd(dt.month);
  buf[6] = decToBcd(dt.year >= 2000 ? dt.year - 2000 : 0);
  
  Wire.beginTransmission(PCF85063_ADDRESS);
  Wire.write(0x04);
  for (int i = 0; i < 7; i++) Wire.write(buf[i]);
  return (Wire.endTransmission() == 0);
}

int CalculateDAP(int startYear, int startMonth, int startDay, int offset, const RTCDateTime& now) {
  struct tm tmStart = {0};
  tmStart.tm_year = startYear - 1900;
  tmStart.tm_mon  = startMonth - 1;
  tmStart.tm_mday = startDay;
  
  struct tm tmNow = {0};
  tmNow.tm_year = now.year - 1900;
  tmNow.tm_mon  = now.month - 1;
  tmNow.tm_mday = now.day;
  
  time_t tStart = mktime(&tmStart);
  time_t tNow   = mktime(&tmNow);
  
  if (tStart == -1 || tNow == -1 || tNow < tStart) {
    return offset;
  }
  
  double diffSec = difftime(tNow, tStart);
  int days = (int)(diffSec / 86400.0);
  return days + offset;
}
