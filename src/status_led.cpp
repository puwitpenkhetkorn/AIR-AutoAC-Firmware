#include "status_led.h"
#include "pins.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

static Adafruit_NeoPixel strip(3, PIN_WS2812B, NEO_GRB + NEO_KHZ800);

constexpr uint32_t kBlinkPeriodMs = 500; // กะพริบม่วง (โหมดเรียนรู้ IR) ครึ่งคาบ

void statusLedBegin() {
  strip.begin();
  strip.setBrightness(60); // สว่างเต็ม 255 จ้าเกินไปสำหรับไฟสถานะติดผนัง
  strip.show();
}

void statusLedUpdate(bool wifiOk, bool tbOk, bool acOn, bool occupied, bool irLearnActive) {
  uint32_t net = wifiOk ? (tbOk ? strip.Color(0, 200, 0) : strip.Color(200, 160, 0)) : strip.Color(200, 0, 0);
  uint32_t ac = acOn ? strip.Color(0, 120, 220) : strip.Color(0, 0, 0);

  uint32_t room;
  if (irLearnActive) {
    bool on = (millis() / kBlinkPeriodMs) % 2 == 0;
    room = on ? strip.Color(160, 0, 220) : strip.Color(0, 0, 0);
  } else {
    room = occupied ? strip.Color(0, 200, 0) : strip.Color(0, 0, 0);
  }

  strip.setPixelColor(0, net);
  strip.setPixelColor(1, ac);
  strip.setPixelColor(2, room);
  strip.show();
}
