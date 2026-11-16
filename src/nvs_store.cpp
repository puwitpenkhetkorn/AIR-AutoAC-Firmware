#include "nvs_store.h"
#include "app.h"
#include <Preferences.h>

static Preferences prefs;

void nvsLoadAll() {
  prefs.begin("autoac", true);
  size_t n = prefs.getBytes("settings", &app::settings, sizeof(app::settings));
  if (n != sizeof(app::settings)) app::settings = autoac::Settings{};

  n = prefs.getBytes("schedule", &app::schedule, sizeof(app::schedule));
  // ถ้าไม่มีของเก่า ใช้ค่าเริ่มต้นที่ initShared() ตั้งไว้แล้ว (ปิดทุกช่วง)

  n = prefs.getBytes("holidays", &app::holidays, sizeof(app::holidays));
  if (n != sizeof(app::holidays)) app::holidays = autoac::Holidays{};
  prefs.end();
}

void nvsSaveSettings() {
  prefs.begin("autoac", false);
  prefs.putBytes("settings", &app::settings, sizeof(app::settings));
  prefs.end();
}

void nvsSaveSchedule() {
  prefs.begin("autoac", false);
  prefs.putBytes("schedule", &app::schedule, sizeof(app::schedule));
  prefs.end();
}

void nvsSaveHolidays() {
  prefs.begin("autoac", false);
  prefs.putBytes("holidays", &app::holidays, sizeof(app::holidays));
  prefs.end();
}
