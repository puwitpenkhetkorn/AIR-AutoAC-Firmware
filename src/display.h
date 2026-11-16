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
      cfg.x_min = 0; cfg.x_max = 239;
      cfg.y_min = 0; cfg.y_max = 319;
      cfg.pin_cs = PIN_TOUCH_CS;
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
