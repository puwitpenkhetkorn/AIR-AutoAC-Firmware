#pragma once

#include <cstdint>

// เรดาร์ Ai-Thinker Rd-01 (24GHz mmWave) — ตรวจจับคนแม่นกว่า PIR/กล้องความร้อน เพราะเห็นคนนั่งนิ่งได้
// อ้างอิง "Rd-01 serial communication protocol V1.0.0" (Ai-Thinker, 2023-06-28):
// baud 256000, 1 stop bit, no parity; โหมดทำงานปกติ (ไม่ใช่ engineering mode) ส่งข้อมูลเองต่อเนื่อง ไม่ต้องขอ

void radarBegin(); // เรียกครั้งเดียวตอนบูต
// เรียกทุก tick ของ Control task — อ่าน UART ที่มีอยู่ ประกอบเฟรม ถ้าครบเฟรมจะอัปเดตผลล่าสุด (ไม่บล็อก)
void radarPoll();
bool radarPresence(); // ผลล่าสุด: true ถ้า target state ไม่ใช่ "ไม่มีเป้าหมาย" (0x00)

// target state ดิบตาม datasheet: 0=ไม่มีเป้าหมาย 1=เคลื่อนไหว 2=นิ่ง 3=เคลื่อนไหว+นิ่ง
struct RadarFrame {
  uint8_t state = 0;
  uint16_t moveDistCm = 0;
  uint8_t moveEnergy = 0;
  uint16_t staticDistCm = 0;
  uint8_t staticEnergy = 0;
  uint16_t detectRangeCm = 0;
  bool everReceived = false; // false = ยังไม่เคยรับเฟรมที่ถูกต้องเลยตั้งแต่บูต (ใช้แยกจาก "ไม่มีเป้าหมาย")
};
// สำหรับหน้าจอ radar บนเครื่อง — อ่านเฟรมล่าสุดแบบเต็ม (ไม่บล็อก)
RadarFrame radarLastFrame();
