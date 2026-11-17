#include "buzzer.h"
#include "pins.h"
#include <Arduino.h>

void buzzerBegin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

// สั้นๆ บล็อกได้ (เรียกไม่บ่อย, ไม่เกิน ~150ms) — ไม่ใช่ทรัพยากรที่แชร์กับ SPI/IR จึงไม่ผิดกติกาห้ามบล็อก
void buzzerBeep(uint16_t freqHz, uint16_t ms) {
  tone(PIN_BUZZER, freqHz, ms);
  delay(ms);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}
