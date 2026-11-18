#pragma once

// ไฟสถานะ WS2812B 3 ดวง (PIN_WS2812B) — แบบตกลงกับผู้ใช้:
// ดวง 1 (ซ้าย)  = เน็ต/ThingsBoard: เขียว=ครบ, เหลือง=WiFi ติดแต่ TB หลุด, แดง=WiFi หลุด
// ดวง 2 (กลาง)  = แอร์: ฟ้า=เปิด, ดับ=ปิด
// ดวง 3 (ขวา)   = คนในห้อง/โหมดพิเศษ: กะพริบม่วง=กำลังเรียนรู้ IR, เขียว=มีคน, ดับ=ห้องว่าง

void statusLedBegin(); // เรียกครั้งเดียวตอนบูต
// เรียกทุก tick ของ Control task (ไม่บล็อก) — ส่งค่าล่าสุดเข้ามาคำนวณสีใหม่ทุกครั้ง
void statusLedUpdate(bool wifiOk, bool tbOk, bool acOn, bool occupied, bool irLearnActive);
