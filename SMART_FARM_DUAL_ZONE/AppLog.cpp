#include "AppLog.h"
#include "Hardware.h"
#include <FS.h>
#include <LittleFS.h>
#include <FFat.h>
#include <stdarg.h>

static fs::FS* logFS = nullptr;
static const char* LOG_FILE_PATH = "/smartfarm.log";
static const char* LOG_OLD_PATH  = "/smartfarm.log.old";
static const size_t MAX_LOG_FILE_SIZE = 150 * 1024; // 150 KB cap per file (~2-3 days of events)

#define RAM_BUFFER_SIZE 4096
static char ramBuffer[RAM_BUFFER_SIZE];
static size_t ramBufferLen = 0;

static unsigned long lastHourlyFlush = 0;
static const unsigned long HOURLY_FLUSH_INTERVAL = 3600000UL; // 1 hour

void AppLog_Init() {
  ramBufferLen = 0;
  lastHourlyFlush = millis();

  // Try LittleFS first (recommended for Arduino ESP32)
  if (LittleFS.begin(true)) {
    logFS = &LittleFS;
    Serial.println("[AppLog] LittleFS mounted successfully.");
  } else if (FFat.begin(true)) {
    // Fallback to FFat (FATFS 9MB partition scheme)
    logFS = &FFat;
    Serial.println("[AppLog] FFat mounted successfully.");
  } else {
    logFS = nullptr;
    Serial.println("[AppLog] WARNING: No Flash filesystem mounted! Logs will stay in RAM only.");
  }
}

void AppLog_Flush() {
  if (ramBufferLen == 0 || logFS == nullptr) return;

  // Rolling log rotation check
  if (logFS->exists(LOG_FILE_PATH)) {
    File fCheck = logFS->open(LOG_FILE_PATH, "r");
    if (fCheck) {
      size_t fSize = fCheck.size();
      fCheck.close();
      if (fSize + ramBufferLen >= MAX_LOG_FILE_SIZE) {
        if (logFS->exists(LOG_OLD_PATH)) {
          logFS->remove(LOG_OLD_PATH);
        }
        logFS->rename(LOG_FILE_PATH, LOG_OLD_PATH);
        Serial.println("[AppLog] Log file rotated: current -> old.");
      }
    }
  }

  File f = logFS->open(LOG_FILE_PATH, "a");
  if (f) {
    f.write((const uint8_t*)ramBuffer, ramBufferLen);
    f.close();
    ramBufferLen = 0;
    lastHourlyFlush = millis();
  } else {
    Serial.println("[AppLog] Error opening log file for writing.");
  }
}

void AppLog_Log(const char* tag, const char* format, ...) {
  char msg[256];
  va_list args;
  va_start(args, format);
  vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  // Format RTC timestamp
  char timeStr[32];
  RTCDateTime dt;
  if (RTC_GetTime(dt) && dt.year >= 2024) {
    snprintf(timeStr, sizeof(timeStr), "[%04d-%02d-%02d %02d:%02d:%02d]", 
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  } else {
    unsigned long sec = millis() / 1000UL;
    snprintf(timeStr, sizeof(timeStr), "[UP:%02lu:%02lu:%02lu]", 
             sec / 3600, (sec % 3600) / 60, sec % 60);
  }

  char line[320];
  int lineLen = snprintf(line, sizeof(line), "%s [%s] %s\n", timeStr, tag, msg);
  if (lineLen <= 0) return;

  // Mirror to Serial USB CDC
  Serial.print(line);

  // Buffer into RAM
  if (ramBufferLen + lineLen >= RAM_BUFFER_SIZE - 1) {
    AppLog_Flush();
  }

  if (ramBufferLen + lineLen < RAM_BUFFER_SIZE) {
    memcpy(ramBuffer + ramBufferLen, line, lineLen);
    ramBufferLen += lineLen;
    ramBuffer[ramBufferLen] = '\0';
  }

  // Urgent events flush immediately to Flash
  if (strcmp(tag, "VALVE") == 0 || strcmp(tag, "FAILSAFE") == 0 || 
      strcmp(tag, "WARN") == 0  || strcmp(tag, "FAIL") == 0 || 
      strcmp(tag, "RESET") == 0 || strcmp(tag, "SYSTEM") == 0 ||
      strcmp(tag, "CROP") == 0) {
    AppLog_Flush();
  }
}

void AppLog_Update() {
  unsigned long now = millis();
  if (now - lastHourlyFlush >= HOURLY_FLUSH_INTERVAL) {
    lastHourlyFlush = now;
    AppLog_Flush();
  }
}

void AppLog_Clear() {
  ramBufferLen = 0;
  if (logFS) {
    if (logFS->exists(LOG_OLD_PATH)) logFS->remove(LOG_OLD_PATH);
    if (logFS->exists(LOG_FILE_PATH)) logFS->remove(LOG_FILE_PATH);
  }
  AppLog_Log("SYSTEM", "Logs cleared by user.");
}

size_t AppLog_GetTotalSize() {
  size_t total = ramBufferLen;
  if (logFS) {
    if (logFS->exists(LOG_OLD_PATH)) {
      File f = logFS->open(LOG_OLD_PATH, "r");
      if (f) { total += f.size(); f.close(); }
    }
    if (logFS->exists(LOG_FILE_PATH)) {
      File f = logFS->open(LOG_FILE_PATH, "r");
      if (f) { total += f.size(); f.close(); }
    }
  }
  return total;
}

String AppLog_GetRecent(size_t maxBytes) {
  String out = "";
  out.reserve(maxBytes + ramBufferLen + 64);

  if (logFS && logFS->exists(LOG_FILE_PATH)) {
    File f = logFS->open(LOG_FILE_PATH, "r");
    if (f) {
      size_t fSize = f.size();
      if (fSize > maxBytes) {
        f.seek(fSize - maxBytes);
        // Advance to next newline
        while (f.available()) {
          char c = f.read();
          if (c == '\n') break;
        }
      }
      char chunk[256];
      while (f.available()) {
        size_t n = f.read((uint8_t*)chunk, sizeof(chunk) - 1);
        chunk[n] = '\0';
        out += chunk;
      }
      f.close();
    }
  }

  if (ramBufferLen > 0) {
    out += ramBuffer;
  }

  if (out.length() == 0) {
    out = "(ยังไม่มีข้อมูล Log ที่บันทึก)";
  }

  return out;
}

void AppLog_StreamToClient(WebServer& server) {
  AppLog_Flush(); // Flush in-memory logs first

  server.sendHeader("Content-Disposition", "attachment; filename=\"smartfarm_debug.log\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");

  char chunk[513];

  if (logFS) {
    if (logFS->exists(LOG_OLD_PATH)) {
      File fOld = logFS->open(LOG_OLD_PATH, "r");
      if (fOld) {
        while (fOld.available()) {
          size_t n = fOld.read((uint8_t*)chunk, 512);
          chunk[n] = '\0';
          server.sendContent(chunk);
        }
        fOld.close();
      }
    }

    if (logFS->exists(LOG_FILE_PATH)) {
      File fCur = logFS->open(LOG_FILE_PATH, "r");
      if (fCur) {
        while (fCur.available()) {
          size_t n = fCur.read((uint8_t*)chunk, 512);
          chunk[n] = '\0';
          server.sendContent(chunk);
        }
        fCur.close();
      }
    }
  }

  server.sendContent(""); // Final empty chunk to signal completion
}
