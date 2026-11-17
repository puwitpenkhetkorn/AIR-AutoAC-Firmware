#pragma once

// ขาทั้งหมดของบอร์ด AIR อยู่ที่นี่ที่เดียว — อ้างอิงจาก md-pcb/SPEC.md (ตารางจริงจาก PCB)
// ⚠️ ทีม PCB เองทำเครื่องหมายไว้ว่า "ยังไม่ได้เทียบ pinout" ของจอ/ทัชกับ FPC จริง — ถ้าจอยังไม่ขึ้นหลังแก้ค่านี้
// ให้สงสัยว่าพินจริงบนบอร์ดอาจต่างจากตารางนี้อีก ต้องวัดจริงด้วยมัลติมิเตอร์/ดูลายวงจรเทียบ
// ห้ามใช้ IO19/20 (USB), IO0 (BOOT), IO35-37 (OPI PSRAM)

// จอ ILI9341 + ทัช XPT2046 (SPI ร่วม — ต้องถือ app::spiLock() เสมอ)
#define PIN_LCD_SCK        12
#define PIN_LCD_MOSI       11
#define PIN_LCD_MISO       13
#define PIN_LCD_CS         10
#define PIN_LCD_DC         9
#define PIN_LCD_RST        14
#define PIN_LCD_BACKLIGHT  21
#define PIN_TOUCH_CS       15
#define PIN_TOUCH_IRQ      16

// microSD (SPI ร่วมกับจอ)
#define PIN_SD_CS          17

// เซนเซอร์
#define PIN_DHT22          4
#define PIN_PIR            5
// I2C (AMG8833 ฯลฯ)
#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        18

// IR
#define PIN_IR_TX          6 // ขับผ่าน Si2302 ไปหลอด IR LED ×6
#define PIN_IR_RX          7 // VS1838B

// ไฟสถานะ + เสียง
#define PIN_WS2812B        48 // ใช้ RMT (core 2.x API)
#define PIN_BUZZER         38

// เรดาร์ Ai-Thinker Rd-01 (24GHz mmWave) — UART ต่อ ESP32 IO1/IO2
#define PIN_RADAR_RX       1 // ESP32 รับ ← Rd-01 ส่ง (TX ของโมดูล)
#define PIN_RADAR_TX       2 // ESP32 ส่ง → Rd-01 รับ (RX ของโมดูล) — ใช้เวลาจะสั่ง config command เท่านั้น
