#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../lib/autoac_core/types.h"
#include "ir_learn.h"

// สถานะ/ทรัพยากรที่ใช้ร่วมกันหลาย task (กติกาเฟิร์มแวร์ CLAUDE.md ข้อ 1-3)
namespace app {

extern QueueHandle_t cmdQueue;       // ทุกคำสั่งจากทุกช่องทางเข้าคิวนี้ก่อนเสมอ
extern SemaphoreHandle_t spiMutex;   // จอ+ทัช+SD ใช้ SPI ร่วม

extern autoac::Settings settings;
extern autoac::Schedule schedule;
extern autoac::Holidays holidays;

void initShared();

// สแนปช็อตล่าสุดของเซนเซอร์/สถานะแอร์ — Control task เป็นคนเขียน, net_task/ui_task อ่านอย่างเดียว
void setLastSensors(float tempC, float humidity, bool occupied);
void setLastAcState(const autoac::AcState& ac);
float lastTempC();
float lastHumidity();
bool lastOccupied();
autoac::AcState lastAcState();

// สถานะระบบ สำหรับหน้าจอเครื่องมือช่าง (ui_task) — net_task เป็นคนเขียน
void setNetStatus(bool wifiOk, bool tbOk);
bool wifiConnected();
bool tbConnected();

// รหัส IR ที่เรียนรู้มา — Control task เป็นคนเขียน, net_task อ่านอย่างเดียว
const LearnedIrCode& irCode(autoac::IrButton b);
void setIrCode(autoac::IrButton b, const LearnedIrCode& code);

// สถานะการเรียนรู้ IR ล่าสุด (เช่น "ok:power_on", "timeout:temp_up") — Control task เขียน, net_task อ่าน
const char* lastIrLearnStatus();
void setLastIrLearnStatus(const char* status);

// แจ้ง net_task ว่า attribute (irMode/irLearned/irLearnStatus ฯลฯ) เปลี่ยน ต้อง publish ใหม่
// ไม่ต้องรอรอบ reconnect MQTT ครั้งถัดไป — net_task เช็ค+เคลียร์ทุกลูป
void markAttributesDirty();
bool consumeAttributesDirty();

// แจ้ง net_task ว่าสถานะแอร์ (ac_on/ac_set_temp/...) เปลี่ยน ต้องส่ง telemetry ทันที
// ไม่ต้องรอรอบ 30 วิปกติ (setLastAcState() เรียกให้อัตโนมัติ)
void markTelemetryDirty();
bool consumeTelemetryDirty();

inline bool spiLock(uint32_t timeoutMs = 1000) {
  return xSemaphoreTake(spiMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
inline void spiUnlock() { xSemaphoreGive(spiMutex); }

inline bool pushCommand(const autoac::Command& cmd) {
  return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE;
}

} // namespace app
