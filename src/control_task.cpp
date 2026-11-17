// Control task — คนเดียวที่เรียก Controller และ ir::send() (กติกาข้อ 2)
#include "control_task.h"
#include "app.h"
#include "net_task.h"
#include "nvs_store.h"
#include "log_sd.h"
#include "pins.h"
#include "../lib/autoac_core/Controller.h"
#include "ir_learn.h"
#include <DHT.h>
#include <IRac.h>
#include <IRsend.h>

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
// หมายเหตุ: ยังไม่มี RTC จริงต่อ (รอบถัดไป) — ใช้เวลาตั้งแต่บูตเป็นตัวหลอกไปก่อน เพื่อให้คอมไพล์/เทสต์ได้
struct ScheduleNow {
  bool activeNow = false;
  int32_t minutesRemaining = -1;
  int32_t minutesToNext = -1;
};

static ScheduleNow computeScheduleNow(uint32_t /*nowMs*/) {
  // TODO: ต่อ DS3231 แล้วแทนที่ด้วยเวลาจริง + วันในสัปดาห์จริง (ตอนนี้คืนค่า "ไม่อยู่ในคาบ" เสมอ)
  return ScheduleNow{};
}

static bool isHolidayToday() {
  // TODO: ต่อ RTC แล้วเทียบ app::holidays กับวันที่จริง
  return false;
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

static void applySetSchedule(const autoac::Command& cmd) {
  int day = cmd.intArg;
  int slot = cmd.intArg2;
  if (day < 0 || day > 6 || slot < 0 || slot > 1) return;
  int en = 0, start = 0, end = 0;
  if (sscanf(cmd.strArg, "%d,%d,%d", &en, &start, &end) != 3) return;
  if (start >= end) return; // UI ต้องกันก่อนส่งอยู่แล้ว กันซ้ำที่นี่ด้วย
  app::schedule[day][slot] = {en != 0, (uint16_t)start, (uint16_t)end};
  nvsSaveSchedule();
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

  uint32_t lastDhtMs = 0;
  float lastTempC = 25, lastHumidity = 50;
  bool dhtOk = true;

  for (;;) {
    uint32_t now = millis();

    autoac::Command cmd;
    while (xQueueReceive(app::cmdQueue, &cmd, 0) == pdTRUE) {
      if (cmd.type == autoac::Command::Type::SetSetting) { applySetSetting(cmd); continue; }
      if (cmd.type == autoac::Command::Type::SetSchedule) { applySetSchedule(cmd); continue; }
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
      // TODO: ถ้า !dhtOk ให้ใช้อุณหภูมิจาก DS3231 แสดงผลแทน (รอบถัดไปเมื่อต่อ RTC)
    }

    bool pirReady = now - g_pirWarmupStartMs >= 60000;
    bool pirMotion = pirReady && digitalRead(PIN_PIR) == HIGH;
    bool occupied = pirMotion; // แสดงผล "มีคนในห้อง" อย่างง่าย (ยังไม่รวม AMG8833/LD2410C — รอบถัดไป)
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
