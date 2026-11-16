#pragma once

// บันทึก/โหลดค่าตั้ง+ตารางเรียน+วันหยุดจาก NVS — เขียนเฉพาะตอนค่าเปลี่ยน (กติกาข้อ 6)
void nvsLoadAll();
void nvsSaveSettings();
void nvsSaveSchedule();
void nvsSaveHolidays();
