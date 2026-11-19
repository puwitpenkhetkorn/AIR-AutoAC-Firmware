#pragma once
#include "types.h"

// ตรรกะอัตโนมัติทั้งหมด (SPEC.md ข้อ 1) — ไม่พึ่ง Arduino/ESP32 ให้เทสต์รันบนคอมได้
// เวลาเป็น uint32_t millis-style เสมอ ใช้ (now - t) unsigned ตามกติกาข้อ 6

namespace autoac {

class Controller {
 public:
  struct Inputs {
    uint32_t nowMs = 0;
    bool pirMotion = false;    // PIR ตรวจจับได้ ณ tick นี้ (จริงจากเซนเซอร์เท่านั้น)
    bool touch = false;        // แตะจอ = มีคน (แต่ไม่นับเป็น PIR จริง)
    float tempC = 0;
    bool tempValid = true;     // false = DHT22 เสีย/NaN (ใช้ DS3231 แสดงผล, หยุดกฎเปิดเมื่อร้อน)
    bool isHoliday = false;
    bool scheduleSlotActiveNow = false;
    int32_t minutesSlotRemaining = -1; // คาบเรียนปัจจุบันเหลือกี่นาที (-1 = ไม่อยู่ในคาบ)
    int32_t minutesToNextSlotStart = -1; // อีกกี่นาทีจะเริ่มคาบถัดไปวันนี้ (-1 = ไม่มี)
  };

  struct Decision {
    bool sendOn = false;
    bool sendOff = false;
    bool updateSetTemp = false;
    uint8_t setTemp = 0;
    bool updateMode = false;
    AcMode mode = AcMode::Cool;
    bool updateFan = false;
    FanSpeed fan = FanSpeed::Auto;
    Source source = Source::Auto;
    const char* reason = "";

    bool any() const { return sendOn || sendOff || updateSetTemp || updateMode || updateFan; }
  };

  explicit Controller(Settings& settings) : settings_(settings) {}

  void setDemoMode(bool on) { demo_ = on; }
  bool manualOnlyMode() const { return manualOnly_; }
  void setManualOnlyMode(bool on) { manualOnly_ = on; }

  // เรียกครั้งเดียวตอนบูต — หลังไฟกลับถือว่าแอร์ปิด (UC-09)
  void begin(uint32_t nowMs) {
    bootMs_ = nowMs;
    // กันสั่งถี่ "นับจากบูต" = ไม่ต้องรอ 3 นาทีแรกหลังบูตก่อนสั่งได้ครั้งแรก
    lastCmdMs_ = nowMs - spamGuardMs();
    lastPresenceMs_ = nowMs; // หลังบูตถือว่ามีคน กันห้องว่างนับถอยหลังทันที
    lastPirMs_ = nowMs - 16u * 60000u; // ยังไม่เคยเจอ PIR จริง (เกิน 15 นาทีไว้ก่อน)
    ac_ = AcState{};
    ac_.on = false;
  }

  // เรียกทุก tick (แนะนำทุก 1 วิ) ด้วยค่าจากเซนเซอร์/ตาราง — คืนคำสั่งที่ควรสั่ง IR (ถ้ามี)
  Decision tick(const Inputs& in) {
    Decision d;
    bool pirReady = elapsed(in.nowMs, bootMs_) >= pirWarmupSec() * 1000u;

    if (pirReady && in.pirMotion) lastPirMs_ = in.nowMs;
    if ((pirReady && in.pirMotion) || in.touch) {
      bool wasIdle = warnUntil_ != 0;
      lastPresenceMs_ = in.nowMs;
      warnUntil_ = 0; // แตะจอ/มีคน = ยกเลิกรอปิด
      (void)wasIdle;
    }

    if (manualOverrideUntil_ != 0 && elapsed(in.nowMs, manualOverrideUntil_) < 0x80000000u &&
        in.nowMs != manualOverrideUntil_ && elapsedSigned(manualOverrideUntil_, in.nowMs) <= 0) {
      // override หมดอายุแล้ว (เทียบแบบ unsigned-safe ด้านล่างแทน)
    }
    if (manualOverrideUntil_ != 0 && elapsed(in.nowMs, manualOverrideStart_) >= manualOverrideMs()) {
      manualOverrideUntil_ = 0; // override 30 นาทีหมดอายุ กลับไปกฎอัตโนมัติ
    }
    bool manualActive = manualOverrideUntil_ != 0;

    // ----- กฎห้องว่าง: ไม่พบการเคลื่อนไหว emptyMin → เตือน graceSec → ปิด -----
    // หมายเหตุ: override 30 นาทีของการสั่งเอง ไม่กันกฎนี้ (เป็นฟีเจอร์ปลอดภัย/ประหยัดไฟ แยกจากกฎเปิด-ปิดอัตโนมัติ)
    // manualOnly_ (FR-17 "สั่งเองอย่างเดียว") ปิดกฎนี้ด้วยตามสเปก
    if (ac_.on && !manualOnly_) {
      uint32_t idleMs = elapsed(in.nowMs, lastPresenceMs_);
      uint32_t emptyMs = (uint32_t)settings_.emptyMin * 60000u;
      uint32_t graceMs = (uint32_t)settings_.graceSec * 1000u;
      if (demo_) { emptyMs = 60000u; graceMs = 20000u; }

      if (idleMs >= emptyMs + graceMs) {
        if (sendOff(d, in.nowMs, Source::Auto, "ไม่พบการเคลื่อนไหวในห้อง")) {
          warnUntil_ = 0;
          return d;
        }
      } else if (idleMs >= emptyMs) {
        warnUntil_ = lastPresenceMs_ + emptyMs + graceMs;
      } else {
        warnUntil_ = 0;
      }
    } else {
      warnUntil_ = 0;
    }

    // ----- กฎปิดตามเวลา: จบคาบเรียน (เตือนก่อนหมดเวลาเหมือนกฎห้องว่าง) -----
    if (ac_.on && !manualActive && !manualOnly_ && in.scheduleSlotActiveNow && in.minutesSlotRemaining >= 0) {
      uint32_t graceSec = demo_ ? 20 : settings_.graceSec;
      if ((uint32_t)in.minutesSlotRemaining * 60u <= graceSec && in.minutesSlotRemaining >= 0) {
        if (in.minutesSlotRemaining == 0) {
          if (sendOff(d, in.nowMs, Source::Schedule, "หมดเวลาคาบเรียน")) return d;
        } else {
          scheduleWarnActive_ = true;
        }
      } else {
        scheduleWarnActive_ = false;
      }
    } else {
      scheduleWarnActive_ = false;
    }

    // ----- กฎเปิดเมื่อร้อน: >= hot ต่อเนื่อง 2 นาที + มี PIR จริงใน 15 นาที + เว้น 3 นาที -----
    if (!manualActive && !manualOnly_ && settings_.autoOn && in.tempValid) {
      if (in.tempC >= settings_.hot) {
        if (hotSinceMs_ == 0) hotSinceMs_ = in.nowMs;
      } else {
        hotSinceMs_ = 0;
      }
      uint32_t hotHoldMs = demo_ ? 10000u : 120000u;
      uint32_t hotCooldownMs = demo_ ? 10000u : 180000u;
      bool pirRecent = elapsed(in.nowMs, lastPirMs_) <= 15u * 60000u;

      if (!ac_.on && hotSinceMs_ != 0 && elapsed(in.nowMs, hotSinceMs_) >= hotHoldMs && pirRecent &&
          elapsed(in.nowMs, lastHotTriggerMs_) >= hotCooldownMs) {
        lastHotTriggerMs_ = in.nowMs;
        if (sendOn(d, in.nowMs, Source::Auto, "ห้องร้อนและมีคนในห้อง")) return d;
      }
    }

    // ----- กฎเปิดล่วงหน้า: ก่อนคาบเรียน preOpenMin นาที, ร้อนกว่า autoOn, ไม่ใช่วันหยุด, โหมด Auto -----
    if (!manualActive && !manualOnly_ && !ac_.on && !in.isHoliday && settings_.autoOn && in.tempValid &&
        in.minutesToNextSlotStart >= 0 && in.minutesToNextSlotStart <= (int32_t)settings_.preOpenMin &&
        in.tempC >= settings_.hot && elapsed(in.nowMs, lastHotTriggerMs_) >= (demo_ ? 10000u : 180000u)) {
      lastHotTriggerMs_ = in.nowMs;
      if (sendOn(d, in.nowMs, Source::Schedule, "เปิดล่วงหน้าก่อนคาบเรียน")) return d;
    }

    return d;
  }

  // คำสั่งจากช่องทางต่างๆ (จอ/Telegram/เว็บ/ThingsBoard) — กันสั่งถี่ในตัว
  bool applyCommand(const Command& cmd, uint32_t nowMs, Decision& out) {
    using T = Command::Type;
    out = Decision{};
    out.source = cmd.source;

    if (cmd.type == T::Touch) {
      lastPresenceMs_ = nowMs;
      warnUntil_ = 0;
      return true;
    }

    bool isAcCommand = cmd.type == T::On || cmd.type == T::Off || cmd.type == T::SetTemp ||
                        cmd.type == T::SetMode || cmd.type == T::SetFan;
    if (isAcCommand && !spamOk(nowMs)) return false;

    switch (cmd.type) {
      case T::On:
        startManualOverride(nowMs);
        sendOn(out, nowMs, cmd.source, "สั่งเปิดด้วยมือ");
        break;
      case T::Off:
        startManualOverride(nowMs);
        sendOff(out, nowMs, cmd.source, "สั่งปิดด้วยมือ");
        break;
      case T::SetTemp: {
        uint8_t lo = minAllowedTempFor(cmd.source, cmd.admin);
        uint8_t v = (uint8_t)cmd.intArg;
        if (v < lo) v = lo;
        if (v > MAX_ALLOWED_TEMP_C) v = MAX_ALLOWED_TEMP_C;
        startManualOverride(nowMs);
        ac_.setTemp = v;
        out.updateSetTemp = true;
        out.setTemp = v;
        lastCmdMs_ = nowMs;
        break;
      }
      case T::SetMode:
        startManualOverride(nowMs);
        ac_.mode = static_cast<AcMode>(cmd.intArg);
        out.updateMode = true;
        out.mode = ac_.mode;
        lastCmdMs_ = nowMs;
        break;
      case T::SetFan:
        startManualOverride(nowMs);
        ac_.fan = static_cast<FanSpeed>(cmd.intArg);
        out.updateFan = true;
        out.fan = ac_.fan;
        lastCmdMs_ = nowMs;
        break;
      case T::Auto:
        manualOverrideUntil_ = 0;
        manualOnly_ = false;
        break;
      case T::Manual:
        manualOnly_ = true; // "สั่งเองอย่างเดียว" (FR-17) ปิดทุกกฎรวมปิดตามเวลา
        manualOverrideUntil_ = 0;
        break;
      case T::Sync:
        break; // ให้ task ชั้นบนส่ง IR ซ้ำสถานะปัจจุบัน
      case T::Extend:
        startManualOverride(nowMs); // ขยายเวลา = ต่อ override ไม่ให้กฎตารางปิด
        break;
      default:
        return true; // SetSetting/SetSchedule/AddHoliday/RemoveHoliday จัดการนอก Controller
    }
    return true;
  }

  // ซิงค์อุณหภูมิที่ซอฟต์แวร์จำไว้ให้ตรงกับที่จอแอร์จริงโชว์ (โหมด Learned ที่กด +/- เป็นสัมพัทธ์ อาจเพี้ยนสะสมได้)
  // แก้ตรงๆ ไม่ใช่ "คำสั่ง" — ไม่ผ่านกันสั่งถี่/ไม่เริ่ม manual override/ไม่ส่ง IR ใดๆ ทั้งสิ้น
  void calibrateSetTemp(uint8_t v) { ac_.setTemp = v; }

  const AcState& state() const { return ac_; }
  bool warnActive() const { return warnUntil_ != 0 || scheduleWarnActive_; }
  uint32_t warnRemainingSec(uint32_t nowMs) const {
    if (warnUntil_ == 0) return 0;
    uint32_t e = elapsed(warnUntil_, nowMs);
    return e > 0x7fffffffu ? 0 : e / 1000u;
  }

 private:
  Settings& settings_;
  AcState ac_;
  bool demo_ = false;
  bool manualOnly_ = false;

  uint32_t bootMs_ = 0;
  uint32_t lastCmdMs_ = 0;
  uint32_t lastPresenceMs_ = 0;
  uint32_t lastPirMs_ = 0;
  uint32_t warnUntil_ = 0;
  bool scheduleWarnActive_ = false;
  uint32_t hotSinceMs_ = 0;
  uint32_t lastHotTriggerMs_ = 0;
  uint32_t manualOverrideStart_ = 0;
  uint32_t manualOverrideUntil_ = 0;

  static uint32_t elapsed(uint32_t now, uint32_t t) { return now - t; } // unsigned wrap-safe
  static int32_t elapsedSigned(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

  uint32_t pirWarmupSec() const { return 60u; }
  uint32_t manualOverrideMs() const { return demo_ ? 15000u : 30u * 60000u; }
  uint32_t spamGuardMs() const { return 5000u; } // กันคอมเพรสเซอร์พัง (เปิด/ปิดถี่เกินไป) — ผู้ใช้ขอลดจาก 10 วิ

  bool spamOk(uint32_t nowMs) { return elapsed(nowMs, lastCmdMs_) >= spamGuardMs(); }

  void startManualOverride(uint32_t nowMs) {
    manualOverrideStart_ = nowMs;
    manualOverrideUntil_ = nowMs + manualOverrideMs();
    if (manualOverrideUntil_ == 0) manualOverrideUntil_ = 1; // กัน 0 แปลว่า "ไม่ active"
  }

  bool sendOn(Decision& d, uint32_t nowMs, Source src, const char* reason) {
    if (ac_.on) return false;
    if (!spamOk(nowMs)) return false;
    ac_.on = true;
    lastCmdMs_ = nowMs;
    lastPresenceMs_ = nowMs; // เปิดแอร์ทุกช่องทาง = เริ่มนับห้องว่างใหม่
    d.sendOn = true;
    d.source = src;
    d.reason = reason;
    return true;
  }

  bool sendOff(Decision& d, uint32_t nowMs, Source src, const char* reason) {
    if (!ac_.on) return false;
    if (!spamOk(nowMs)) return false;
    ac_.on = false;
    lastCmdMs_ = nowMs;
    d.sendOff = true;
    d.source = src;
    d.reason = reason;
    return true;
  }
};

} // namespace autoac
