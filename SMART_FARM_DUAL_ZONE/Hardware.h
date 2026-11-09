#pragma once
#include <Arduino.h>

void Hardware_Init();
void Relay_SetChannel(uint8_t ch, bool state);
void Relay_SetAll(bool state);
bool Relay_GetChannel(uint8_t ch);
void Relay_EmergencyShutdown();

// RTC PCF85063 Helpers
struct RTCDateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

bool RTC_Init();
bool RTC_GetTime(RTCDateTime& dt);
bool RTC_SetTime(const RTCDateTime& dt);
int CalculateDAP(int startYear, int startMonth, int startDay, int offset, const RTCDateTime& now);
