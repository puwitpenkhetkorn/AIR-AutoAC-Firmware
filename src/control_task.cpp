// Control task — คนเดียวที่เรียก Controller และ ir::send() (กติกาข้อ 2)
#include "control_task.h"
#include "app.h"
#include "net_task.h"
#include "nvs_store.h"
#include "log_sd.h"
#include "pins.h"
#include "../lib/autoac_core/Controller.h"
#include <DHT.h>
#include <IRac.h>

static DHT dht(PIN_DHT22, DHT22);
static autoac::Controller controller(app::settings);
static IRac irAc(PIN_IR_TX);

static uint32_t g_pirWarmupStartMs = 0;

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

static void applyDecision(const autoac::Controller::Decision& d) {
  if (!d.any()) return;
  autoac::AcState ac = controller.state();
  irSend(ac.on, ac.setTemp, ac.mode, ac.fan);
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

      autoac::Controller::Decision d;
      if (controller.applyCommand(cmd, now, d)) applyDecision(d);
    }

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

    autoac::Controller::Decision d = controller.tick(in);
    applyDecision(d);

    logTick();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void controlTaskStart() {
  xTaskCreatePinnedToCore(controlTaskLoop, "control", 8192, nullptr, 2, nullptr, 1);
}
