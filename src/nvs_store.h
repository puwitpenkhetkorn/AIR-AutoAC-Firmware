#pragma once
#include "ir_learn.h"
#include "../lib/autoac_core/types.h"

// บันทึก/โหลดค่าตั้ง+ตารางเรียน+วันหยุดจาก NVS — เขียนเฉพาะตอนค่าเปลี่ยน (กติกาข้อ 6)
void nvsLoadAll();
void nvsSaveSettings();
void nvsSaveSchedule();
void nvsSaveHolidays();

// รหัส IR ที่เรียนรู้มา ต่อปุ่ม คีย์ ir0..ir3
void nvsLoadIrCodes(LearnedIrCode out[autoac::IR_BUTTON_COUNT]);
void nvsSaveIrCode(autoac::IrButton b, const LearnedIrCode& code);
