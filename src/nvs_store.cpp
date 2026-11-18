#include "nvs_store.h"
#include "app.h"
#include <Preferences.h>

static Preferences prefs;

// emptyMin/graceSec/hot/autoOn/preOpenMin เก็บเป็นบล็อกเดียวเหมือนเดิม — irMode เก็บแยกคีย์ต่างหาก (ดูเหตุผลข้างล่าง)
struct LegacySettingsBlob {
  uint16_t emptyMin;
  uint16_t graceSec;
  uint8_t hot;
  bool autoOn;
  uint16_t preOpenMin;
};

void nvsLoadAll() {
  prefs.begin("autoac", true);

  LegacySettingsBlob legacy;
  size_t n = prefs.getBytes("settings", &legacy, sizeof(legacy));
  if (n == sizeof(legacy)) {
    app::settings.emptyMin = legacy.emptyMin;
    app::settings.graceSec = legacy.graceSec;
    app::settings.hot = legacy.hot;
    app::settings.autoOn = legacy.autoOn;
    app::settings.preOpenMin = legacy.preOpenMin;
  } else {
    app::settings = autoac::Settings{};
  }
  // irMode เก็บคีย์แยก ("irMode") ไม่รวมเข้า blob "settings" — ถ้าเพิ่ม field ใหม่ใน Settings ในอนาคต
  // จะได้ไม่ทำให้ sizeof(Settings) เปลี่ยนแล้วบล็อก "settings" เดิมของห้องที่ตั้งค่าไว้แล้วถูกล้างทิ้งทั้งก้อน
  uint8_t irModeByte = 0;
  n = prefs.getBytes("irMode", &irModeByte, sizeof(irModeByte));
  app::settings.irMode = (n == sizeof(irModeByte)) ? static_cast<autoac::IrMode>(irModeByte) : autoac::IrMode::Protocol;

  prefs.getBytes("schedulePeriods", &app::schedulePeriods, sizeof(app::schedulePeriods));
  // ถ้าไม่มีของเก่า/ขนาดไม่ตรง getBytes จะไม่เขียนทับ — app::schedulePeriods เป็น global array ที่ default-init ไว้แล้วว่างเปล่า (used=false ทุกช่อง) อยู่แล้ว

  n = prefs.getBytes("holidays", &app::holidays, sizeof(app::holidays));
  if (n != sizeof(app::holidays)) app::holidays = autoac::Holidays{};
  prefs.end();
}

void nvsSaveSettings() {
  LegacySettingsBlob legacy{app::settings.emptyMin, app::settings.graceSec, app::settings.hot,
                            app::settings.autoOn, app::settings.preOpenMin};
  uint8_t irModeByte = (uint8_t)app::settings.irMode;
  prefs.begin("autoac", false);
  prefs.putBytes("settings", &legacy, sizeof(legacy));
  prefs.putBytes("irMode", &irModeByte, sizeof(irModeByte));
  prefs.end();
}

void nvsSaveSchedule() {
  prefs.begin("autoac", false);
  prefs.putBytes("schedulePeriods", &app::schedulePeriods, sizeof(app::schedulePeriods));
  prefs.end();
}

void nvsSaveHolidays() {
  prefs.begin("autoac", false);
  prefs.putBytes("holidays", &app::holidays, sizeof(app::holidays));
  prefs.end();
}

void nvsLoadIrCodes(LearnedIrCode out[autoac::IR_BUTTON_COUNT]) {
  prefs.begin("autoac", true);
  for (int i = 0; i < autoac::IR_BUTTON_COUNT; i++) {
    char key[4];
    snprintf(key, sizeof(key), "ir%d", i);
    size_t n = prefs.getBytes(key, &out[i], sizeof(LearnedIrCode));
    if (n != sizeof(LearnedIrCode)) out[i] = LearnedIrCode{};
  }
  prefs.end();
}

void nvsSaveIrCode(autoac::IrButton b, const LearnedIrCode& code) {
  char key[4];
  snprintf(key, sizeof(key), "ir%d", (int)b);
  prefs.begin("autoac", false);
  prefs.putBytes(key, &code, sizeof(LearnedIrCode));
  prefs.end();
}
