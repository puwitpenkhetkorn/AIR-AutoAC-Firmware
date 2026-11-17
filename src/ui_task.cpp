// หน้าจอเครื่องมือช่าง — สำหรับทีมซ่อมบำรุงเท่านั้น ไม่ใช่หน้าควบคุมสำหรับผู้ใช้ทั่วไป
// แสดงสถานะเซนเซอร์/ระบบ + ปุ่มทดสอบ IR ดิบ (ไม่ผ่าน Controller) + หน้าตั้งค่าแบบใส่ PIN (1234)
#include "ui_task.h"

#ifdef AUTOAC_NO_DISPLAY

void uiTaskStart() {} // env autoac_bench: ไม่ต่อจอ — ข้าม task นี้ทั้งหมด

#else

#include "app.h"
#include "display.h"

static LGFX display;

enum class UiScreen { Status, Pin, Settings };
static UiScreen g_screen = UiScreen::Status;

struct Btn { int16_t x, y, w, h; const char* label; uint32_t color; };
// จอ 240x320 (แนวตั้ง) — ปุ่ม 2 ใบเคียงกัน ต้องรวมความกว้าง ≤ 240
// ข้อความบนจอเป็นภาษาอังกฤษทั้งหมด — ฟอนต์เริ่มต้นของ LovyanGFX ไม่มีตัวอักษรไทย (ไม่ฝังฟอนต์ไทยเพราะหน้านี้ใช้ภายในทีมช่างเท่านั้น)
static Btn BTN_ON{10, 260, 105, 50, "IR ON", TFT_DARKGREEN};
static Btn BTN_OFF{125, 260, 105, 50, "IR OFF", TFT_MAROON};
static Btn BTN_OPEN_SETTINGS{10, 200, 220, 40, "SETTINGS", TFT_NAVY};

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

// ---------------- หน้าใส่ PIN (ก่อนเข้าหน้าตั้งค่าอัตโนมัติ — PIN = 1234) ----------------

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
  if (inside(BTN_PIN_BACK, tx, ty)) { g_screen = UiScreen::Status; return; }
  for (auto& b : BTN_PIN_KEYS) {
    if (!inside(b, tx, ty)) continue;
    if (!strcmp(b.label, "C")) {
      g_pinLen = 0;
    } else if (!strcmp(b.label, "OK")) {
      g_pinEntered[g_pinLen] = '\0';
      if (g_pinLen == 4 && !strcmp(g_pinEntered, kPin)) {
        enterSettingsScreen();
        return;
      } else {
        g_pinLen = 0; // PIN ผิด — เคลียร์แล้วให้ใส่ใหม่ (ไม่ต้องแยก error state ให้ซับซ้อน)
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
}

static void uiTaskLoop(void*) {
  pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_LCD_BACKLIGHT, HIGH); // backlight เป็น GPIO แยก (IO21) ไม่ใช่ต่อไฟตรง — ต้องสั่งเปิดเอง

  if (!app::spiLock(2000)) { vTaskDelete(nullptr); return; }
  display.init();
  display.setRotation(0); // แนวตั้ง 240x320 — จอแนวนอนตาม FRONTEND.md ยังไม่ทำรอบนี้ (เครื่องมือช่างใช้แนวตั้งพอ)
  display.setTextSize(1);
  drawStatusScreen();
  app::spiUnlock();

  bool wasPressed = false;
  uint32_t lastRedrawMs = 0;

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
          case UiScreen::Status: {
            autoac::Command cmd{};
            cmd.type = autoac::Command::Type::HwIrTest;
            cmd.source = autoac::Source::Hw;
            if (inside(BTN_ON, tx, ty)) { cmd.intArg = 1; app::pushCommand(cmd); }
            else if (inside(BTN_OFF, tx, ty)) { cmd.intArg = 0; app::pushCommand(cmd); }
            else if (inside(BTN_OPEN_SETTINGS, tx, ty)) { enterPinScreen(); }
            break;
          }
          case UiScreen::Pin: handlePinTouch(tx, ty); break;
          case UiScreen::Settings: handleSettingsTouch(tx, ty); break;
        }
        // เปลี่ยนหน้าแบบ "BACK" กลับ Status ต้องวาดใหม่ทั้งจอ (handlePinTouch/Settings อาจตั้ง g_screen=Status)
        if (screenBefore != UiScreen::Status && g_screen == UiScreen::Status) drawStatusScreen();
      }
      wasPressed = pressed;

      // ---- อัปเดตสถานะ (เฉพาะหน้า Status, วาดใหม่แค่ทุก 500ms) ----
      uint32_t now = millis();
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
        display.fillRect(0, 170, 240, 18, TFT_BLACK);
        display.setTextColor(TFT_DARKGREY);
        display.drawString("Technician tool - not for end users", 4, 170);
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
