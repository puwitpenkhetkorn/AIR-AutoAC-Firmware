#pragma once

// บันทึก log ลง microSD (SPEC.md ข้อ 5) — SD ใช้ SPI ร่วมกับจอ ต้องถือ app::spiLock() เสมอ
void logInit();
void logTick(); // เรียกทุก loop ของ control task — เขียนแถวเซนเซอร์เองทุก 1 นาที
void logEvent(const char* command, const char* source, const char* reason);
bool logTimeValid();
bool sdPresent(); // การ์ด SD เปิดได้สำเร็จตอนบูตไหม (สำหรับหน้าเช็คเซนเซอร์)
