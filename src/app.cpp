#include "app.h"

namespace app {

QueueHandle_t cmdQueue = nullptr;
SemaphoreHandle_t spiMutex = nullptr;

autoac::Settings settings;
autoac::Schedule schedule;
autoac::Holidays holidays;

static float g_tempC = 0;
static float g_humidity = 0;
static bool g_occupied = false;
static autoac::AcState g_ac;
static SemaphoreHandle_t g_snapshotMutex = nullptr;

void setLastSensors(float tempC, float humidity, bool occupied) {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  g_tempC = tempC;
  g_humidity = humidity;
  g_occupied = occupied;
  xSemaphoreGive(g_snapshotMutex);
}

void setLastAcState(const autoac::AcState& ac) {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  g_ac = ac;
  xSemaphoreGive(g_snapshotMutex);
}

float lastTempC() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  float v = g_tempC;
  xSemaphoreGive(g_snapshotMutex);
  return v;
}

float lastHumidity() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  float v = g_humidity;
  xSemaphoreGive(g_snapshotMutex);
  return v;
}

bool lastOccupied() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  bool v = g_occupied;
  xSemaphoreGive(g_snapshotMutex);
  return v;
}

autoac::AcState lastAcState() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  autoac::AcState v = g_ac;
  xSemaphoreGive(g_snapshotMutex);
  return v;
}

static bool g_wifiOk = false;
static bool g_tbOk = false;

void setNetStatus(bool wifiOk, bool tbOk) {
  g_wifiOk = wifiOk;
  g_tbOk = tbOk;
}
bool wifiConnected() { return g_wifiOk; }
bool tbConnected() { return g_tbOk; }

void initShared() {
  g_snapshotMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(16, sizeof(autoac::Command));
  spiMutex = xSemaphoreCreateMutex();

  // ตารางเริ่มต้น: ปิดทุกช่วง จนกว่าจะตั้งจากเว็บ/RPC (setSchedule)
  for (int d = 0; d < 7; d++) {
    schedule[d][0] = {false, 480, 1020};
    schedule[d][1] = {false, 480, 1020};
  }
}

} // namespace app
