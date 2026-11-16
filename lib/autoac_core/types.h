#pragma once
#include <cstdint>

// ไม่ include Arduino/ESP32 ในนี้ (กติกาเฟิร์มแวร์ ข้อ 5) — ให้เทสต์รันบนคอมได้

namespace autoac {

enum class Source : uint8_t { Screen, Telegram, Web, ThingsBoard, Auto, Schedule, Hw };

enum class AcMode : uint8_t { Cool = 0, Fan = 1, Dry = 2, Auto = 3 };
enum class FanSpeed : uint8_t { Auto = 0, Low = 1, Mid = 2, High = 3 };

// โหมดส่ง IR ต่อห้อง: Protocol = ใช้ IRac (เช่น Haier), Learned = ใช้รหัสดิบที่เรียนรู้จากรีโมทจริง
enum class IrMode : uint8_t { Protocol = 0, Learned = 1 };
// ปุ่มที่เรียนรู้ได้ในโหมด Learned (นับจำนวนครั้งได้แม่นยำเท่านั้น — ไม่รวมโหมด/พัดลมที่เป็นปุ่มวนลูป)
enum class IrButton : uint8_t { PowerOn = 0, PowerOff = 1, TempUp = 2, TempDown = 3 };
constexpr int IR_BUTTON_COUNT = 4;

// คำสั่งที่เข้าคิวกลาง cmdQueue จากทุกช่องทาง (กติกาข้อ 1)
struct Command {
  enum class Type : uint8_t {
    On, Off, SetTemp, SetMode, SetFan, Auto, Manual, Sync, Extend,
    SetSetting, SetSchedule, AddHoliday, RemoveHoliday, Touch,
    HwIrTest, // ปุ่มทดสอบ IR ดิบจากหน้าจอเครื่องมือช่าง — ไม่ผ่าน Controller (intArg: 1=on, 0=off)
    IrLearnStart, // เริ่มเรียนรู้ปุ่ม IR — ไม่ผ่าน Controller (intArg: ค่า IrButton ที่จะเรียนรู้)
    IrLearnCancel, // ยกเลิกการเรียนรู้ที่กำลังทำอยู่ — ไม่ผ่าน Controller (ไม่ใช้ intArg)
  };

  Type type;
  Source source;
  bool admin = false; // Telegram admin / เว็บบอร์ดใส่รหัส / จอใส่ PIN (SPEC.md ข้อ 2)
  int32_t intArg = 0;
  int32_t intArg2 = 0;
  char strArg[16] = {0}; // วันที่ addHoliday/removeHoliday, key ของ setSetting
};

// ค่าตั้งที่แก้ได้ (ตรงชื่อ attribute ที่ SRS ต้องการ — ดู SPEC.md ข้อ 1,3)
struct Settings {
  uint16_t emptyMin = 15;   // ไม่พบการเคลื่อนไหว 5–60 นาที
  uint16_t graceSec = 60;   // เตือนก่อนปิด 30–300 วินาที
  uint8_t hot = 27;         // เปิดเมื่อร้อน 24–32 °C
  bool autoOn = true;       // เปิดอัตโนมัติเมื่อร้อน (ไม่ผูกกับขอบล่างของช่องทาง)
  uint16_t preOpenMin = 10; // เปิดล่วงหน้า 0–30 นาที ก่อนคาบเรียน
  IrMode irMode = IrMode::Protocol; // โหมดส่ง IR ของห้องนี้
};

struct ScheduleSlot {
  bool en = false;
  uint16_t startMin = 480;  // นาทีจากเที่ยงคืน
  uint16_t endMin = 1020;

  ScheduleSlot() = default;
  ScheduleSlot(bool e, uint16_t s, uint16_t en_) : en(e), startMin(s), endMin(en_) {}
};

// ตารางเรียน 7 วัน x 2 ช่วง/วัน
using Schedule = ScheduleSlot[7][2];

constexpr int MAX_HOLIDAYS = 32;
struct Holidays {
  char dates[MAX_HOLIDAYS][11] = {{0}}; // "YYYY-MM-DD"
  int count = 0;

  bool contains(const char* ymd) const {
    for (int i = 0; i < count; i++) {
      bool same = true;
      for (int j = 0; j < 10; j++) {
        if (dates[i][j] != ymd[j]) { same = false; break; }
      }
      if (same) return true;
    }
    return false;
  }
};

// สถานะแอร์ที่ควบคุมมา (ไม่ใช่สถานะจริง — IR ส่งทางเดียว NFR-05)
struct AcState {
  bool on = false;
  uint8_t setTemp = 24;
  AcMode mode = AcMode::Cool;
  FanSpeed fan = FanSpeed::Auto;
};

// ความช่วงอุณหภูมิที่สั่งได้ต่อช่องทาง (SPEC.md ข้อ 2)
inline uint8_t minAllowedTempFor(Source src, bool admin) {
  if (src == Source::Screen) return admin ? 16 : 20;
  if (src == Source::Telegram) return admin ? 16 : 20;
  if (src == Source::Web) return admin ? 16 : 20; // แอป ThingsBoard / เว็บบอร์ดใส่รหัส = 16, ไม่ใส่รหัส = 20
  return 16; // ThingsBoard RPC (แอปหลัก), Auto, Schedule, Hw
}
constexpr uint8_t MAX_ALLOWED_TEMP_C = 30;

} // namespace autoac
