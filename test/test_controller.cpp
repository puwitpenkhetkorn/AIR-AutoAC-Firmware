#include <unity.h>
#include "../lib/autoac_core/Controller.h"

using namespace autoac;

static Settings makeSettings() {
  Settings s;
  s.emptyMin = 15;
  s.graceSec = 60;
  s.hot = 27;
  s.autoOn = true;
  s.preOpenMin = 10;
  return s;
}

static Controller::Inputs baseInputs(uint32_t now) {
  Controller::Inputs in;
  in.nowMs = now;
  in.tempValid = true;
  in.tempC = 24;
  return in;
}

// ---- กันสั่งถี่ + manual override ----

void test_manual_on_off_basic() {
  Settings s = makeSettings();
  Controller c(s);
  c.begin(0);

  Command on{Command::Type::On, Source::Web};
  Controller::Decision d;
  TEST_ASSERT_TRUE(c.applyCommand(on, 1000, d));
  TEST_ASSERT_TRUE(d.sendOn);
  TEST_ASSERT_TRUE(c.state().on);
}

void test_anti_spam_blocks_rapid_commands() {
  Settings s = makeSettings();
  Controller c(s);
  c.begin(0);

  Command on{Command::Type::On, Source::Web};
  Controller::Decision d;
  TEST_ASSERT_TRUE(c.applyCommand(on, 1000, d));

  Command off{Command::Type::Off, Source::Web};
  // ยังไม่ถึง 3 นาที (180000 ms) นับจากคำสั่งก่อนหน้า (ที่ 1000ms) → ต้องถูกบล็อก
  TEST_ASSERT_FALSE(c.applyCommand(off, 2000, d));
  TEST_ASSERT_TRUE(c.state().on); // ยังเปิดอยู่

  // ผ่าน 3 นาทีแล้ว → สั่งได้
  TEST_ASSERT_TRUE(c.applyCommand(off, 1000 + 181000, d));
  TEST_ASSERT_TRUE(d.sendOff);
}

void test_set_temp_clamped_by_channel_min() {
  Settings s = makeSettings();
  Controller c(s);
  c.begin(0);

  Command t{Command::Type::SetTemp, Source::Screen};
  t.admin = false; // จอไม่ใส่ PIN → ขอบล่าง 20
  t.intArg = 16;
  Controller::Decision d;
  TEST_ASSERT_TRUE(c.applyCommand(t, 1000, d));
  TEST_ASSERT_EQUAL_UINT8(20, d.setTemp);

  Command t2{Command::Type::SetTemp, Source::Screen};
  t2.admin = true; // จอใส่ PIN → ขอบล่าง 16
  t2.intArg = 16;
  TEST_ASSERT_TRUE(c.applyCommand(t2, 1000 + 181000, d));
  TEST_ASSERT_EQUAL_UINT8(16, d.setTemp);
}

// ---- กฎห้องว่าง + เตือนก่อนปิด ----

void test_empty_room_warns_then_turns_off() {
  Settings s = makeSettings();
  s.emptyMin = 15;
  s.graceSec = 60;
  Controller c(s);
  c.begin(0);

  Command on{Command::Type::On, Source::Web};
  Controller::Decision d;
  c.applyCommand(on, 0, d);

  uint32_t emptyMs = 15u * 60000u;

  // ก่อนครบ emptyMin ไม่มีคำสั่ง ไม่เตือน
  auto in1 = baseInputs(emptyMs - 1000);
  Controller::Decision tick1 = c.tick(in1);
  TEST_ASSERT_FALSE(tick1.any());
  TEST_ASSERT_FALSE(c.warnActive());

  // ครบ emptyMin → เริ่มเตือน (ยังไม่ปิด)
  auto in2 = baseInputs(emptyMs + 1000);
  Controller::Decision tick2 = c.tick(in2);
  TEST_ASSERT_FALSE(tick2.any());
  TEST_ASSERT_TRUE(c.warnActive());

  // ครบ emptyMin + graceSec → ปิด
  auto in3 = baseInputs(emptyMs + 60000 + 1000);
  Controller::Decision tick3 = c.tick(in3);
  TEST_ASSERT_TRUE(tick3.sendOff);
  TEST_ASSERT_FALSE(c.state().on);
}

void test_motion_resets_empty_timer() {
  Settings s = makeSettings();
  Controller c(s);
  c.begin(0);
  Command on{Command::Type::On, Source::Web};
  Controller::Decision d;
  c.applyCommand(on, 0, d);

  uint32_t emptyMs = 15u * 60000u;

  auto in = baseInputs(emptyMs - 1000);
  in.pirMotion = true;
  c.tick(in); // รีเซ็ตนับเวลา (pirReady เพราะเกิน 60s warmup แล้ว)

  auto in2 = baseInputs(emptyMs - 1000 + emptyMs - 2000);
  Controller::Decision tick2 = c.tick(in2);
  TEST_ASSERT_FALSE(tick2.sendOff); // ยังไม่ปิดเพราะมีคนขยับเมื่อกี้
}

// ---- กฎเปิดเมื่อร้อน ----

void test_hot_rule_turns_on_with_recent_pir() {
  Settings s = makeSettings();
  s.hot = 27;
  Controller c(s);
  c.begin(0);

  uint32_t t0 = 70000; // ผ่าน PIR warm-up (60s) แล้ว
  auto in = baseInputs(t0);
  in.pirMotion = true;
  in.tempC = 28;
  c.tick(in); // ตั้ง lastPirMs + hotSince

  auto in2 = baseInputs(t0 + 120000 + 1000); // ร้อนต่อเนื่องครบ 2 นาที
  in2.tempC = 28;
  Controller::Decision d2 = c.tick(in2);
  TEST_ASSERT_TRUE(d2.sendOn);
  TEST_ASSERT_TRUE(c.state().on);
}

void test_hot_rule_requires_recent_pir() {
  Settings s = makeSettings();
  s.hot = 27;
  Controller c(s);
  c.begin(0);

  uint32_t t0 = 70000;
  auto in = baseInputs(t0);
  in.tempC = 28; // ร้อนแต่ไม่มี PIR เลย
  c.tick(in);

  auto in2 = baseInputs(t0 + 120000 + 1000);
  in2.tempC = 28;
  Controller::Decision d2 = c.tick(in2);
  TEST_ASSERT_FALSE(d2.sendOn); // ไม่มีคนจาก PIR จริงใน 15 นาทีล่าสุด
}

void test_dht_invalid_disables_hot_rule() {
  Settings s = makeSettings();
  Controller c(s);
  c.begin(0);

  uint32_t t0 = 70000;
  auto in = baseInputs(t0);
  in.pirMotion = true;
  in.tempC = 30;
  in.tempValid = false; // DHT22 เสีย
  c.tick(in);

  auto in2 = baseInputs(t0 + 120000 + 1000);
  in2.tempC = 30;
  in2.tempValid = false;
  Controller::Decision d2 = c.tick(in2);
  TEST_ASSERT_FALSE(d2.sendOn);
}

// ---- โหมดสั่งเองอย่างเดียว (manual-only) ----

void test_manual_only_mode_disables_auto_rules() {
  Settings s = makeSettings();
  s.emptyMin = 15;
  s.graceSec = 60;
  Controller c(s);
  c.begin(0);

  Command manual{Command::Type::Manual, Source::Web};
  Controller::Decision d;
  c.applyCommand(manual, 0, d);

  Command on{Command::Type::On, Source::Web};
  c.applyCommand(on, 1000, d);

  uint32_t emptyMs = 15u * 60000u;
  auto in = baseInputs(emptyMs + 60000 + 2000);
  Controller::Decision tick = c.tick(in);
  TEST_ASSERT_FALSE(tick.sendOff); // manual-only ปิดกฎห้องว่างด้วย
  TEST_ASSERT_TRUE(c.state().on);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_manual_on_off_basic);
  RUN_TEST(test_anti_spam_blocks_rapid_commands);
  RUN_TEST(test_set_temp_clamped_by_channel_min);
  RUN_TEST(test_empty_room_warns_then_turns_off);
  RUN_TEST(test_motion_resets_empty_timer);
  RUN_TEST(test_hot_rule_turns_on_with_recent_pir);
  RUN_TEST(test_hot_rule_requires_recent_pir);
  RUN_TEST(test_dht_invalid_disables_hot_rule);
  RUN_TEST(test_manual_only_mode_disables_auto_rules);
  return UNITY_END();
}
