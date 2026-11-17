#include "buzzer.h"
#include "pins.h"
#include <Arduino.h>

void buzzerBegin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

// สั้นๆ บล็อกได้ (เรียกไม่บ่อย, ไม่เกิน ~200ms) — ไม่ใช่ทรัพยากรที่แชร์กับ SPI/IR จึงไม่ผิดกติกาห้ามบล็อก
// สลับ GPIO มือเอง ไม่ใช้ tone()/noTone() — เจอ error "LEDC is not initialized" ตอนทดสอบจริงบนบอร์ดนี้
void buzzerBeep(uint16_t freqHz, uint16_t ms) {
  uint32_t halfPeriodUs = 500000UL / freqHz;
  uint32_t cycles = (uint32_t)ms * 1000UL / (halfPeriodUs * 2);
  for (uint32_t i = 0; i < cycles; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(halfPeriodUs);
  }
}
