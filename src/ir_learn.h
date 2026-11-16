#pragma once
#include <cstdint>
#include "../lib/autoac_core/types.h"

// เรียนรู้รหัส IR ดิบจากรีโมทจริง (โหมด Learned) — ไม่บล็อก ต้อง poll ทุก tick ของ Control task
// (กติกาเฟิร์มแวร์: ห้าม task ค้างรอสัญญาณ IR)

struct LearnedIrCode {
  uint16_t raw[150] = {0};
  uint8_t len = 0;
  bool learned = false;
};

void irLearnBegin(); // เรียกครั้งเดียวตอนบูต
bool irLearnActive();
void irLearnStart(autoac::IrButton b, uint32_t timeoutMs = 15000); // ไม่ทำอะไรถ้ากำลังเรียนรู้ปุ่มอื่นอยู่
void irLearnCancel(); // ไม่ทำอะไรถ้าไม่ได้เรียนรู้อยู่

// เรียกทุก tick — คืน true ครั้งเดียวตอนจบ session (สำเร็จ/timeout/ยาวเกินบัฟเฟอร์)
bool irLearnPoll(bool& outOk, autoac::IrButton& outButton, LearnedIrCode& outCode);
