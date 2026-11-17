#include "ir_learn.h"
#include "pins.h"
#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

static IRrecv irrecv(PIN_IR_RX, 1024, 15, true);

static bool g_active = false;
static autoac::IrButton g_button = autoac::IrButton::PowerOn;
static uint32_t g_deadlineMs = 0;

void irLearnBegin() {
  irrecv.enableIRIn();
}

bool irLearnActive() { return g_active; }

void irLearnStart(autoac::IrButton b, uint32_t timeoutMs) {
  if (g_active) return; // กำลังเรียนรู้ปุ่มอื่นอยู่ — ไม่แทรก
  g_active = true;
  g_button = b;
  g_deadlineMs = millis() + timeoutMs;
  irrecv.resume(); // ล้างสัญญาณเก่าที่อาจค้างอยู่ในบัฟเฟอร์
}

void irLearnCancel() {
  g_active = false; // ไม่ทำอะไรอยู่แล้วถ้า g_active เป็น false ตั้งแต่ต้น
}

bool irLearnPoll(bool& outOk, autoac::IrButton& outButton, LearnedIrCode& outCode) {
  if (!g_active) return false;

  decode_results results;
  if (irrecv.decode(&results)) {
    Serial.printf("[ir_learn] decode สำเร็จ rawlen=%u\n", results.rawlen);
    outButton = g_button;
    // rawbuf เก็บเป็น "tick" (2us) และ rawbuf[0] เป็นค่าดัมมี่จากอินเทอร์รัปต์ ไม่ใช่สัญญาณจริง
    // ต้องแปลงเป็นไมโครวินาทีจริงด้วย resultToRawArray/getCorrectedRawLength ก่อนเก็บ/ยิงซ้ำ (IRutils.h)
    uint16_t correctedLen = getCorrectedRawLength(&results);
    if (correctedLen > IR_RAW_MAX) {
      outOk = false; // too_long — ไม่บันทึก
    } else {
      uint16_t* arr = resultToRawArray(&results);
      outCode = LearnedIrCode{};
      outCode.len = correctedLen;
      for (uint16_t i = 0; i < correctedLen; i++) outCode.raw[i] = arr[i];
      delete[] arr;
      outCode.learned = true;
      outOk = true;
    }
    g_active = false;
    irrecv.resume();
    return true;
  }

  if ((int32_t)(millis() - g_deadlineMs) >= 0) {
    outOk = false;
    outButton = g_button;
    g_active = false;
    return true; // timeout
  }

  return false;
}
