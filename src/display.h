#pragma once
// กำหนดค่าจอ ILI9341 + ทัช XPT2046 ด้วย LovyanGFX ตาม pins.h
// SPI ร่วมกับ SD — ต้องถือ app::spiLock() ก่อนวาดทุกครั้ง (กติกาข้อ 3)
#ifndef AUTOAC_NO_DISPLAY

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_XPT2046 _touch_instance;

 public:
  LGFX() {
    { // bus
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 27000000; // CLAUDE.md: จอ 40MHz เพี้ยน → ใช้ 27
      cfg.freq_read = 16000000;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      cfg.pin_dc = PIN_LCD_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.invert = false;
      _panel_instance.config(cfg);
    }
    { // touch (ทัช ≤ 2.5MHz — CLAUDE.md)
      auto cfg = _touch_instance.config();
      // x_min/x_max/y_min/y_max คือช่วงค่าดิบจาก ADC ของชิป XPT2046 (ปกติ ~300-3900)
      // ไม่ใช่พิกเซลจอ — ของเดิมใส่เป็นพิกเซล (0-239,0-319) ทับ default ที่ถูกต้องไปโดยไม่ตั้งใจ
      // ปล่อยให้ Touch_XPT2046 ใช้ default ของตัวเอง (x:300-3900, y:400-3900) ไปก่อนจนกว่าจะมีค่า calibrate จริง
      cfg.pin_cs = PIN_TOUCH_CS;
      // ไม่ตั้ง pin_int — ถ้าขา IRQ ลอย/ไม่ได้ต่อจริง จะทำให้ไลบรารีคืนค่า "ไม่กด" ทันทีเสมอ
      // โดยไม่สนใจสัญญาณ X/Y เลย (ยังไม่ยืนยันว่าขานี้ต่อจริงบนบอร์ด — ปล่อยใช้วิธี poll ผ่าน SPI อย่างเดียวไปก่อน)
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 2000000;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};

#endif // AUTOAC_NO_DISPLAY
