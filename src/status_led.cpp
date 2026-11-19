#include "status_led.h"
#include "pins.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

static Adafruit_NeoPixel strip(3, PIN_WS2812B, NEO_GRB + NEO_KHZ800);

constexpr uint32_t kBlinkPeriodMs = 500; // กะพริบส้ม (โหมดเรียนรู้ IR) ครึ่งคาบ

static const uint32_t kGreen = strip.Color(0, 200, 0);
static const uint32_t kOrange = strip.Color(230, 110, 0);
static const uint32_t kRed = strip.Color(200, 0, 0);
static const uint32_t kOff = 0;

void statusLedBegin() {
  strip.begin();
  strip.setBrightness(60); // สว่างเต็ม 255 จ้าเกินไปสำหรับไฟสถานะติดผนัง
  strip.show();
}

void statusLedUpdate(bool wifiOk, bool tbOk, bool dhtOk, bool warnActive, bool irLearnActive) {
  uint32_t green = kOff, orange = kOff, red = kOff;

  if (irLearnActive) {
    // โหมดพิเศษ — กะพริบส้มทับสถานะอื่นทุกกรณี เตือนว่าอย่าเพิ่งยุ่งกับรีโมท
    orange = (millis() / kBlinkPeriodMs) % 2 == 0 ? kOrange : kOff;
  } else if (!wifiOk) {
    red = kRed; // วิกฤตสุด — ควบคุม/รายงานจากเว็บไม่ได้เลย
  } else if (!tbOk || !dhtOk || warnActive) {
    orange = kOrange; // ต้องดู แต่ยังไม่ถึงขั้นวิกฤต
  } else {
    green = kGreen; // ปกติทุกอย่าง
  }

  strip.setPixelColor(0, green);
  strip.setPixelColor(1, orange);
  strip.setPixelColor(2, red);
  strip.show();
}
