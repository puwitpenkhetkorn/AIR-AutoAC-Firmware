#include "amg8833.h"
#include <Adafruit_AMG88xx.h>
#include <Arduino.h>

static Adafruit_AMG88xx amg;
static bool g_present = false;
static float g_pixels[AMG88xx_PIXEL_ARRAY_SIZE];

constexpr float kPresenceDeltaC = 3.5f; // จุดที่ร้อนกว่าค่าเฉลี่ยห้องเกินนี้ ถือว่าเป็นคน (ตัวเลขเริ่มต้น ปรับได้ถ้าหน้างานจริงไม่พอดี)

void amg8833Begin() {
  g_present = amg.begin(); // I2C เดียวกับ DS3231 (Wire.begin() เรียกไปแล้วใน rtcBegin())
  Serial.println(g_present ? "[amg8833] เจอชิปแล้ว" : "[amg8833] ไม่เจอชิป — ใช้ PIR อย่างเดียว");
}

bool amg8833Present() { return g_present; }

bool amg8833DetectPresence() {
  if (!g_present) return false;
  amg.readPixels(g_pixels);

  float sum = 0, maxV = g_pixels[0];
  for (float v : g_pixels) {
    sum += v;
    if (v > maxV) maxV = v;
  }
  float avg = sum / AMG88xx_PIXEL_ARRAY_SIZE;
  return (maxV - avg) >= kPresenceDeltaC;
}
