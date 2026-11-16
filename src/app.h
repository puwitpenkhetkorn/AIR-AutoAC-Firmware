#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../lib/autoac_core/types.h"

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

inline bool spiLock(uint32_t timeoutMs = 1000) {
  return xSemaphoreTake(spiMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
inline void spiUnlock() { xSemaphoreGive(spiMutex); }

inline bool pushCommand(const autoac::Command& cmd) {
  return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE;
}

} // namespace app
