#pragma once
#include <cstdint>

// บัซเซอร์แจ้งเตือน (PIN_BUZZER) — ใช้ LEDC tone ธรรมดา เรียกจาก task ไหนก็ได้ (ไม่แชร์ทรัพยากรกับ SPI/IR)
void buzzerBegin();
void buzzerBeep(uint16_t freqHz = 2000, uint16_t ms = 120);
