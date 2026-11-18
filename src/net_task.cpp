// WiFi + ThingsBoard (MQTT) — รัน core 0, ห้าม block (กติกาข้อ 4)
// เน็ตหลุดแล้วกฎอัตโนมัติ + จอ + log ต้องทำงานต่อ (NFR-02) — task นี้แค่ reconnect วนไป ไม่บล็อก control task
#include "net_task.h"
#include "app.h"
#include "secrets.h"
#include "rtc.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <memory>

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static const uint32_t TELEMETRY_MS = 30000; // SRS ต้องการ 30 วิ (SPEC.md ข้อ 3 — จุดที่เฟิร์มแวร์เดิมผิดคือส่งทุก 60 วิ)
static void publishSchedule(); // นิยามอยู่ท้าย publishAttributes() เรียกใช้ก่อนประกาศจริง

static void publishTelemetry() {
  const autoac::AcState& ac = app::lastAcState();
  StaticJsonDocument<256> doc;
  doc["temp"] = app::lastTempC();
  doc["humidity"] = app::lastHumidity();
  doc["occupied"] = app::lastOccupied();
  doc["ac_on"] = ac.on;
  doc["ac_set_temp"] = ac.setTemp;
  doc["ac_mode"] = static_cast<int>(ac.mode);
  doc["ac_fan"] = static_cast<int>(ac.fan);

  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("v1/devices/me/telemetry", (const uint8_t*)buf, n, false);
}

// AMG8833 heatmap + Rd-01 radar ให้เว็บแสดงเหมือนหน้าจอเครื่องมือช่าง — รอบตัวเองแยกจาก telemetry หลัก (ถี่กว่า เพราะอยากให้เห็นสดๆ)
static void publishSensorTelemetry() {
  app::ThermalSnapshot thermal = app::lastThermal();
  RadarFrame radar = app::lastRadar();

  StaticJsonDocument<640> doc;
  doc["thermalPresent"] = thermal.present;
  if (thermal.present) {
    doc["thermalMax"] = thermal.maxC;
    doc["thermalMin"] = thermal.minC;
    // CSV ของพิกเซล 64 ค่า ทศนิยม 1 ตำแหน่ง — เบากว่า JSON array เดิมพอสมควร (ไม่ต้องมี [] กับ , คั่นด้วย comma ซ้ำกัน)
    char csv[64 * 6] = {0}; // "99.9," x64 เผื่อพอ
    size_t pos = 0;
    for (int i = 0; i < app::THERMAL_PIXEL_COUNT; i++) {
      pos += snprintf(csv + pos, sizeof(csv) - pos, i == 0 ? "%.1f" : ",%.1f", thermal.pixels[i]);
    }
    doc["thermalPixels"] = csv;
  }
  doc["radarState"] = radar.state;
  doc["radarMoveDistCm"] = radar.moveDistCm;
  doc["radarMoveEnergy"] = radar.moveEnergy;
  doc["radarStaticDistCm"] = radar.staticDistCm;
  doc["radarStaticEnergy"] = radar.staticEnergy;
  doc["radarDetectRangeCm"] = radar.detectRangeCm;

  char buf[640];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("v1/devices/me/telemetry", (const uint8_t*)buf, n, false);
}

void publishEvent(const char* command, const char* source, const char* reason) {
  StaticJsonDocument<192> doc;
  doc["ac_command"] = command;
  doc["source"] = source;
  doc["reason"] = reason;
  char buf[192];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("v1/devices/me/telemetry", (const uint8_t*)buf, n, false);
}

static void publishAttributes() {
  StaticJsonDocument<256> doc;
  doc["emptyMin"] = app::settings.emptyMin;
  doc["graceSec"] = app::settings.graceSec;
  doc["hot"] = app::settings.hot;
  doc["autoOn"] = app::settings.autoOn;
  doc["preOpenMin"] = app::settings.preOpenMin;
  doc["irMode"] = (int)app::settings.irMode;
  char irLearned[8]; // CSV "1,0,1,0" ตามลำดับ IrButton (PowerOn,PowerOff,TempUp,TempDown)
  snprintf(irLearned, sizeof(irLearned), "%c,%c,%c,%c",
           app::irCode(autoac::IrButton::PowerOn).learned ? '1' : '0',
           app::irCode(autoac::IrButton::PowerOff).learned ? '1' : '0',
           app::irCode(autoac::IrButton::TempUp).learned ? '1' : '0',
           app::irCode(autoac::IrButton::TempDown).learned ? '1' : '0');
  doc["irLearned"] = irLearned;
  doc["irLearnStatus"] = app::lastIrLearnStatus();
  JsonArray holidaysArr = doc["holidays"].to<JsonArray>(); // บั๊กเดิม: ไม่เคยส่งรายการวันหยุดขึ้นเว็บเลย — เว็บอ่านได้แต่ array ว่างเสมอ
  for (int i = 0; i < app::holidays.count; i++) holidaysArr.add(app::holidays.dates[i]);
  char buf[1024]; // MAX_HOLIDAYS=32 วันที่ (~14 ไบต์/รายการ) ทำให้ 256 เดิมเล็กไป
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish("v1/devices/me/attributes", (const uint8_t*)buf, n, false);

  publishSchedule();
}

// รายการคาบเรียนทั้งหมด — JSON array of arrays แบบย่อ (ไม่ใช้ key ชื่อฟิลด์ ประหยัดขนาด):
// [id, day, startMin, endMin, validFromDay, validToDay, enabled, name]
// ใช้งานจริงไม่น่าเกินหลักสิบรายการ/ห้อง แม้ spec เผื่อสูงสุด 350 — ถ้าใช้เกือบเต็มจริงๆ อาจเกิน MQTT buffer ได้ (รู้ข้อจำกัดนี้ไว้)
static void publishSchedule() {
  // บัฟเฟอร์ใหญ่ (~16KB รวม) ต้องอยู่บน heap ไม่ใช่ stack — net_task stack มีแค่ 8KB เอง (xTaskCreatePinnedToCore)
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < autoac::SCHEDULE_MAX_PERIODS; i++) {
    const autoac::SchedulePeriod& s = app::schedulePeriods[i];
    if (!s.used) continue;
    JsonArray row = arr.add<JsonArray>();
    row.add(i + 1); // id = index+1
    row.add(s.day);
    row.add(s.startMin);
    row.add(s.endMin);
    row.add(s.validFromDay);
    row.add(s.validToDay);
    row.add(s.enabled);
    row.add(s.name);
  }
  std::unique_ptr<char[]> buf(new char[8192]);
  size_t n = serializeJson(doc, buf.get(), 8192);

  StaticJsonDocument<64> wrap;
  wrap["schedulePeriods"] = serialized(String(buf.get(), n)); // ส่งเป็น attribute ชื่อเดียว ค่าเป็น JSON string ของ array
  std::unique_ptr<char[]> wbuf(new char[8300]);
  size_t wn = serializeJson(wrap, wbuf.get(), 8300);
  mqtt.publish("v1/devices/me/attributes", (const uint8_t*)wbuf.get(), wn, false);
}

// ----- รับ RPC สองทาง (`v1/devices/me/rpc/request/+`) แล้วแปลงเป็น autoac::Command เข้า cmdQueue -----
static void onMqttMessage(char* topic, uint8_t* payload, unsigned int len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;

  const char* method = doc["method"] | "";
  JsonVariant params = doc["params"];

  autoac::Command cmd{};
  cmd.source = autoac::Source::ThingsBoard;
  cmd.admin = true; // แอป ThingsBoard = ขอบล่างอุณหภูมิ 16 (SPEC.md ข้อ 2)
  bool known = true;

  if (!strcmp(method, "on")) cmd.type = autoac::Command::Type::On;
  else if (!strcmp(method, "off")) cmd.type = autoac::Command::Type::Off;
  else if (!strcmp(method, "temp")) { cmd.type = autoac::Command::Type::SetTemp; cmd.intArg = params.as<int>(); }
  else if (!strcmp(method, "mode")) { cmd.type = autoac::Command::Type::SetMode; cmd.intArg = params.as<int>(); }
  else if (!strcmp(method, "fan")) { cmd.type = autoac::Command::Type::SetFan; cmd.intArg = params.as<int>(); }
  else if (!strcmp(method, "auto")) cmd.type = autoac::Command::Type::Auto;
  else if (!strcmp(method, "manual")) cmd.type = autoac::Command::Type::Manual;
  else if (!strcmp(method, "sync")) cmd.type = autoac::Command::Type::Sync;
  else if (!strcmp(method, "extend")) cmd.type = autoac::Command::Type::Extend;
  else if (!strcmp(method, "setSetting")) {
    const char* key = params["key"] | "";
    if (!strcmp(key, "irLearnStart")) {
      int btn = params["value"].as<int>();
      if (btn < 0 || btn >= autoac::IR_BUTTON_COUNT) {
        known = false; // ค่าปุ่มนอกช่วง 0-3 — ไม่รับคำสั่ง กันเขียนเลยขอบ g_irCodes[]
      } else {
        cmd.type = autoac::Command::Type::IrLearnStart;
        cmd.intArg = btn;
      }
    } else if (!strcmp(key, "irLearnCancel")) {
      cmd.type = autoac::Command::Type::IrLearnCancel;
    } else if (!strcmp(key, "irClear")) {
      int btn = params["value"].as<int>();
      if (btn < 0 || btn >= autoac::IR_BUTTON_COUNT) known = false;
      else { cmd.type = autoac::Command::Type::IrClear; cmd.intArg = btn; }
    } else if (!strcmp(key, "calibrateTemp")) {
      cmd.type = autoac::Command::Type::CalibrateTemp;
      cmd.intArg = params["value"].as<int>();
    } else {
      cmd.type = autoac::Command::Type::SetSetting;
      strncpy(cmd.strArg, key, sizeof(cmd.strArg) - 1);
      cmd.intArg = params["value"].as<int>();
    }
  } else if (!strcmp(method, "scheduleAdd") || !strcmp(method, "scheduleEdit")) {
    // params: {id (edit เท่านั้น), day, start, end, validFrom, validTo, enabled, name}
    // validFrom/validTo เป็นสตริง "YYYY-MM-DD" (ว่าง = ไม่จำกัด) — แปลงเป็น days-since-epoch ที่นี่
    cmd.type = !strcmp(method, "scheduleAdd") ? autoac::Command::Type::ScheduleAdd : autoac::Command::Type::ScheduleEdit;
    cmd.intArg = params["id"] | 0; // ใช้เฉพาะ edit
    cmd.intArg2 = params["day"].as<int>();
    int validFrom = 0, validTo = 0;
    int y, m, d;
    const char* vf = params["validFrom"] | "";
    if (sscanf(vf, "%d-%d-%d", &y, &m, &d) == 3) validFrom = (int)autoac::daysFromCivil(y, m, d);
    const char* vt = params["validTo"] | "";
    if (sscanf(vt, "%d-%d-%d", &y, &m, &d) == 3) validTo = (int)autoac::daysFromCivil(y, m, d);
    snprintf(cmd.strArg, sizeof(cmd.strArg), "%d,%d,%d,%d,%d",
             params["start"].as<int>(), params["end"].as<int>(), validFrom, validTo,
             (params["enabled"] | true) ? 1 : 0);
    strncpy(cmd.scheduleName, params["name"] | "", sizeof(cmd.scheduleName) - 1);
  } else if (!strcmp(method, "scheduleDelete")) {
    cmd.type = autoac::Command::Type::ScheduleDelete;
    cmd.intArg = params["id"] | 0;
  } else if (!strcmp(method, "addHoliday")) {
    cmd.type = autoac::Command::Type::AddHoliday;
    strncpy(cmd.strArg, params["date"] | "", sizeof(cmd.strArg) - 1);
  } else if (!strcmp(method, "removeHoliday")) {
    cmd.type = autoac::Command::Type::RemoveHoliday;
    strncpy(cmd.strArg, params["date"] | "", sizeof(cmd.strArg) - 1);
  } else {
    known = false;
  }

  // ตอบ RPC กลับทันที — {ok:true} = เข้าคิวแล้ว ไม่ใช่สำเร็จ (SPEC.md ข้อ 3)
  String reqId = String(topic);
  int lastSlash = reqId.lastIndexOf('/');
  String respTopic = "v1/devices/me/rpc/response/" + reqId.substring(lastSlash + 1);
  StaticJsonDocument<32> resp;
  resp["ok"] = known;
  char respBuf[32];
  size_t n = serializeJson(resp, respBuf);
  mqtt.publish(respTopic.c_str(), (const uint8_t*)respBuf, n);

  if (known) app::pushCommand(cmd);
}

static bool g_loggedWifiOk = false;
static bool g_loggedMqttOk = false;

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_loggedWifiOk) {
      Serial.printf("[net] WiFi เชื่อมต่อแล้ว ip=%s\n", WiFi.localIP().toString().c_str());
      // ยังไม่มี RTC จริง (DS3231) ต่อ — ใช้ NTP แทนไปก่อนเพื่อให้ log_sd.cpp รู้เวลา (SPEC.md ข้อ 5)
      configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      g_loggedWifiOk = true;
    }
    return;
  }
  g_loggedWifiOk = false;
  static uint32_t lastAttempt = 0;
  uint32_t now = millis();
  if (now - lastAttempt < 5000) return;
  lastAttempt = now;
  Serial.printf("[net] กำลังเชื่อม WiFi \"%s\" (status=%d)...\n", WIFI_SSID, (int)WiFi.status());
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static void connectMqtt() {
  if (mqtt.connected()) {
    if (!g_loggedMqttOk) {
      Serial.println("[net] เชื่อม ThingsBoard (MQTT) แล้ว");
      g_loggedMqttOk = true;
    }
    return;
  }
  g_loggedMqttOk = false;

  static uint32_t lastMqttAttempt = 0;
  uint32_t now = millis();
  if (now - lastMqttAttempt < 5000) return; // กัน reconnect รัวๆ จนโดน ThingsBoard limit
  lastMqttAttempt = now;

  mqtt.setServer(TB_HOST, TB_PORT);
  mqtt.setCallback(onMqttMessage);
  // ค่า default (256) เล็กไปสำหรับ telemetry ที่มี thermalPixels (~320 ไบต์) และตารางเรียนคาบเยอะๆ — publish() จะ fail เงียบๆ ถ้าเกิน
  // 8192 คลุม schedulePeriods ได้หลักสิบรายการสบายๆ (ใช้งานจริงไม่น่าเกินนี้ แม้ spec เผื่อสูงสุด 350 รายการ/ระบบ)
  mqtt.setBufferSize(8192);
  Serial.println("[net] กำลังเชื่อม ThingsBoard...");
  if (mqtt.connect("AutoAC", TB_DEVICE_TOKEN, nullptr)) {
    mqtt.subscribe("v1/devices/me/rpc/request/+");
    mqtt.subscribe("v1/devices/me/attributes");
    publishAttributes();
  } else {
    Serial.printf("[net] เชื่อม ThingsBoard ไม่สำเร็จ state=%d\n", mqtt.state());
  }
}

static const uint32_t SENSOR_TELEMETRY_MS = 2000; // thermal/radar อยากให้เห็นสดกว่า telemetry หลัก (30s)

static void netTaskLoop(void*) {
  uint32_t lastTelemetry = 0;
  uint32_t lastSensorTelemetry = 0;
  bool rtcSynced = false;
  for (;;) {
    connectWifi();
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    if (wifiOk && !rtcSynced && time(nullptr) > 1700000000) {
      rtcSyncFromSystemTime(); // NTP sync สำเร็จครั้งแรก — เขียนเวลาที่ได้กลับเข้า DS3231 (no-op ถ้าไม่มี RTC ต่อ)
      rtcSynced = true;
    }
    if (wifiOk) {
      connectMqtt();
      if (mqtt.connected()) {
        mqtt.loop();
        uint32_t now = millis();
        if (now - lastTelemetry >= TELEMETRY_MS || app::consumeTelemetryDirty()) {
          lastTelemetry = now;
          publishTelemetry(); // เปลี่ยนสถานะแอร์ (เปิด/ปิด/อุณหภูมิ) ต้องขึ้นเว็บทันที ไม่รอรอบ 30 วิปกติ
        }
        if (app::consumeAttributesDirty()) {
          publishAttributes(); // แจ้งเว็บทันทีเมื่อ irMode/irLearned/irLearnStatus เปลี่ยน ไม่ต้องรอ reconnect รอบหน้า
        }
        if (now - lastSensorTelemetry >= SENSOR_TELEMETRY_MS) {
          lastSensorTelemetry = now;
          publishSensorTelemetry();
        }
      }
    }
    app::setNetStatus(wifiOk, mqtt.connected());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netTaskStart() {
  // เดิม 8192 — ไม่พอแล้วตอนนี้ publishSchedule() มี DynamicJsonDocument(8192)+String ทำงานอยู่ในฟังก์ชันเดียวกัน
  xTaskCreatePinnedToCore(netTaskLoop, "net", 16384, nullptr, 1, nullptr, 0);
}
