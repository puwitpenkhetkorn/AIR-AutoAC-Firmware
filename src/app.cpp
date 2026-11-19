#include "app.h"
#include "nvs_store.h"

namespace app {

QueueHandle_t cmdQueue = nullptr;
SemaphoreHandle_t spiMutex = nullptr;

autoac::Settings settings;
autoac::SchedulePeriods schedulePeriods;
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
  markTelemetryDirty();
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

static ThermalSnapshot g_thermal{};
static RadarFrame g_radar{};

void setLastThermal(const float pixels[THERMAL_PIXEL_COUNT], float maxC, float minC, bool present) {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  memcpy(g_thermal.pixels, pixels, sizeof(g_thermal.pixels));
  g_thermal.maxC = maxC;
  g_thermal.minC = minC;
  g_thermal.present = present;
  xSemaphoreGive(g_snapshotMutex);
}

ThermalSnapshot lastThermal() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  ThermalSnapshot v = g_thermal;
  xSemaphoreGive(g_snapshotMutex);
  return v;
}

void setLastRadar(const RadarFrame& frame) {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  g_radar = frame;
  xSemaphoreGive(g_snapshotMutex);
}

RadarFrame lastRadar() {
  xSemaphoreTake(g_snapshotMutex, portMAX_DELAY);
  RadarFrame v = g_radar;
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

static bool g_dhtOk = false;
void setDhtOk(bool ok) { g_dhtOk = ok; }
bool dhtOk() { return g_dhtOk; }

static LearnedIrCode g_irCodes[autoac::IR_BUTTON_COUNT];

const LearnedIrCode& irCode(autoac::IrButton b) { return g_irCodes[(int)b]; }
void setIrCode(autoac::IrButton b, const LearnedIrCode& code) {
  g_irCodes[(int)b] = code;
  markAttributesDirty();
}

static char g_irLearnStatus[24] = "idle";

const char* lastIrLearnStatus() { return g_irLearnStatus; }
void setLastIrLearnStatus(const char* status) {
  strncpy(g_irLearnStatus, status, sizeof(g_irLearnStatus) - 1);
  g_irLearnStatus[sizeof(g_irLearnStatus) - 1] = '\0';
  markAttributesDirty();
}

static bool g_attributesDirty = false;
static bool g_telemetryDirty = false;

void markAttributesDirty() { g_attributesDirty = true; }
void markTelemetryDirty() { g_telemetryDirty = true; }
bool consumeTelemetryDirty() {
  bool v = g_telemetryDirty;
  g_telemetryDirty = false;
  return v;
}
bool consumeAttributesDirty() {
  bool v = g_attributesDirty;
  g_attributesDirty = false;
  return v;
}

void initShared() {
  g_snapshotMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(16, sizeof(autoac::Command));
  spiMutex = xSemaphoreCreateMutex();

  // schedulePeriods เป็น global array — default member initializer ของ SchedulePeriod (used=false ฯลฯ) ใช้ค่าเริ่มต้นอยู่แล้ว ไม่ต้อง init เอง

  nvsLoadIrCodes(g_irCodes);
}

} // namespace app
