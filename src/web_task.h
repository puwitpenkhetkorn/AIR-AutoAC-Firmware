#pragma once

// เว็บเซิร์ฟเวอร์ในตัวบอร์ด — เสิร์ฟไฟล์ log บน microSD (sensor_YYYYMMDD.csv / event_YYYYMMDD.csv)
// เข้าได้ผ่าน http://autoac.local/ (mDNS) หรือ IP ของบอร์ดตรงๆ — ไม่ผ่าน ThingsBoard (อ่านเฉพาะ LAN เดียวกัน)
void webTaskStart(); // เรียกครั้งเดียวตอนบูต — รันเป็น task แยก ไม่บล็อก net_task (กติกาข้อ 4)
