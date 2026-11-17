#pragma once

// เรดาร์ Ai-Thinker Rd-01 (24GHz mmWave) — ตรวจจับคนแม่นกว่า PIR/กล้องความร้อน เพราะเห็นคนนั่งนิ่งได้
// อ้างอิง "Rd-01 serial communication protocol V1.0.0" (Ai-Thinker, 2023-06-28):
// baud 256000, 1 stop bit, no parity; โหมดทำงานปกติ (ไม่ใช่ engineering mode) ส่งข้อมูลเองต่อเนื่อง ไม่ต้องขอ

void radarBegin(); // เรียกครั้งเดียวตอนบูต
// เรียกทุก tick ของ Control task — อ่าน UART ที่มีอยู่ ประกอบเฟรม ถ้าครบเฟรมจะอัปเดตผลล่าสุด (ไม่บล็อก)
void radarPoll();
bool radarPresence(); // ผลล่าสุด: true ถ้า target state ไม่ใช่ "ไม่มีเป้าหมาย" (0x00)
