// หน้าจอเครื่องมือช่าง — สำหรับทีมซ่อมบำรุงเท่านั้น ไม่ใช่หน้าควบคุมสำหรับผู้ใช้ทั่วไป
// แสดงสถานะเซนเซอร์/ระบบ + ปุ่มทดสอบ IR ดิบ (ไม่ผ่าน Controller) + หน้าตั้งค่าแบบใส่ PIN (1234)
#include "ui_task.h"

#ifdef AUTOAC_NO_DISPLAY

void uiTaskStart() {} // env autoac_bench: ไม่ต่อจอ — ข้าม task นี้ทั้งหมด

#else

#include "app.h"
#include "display.h"
#include "buzzer.h"
#include "amg8833.h"
#include "radar.h"
#include "rtc.h"
#include "log_sd.h"
#include "splash_cpe.h"
#include "splash_air.h"

static LGFX display;

enum class UiScreen { Idle, Status, Pin, Settings, Thermal, Radar, Sensors };
static UiScreen g_screen = UiScreen::Idle; // บูตมาเจอสไลด์โชว์โลโก้ก่อนเสมอ — ต้องแตะจอถึงจะขึ้นหน้าใส่ PIN
static uint32_t g_lastTouchMs = 0; // ไม่มีใครแตะจอเกิน 10 วิ (ทุกหน้ายกเว้น Idle) -> ล็อกกลับไปหน้า Idle อัตโนมัติ
constexpr uint32_t kAutoLockMs = 10000;

struct Btn { int16_t x, y, w, h; const char* label; uint32_t color; };
// จอ 240x320 (แนวตั้ง) — ปุ่ม 2 ใบเคียงกัน ต้องรวมความกว้าง ≤ 240
// ข้อความบนจอเป็นภาษาอังกฤษทั้งหมด — ฟอนต์เริ่มต้นของ LovyanGFX ไม่มีตัวอักษรไทย (ไม่ฝังฟอนต์ไทยเพราะหน้านี้ใช้ภายในทีมช่างเท่านั้น)
static Btn BTN_ON{10, 260, 105, 50, "IR ON", TFT_DARKGREEN};
static Btn BTN_OFF{125, 260, 105, 50, "IR OFF", TFT_MAROON};
static Btn BTN_OPEN_SETTINGS{10, 170, 220, 24, "SETTINGS", TFT_NAVY};
static Btn BTN_OPEN_THERMAL{10, 200, 105, 30, "THERMAL", TFT_PURPLE};
static Btn BTN_OPEN_RADAR{125, 200, 105, 30, "RADAR", TFT_ORANGE};
static Btn BTN_OPEN_SENSORS{10, 234, 220, 22, "SENSOR CHECK", TFT_DARKCYAN};
static void drawThermalScreen(); // นิยามอยู่ท้ายไฟล์ — handlePinTouch เรียกใช้ก่อนที่จะประกาศจริง
static void drawRadarScreen();
static void drawStatusScreen();
static void drawSensorsScreen();

static void drawButton(const Btn& b) {
  display.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  display.setTextColor(TFT_WHITE);
  display.setTextDatum(middle_center);
  display.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
}

static bool inside(const Btn& b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void drawRow(int y, const char* label, const String& value, uint32_t color = TFT_WHITE) {
  display.fillRect(0, y, 240, 18, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  display.drawString(label, 4, y);
  display.setTextColor(color);
  display.drawString(value, 100, y);
}

// ---------------- หน้าใส่ PIN (ขึ้นตอนบูต + ทุกครั้งที่ไม่มีใครแตะจอเกิน 10 วิ — PIN = 1234) ----------------
// ใส่ถูกแล้วเข้าหน้า Status เต็มสิทธิ์เลย ไม่ต้องใส่ PIN ซ้ำตอนกดเข้า SETTINGS/THERMAL/RADAR อีก

static const char* kPin = "1234";
static char g_pinEntered[5] = {0};
static uint8_t g_pinLen = 0;

static Btn BTN_PIN_BACK{5, 5, 60, 30, "BACK", TFT_MAROON};
static Btn BTN_PIN_KEYS[12] = {
    {10, 60, 68, 50, "1", TFT_NAVY}, {86, 60, 68, 50, "2", TFT_NAVY}, {162, 60, 68, 50, "3", TFT_NAVY},
    {10, 120, 68, 50, "4", TFT_NAVY}, {86, 120, 68, 50, "5", TFT_NAVY}, {162, 120, 68, 50, "6", TFT_NAVY},
    {10, 180, 68, 50, "7", TFT_NAVY}, {86, 180, 68, 50, "8", TFT_NAVY}, {162, 180, 68, 50, "9", TFT_NAVY},
    {10, 240, 68, 50, "C", TFT_MAROON}, {86, 240, 68, 50, "0", TFT_NAVY}, {162, 240, 68, 50, "OK", TFT_DARKGREEN},
};

static void drawPinDots() {
  display.fillRect(0, 30, 240, 20, TFT_BLACK);
  for (int i = 0; i < 4; i++) {
    int cx = 100 + i * 20;
    if (i < g_pinLen) display.fillCircle(cx, 40, 6, TFT_WHITE);
    else display.drawCircle(cx, 40, 6, TFT_DARKGREY);
  }
}

static void enterPinScreen() {
  g_screen = UiScreen::Pin;
  g_pinLen = 0;
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("Enter PIN", 80, 5);
  drawButton(BTN_PIN_BACK);
  drawPinDots();
  for (auto& b : BTN_PIN_KEYS) drawButton(b);
}

// ---------------- หน้า Idle (สไลด์โชว์โลโก้สลับทุก 2 วิ — ขึ้นตอนบูตและตอนไม่มีใครแตะจอเกิน 10 วิ) ----------------

static void drawIdleScreen() {
  display.fillScreen(TFT_WHITE);
  bool showCpe = (millis() / 2000) % 2 == 0;
  if (showCpe) {
    display.pushImage((240 - gSplashCpe_W) / 2, (320 - gSplashCpe_H) / 2, gSplashCpe_W, gSplashCpe_H, gSplashCpe);
  } else {
    display.pushImage((240 - gSplashAir_W) / 2, (320 - gSplashAir_H) / 2, gSplashAir_W, gSplashAir_H, gSplashAir);
  }
}

// ---------------- หน้าตั้งค่าอัตโนมัติ (หลังใส่ PIN ถูก) ----------------
// ปรับค่าเดียวกับที่เว็บหน้า "ตั้งค่า" ปรับได้ (SPEC.md ข้อ 4) — ยิงเป็น Command::SetSetting ผ่าน cmdQueue
// เหมือนที่เว็บ/ThingsBoard ทำ ไม่เขียน app::settings ตรงๆ (กติกาสถาปัตยกรรม: Control task เท่านั้นที่เขียน)

struct SettingRow { const char* key; const char* label; int value; int lo; int hi; int step; const char* unit; };
static SettingRow g_rows[5] = {
    {"emptyMin", "Empty room (min)", 15, 5, 60, 5, ""},
    {"graceSec", "Grace period (sec)", 60, 30, 300, 30, ""},
    {"hot", "Hot threshold (C)", 27, 24, 32, 1, ""},
    {"autoOn", "Auto-on when hot", 1, 0, 1, 1, ""},
    {"preOpenMin", "Pre-open (min)", 10, 0, 30, 5, ""},
};

static Btn BTN_SET_BACK{5, 5, 60, 30, "BACK", TFT_MAROON};
static Btn BTN_SET_SAVE{175, 5, 60, 30, "SAVE", TFT_DARKGREEN};

static int settingRowY(int i) { return 28 + i * 50; }

static void drawSettingRow(int i) {
  int y = settingRowY(i);
  SettingRow& r = g_rows[i];
  display.fillRect(0, y, 240, 46, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  display.drawString(r.label, 4, y);
  display.setTextColor(TFT_WHITE);
  display.setTextDatum(top_left);
  if (!strcmp(r.key, "autoOn")) {
    display.drawString(r.value ? "ON" : "OFF", 4, y + 18);
    Btn toggle{140, y + 12, 95, 28, r.value ? "ON" : "OFF", r.value ? TFT_DARKGREEN : TFT_MAROON};
    drawButton(toggle);
  } else {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", r.value);
    display.drawString(buf, 4, y + 18);
    Btn minus{140, y + 12, 28, 28, "-", TFT_NAVY};
    Btn plus{207, y + 12, 28, 28, "+", TFT_NAVY};
    drawButton(minus);
    drawButton(plus);
  }
}

static void enterSettingsScreen() {
  g_screen = UiScreen::Settings;
  g_rows[0].value = (int)app::settings.emptyMin;
  g_rows[1].value = (int)app::settings.graceSec;
  g_rows[2].value = (int)app::settings.hot;
  g_rows[3].value = app::settings.autoOn ? 1 : 0;
  g_rows[4].value = (int)app::settings.preOpenMin;

  display.fillScreen(TFT_BLACK);
  drawButton(BTN_SET_BACK);
  drawButton(BTN_SET_SAVE);
  for (int i = 0; i < 5; i++) drawSettingRow(i);
}

static void saveSettingsAndReturn() {
  for (auto& r : g_rows) {
    autoac::Command cmd{};
    cmd.type = autoac::Command::Type::SetSetting;
    cmd.source = autoac::Source::Hw;
    cmd.admin = true;
    strncpy(cmd.strArg, r.key, sizeof(cmd.strArg) - 1);
    cmd.intArg = r.value;
    app::pushCommand(cmd);
  }
  g_screen = UiScreen::Status;
}

static void handleSettingsTouch(int16_t tx, int16_t ty) {
  if (inside(BTN_SET_BACK, tx, ty)) { g_screen = UiScreen::Status; return; }
  if (inside(BTN_SET_SAVE, tx, ty)) { saveSettingsAndReturn(); return; }
  for (int i = 0; i < 5; i++) {
    int y = settingRowY(i);
    SettingRow& r = g_rows[i];
    if (!strcmp(r.key, "autoOn")) {
      Btn toggle{140, y + 12, 95, 28, "", 0};
      if (inside(toggle, tx, ty)) { r.value = r.value ? 0 : 1; drawSettingRow(i); }
    } else {
      Btn minus{140, y + 12, 28, 28, "", 0};
      Btn plus{207, y + 12, 28, 28, "", 0};
      if (inside(minus, tx, ty)) { r.value = max(r.lo, r.value - r.step); drawSettingRow(i); }
      else if (inside(plus, tx, ty)) { r.value = min(r.hi, r.value + r.step); drawSettingRow(i); }
    }
  }
}

static void handlePinTouch(int16_t tx, int16_t ty) {
  if (inside(BTN_PIN_BACK, tx, ty)) { g_screen = UiScreen::Idle; return; }
  for (auto& b : BTN_PIN_KEYS) {
    if (!inside(b, tx, ty)) continue;
    if (!strcmp(b.label, "C")) {
      g_pinLen = 0;
    } else if (!strcmp(b.label, "OK")) {
      g_pinEntered[g_pinLen] = '\0';
      if (g_pinLen == 4 && !strcmp(g_pinEntered, kPin)) {
        drawStatusScreen();
        g_screen = UiScreen::Status;
        return;
      } else {
        g_pinLen = 0; // PIN ผิด — เคลียร์แล้วให้ใส่ใหม่ (ไม่ต้องแยก error state ให้ซับซ้อน)
        buzzerBeep(400, 200); // เสียงทุ้มยาวกว่าปกติ แยกจากเสียงกดปุ่มทั่วไป (ยังไม่มี — แค่ตอนนี้)
      }
    } else if (g_pinLen < 4) {
      g_pinEntered[g_pinLen++] = b.label[0];
    }
    drawPinDots();
    return;
  }
}

static void drawStatusScreen() {
  display.fillScreen(TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("AIR - Technician Tool", 4, 4);
  display.drawFastHLine(0, 22, 240, TFT_DARKGREY);
  drawButton(BTN_ON);
  drawButton(BTN_OFF);
  drawButton(BTN_OPEN_SETTINGS);
  drawButton(BTN_OPEN_THERMAL);
  drawButton(BTN_OPEN_RADAR);
  drawButton(BTN_OPEN_SENSORS);
}

// ---------------- หน้า AMG8833 heatmap (กล้องความร้อน 8x8) ----------------

static Btn BTN_THERMAL_BACK{5, 5, 60, 30, "BACK", TFT_MAROON};
constexpr int kThermalGridPx = 160; // พื้นที่วาด grid: 160x160 (20px ต่อช่อง, 8x8)
constexpr int kThermalCellPx = kThermalGridPx / AMG8833_GRID;
constexpr int kThermalX = 8, kThermalY = 50;

// ไล่สี "jet-like": น้ำเงิน -> ฟ้า -> เขียว -> เหลือง -> แดง -> ขาว ตามสัดส่วน t (0..1)
static uint32_t thermalColor(float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint8_t r, g, b;
  if (t < 0.25f) { // น้ำเงินเข้ม -> ฟ้า
    float u = t / 0.25f;
    r = 0; g = (uint8_t)(u * 180); b = 255;
  } else if (t < 0.5f) { // ฟ้า -> เขียว
    float u = (t - 0.25f) / 0.25f;
    r = 0; g = 180 + (uint8_t)(u * 75); b = (uint8_t)(255 * (1 - u));
  } else if (t < 0.75f) { // เขียว -> เหลือง/แดง
    float u = (t - 0.5f) / 0.25f;
    r = (uint8_t)(u * 255); g = 255; b = 0;
  } else { // แดง -> ขาว (จุดร้อนสุด)
    float u = (t - 0.75f) / 0.25f;
    r = 255; g = (uint8_t)(255 - u * 55); b = (uint8_t)(u * 200);
  }
  return display.color888(r, g, b);
}

static void drawThermalScreen() {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("Thermal (AMG8833)", 70, 20);
  drawButton(BTN_THERMAL_BACK);

  app::ThermalSnapshot snap = app::lastThermal();
  if (!snap.present) {
    display.setTextColor(TFT_RED);
    display.drawString("Sensor not found", kThermalX, kThermalY + 60);
    return;
  }
  const float* pixels = snap.pixels;
  float maxC = snap.maxC, minC = snap.minC;

  float range = maxC - minC;
  if (range < 0.5f) range = 0.5f; // กันหารด้วยเลขใกล้ 0 ตอนห้องอุณหภูมิเรียบมาก
  for (int row = 0; row < AMG8833_GRID; row++) {
    for (int col = 0; col < AMG8833_GRID; col++) {
      float v = pixels[row * AMG8833_GRID + col];
      float t = (v - minC) / range;
      display.fillRect(kThermalX + col * kThermalCellPx, kThermalY + row * kThermalCellPx,
                        kThermalCellPx, kThermalCellPx, thermalColor(t));
    }
  }
  display.drawRect(kThermalX, kThermalY, kThermalGridPx, kThermalGridPx, TFT_DARKGREY);

  // MAX/MIN ด้านขวาของ grid เหมือนภาพอ้างอิง
  int labelX = kThermalX + kThermalGridPx + 12;
  char buf[16];
  display.setTextColor(TFT_WHITE);
  display.drawString("MAX:", labelX, kThermalY + 40);
  snprintf(buf, sizeof(buf), "%.1f C", maxC);
  display.drawString(buf, labelX, kThermalY + 58);
  display.drawString("MIN:", labelX, kThermalY + 90);
  snprintf(buf, sizeof(buf), "%.1f C", minC);
  display.drawString(buf, labelX, kThermalY + 108);
}

static void handleThermalTouch(int16_t tx, int16_t ty) {
  if (inside(BTN_THERMAL_BACK, tx, ty)) { g_screen = UiScreen::Status; }
}

// ---------------- หน้า Rd-01 radar (ไม่มีพิกเซลภาพแบบ AMG8833 — มีแค่ state/ระยะ/พลังงานต่อเฟรม) ----------------

static Btn BTN_RADAR_BACK{5, 5, 60, 30, "BACK", TFT_MAROON};

static void drawEnergyBar(int y, const char* label, uint8_t energy, uint16_t distCm) {
  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  display.drawString(label, 8, y);
  char buf[24];
  snprintf(buf, sizeof(buf), "%u cm", distCm);
  display.setTextColor(TFT_WHITE);
  display.drawString(buf, 150, y);

  int barX = 8, barY = y + 18, barW = 224, barH = 14;
  display.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
  int fillW = (int)((barW - 2) * (energy / 255.0f));
  uint32_t color = energy > 150 ? TFT_RED : (energy > 60 ? TFT_YELLOW : TFT_GREEN);
  display.fillRect(barX + 1, barY + 1, fillW, barH - 2, color);
  display.fillRect(barX + 1 + fillW, barY + 1, (barW - 2) - fillW, barH - 2, TFT_BLACK);
}

static void drawRadarScreen() {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("Radar (Rd-01)", 80, 20);
  drawButton(BTN_RADAR_BACK);

  RadarFrame f = app::lastRadar();
  if (!f.everReceived) {
    display.setTextColor(TFT_RED);
    display.drawString("No data from sensor yet", 20, 150);
    return;
  }

  const char* stateLabel;
  uint32_t stateColor;
  switch (f.state) {
    case 0: stateLabel = "NO TARGET"; stateColor = TFT_DARKGREY; break;
    case 1: stateLabel = "MOTION"; stateColor = TFT_YELLOW; break;
    case 2: stateLabel = "STATIONARY"; stateColor = TFT_GREEN; break;
    default: stateLabel = "MOTION + STATIONARY"; stateColor = TFT_ORANGE; break;
  }
  display.setTextSize(2);
  display.setTextDatum(top_center);
  display.setTextColor(stateColor);
  display.drawString(stateLabel, 120, 55);
  display.setTextSize(1);

  drawEnergyBar(100, "Moving energy:", f.moveEnergy, f.moveDistCm);
  drawEnergyBar(150, "Static energy:", f.staticEnergy, f.staticDistCm);

  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  char buf[32];
  snprintf(buf, sizeof(buf), "Detect range: %u cm", f.detectRangeCm);
  display.drawString(buf, 8, 200);
}

static void handleRadarTouch(int16_t tx, int16_t ty) {
  if (inside(BTN_RADAR_BACK, tx, ty)) { g_screen = UiScreen::Status; }
}

// ---------------- หน้าเช็คเซนเซอร์ทั้งหมด (SENSOR CHECK) ----------------

static Btn BTN_SENSORS_BACK{5, 5, 60, 30, "BACK", TFT_MAROON};

static void drawSensorRow(int y, const char* label, bool known, bool ok, const char* okText, const char* failText) {
  display.fillRect(0, y, 240, 24, TFT_BLACK);
  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  display.drawString(label, 6, y + 4);
  display.setTextColor(known ? (ok ? TFT_GREEN : TFT_RED) : TFT_YELLOW);
  display.setTextDatum(top_right);
  display.drawString(known ? (ok ? okText : failText) : "N/A", 234, y + 4);
}

static void drawSensorsScreen() {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("Sensor Check", 75, 20);
  drawButton(BTN_SENSORS_BACK);

  int y = 50;
  drawSensorRow(y, "WiFi", true, app::wifiConnected(), "OK", "FAIL");          y += 26;
  drawSensorRow(y, "ThingsBoard", true, app::tbConnected(), "OK", "FAIL");     y += 26;
  drawSensorRow(y, "DHT22 (temp/hum)", true, app::dhtOk(), "OK", "FAIL");      y += 26;
  drawSensorRow(y, "PIR motion", true, true, app::lastOccupied() ? "HIGH" : "LOW", "FAIL"); y += 26;
  drawSensorRow(y, "AMG8833 (thermal)", true, amg8833Present(), "OK", "NOT FOUND"); y += 26;
  drawSensorRow(y, "DS3231 (RTC)", true, rtcPresent(), "OK", "NOT FOUND");     y += 26;
  drawSensorRow(y, "Rd-01 (radar)", true, app::lastRadar().everReceived, "OK", "NO DATA"); y += 26;
  drawSensorRow(y, "microSD", true, sdPresent(), "OK", "NOT FOUND");           y += 26;

  display.setTextDatum(top_left);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("IR TX/RX, buzzer, touch: ไม่มี self-test อัตโนมัติ", 6, y + 8);
}

static void handleSensorsTouch(int16_t tx, int16_t ty) {
  if (inside(BTN_SENSORS_BACK, tx, ty)) { g_screen = UiScreen::Status; }
}

static void uiTaskLoop(void*) {
  pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_LCD_BACKLIGHT, HIGH); // backlight เป็น GPIO แยก (IO21) ไม่ใช่ต่อไฟตรง — ต้องสั่งเปิดเอง

  if (!app::spiLock(2000)) { vTaskDelete(nullptr); return; }
  display.init();
  display.setRotation(0); // แนวตั้ง 240x320 — จอแนวนอนตาม FRONTEND.md ยังไม่ทำรอบนี้ (เครื่องมือช่างใช้แนวตั้งพอ)
  display.setTextSize(1);
  drawIdleScreen();
  app::spiUnlock();

  bool wasPressed = false;
  uint32_t lastRedrawMs = 0;
  g_lastTouchMs = millis();

  for (;;) {
    if (app::spiLock(200)) {
      // ---- อ่านทัช (ทุก loop ~50ms เพื่อให้กดแล้วตอบสนองไว) ----
      int32_t tx, ty;
      bool pressed = display.getTouch(&tx, &ty);
      tx = 239 - tx; // แผงทัชติดตั้งกลับด้านกับจอ (แกน X) — ยืนยันจากฮาร์ดแวร์จริง: กด ON ไปโดนปุ่ม OFF
      static uint32_t lastTouchLog = 0;
      if (millis() - lastTouchLog > 500) { // debug: log ดิบทุก 0.5 วิ ไม่ว่าจะกดหรือไม่
        lastTouchLog = millis();
        Serial.printf("[touch] pressed=%d x=%ld y=%ld\n", pressed, (long)tx, (long)ty);
      }

      UiScreen screenBefore = g_screen;
      if (pressed && !wasPressed) {
        switch (g_screen) {
          case UiScreen::Idle: enterPinScreen(); break; // แตะตรงไหนก็ได้บนหน้าสไลด์โชว์ -> ขอ PIN
          case UiScreen::Status: {
            autoac::Command cmd{};
            cmd.type = autoac::Command::Type::HwIrTest;
            cmd.source = autoac::Source::Hw;
            if (inside(BTN_ON, tx, ty)) { cmd.intArg = 1; app::pushCommand(cmd); }
            else if (inside(BTN_OFF, tx, ty)) { cmd.intArg = 0; app::pushCommand(cmd); }
            else if (inside(BTN_OPEN_SETTINGS, tx, ty)) { enterSettingsScreen(); }
            else if (inside(BTN_OPEN_THERMAL, tx, ty)) { g_screen = UiScreen::Thermal; drawThermalScreen(); }
            else if (inside(BTN_OPEN_RADAR, tx, ty)) { g_screen = UiScreen::Radar; drawRadarScreen(); }
            else if (inside(BTN_OPEN_SENSORS, tx, ty)) { g_screen = UiScreen::Sensors; drawSensorsScreen(); }
            break;
          }
          case UiScreen::Pin: handlePinTouch(tx, ty); break;
          case UiScreen::Settings: handleSettingsTouch(tx, ty); break;
          case UiScreen::Thermal: handleThermalTouch(tx, ty); break;
          case UiScreen::Radar: handleRadarTouch(tx, ty); break;
          case UiScreen::Sensors: handleSensorsTouch(tx, ty); break;
        }
        // เปลี่ยนหน้าแบบ "BACK" กลับ Status ต้องวาดใหม่ทั้งจอ (handleSettingsTouch อาจตั้ง g_screen=Status)
        if (screenBefore != UiScreen::Status && g_screen == UiScreen::Status && screenBefore != UiScreen::Pin) drawStatusScreen();
        g_lastTouchMs = millis(); // รีเซ็ตนับถอยหลัง auto-lock ทุกครั้งที่แตะจอ (บั๊กเดิม: ข้ามรีเซ็ตตอนออกจากหน้า Idle ทำให้ auto-lock ตัดกลับทันที)
      }
      wasPressed = pressed;

      // ---- ไม่มีใครแตะจอเกิน 10 วิ (ยกเว้นตอนอยู่หน้า Idle เองอยู่แล้ว) -> ล็อกกลับไปหน้า Idle ----
      uint32_t nowLock = millis();
      if (g_screen != UiScreen::Idle && nowLock - g_lastTouchMs >= kAutoLockMs) {
        g_screen = UiScreen::Idle;
        drawIdleScreen();
      }

      // ---- อัปเดตสถานะ/heatmap/สไลด์โชว์ (วาดใหม่แค่ทุกช่วงเวลาที่กำหนด กันจอกระพริบ/SPI ชนกัน) ----
      uint32_t now = millis();
      if (g_screen == UiScreen::Idle && now - lastRedrawMs >= 2000) {
        lastRedrawMs = now;
        drawIdleScreen();
      }
      if (g_screen == UiScreen::Thermal && now - lastRedrawMs >= 500) {
        lastRedrawMs = now;
        drawThermalScreen();
      }
      if (g_screen == UiScreen::Radar && now - lastRedrawMs >= 300) {
        lastRedrawMs = now;
        drawRadarScreen();
      }
      if (g_screen == UiScreen::Sensors && now - lastRedrawMs >= 500) {
        lastRedrawMs = now;
        drawSensorsScreen();
      }
      if (g_screen == UiScreen::Status && now - lastRedrawMs >= 500) {
        lastRedrawMs = now;
        bool wifiOk = app::wifiConnected();
        bool tbOk = app::tbConnected();
        drawRow(30, "WiFi:", wifiOk ? "Connected" : "Disconnected", wifiOk ? TFT_GREEN : TFT_RED);
        drawRow(50, "ThingsBoard:", tbOk ? "Connected" : "Disconnected", tbOk ? TFT_GREEN : TFT_RED);

        float t = app::lastTempC();
        float h = app::lastHumidity();
        char tbuf[16], hbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%.1f C", t);
        snprintf(hbuf, sizeof(hbuf), "%.0f %%", h);
        drawRow(78, "Temp:", tbuf);
        drawRow(98, "Humidity:", hbuf);
        drawRow(118, "PIR motion:", app::lastOccupied() ? "Detected" : "None");

        autoac::AcState ac = app::lastAcState();
        drawRow(146, "AC command:", ac.on ? "ON" : "OFF", ac.on ? TFT_GREEN : TFT_DARKGREY);
      }

      app::spiUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void uiTaskStart() {
  xTaskCreatePinnedToCore(uiTaskLoop, "ui", 8192, nullptr, 1, nullptr, 1);
}

#endif // AUTOAC_NO_DISPLAY
