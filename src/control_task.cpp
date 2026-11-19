// Control task — คนเดียวที่เรียก Controller และ ir::send() (กติกาข้อ 2)
#include "control_task.h"
#include "app.h"
#include "net_task.h"
#include "nvs_store.h"
#include "log_sd.h"
#include "pins.h"
#include "../lib/autoac_core/Controller.h"
#include "ir_learn.h"
#include "rtc.h"
#include "amg8833.h"
#include "buzzer.h"
#include "radar.h"
#include "status_led.h"
#include <DHT.h>
#include <IRac.h>
#include <IRsend.h>
#include <time.h>

static DHT dht(PIN_DHT22, DHT22);
static autoac::Controller controller(app::settings);
static IRac irAc(PIN_IR_TX);
static IRsend irSendDev(PIN_IR_TX);

static uint32_t g_pirWarmupStartMs = 0;

static const char* irButtonName(autoac::IrButton b) {
  switch (b) {
    case autoac::IrButton::PowerOn: return "power_on";
    case autoac::IrButton::PowerOff: return "power_off";
    case autoac::IrButton::TempUp: return "temp_up";
    default: return "temp_down";
  }
}

static stdAc::opmode_t toIrMode(autoac::AcMode m) {
  switch (m) {
    case autoac::AcMode::Fan: return stdAc::opmode_t::kFan;
    case autoac::AcMode::Dry: return stdAc::opmode_t::kDry;
    case autoac::AcMode::Auto: return stdAc::opmode_t::kAuto;
    default: return stdAc::opmode_t::kCool;
  }
}

static stdAc::fanspeed_t toIrFan(autoac::FanSpeed f) {
  switch (f) {
    case autoac::FanSpeed::Low: return stdAc::fanspeed_t::kLow;
    case autoac::FanSpeed::Mid: return stdAc::fanspeed_t::kMedium;
    case autoac::FanSpeed::High: return stdAc::fanspeed_t::kHigh;
    default: return stdAc::fanspeed_t::kAuto;
  }
}

// ยิง IR จริงผ่าน IRac — สร้างรหัสแบบสถานะเต็มตามโปรโตคอล HAIER_AC176 ที่ทีมทดสอบกับแอร์จริงแล้วว่าใช้ได้
// (md-pcb/CHANGELOG.md: "ยิง IR สั่งแอร์ Haier ได้ด้วย HAIER_AC176") · ยิงซ้ำ 2 ครั้งต่อคำสั่งกันรับสัญญาณไม่ครบ
static void irSend(bool on, uint8_t setTemp, autoac::AcMode mode, autoac::FanSpeed fan) {
  stdAc::state_t state{};
  state.protocol = decode_type_t::HAIER_AC176;
  state.model = -1;
  state.power = on;
  state.mode = toIrMode(mode);
  state.celsius = true;
  state.degrees = setTemp;
  state.fanspeed = toIrFan(fan);
  state.swingv = stdAc::swingv_t::kOff;
  state.swingh = stdAc::swingh_t::kOff;
  state.light = true;

  for (int i = 0; i < 2; i++) {
    irAc.sendAc(state);
    if (i == 0) vTaskDelay(pdMS_TO_TICKS(400));
  }
  Serial.printf("[ir] ส่งแล้ว: %s temp=%u mode=%d fan=%d (HAIER_AC176, ยิงซ้ำ 2 ครั้ง)\n",
                on ? "ON" : "OFF", setTemp, (int)mode, (int)fan);
}

// ----- โหมด Learned: ยิงรหัสดิบที่เรียนรู้จากรีโมทจริง แทนโปรโตคอล IRac -----
// เปิด/ปิด = ยิง 1 ครั้งทันที, ปรับอุณหภูมิ = ยิง TempUp/TempDown ซ้ำตามจำนวนองศาที่ต่าง (เว้น 400ms/ครั้ง ไม่บล็อก)
static int g_pendingPresses = 0;
static autoac::IrButton g_pendingButton = autoac::IrButton::TempUp;
static uint32_t g_nextPressMs = 0;

static void sendLearnedButton(autoac::IrButton b) {
  const LearnedIrCode& code = app::irCode(b);
  if (!code.learned) {
    Serial.printf("[ir] ยังไม่เรียนรู้ปุ่ม %s — ข้าม\n", irButtonName(b));
    return;
  }
  irSendDev.sendRaw(code.raw, code.len, 38);
  Serial.printf("[ir] ยิงปุ่มที่เรียนรู้ไว้: %s\n", irButtonName(b));
}

static void irSendLearned(const autoac::Controller::Decision& d, int8_t tempDelta) {
  if (irLearnActive()) return; // RX/TX ห้ามชนกัน — ข้ามรอบนี้ ตัดสินใจใหม่รอบถัดไป

  // เปิด/ปิดยิงทันทีเสมอ (ไม่ขึ้นกับ burst อุณหภูมิค้างอยู่) — กฎความปลอดภัย/ประหยัดไฟอย่างปิดห้องว่าง
  // ต้องไม่ถูกเงียบๆ ข้ามเพียงเพราะมี burst อุณหภูมิเก่าค้างอยู่ (แก้จากรีวิว I4)
  if (d.sendOn || d.sendOff) {
    sendLearnedButton(d.sendOn ? autoac::IrButton::PowerOn : autoac::IrButton::PowerOff);
  }

  if (d.updateSetTemp && tempDelta != 0) {
    if (g_pendingPresses > 0) {
      Serial.println("[ir] กำลังส่งคำสั่งอุณหภูมิค้างอยู่ — ข้ามคำสั่งอุณหภูมิใหม่นี้");
    } else {
      autoac::IrButton btn = tempDelta > 0 ? autoac::IrButton::TempUp : autoac::IrButton::TempDown;
      if (!app::irCode(btn).learned) {
        Serial.printf("[ir] ยังไม่เรียนรู้ปุ่ม %s — ข้ามคำสั่งอุณหภูมิ\n", irButtonName(btn));
      } else {
        int presses = abs((int)tempDelta);
        g_pendingPresses = presses > 14 ? 14 : presses;
        g_pendingButton = btn;
        // ถ้าเพิ่งสั่งเปิดในดีซิชันเดียวกัน รอ ~1 วิก่อนกดอุณหภูมิ — แอร์หลายรุ่นไม่รับสัญญาณช่วงเพิ่งเปิด (รีวิว I6)
        g_nextPressMs = millis() + (d.sendOn ? 1000 : 0);
      }
    }
  }
}

static void irSendLearnedTick() {
  if (irLearnActive()) return; // RX/TX ห้ามชนกัน
  if (g_pendingPresses <= 0) return;
  if (millis() < g_nextPressMs) return;
  sendLearnedButton(g_pendingButton);
  g_pendingPresses--;
  g_nextPressMs = millis() + 400;
}

static bool readDHT(float& outTempC, float& outHumidity) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return false;
  outTempC = t;
  outHumidity = h;
  return true;
}

// ตารางเรียน: คำนวณว่าตอนนี้อยู่ในคาบไหม, เหลือกี่นาที, อีกกี่นาทีจะเริ่มคาบถัดไป
// ใช้เวลาจริงจาก DS3231/NTP (time.h) — ถ้ายังไม่รู้เวลาจริง ถือว่า "ไม่อยู่ในคาบ" เสมอ (ไม่รู้เวลา = ไม่ทำอะไรเสี่ยง)
struct ScheduleNow {
  bool activeNow = false;
  int32_t minutesRemaining = -1;
  int32_t minutesToNext = -1;
};

static bool systemTimeValid() {
  return time(nullptr) > 1700000000; // ประมาณปี 2023 ขึ้นไป ถือว่ารู้เวลาจริงแล้ว (เดียวกับ log_sd.cpp)
}

static ScheduleNow computeScheduleNow(uint32_t /*nowMs*/) {
  if (!systemTimeValid()) return ScheduleNow{};

  time_t now = time(nullptr);
  struct tm tminfo;
  localtime_r(&now, &tminfo);
  int dayIndex = (tminfo.tm_wday + 6) % 7; // tm_wday: 0=อาทิตย์ → 0=จันทร์..6=อาทิตย์ (ตรงกับ DAY_NAMES ฝั่งเว็บ)
  int nowMin = tminfo.tm_hour * 60 + tminfo.tm_min;
  int32_t today = autoac::daysFromCivil(tminfo.tm_year + 1900, tminfo.tm_mon + 1, tminfo.tm_mday);

  ScheduleNow out;
  for (auto& s : app::schedulePeriods) {
    if (!s.used || !s.enabled || s.day != dayIndex) continue;
    if (s.validFromDay != 0 && today < s.validFromDay) continue; // ยังไม่ถึงวันเริ่มมีผล
    if (s.validToDay != 0 && today > s.validToDay) continue;     // เลยวันสิ้นสุดมีผลแล้ว
    if (nowMin >= s.startMin && nowMin < s.endMin) {
      out.activeNow = true;
      out.minutesRemaining = s.endMin - nowMin;
      return out; // อยู่ในคาบนี้แล้ว ไม่ต้องหาคาบถัดไปต่อ
    }
    if (s.startMin > nowMin) {
      int toStart = s.startMin - nowMin;
      if (out.minutesToNext < 0 || toStart < out.minutesToNext) out.minutesToNext = toStart;
    }
  }
  return out;
}

static bool isHolidayToday() {
  if (!systemTimeValid()) return false; // ไม่รู้เวลา = ไม่ถือว่าเป็นวันหยุด (กันเปิดล่วงหน้าผิดวัน)
  time_t now = time(nullptr);
  struct tm tminfo;
  localtime_r(&now, &tminfo);
  char ymd[11];
  strftime(ymd, sizeof(ymd), "%Y-%m-%d", &tminfo);
  return app::holidays.contains(ymd);
}

static void applySetSetting(const autoac::Command& cmd) {
  int v = cmd.intArg;
  if (!strcmp(cmd.strArg, "emptyMin")) app::settings.emptyMin = (uint16_t)constrain(v, 5, 60);
  else if (!strcmp(cmd.strArg, "graceSec")) app::settings.graceSec = (uint16_t)constrain(v, 30, 300);
  else if (!strcmp(cmd.strArg, "hot")) app::settings.hot = (uint8_t)constrain(v, 24, 32);
  else if (!strcmp(cmd.strArg, "autoOn")) app::settings.autoOn = v != 0;
  else if (!strcmp(cmd.strArg, "preOpenMin")) app::settings.preOpenMin = (uint16_t)constrain(v, 0, 30);
  else if (!strcmp(cmd.strArg, "irMode")) { app::settings.irMode = v != 0 ? autoac::IrMode::Learned : autoac::IrMode::Protocol; app::markAttributesDirty(); }
  else return; // key ไม่รู้จัก — ไม่บันทึก
  nvsSaveSettings();
}

// cmd.strArg เข้ารหัสฟิลด์ตัวเลขเป็น CSV: "start,end,validFromDay,validToDay,enabled"
// cmd.intArg = id (index+1 ใน app::schedulePeriods; 0 = ไม่ใช้/เพิ่มใหม่), cmd.intArg2 = day (0-6)
static bool parseScheduleCsv(const char* csv, int& start, int& end, int& validFrom, int& validTo, int& enabled) {
  return sscanf(csv, "%d,%d,%d,%d,%d", &start, &end, &validFrom, &validTo, &enabled) == 5;
}

static int countUsedForDay(int day) {
  int n = 0;
  for (auto& s : app::schedulePeriods) if (s.used && s.day == day) n++;
  return n;
}

static void applyScheduleAdd(const autoac::Command& cmd) {
  int day = cmd.intArg2;
  int start, end, validFrom, validTo, enabled;
  if (day < 0 || day > 6) return;
  if (!parseScheduleCsv(cmd.strArg, start, end, validFrom, validTo, enabled)) return;
  if (start < 0 || end > 1439 || start >= end) return;
  if (countUsedForDay(day) >= autoac::SCHEDULE_MAX_PER_DAY) return; // เต็ม 50/วันแล้ว (SPEC ใหม่)

  for (auto& s : app::schedulePeriods) {
    if (s.used) continue;
    s = autoac::SchedulePeriod{};
    s.used = true;
    s.enabled = enabled != 0;
    s.day = (uint8_t)day;
    s.startMin = (uint16_t)start;
    s.endMin = (uint16_t)end;
    s.validFromDay = (uint16_t)validFrom;
    s.validToDay = (uint16_t)validTo;
    strncpy(s.name, cmd.scheduleName, autoac::SCHEDULE_NAME_LEN - 1);
    nvsSaveSchedule();
    app::markAttributesDirty();
    return;
  }
  // ไม่มีช่องว่างเหลือเลย (เต็ม SCHEDULE_MAX_PERIODS ทั้งระบบ) — ทิ้งคำสั่งเงียบๆ เหมือนกรณี >50/วัน
}

static void applyScheduleEdit(const autoac::Command& cmd) {
  int id = cmd.intArg;
  int day = cmd.intArg2;
  if (id < 1 || id > autoac::SCHEDULE_MAX_PERIODS || day < 0 || day > 6) return;
  autoac::SchedulePeriod& s = app::schedulePeriods[id - 1];
  if (!s.used) return;
  int start, end, validFrom, validTo, enabled;
  if (!parseScheduleCsv(cmd.strArg, start, end, validFrom, validTo, enabled)) return;
  if (start < 0 || end > 1439 || start >= end) return;

  s.enabled = enabled != 0;
  s.day = (uint8_t)day;
  s.startMin = (uint16_t)start;
  s.endMin = (uint16_t)end;
  s.validFromDay = (uint16_t)validFrom;
  s.validToDay = (uint16_t)validTo;
  strncpy(s.name, cmd.scheduleName, autoac::SCHEDULE_NAME_LEN - 1);
  s.name[autoac::SCHEDULE_NAME_LEN - 1] = '\0';
  nvsSaveSchedule();
  app::markAttributesDirty();
}

static void applyScheduleDelete(const autoac::Command& cmd) {
  int id = cmd.intArg;
  if (id < 1 || id > autoac::SCHEDULE_MAX_PERIODS) return;
  app::schedulePeriods[id - 1] = autoac::SchedulePeriod{};
  nvsSaveSchedule();
  app::markAttributesDirty();
}

static void applyHoliday(const autoac::Command& cmd, bool add) {
  auto& h = app::holidays;
  if (add) {
    if (h.count < autoac::MAX_HOLIDAYS && !h.contains(cmd.strArg)) {
      strncpy(h.dates[h.count], cmd.strArg, 10);
      h.count++;
    }
  } else {
    for (int i = 0; i < h.count; i++) {
      if (!strcmp(h.dates[i], cmd.strArg)) {
        for (int j = i; j < h.count - 1; j++) strcpy(h.dates[j], h.dates[j + 1]);
        h.count--;
        break;
      }
    }
  }
  nvsSaveHolidays();
  app::markAttributesDirty(); // บั๊กเดิม: ไม่เคยแจ้งให้ net_task publish holidays ขึ้นเว็บเลย (เว็บเลยเห็นว่างตลอด)
}

static void applyDecision(const autoac::Controller::Decision& d, const autoac::AcState& before) {
  if (!d.any()) return;
  autoac::AcState ac = controller.state();

  if (app::settings.irMode == autoac::IrMode::Protocol) {
    irSend(ac.on, ac.setTemp, ac.mode, ac.fan);
  } else {
    int8_t tempDelta = d.updateSetTemp ? (int8_t)((int)ac.setTemp - (int)before.setTemp) : 0;
    irSendLearned(d, tempDelta);
  }
  app::setLastAcState(ac);

  const char* cmdName = d.sendOn ? "on" : d.sendOff ? "off" : d.updateSetTemp ? "temp" :
                         d.updateMode ? "mode" : "fan";
  const char* srcName = "auto";
  switch (d.source) {
    case autoac::Source::Screen: srcName = "screen"; break;
    case autoac::Source::Telegram: srcName = "telegram"; break;
    case autoac::Source::Web: srcName = "web"; break;
    case autoac::Source::ThingsBoard: srcName = "thingsboard"; break;
    case autoac::Source::Schedule: srcName = "schedule"; break;
    default: srcName = "auto"; break;
  }
  publishEvent(cmdName, srcName, d.reason);
  logEvent(cmdName, srcName, d.reason);
}

static void controlTaskLoop(void*) {
  nvsLoadAll();
  logInit();
  controller.begin(millis());
  g_pirWarmupStartMs = millis();
  pinMode(PIN_PIR, INPUT);
  dht.begin();
  irLearnBegin();
  irSendDev.begin(); // IRac ของ Protocol mode เรียก begin() ให้เองใน sendAc() แต่ irSendDev (โหมด Learned) ไม่มีใครเรียกให้
  rtcBegin(); // ตั้งเวลาระบบจาก DS3231 ถ้ามี — ให้ตารางเรียน/วันหยุด/log รู้เวลาได้ทันทีแม้ WiFi ยังไม่ต่อ
  amg8833Begin(); // I2C เดียวกับ DS3231 (ต่อ rtcBegin() เรียก Wire.begin() ไว้แล้ว)
  buzzerBegin();
  radarBegin();
  statusLedBegin();

  uint32_t lastDhtMs = 0;
  float lastTempC = 25, lastHumidity = 50;
  bool dhtOk = true;

  for (;;) {
    uint32_t now = millis();

    autoac::Command cmd;
    while (xQueueReceive(app::cmdQueue, &cmd, 0) == pdTRUE) {
      if (cmd.type == autoac::Command::Type::SetSetting) { applySetSetting(cmd); continue; }
      if (cmd.type == autoac::Command::Type::ScheduleAdd) { applyScheduleAdd(cmd); continue; }
      if (cmd.type == autoac::Command::Type::ScheduleEdit) { applyScheduleEdit(cmd); continue; }
      if (cmd.type == autoac::Command::Type::ScheduleDelete) { applyScheduleDelete(cmd); continue; }
      if (cmd.type == autoac::Command::Type::AddHoliday) { applyHoliday(cmd, true); continue; }
      if (cmd.type == autoac::Command::Type::RemoveHoliday) { applyHoliday(cmd, false); continue; }
      if (cmd.type == autoac::Command::Type::HwIrTest) {
        // ปุ่มทดสอบ IR ดิบจากหน้าจอเครื่องมือช่าง — ไม่ผ่าน Controller (กติกาสถาปัตยกรรมข้อ 2)
        bool on = cmd.intArg != 0;
        irSend(on, controller.state().setTemp, controller.state().mode, controller.state().fan);
        publishEvent(on ? "hw_on" : "hw_off", "hw", "ทดสอบ IR จากหน้าจอเครื่องมือช่าง");
        logEvent(on ? "hw_on" : "hw_off", "hw", "ทดสอบ IR จากหน้าจอเครื่องมือช่าง");
        continue;
      }
      if (cmd.type == autoac::Command::Type::IrLearnStart) {
        // เรียนรู้ปุ่ม IR จากรีโมทจริง — ไม่ผ่าน Controller (กติกาสถาปัตยกรรมข้อ 2)
        if (irLearnActive() || g_pendingPresses > 0) {
          // กำลังฟังปุ่มอื่นอยู่ หรือกำลังส่ง burst อุณหภูมิค้างอยู่ — ไม่แทรก และห้ามรายงานว่า listening ปุ่มใหม่
          app::setLastIrLearnStatus("busy");
          publishEvent("ir_learn", "web", "กำลังเรียนรู้/ส่งคำสั่งอยู่ ลองใหม่อีกครั้ง");
        } else {
          autoac::IrButton b = static_cast<autoac::IrButton>(cmd.intArg);
          irLearnStart(b);
          char status[24];
          snprintf(status, sizeof(status), "listening:%s", irButtonName(b));
          app::setLastIrLearnStatus(status);
          Serial.printf("[ir_learn] เริ่มฟัง %s\n", irButtonName(b));
        }
        continue;
      }
      if (cmd.type == autoac::Command::Type::IrLearnCancel) {
        irLearnCancel();
        app::setLastIrLearnStatus("idle");
        continue;
      }
      if (cmd.type == autoac::Command::Type::IrClear) {
        if (cmd.intArg >= 0 && cmd.intArg < autoac::IR_BUTTON_COUNT) {
          autoac::IrButton b = static_cast<autoac::IrButton>(cmd.intArg);
          LearnedIrCode cleared{}; // learned=false, len=0
          app::setIrCode(b, cleared); // markAttributesDirty() ให้เองข้างใน (ดู app.cpp)
          nvsSaveIrCode(b, cleared);
          app::setLastIrLearnStatus("idle");
          Serial.printf("[ir_learn] ลบรหัสที่เรียนรู้ไว้ของ %s\n", irButtonName(b));
        }
        continue;
      }
      if (cmd.type == autoac::Command::Type::CalibrateTemp) {
        // ซิงค์อุณหภูมิจริง (โหมด Learned ที่นับแบบสัมพัทธ์อาจเพี้ยนสะสม) — ไม่ผ่าน Controller::applyCommand ไม่ส่ง IR
        uint8_t v = (uint8_t)constrain(cmd.intArg, (int32_t)autoac::MAX_ALLOWED_TEMP_C - 14, (int32_t)autoac::MAX_ALLOWED_TEMP_C);
        controller.calibrateSetTemp(v);
        app::setLastAcState(controller.state());
        publishEvent("calibrate_temp", "web", "ซิงค์อุณหภูมิให้ตรงจอแอร์จริง");
        logEvent("calibrate_temp", "web", "ซิงค์อุณหภูมิให้ตรงจอแอร์จริง");
        continue;
      }

      autoac::Controller::Decision d;
      autoac::AcState before = controller.state();
      if (controller.applyCommand(cmd, now, d)) applyDecision(d, before);
    }

    {
      bool learnOk = false;
      autoac::IrButton learnButton = autoac::IrButton::PowerOn;
      LearnedIrCode learnCode;
      if (irLearnPoll(learnOk, learnButton, learnCode)) {
        const char* statusWord;
        if (learnOk) {
          app::setIrCode(learnButton, learnCode);
          nvsSaveIrCode(learnButton, learnCode);
          statusWord = "ok";
        } else {
          statusWord = "timeout"; // ครอบคลุม too_long ด้วย (ทั้งคู่ = ไม่ได้รหัส)
        }
        char status[24];
        snprintf(status, sizeof(status), "%s:%s", statusWord, irButtonName(learnButton));
        app::setLastIrLearnStatus(status);
        publishEvent("ir_learn", "web", status);
        logEvent("ir_learn", "web", status);
        Serial.printf("[ir_learn] จบ session: %s\n", status);
      }
    }
    irSendLearnedTick();

    if (now - lastDhtMs >= 2200) { // DHT22 ต้องเว้น >= 2 วิ/ครั้ง (CLAUDE.md)
      lastDhtMs = now;
      float t, h;
      dhtOk = readDHT(t, h);
      if (dhtOk) { lastTempC = t; lastHumidity = h; }
      app::setDhtOk(dhtOk);
      // TODO: ถ้า !dhtOk ให้ใช้อุณหภูมิจาก DS3231 แสดงผลแทน (รอบถัดไปเมื่อต่อ RTC)
    }

    bool pirReady = now - g_pirWarmupStartMs >= 60000;
    bool pirMotion = pirReady && digitalRead(PIN_PIR) == HIGH;
    radarPoll(); // ไม่บล็อก — แค่อ่าน UART ที่มีอยู่แล้วประกอบเฟรม

    float thermalPixels[app::THERMAL_PIXEL_COUNT];
    float thermalMax = 0, thermalMin = 0;
    bool thermalOk = amg8833ReadFrame(thermalPixels, thermalMax, thermalMin);
    if (thermalOk) app::setLastThermal(thermalPixels, thermalMax, thermalMin, true);
    else app::setLastThermal(app::lastThermal().pixels, 0, 0, false);
    app::setLastRadar(radarLastFrame());

    // เจอฝั่งใดฝั่งหนึ่งถือว่ามีคน — PIR จับได้แค่ตอนเคลื่อนไหว กล้องความร้อน/เรดาร์ยังเห็นคนนั่งนิ่งได้ (ใช้ threshold เดิมจาก amg8833.cpp)
    bool occupied = pirMotion || amg8833DetectPresence() || radarPresence();
    app::setLastSensors(lastTempC, lastHumidity, occupied);

    ScheduleNow sched = computeScheduleNow(now);

    autoac::Controller::Inputs in;
    in.nowMs = now;
    in.pirMotion = pirMotion;
    in.touch = false; // TODO: เชื่อมจากจอสัมผัสรอบถัดไป
    in.tempC = lastTempC;
    in.tempValid = dhtOk;
    in.isHoliday = isHolidayToday();
    in.scheduleSlotActiveNow = sched.activeNow;
    in.minutesSlotRemaining = sched.minutesRemaining;
    in.minutesToNextSlotStart = sched.minutesToNext;

    autoac::AcState beforeTick = controller.state();
    autoac::Controller::Decision d = controller.tick(in);
    applyDecision(d, beforeTick);

    statusLedUpdate(app::wifiConnected(), app::tbConnected(), controller.state().on, occupied, irLearnActive());

    // บี๊บเตือนตอนเริ่มนับถอยหลังจะปิดแอร์อัตโนมัติ (ห้องว่าง/ใกล้หมดคาบ) — บี๊บครั้งเดียวตอนเริ่ม ไม่ใช่รัวทุกวินาที
    static bool wasWarning = false;
    bool isWarning = controller.warnActive();
    if (isWarning && !wasWarning) buzzerBeep();
    wasWarning = isWarning;

    logTick();

    // ระหว่างมี burst อุณหภูมิค้างอยู่ (โหมด Learned) ต้องวนถี่กว่า 1 วิ ไม่งั้นจะกดห่างกันเกิน 400ms ตามสเปก (รีวิว I5)
    uint32_t loopDelayMs = 1000;
    if (g_pendingPresses > 0) {
      int32_t untilNextPress = (int32_t)(g_nextPressMs - millis());
      loopDelayMs = untilNextPress > 0 ? (uint32_t)untilNextPress : 0;
      if (loopDelayMs > 1000) loopDelayMs = 1000;
    }
    vTaskDelay(pdMS_TO_TICKS(loopDelayMs));
  }
}

void controlTaskStart() {
  xTaskCreatePinnedToCore(controlTaskLoop, "control", 8192, nullptr, 2, nullptr, 1);
}
