#include "rtc.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <sys/time.h>

static RTC_DS3231 rtc;
static bool g_present = false;

bool rtcPresent() { return g_present; }

void rtcBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  g_present = rtc.begin();
  if (!g_present) {
    Serial.println("[rtc] ไม่เจอ DS3231 — ใช้ NTP อย่างเดียวไปก่อน");
    return;
  }
  if (rtc.lostPower()) {
    // ไฟดับนานจนแบตสำรองหมด/ไม่มีแบต — เวลาที่ชิปจำไว้ไม่น่าเชื่อถือ รอ NTP sync แล้ว rtcSyncFromSystemTime() จะเขียนทับให้เอง
    Serial.println("[rtc] DS3231 ไฟดับเวลาหาย — รอ NTP sync ครั้งแรกเพื่อตั้งเวลาใหม่");
    return;
  }
  DateTime now = rtc.now();
  struct timeval tv = { .tv_sec = now.unixtime(), .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  Serial.printf("[rtc] ตั้งเวลาระบบจาก DS3231: %04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
}

void rtcSyncFromSystemTime() {
  if (!g_present) return;
  time_t now = time(nullptr);
  if (now <= 1700000000) return; // ระบบยังไม่รู้เวลาจริง (ยังไม่ sync NTP) — ไม่เขียนทับ RTC ด้วยค่าขยะ
  rtc.adjust(DateTime((uint32_t)now));
}
