// เว็บเซิร์ฟเวอร์ในตัวบอร์ด — เสิร์ฟ log จาก microSD ให้ดูผ่านเว็บในวง LAN เดียวกัน (ไม่ผ่าน ThingsBoard)
// รันเป็น task แยกจาก net_task เพราะ WebServer::handleClient() เป็น synchronous/blocking ต่อ request
#include "web_task.h"
#include "app.h"
#include "pins.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>

static WebServer server(80);
static bool g_mdnsStarted = false;

static void sendPageHead(const char* title) {
  server.sendContent("<!doctype html><html><head><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>");
  server.sendContent(title);
  server.sendContent("</title><style>body{font-family:sans-serif;margin:16px;background:#11151c;color:#e6e8ee}"
                      "a{color:#6fc3ff}h1{font-size:18px}pre{white-space:pre-wrap;word-break:break-all;"
                      "background:#1a1f29;padding:10px;border-radius:8px;font-size:13px}"
                      "ul{padding-left:18px}li{margin:6px 0}</style></head><body>");
}

// รายชื่อไฟล์ log ทั้งหมดบน SD — ลิงก์ไปหน้า /view?name=... (อ่านทีละไฟล์ ไม่โหลดทั้งหมดลง RAM)
static void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendPageHead("AIR - Log Viewer");
  server.sendContent("<h1>AIR — microSD Log Viewer</h1><ul>");

  if (app::spiLock(1000)) {
    File root = SD.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String name = f.name();
          if (name.startsWith("/")) name = name.substring(1);
          if (name.endsWith(".csv")) {
            server.sendContent("<li><a href='/view?name=" + name + "'>" + name + "</a> (" + String(f.size()) + " bytes)</li>");
          }
        }
        f = root.openNextFile();
      }
      root.close();
    } else {
      server.sendContent("<li>เปิดการ์ด SD ไม่ได้</li>");
    }
    app::spiUnlock();
  } else {
    server.sendContent("<li>SPI ไม่ว่าง ลองใหม่อีกครั้ง</li>");
  }

  server.sendContent("</ul></body></html>");
  server.sendContent("");
}

// สตรีมเนื้อไฟล์ CSV ทีละ chunk แทนการโหลดทั้งไฟล์ลง RAM (ไฟล์ log อาจยาวหลายวัน)
static void handleView() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "missing name"); return; }
  String name = server.arg("name");
  if (name.indexOf("..") >= 0 || !name.endsWith(".csv")) { server.send(400, "text/plain", "invalid name"); return; } // กัน path traversal
  String path = "/" + name;

  if (!app::spiLock(1000)) { server.send(503, "text/plain", "SPI busy, try again"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { app::spiUnlock(); server.send(404, "text/plain", "not found"); return; }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendPageHead(name.c_str());
  server.sendContent("<h1>" + name + "</h1><p><a href='/'>&larr; กลับรายการไฟล์</a></p><pre>");

  uint8_t buf[256];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    server.sendContent((const char*)buf, n); // CSV เป็น ASCII ล้วน ส่งดิบตรงๆ ปลอดภัยใน <pre>
  }
  f.close();
  app::spiUnlock();

  server.sendContent("</pre></body></html>");
  server.sendContent("");
}

// JSON รายชื่อไฟล์ — ให้หน้าเว็บ (Wabapp) ดึงไปต่อเอง ต่างจาก handleRoot ที่ตอบเป็น HTML
static void handleApiFiles() {
  if (!app::spiLock(1000)) { server.send(503, "application/json", "[]"); return; }
  String json = "[";
  File root = SD.open("/");
  if (root) {
    File f = root.openNextFile();
    bool first = true;
    while (f) {
      if (!f.isDirectory()) {
        String name = f.name();
        if (name.startsWith("/")) name = name.substring(1);
        if (name.endsWith(".csv")) {
          if (!first) json += ",";
          json += "\"" + name + "\"";
          first = false;
        }
      }
      f = root.openNextFile();
    }
    root.close();
  }
  json += "]";
  app::spiUnlock();
  server.send(200, "application/json", json);
}

// เนื้อไฟล์ CSV ดิบ (ไม่ห่อ HTML) — ให้หน้าเว็บ fetch() ไป parse เอง ต่อจากที่ handleView ทำไว้สำหรับดูตรงในเบราว์เซอร์
static void handleApiCsv() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "missing name"); return; }
  String name = server.arg("name");
  if (name.indexOf("..") >= 0 || !name.endsWith(".csv")) { server.send(400, "text/plain", "invalid name"); return; }
  String path = "/" + name;

  if (!app::spiLock(1000)) { server.send(503, "text/plain", "SPI busy, try again"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { app::spiUnlock(); server.send(404, "text/plain", "not found"); return; }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  uint8_t buf[256];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    server.sendContent((const char*)buf, n);
  }
  f.close();
  app::spiUnlock();
  server.sendContent("");
}

static void handleNotFound() { server.send(404, "text/plain", "not found"); }

static void webTaskLoop(void*) {
  server.enableCORS(true); // ให้เว็บ Wabapp (คนละ origin) fetch() ข้อมูลจาก SD การ์ดได้โดยตรงตอนอยู่ใน LAN เดียวกัน
  server.on("/", handleRoot);
  server.on("/view", handleView);
  server.on("/api/files", handleApiFiles);
  server.on("/api/csv", handleApiCsv);
  server.onNotFound(handleNotFound);

  // รอ WiFi ขึ้นสแตก lwIP ก่อน (net_task เป็นคนเรียก WiFi.begin()) — เรียก server.begin()/MDNS.begin() ก่อนหน้านั้น
  // ทำให้ lwIP mbox ยังไม่พร้อมแล้ว assert ("Invalid mbox") บูตวน พบจากการทดสอบจริง
  bool serverStarted = false;
  for (;;) {
    if (!serverStarted && app::wifiConnected()) {
      server.begin();
      serverStarted = true;
      Serial.println("[web] เซิร์ฟเวอร์เริ่มที่พอร์ต 80");
    }
    if (!g_mdnsStarted && app::wifiConnected()) {
      if (MDNS.begin("autoac")) {
        MDNS.addService("http", "tcp", 80);
        g_mdnsStarted = true;
        Serial.println("[web] mDNS พร้อม — เข้าได้ที่ http://autoac.local/");
      }
    }
    if (serverStarted) server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void webTaskStart() {
  xTaskCreatePinnedToCore(webTaskLoop, "web", 8192, nullptr, 1, nullptr, 1);
}
