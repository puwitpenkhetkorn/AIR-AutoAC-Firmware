# PROGRESS.md — สรุปความคืบหน้า AIR (firmware + web)

อัปเดตล่าสุด: 19 พ.ย. 2026 · repo: `Firmware/AutoAC` (main `feccea2`) + `Wabapp` (main `4ecf426`)

## ทำเสร็จและ**ทดสอบกับฮาร์ดแวร์จริงแล้ว**

### IR Learning (เรียนรู้รีโมทจริง — สำหรับห้องที่แอร์ไม่รองรับโปรโตคอลมาตรฐาน)
- เรียนรู้ 4 ปุ่ม (เปิด/ปิด/อุณหภูมิ+1/-1) ผ่านหน้าเว็บ "ตั้งค่า" → เก็บรหัส IR ดิบลง NVS
- ควบคุมเปิด/ปิด/ปรับอุณหภูมิผ่านเว็บได้จริงกับแอร์ Haier จริง (โหมด Learned)
- **ใหม่วันนี้:** เพิ่มปุ่ม "ลบ" รหัสที่เรียนรู้ไว้ (ต่อปุ่ม) — RPC `setSetting:irClear` + `Command::Type::IrClear` ฝั่งเฟิร์มแวร์
- เพิ่มคำสั่ง **ซิงค์อุณหภูมิจริง** (`calibrateTemp`) แก้ปัญหาอุณหภูมิที่ซอฟต์แวร์จำไว้เพี้ยนจากของจริง

### จอสัมผัส + หน้าเครื่องมือช่างบนจอ (technician tool)
- จอ ILI9341 + ทัช XPT2046 ใช้งานได้ (แก้ปัญหาฮาร์ดแวร์เดิมแล้ว)
- **ใหม่วันนี้ — ปรับโฟลว์ PIN ใหม่ทั้งหมด:** บูตเครื่องเจอหน้า **สไลด์โชว์โลโก้ CPE/AIR สลับทุก 2 วิ** (Idle) ก่อนเสมอ — แตะจอตรงไหนก็ได้ขึ้นหน้าใส่ PIN (1234) → ใส่ถูกเข้าหน้า Status เต็มสิทธิ์เลย (ไม่ต้องใส่ PIN ซ้ำตอนกด SETTINGS/THERMAL/RADAR) → ไม่มีใครแตะจอเกิน 10 วิ ล็อกกลับไปหน้าสไลด์โชว์อัตโนมัติ
- **ใหม่วันนี้ — หน้า THERMAL:** heatmap AMG8833 8x8 สี jet-like + MAX/MIN °C
- **ใหม่วันนี้ — หน้า RADAR:** ป้ายสถานะ (NO TARGET/MOTION/STATIONARY/ทั้งสอง) + แถบพลังงาน/ระยะเคลื่อนไหว-นิ่ง ของ Rd-01
- สีภาพ push ลงจอ (pushImage) ยังไม่ตรงกับสีจริง 100% (ลองสลับช่องสี R/B และสลับ byte สูง/ต่ำแล้ว ยังไม่ตรงเป๊ะ — ของเดิมที่ค้างอยู่)

### ฮาร์ดแวร์เสริม — ยืนยันทำงานจริงทุกตัว
- **DS3231** (นาฬิกาเรียลไทม์), **AMG8833** (กล้องความร้อน), **บัซเซอร์**, **Ai-Thinker RD-01** (เรดาร์ 24GHz) — ตรวจจับคน 3 ชั้น (PIR OR AMG8833 OR เรดาร์)
- **ใหม่วันนี้ — ไฟสถานะ WS2812B (3 ดวง):** ซ้าย=เน็ต/ThingsBoard (เขียว/เหลือง/แดง), กลาง=แอร์เปิด/ปิด (ฟ้า/ดับ), ขวา=คนในห้อง/กำลังเรียนรู้ IR (เขียว/กะพริบม่วง/ดับ) — ตามสเปก PCB ดวงขวาจริงๆ ควรเป็น "ไฟเลี้ยง" (สีขาวเสมอ) ไม่ใช่คนในห้อง ยังไม่ได้แก้ตรงนี้

### หน้าเว็บ — เซนเซอร์เสริม
- **ใหม่วันนี้:** หน้า Room มีการ์ด "เซนเซอร์เสริม" แสดง heatmap AMG8833 + ข้อมูลเรดาร์เหมือนจอเครื่อง (บั๊กที่เจอ: PubSubClient buffer default 256 ไบต์เล็กไป publish() fail เงียบๆ ตอนมี thermalPixels → เพิ่มเป็น 8192)

### เว็บเซิร์ฟเวอร์ในตัวบอร์ด (ใหม่ทั้งหมดวันนี้)
- `http://autoac.local/` (หรือ IP บอร์ดตรงๆ) — ดูไฟล์ log บน microSD ผ่านเว็บ (`sensor_*.csv` / `event_*.csv`)
- API สำหรับเว็บ Wabapp ดึงไปใช้เอง: `/api/files` (JSON รายชื่อไฟล์), `/api/csv?name=...` (เนื้อ CSV ดิบ) — เปิด CORS ให้ fetch ข้าม origin ได้ (ใช้ได้เฉพาะตอนอยู่วง LAN เดียวกับบอร์ด)
- เว็บ: หน้า "ตั้งค่า → Log บน microSD" (ฝัง iframe ของเว็บบอร์ด) + หน้า "ประวัติ" ตอนนี้ดึงจาก ThingsBoard **และ** SD การ์ดมา merge กัน (SD ละเอียดกว่า บันทึกทุก 1 นาที)
- บั๊กที่เจอระหว่างทำ: `WebServer::begin()`/`MDNS.begin()` เรียกก่อน WiFi stack พร้อม → lwIP assert บูตวนไม่มี log เลย (แก้โดยรอ `app::wifiConnected()` ก่อน)

### ตารางเรียนแบบใหม่ (ใหม่ทั้งหมดวันนี้ — เปลี่ยน data model)
- จากเดิม fix 2 ช่วง/วัน ไม่มีชื่อ ไม่มีช่วงวันที่มีผล → ตอนนี้เป็น **คาบเรียนแบบมีชื่อ, ช่วงวันที่มีผล (validFrom/validTo), สูงสุด 50 คาบ/วัน** (รวมสูงสุด 350 คาบทั้งระบบ)
- RPC ใหม่: `scheduleAdd` / `scheduleEdit` / `scheduleDelete` — sync ขึ้นเว็บผ่าน attribute `schedulePeriods`
- เว็บ: หน้าตารางเรียนออกแบบใหม่ทั้งหมด (day-pill เลือกวัน, การ์ดคาบ, ฟอร์ม bottom sheet, นับ "ใช้ไป X/50")
- หน้าวันหยุดก็เปลี่ยนเป็นปฏิทินรายเดือนจริง (เดิมเป็นแค่ลิสต์เพิ่ม/ลบวันที่)
- **บั๊กใหญ่ที่เจอและแก้ระหว่างทาง:** NVS เดิมมีแค่ 20KB เล็กเกินไปสำหรับตารางใหม่ → ต้องทำ custom partition table ขยายเป็น 256KB ซึ่งเจอต่อว่า **PlatformIO ไม่ได้อ่าน offset ของ app0 จาก partitions.csv เอง** (hardcode 0x10000 เป็น default) ทำให้ย้าย app0 ไปที่อื่นแล้วบอร์ด boot loop ไม่มี log เลย ต้องตั้ง `board_upload.offset_address` เองให้ตรงกับตารางใหม่ — แก้แล้วบูตปกติ
- **บั๊กที่เจอและแก้คู่กัน:** วันหยุด (`holidays`) ไม่เคยถูก publish ขึ้น ThingsBoard เลยตั้งแต่แรก (เว็บเห็นว่างตลอดแม้เพิ่มสำเร็จฝั่งบอร์ด) — แก้ให้ `markAttributesDirty()` ตอน add/remove holiday และเพิ่ม `holidays` เข้า `publishAttributes()`
- **ผลข้างเคียงของการเปลี่ยน partition:** ต้อง full chip erase ระหว่างทาง ทำให้ settings/IR codes/ตารางเดิมหายหมด ต้องตั้งค่าใหม่

## ยังไม่ได้เริ่ม / ค้างอยู่
- **Telegram control channel** — ยังไม่เขียน
- **Factory reset** — ยังไม่เขียน
- **แอปมือถือ (mobile app)** — ยังไม่เริ่มเลย ตอนนี้มีแต่เว็บ (responsive) ยังไม่ได้ทำเป็นแอปจริง (เช่น ผ่าน React Native/Capacitor หรือ native)
- **ยังไม่เคย push ขึ้น GitHub เลยทั้ง 2 repo** — มีแต่ local git, ยังไม่มี remote

## ตัดออกจากสโคป (ตัดสินใจแล้ว)
- **หน้าจอผู้ใช้ทั่วไปแบบเต็ม** (นอกจาก technician tool) — ไม่ทำ
- **ทดสอบ 24 ชม. ต่อเนื่อง** — ตัดออก
- **ทดสอบระยะยิง IR ≥ 8m** (NFR-09) — ตัดออก เพราะทดสอบจริงแล้วผ่าน

## สิ่งที่ควรรู้/ระวังต่อ
- **สีภาพบนจอ (splash screen) ยังไม่ตรง** — ลองทั้งสลับ R/B และสลับ byte บน/ล่างของค่าสี RGB565 แล้วยังไม่ตรง ต้องสืบเพิ่ม (อาจต้องดูว่า LovyanGFX panel นี้ตั้ง `rgb_order` ไว้ยังไงจริงๆ หรือทดสอบแบบ pixel เดี่ยวๆ ไล่สีทีละสีเพื่อหาสูตรที่ถูก)
- **ไฟ WS2812B ดวงขวา** ตามสเปก PCB จริงๆ คือ "ไฟเลี้ยง" (ควรติดขาวเสมอตอนมีไฟ) แต่โค้ดตอนนี้ยังผูกกับคนในห้อง/โหมดเรียนรู้ IR อยู่ — ของเดิมที่คุยไว้แล้วยังไม่ได้แก้
- **เว็บเซิร์ฟเวอร์ในบอร์ด + หน้าประวัติ SD** ใช้ได้เฉพาะตอนเครื่องที่เปิดเว็บอยู่ในวง WiFi เดียวกับบอร์ดเท่านั้น (ไม่ผ่านอินเทอร์เน็ต/ThingsBoard)
- ตารางเรียนสเปกเผื่อสูงสุด 350 คาบทั้งระบบ แต่ถ้าใช้เกือบเต็มจริงๆ attribute `schedulePeriods` อาจมีขนาดใหญ่เกิน MQTT buffer (8192) ได้ — ใช้งานจริงไม่น่าเกินหลักสิบรายการต่อห้องจึงไม่น่าเจอ แต่รู้ข้อจำกัดนี้ไว้
- partition table เปลี่ยนที่ `partitions_16mb.csv` + ต้องมี `board_upload.offset_address` ใน `platformio.ini` คู่กันเสมอ ถ้าแก้ offset ของ app0/app1 อีกในอนาคต อย่าลืมแก้ค่านี้ด้วย ไม่งั้นบอร์ด boot loop แบบไม่มี log เลย

## Commit ล่าสุด (วันนี้)

### Firmware (`Firmware/AutoAC`, branch `main`)
```
feccea2 feat: thermal/radar UI, status LEDs, onboard web server, schedule periods
001a268 feat(hw): add Ai-Thinker RD-01 24GHz mmWave radar presence detection
fc5f596 fix(buzzer): replace tone()/noTone() with manual GPIO bit-banging
0227306 feat(hw): add AMG8833 thermal presence detection and buzzer alerts
dafc28d feat(rtc): add DS3231 real-time clock support for schedule/holiday logic
2957423 feat(ui): add PIN-gated on-device settings screen (1234)
... (ดูรายการเต็มด้วย git log)
```

### Web (`Wabapp`, branch `main`)
```
4ecf426 feat: sensor panel, SD log viewer, schedule periods, holidays calendar, IR clear
8530e3f feat(ir-learn): add sync-real-temperature control to Room page for learned-mode rooms
6344fb9 fix(ir-learn): read ThingsBoard CLIENT_SCOPE attributes instead of SHARED_SCOPE
... (ดูรายการเต็มด้วย git log)
```
