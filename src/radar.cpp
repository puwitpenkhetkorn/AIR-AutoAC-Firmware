#include "radar.h"
#include "pins.h"
#include <Arduino.h>

static HardwareSerial radarSerial(1);
static bool g_presence = false;
static RadarFrame g_frame;

// เฟรมรายงานผล (ปกติ ไม่ engineering mode): header 4 + len(2,LE) + [type(1)=0x02 AA(1) target(9) 55(1) check(1)] + footer 4
// อ้างอิง Rd-01 serial communication protocol V1.0.0 ตาราง 8-12
static const uint8_t kHeader[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t kFooter[4] = {0xF8, 0xF7, 0xF6, 0xF5};
constexpr size_t kMaxPayload = 64;

static uint8_t g_buf[4 + 2 + kMaxPayload + 4];
static size_t g_bufLen = 0;

void radarBegin() {
  radarSerial.begin(256000, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
}

static void tryParseTargetData(const uint8_t* payload, size_t len) {
  // payload: type(1) AA(1) target(9) 55(1) check(1) — มีแค่โหมดปกติ (type 0x02) ที่ parse ตอนนี้
  // target(9) = state(1) move_dist(2,LE) move_energy(1) static_dist(2,LE) static_energy(1) detect_range(2,LE)
  if (len < 11) return;
  if (payload[0] != 0x02 || payload[1] != 0xAA) return;
  const uint8_t* t = &payload[2];
  g_frame.state = t[0];
  g_frame.moveDistCm = t[1] | (t[2] << 8);
  g_frame.moveEnergy = t[3];
  g_frame.staticDistCm = t[4] | (t[5] << 8);
  g_frame.staticEnergy = t[6];
  g_frame.detectRangeCm = t[7] | (t[8] << 8);
  g_frame.everReceived = true;
  g_presence = (g_frame.state != 0x00);
}

void radarPoll() {
  while (radarSerial.available()) {
    uint8_t b = (uint8_t)radarSerial.read();

    if (g_bufLen < 4) {
      // กำลังหา header 4 ไบต์ — เลื่อน window ทีละ byte จนกว่าจะเจอ
      if (b == kHeader[g_bufLen]) {
        g_buf[g_bufLen++] = b;
      } else if (b == kHeader[0]) {
        g_buf[0] = b;
        g_bufLen = 1;
      } else {
        g_bufLen = 0;
      }
      continue;
    }

    g_buf[g_bufLen++] = b;

    if (g_bufLen == 6) {
      // อ่านความยาว intra-frame data ได้แล้ว (2 bytes หลัง header, little-endian)
      uint16_t payloadLen = g_buf[4] | (g_buf[5] << 8);
      if (payloadLen > kMaxPayload) { g_bufLen = 0; continue; } // ผิดปกติ/ยาวเกิน — ทิ้งแล้วเริ่มหา header ใหม่
    }

    if (g_bufLen >= 6) {
      uint16_t payloadLen = g_buf[4] | (g_buf[5] << 8);
      size_t frameLen = 6 + payloadLen + 4; // header+len + payload + footer
      if (g_bufLen >= frameLen) {
        bool footerOk = memcmp(&g_buf[6 + payloadLen], kFooter, 4) == 0;
        if (footerOk) tryParseTargetData(&g_buf[6], payloadLen);
        g_bufLen = 0; // ไม่ว่าจะ footer ถูกไหม เฟรมนี้จบแล้ว เริ่มหาเฟรมถัดไป
      } else if (frameLen > sizeof(g_buf)) {
        g_bufLen = 0; // กันล้นบัฟเฟอร์ถ้าความยาวผิดปกติจริงๆ
      }
    }
  }
}

bool radarPresence() { return g_presence; }
RadarFrame radarLastFrame() { return g_frame; }
