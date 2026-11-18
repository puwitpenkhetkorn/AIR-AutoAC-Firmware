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
    SetSetting, AddHoliday, RemoveHoliday, Touch,
    HwIrTest, // ปุ่มทดสอบ IR ดิบจากหน้าจอเครื่องมือช่าง — ไม่ผ่าน Controller (intArg: 1=on, 0=off)
    IrLearnStart, // เริ่มเรียนรู้ปุ่ม IR — ไม่ผ่าน Controller (intArg: ค่า IrButton ที่จะเรียนรู้)
    IrLearnCancel, // ยกเลิกการเรียนรู้ที่กำลังทำอยู่ — ไม่ผ่าน Controller (ไม่ใช้ intArg)
    IrClear, // ลบรหัส IR ที่เรียนรู้ไว้ของปุ่มนี้ทิ้ง — ไม่ผ่าน Controller (intArg: ค่า IrButton)
    CalibrateTemp, // ซิงค์อุณหภูมิที่จำไว้ให้ตรงจอแอร์จริง — ไม่ผ่าน Controller::applyCommand (intArg: อุณหภูมิจริง)
    ScheduleAdd, ScheduleEdit, ScheduleDelete, // CRUD คาบเรียน (ดู SchedulePeriod ด้านล่าง)
  };

  Type type;
  Source source;
  bool admin = false; // Telegram admin / เว็บบอร์ดใส่รหัส / จอใส่ PIN (SPEC.md ข้อ 2)
  int32_t intArg = 0;
  int32_t intArg2 = 0;
  char strArg[32] = {0}; // วันที่ addHoliday/removeHoliday, key ของ setSetting, CSV ฟิลด์ตัวเลขของ schedule (ดู applySchedule* ใน control_task.cpp)
  char scheduleName[24] = {0}; // ชื่อคาบ — ใช้เฉพาะ ScheduleAdd/ScheduleEdit (แยกฟิลด์เพราะอาจมีคอมมาในชื่อ)
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

// คาบเรียน 1 รายการ — รองรับชื่อคาบ + ช่วงวันที่มีผล (ไม่ใช่แค่ enable/disable เฉยๆ แบบเดิม)
// เก็บเป็น array ขนาดคงที่ (fixed-size sparse): used=false คือช่องว่าง, ลบแล้วเอาไปใช้ซ้ำได้
// validFromDay/validToDay: จำนวนวันนับจาก 1970-01-01 (0 = ไม่จำกัด) — ใช้ daysFromCivil() แปลงจากวันที่จริง
constexpr int SCHEDULE_MAX_PERIODS = 350; // สูงสุด 50 คาบ/วัน x 7 วัน
constexpr int SCHEDULE_NAME_LEN = 24;
constexpr int SCHEDULE_MAX_PER_DAY = 50;

struct SchedulePeriod {
  bool used = false;    // false = ช่องว่าง (ไม่ใช่คาบจริง)
  bool enabled = false;
  uint8_t day = 0;       // 0=จันทร์..6=อาทิตย์ (ตรงกับ DAY_NAMES ฝั่งเว็บ)
  uint16_t startMin = 0;
  uint16_t endMin = 0;
  uint16_t validFromDay = 0; // 0 = ไม่จำกัดวันเริ่ม
  uint16_t validToDay = 0;   // 0 = ไม่จำกัดวันสิ้นสุด
  char name[SCHEDULE_NAME_LEN] = {0};
};

using SchedulePeriods = SchedulePeriod[SCHEDULE_MAX_PERIODS];

// จำนวนวันนับจาก 1970-01-01 (civil calendar) — อัลกอริทึมของ Howard Hinnant (ไม่ขึ้นกับ timezone/DST)
// ใช้เทียบ validFromDay/validToDay กับวันที่ปัจจุบัน
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

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
