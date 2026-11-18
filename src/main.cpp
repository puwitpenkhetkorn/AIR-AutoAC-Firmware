#include <Arduino.h>
#include "app.h"
#include "control_task.h"
#include "net_task.h"
#include "ui_task.h"
#include "web_task.h"

// ตั้ง CS ของ SPI ร่วม (จอ/ทัช/SD) เป็น OUTPUT HIGH เป็นอย่างแรกใน setup() (กติกาข้อ 8)
// รอบนี้ยังไม่ต่อจอ (ใช้ env autoac_bench, AUTOAC_NO_DISPLAY) — คง guard ไว้เผื่อรอบถัดไปต่อจอจริง
#include "pins.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] AIR Auto AC Controller");

#ifndef AUTOAC_NO_DISPLAY
  pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
  pinMode(PIN_TOUCH_CS, OUTPUT); digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
#endif

  app::initShared();
  controlTaskStart();
  netTaskStart();
  uiTaskStart(); // env autoac_bench: ไม่ต่อจอ — uiTaskStart() เป็น no-op อัตโนมัติ
  webTaskStart(); // เว็บดู log บน microSD (http://autoac.local/) — task แยก ไม่บล็อก net_task
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
