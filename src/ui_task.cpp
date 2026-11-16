// หน้าจอเครื่องมือช่าง — สำหรับทีมซ่อมบำรุงเท่านั้น ไม่ใช่หน้าควบคุมสำหรับผู้ใช้ทั่วไป
// แสดงสถานะเซนเซอร์/ระบบ + ปุ่มทดสอบ IR ดิบ (ไม่ผ่าน Controller)
#include "ui_task.h"

#ifdef AUTOAC_NO_DISPLAY

void uiTaskStart() {} // env autoac_bench: ไม่ต่อจอ — ข้าม task นี้ทั้งหมด

#else

#include "app.h"
#include "display.h"

static LGFX display;

struct Btn { int16_t x, y, w, h; const char* label; bool on; };
// จอ 240x320 (แนวตั้ง) — ปุ่ม 2 ใบเคียงกัน ต้องรวมความกว้าง ≤ 240
// ข้อความบนจอเป็นภาษาอังกฤษทั้งหมด — ฟอนต์เริ่มต้นของ LovyanGFX ไม่มีตัวอักษรไทย (ไม่ฝังฟอนต์ไทยเพราะหน้านี้ใช้ภายในทีมช่างเท่านั้น)
static Btn BTN_ON{10, 260, 105, 50, "IR ON", true};
static Btn BTN_OFF{125, 260, 105, 50, "IR OFF", false};

static void drawButton(const Btn& b) {
  display.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.on ? TFT_DARKGREEN : TFT_MAROON);
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

static void uiTaskLoop(void*) {
  pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_LCD_BACKLIGHT, HIGH); // backlight เป็น GPIO แยก (IO21) ไม่ใช่ต่อไฟตรง — ต้องสั่งเปิดเอง

  if (!app::spiLock(2000)) { vTaskDelete(nullptr); return; }
  display.init();
  display.setRotation(0); // แนวตั้ง 240x320 — จอแนวนอนตาม FRONTEND.md ยังไม่ทำรอบนี้ (เครื่องมือช่างใช้แนวตั้งพอ)
  display.fillScreen(TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_CYAN);
  display.setTextDatum(top_left);
  display.drawString("AIR - Technician Tool", 4, 4);
  display.drawFastHLine(0, 22, 240, TFT_DARKGREY);
  drawButton(BTN_ON);
  drawButton(BTN_OFF);
  app::spiUnlock();

  bool wasPressed = false;

  for (;;) {
    if (app::spiLock(200)) {
      // ---- อ่านทัช ----
      int32_t tx, ty;
      bool pressed = display.getTouch(&tx, &ty);
      static uint32_t lastTouchLog = 0;
      if (millis() - lastTouchLog > 500) { // debug: log ดิบทุก 0.5 วิ ไม่ว่าจะกดหรือไม่
        lastTouchLog = millis();
        Serial.printf("[touch] pressed=%d x=%ld y=%ld\n", pressed, (long)tx, (long)ty);
      }
      if (pressed && !wasPressed) {
        autoac::Command cmd{};
        cmd.type = autoac::Command::Type::HwIrTest;
        cmd.source = autoac::Source::Hw;
        if (inside(BTN_ON, tx, ty)) { cmd.intArg = 1; app::pushCommand(cmd); }
        else if (inside(BTN_OFF, tx, ty)) { cmd.intArg = 0; app::pushCommand(cmd); }
      }
      wasPressed = pressed;

      // ---- อัปเดตสถานะ ----
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

      app::spiUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

void uiTaskStart() {
  xTaskCreatePinnedToCore(uiTaskLoop, "ui", 8192, nullptr, 1, nullptr, 1);
}

#endif // AUTOAC_NO_DISPLAY
