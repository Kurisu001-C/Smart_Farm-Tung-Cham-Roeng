#pragma once
#include <Arduino.h>
#include <WebServer.h>

void AppLog_Init();
void AppLog_Update();
void AppLog_Log(const char* tag, const char* format, ...);
void AppLog_Flush();
void AppLog_Clear();
size_t AppLog_GetTotalSize();
String AppLog_GetRecent(size_t maxBytes = 2500);
void AppLog_StreamToClient(WebServer& server);
