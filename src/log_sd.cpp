// microSD log — CSV ทุก 1 นาที (sensor_YYYYMMDD.csv) + เหตุการณ์ (event_YYYYMMDD.csv) (SPEC.md ข้อ 5)
// flush ทุกบรรทัด · ไม่รู้เวลา (ยังไม่มี RTC จริง ใช้ NTP แทนไปก่อน — ไม่มี NTP = ไม่เขียน)
#include "log_sd.h"
#include "app.h"
#include "pins.h"
#include <SD.h>
#include <time.h>

static bool g_sdOk = false;
static uint32_t g_lastSensorLogMs = 0;

bool sdPresent() { return g_sdOk; }

bool logTimeValid() {
  time_t now = time(nullptr);
  return now > 1700000000; // ประมาณปี 2023 ขึ้นไป ถือว่า NTP sync แล้ว
}

static bool todayStamp(char* out, size_t outLen) {
  time_t now = time(nullptr);
  if (now <= 1700000000) return false;
  struct tm tminfo;
  localtime_r(&now, &tminfo);
  strftime(out, outLen, "%Y%m%d", &tminfo);
  return true;
}

static bool isoTimestamp(char* out, size_t outLen) {
  time_t now = time(nullptr);
  if (now <= 1700000000) return false;
  struct tm tminfo;
  localtime_r(&now, &tminfo);
  strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &tminfo);
  return true;
}

void logInit() {
  if (!app::spiLock(2000)) return;
  g_sdOk = SD.begin(PIN_SD_CS);
  app::spiUnlock();
  if (!g_sdOk) {
    Serial.println("[sd] การ์ดไม่พร้อม — ข้ามการบันทึก log");
  }
}

void logTick() {
  if (!g_sdOk) return;
  uint32_t now = millis();
  if (now - g_lastSensorLogMs < 60000) return;
  g_lastSensorLogMs = now;

  char ts[24];
  if (!isoTimestamp(ts, sizeof(ts))) {
    Serial.println("[sd] ยังไม่รู้เวลา (รอ NTP sync) — ข้ามการเขียนรอบนี้"); // SPEC.md ข้อ 5
    return;
  }
  char dateStamp[16];
  todayStamp(dateStamp, sizeof(dateStamp));

  autoac::AcState ac = app::lastAcState();
  char line[160];
  snprintf(line, sizeof(line), "%s,%.1f,%.0f,%d,%d,%u,%d,%d\n",
           ts, app::lastTempC(), app::lastHumidity(), app::lastOccupied() ? 1 : 0,
           ac.on ? 1 : 0, ac.setTemp, (int)ac.mode, (int)ac.fan);

  char path[48];
  snprintf(path, sizeof(path), "/sensor_%s.csv", dateStamp);

  if (!app::spiLock(500)) return;
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (f) {
    if (isNew) f.print("ts,temp,humidity,occupied,ac_on,ac_set_temp,ac_mode,ac_fan\n");
    f.print(line);
    f.flush(); // flush ทุกบรรทัด (SPEC.md ข้อ 5)
    f.close();
    Serial.printf("[sd] wrote %s: %s", path, line);
  } else {
    Serial.printf("[sd] เปิดไฟล์ %s ไม่ได้\n", path);
  }
  app::spiUnlock();
}

void logEvent(const char* command, const char* source, const char* reason) {
  if (!g_sdOk) return;
  char ts[24];
  if (!isoTimestamp(ts, sizeof(ts))) return;
  char dateStamp[16];
  todayStamp(dateStamp, sizeof(dateStamp));

  char line[160];
  snprintf(line, sizeof(line), "%s,%s,%s,%s\n", ts, command, source, reason ? reason : "");

  char path[48];
  snprintf(path, sizeof(path), "/event_%s.csv", dateStamp);

  if (!app::spiLock(500)) return;
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (f) {
    if (isNew) f.print("ts,command,source,reason\n");
    f.print(line);
    f.flush();
    f.close();
  }
  app::spiUnlock();
}
