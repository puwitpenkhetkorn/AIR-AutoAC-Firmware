// WiFi + ThingsBoard (MQTT) — รัน core 0, ห้าม block (กติกาข้อ 4)
// เน็ตหลุดแล้วกฎอัตโนมัติ + จอ + log ต้องทำงานต่อ (NFR-02) — task นี้แค่ reconnect วนไป ไม่บล็อก control task
#include "net_task.h"
#include "app.h"
#include "secrets.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static const uint32_t TELEMETRY_MS = 30000; // SRS ต้องการ 30 วิ (SPEC.md ข้อ 3 — จุดที่เฟิร์มแวร์เดิมผิดคือส่งทุก 60 วิ)

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
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("v1/devices/me/attributes", (const uint8_t*)buf, n, false);
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
    cmd.type = autoac::Command::Type::SetSetting;
    const char* key = params["key"] | "";
    strncpy(cmd.strArg, key, sizeof(cmd.strArg) - 1);
    cmd.intArg = params["value"].as<int>();
  } else if (!strcmp(method, "setSchedule")) {
    cmd.type = autoac::Command::Type::SetSchedule;
    cmd.intArg = params["day"].as<int>();
    cmd.intArg2 = params["slot"].as<int>();
    // en/start/end อ่านเพิ่มใน control_task ผ่าน strArg เป็น "en,start,end" ง่ายๆ
    snprintf(cmd.strArg, sizeof(cmd.strArg), "%d,%d,%d",
             params["en"].as<int>(), params["start"].as<int>(), params["end"].as<int>());
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
  Serial.println("[net] กำลังเชื่อม ThingsBoard...");
  if (mqtt.connect("AutoAC", TB_DEVICE_TOKEN, nullptr)) {
    mqtt.subscribe("v1/devices/me/rpc/request/+");
    mqtt.subscribe("v1/devices/me/attributes");
    publishAttributes();
  } else {
    Serial.printf("[net] เชื่อม ThingsBoard ไม่สำเร็จ state=%d\n", mqtt.state());
  }
}

static void netTaskLoop(void*) {
  uint32_t lastTelemetry = 0;
  for (;;) {
    connectWifi();
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    if (wifiOk) {
      connectMqtt();
      if (mqtt.connected()) {
        mqtt.loop();
        uint32_t now = millis();
        if (now - lastTelemetry >= TELEMETRY_MS) {
          lastTelemetry = now;
          publishTelemetry();
        }
      }
    }
    app::setNetStatus(wifiOk, mqtt.connected());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netTaskStart() {
  xTaskCreatePinnedToCore(netTaskLoop, "net", 8192, nullptr, 1, nullptr, 0);
}
