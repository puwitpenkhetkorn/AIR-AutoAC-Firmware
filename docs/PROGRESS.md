# PROGRESS.md — สรุปความคืบหน้า AIR (firmware + web)

อัปเดตล่าสุด: 17 พ.ย. 2026 · repo: `Firmware/AutoAC` (main `001a268`) + `Wabapp` (main `8530e3f`)

## ทำเสร็จและ**ทดสอบกับฮาร์ดแวร์จริงแล้ว**

### IR Learning (เรียนรู้รีโมทจริง — สำหรับห้องที่แอร์ไม่รองรับโปรโตคอลมาตรฐาน)
- เรียนรู้ 4 ปุ่ม (เปิด/ปิด/อุณหภูมิ+1/-1) ผ่านหน้าเว็บ "ตั้งค่า" → เก็บรหัส IR ดิบลง NVS
- ควบคุมเปิด/ปิด/ปรับอุณหภูมิผ่านเว็บได้จริงกับแอร์ Haier จริง (โหมด Learned)
- แก้บั๊กจากรีวิวรอบ final: รูปแบบรหัส IR ผิด (ต้องแปลง tick→microsecond), `IRsend::begin()` ไม่เคยถูกเรียก, attribute ไม่ sync ให้เว็บทันที, NVS settings หายตอนอัปเกรด, bounds check ปุ่มเรียนรู้, burst timing ของปุ่มอุณหภูมิ
- ขยาย raw buffer จาก 150 → 400 ช่องสัญญาณ (รีโมทจริงยาว 230 ช่อง — 150 เดิมเล็กไป)
- เพิ่มคำสั่ง **ซิงค์อุณหภูมิจริง** (`calibrateTemp`) แก้ปัญหาอุณหภูมิที่ซอฟต์แวร์จำไว้เพี้ยนจากของจริง (เพราะนับจากการกด +/- แบบสัมพัทธ์)
- เว็บ: แก้บั๊ก `/api/tenant/deviceInfos` ใช้ endpoint ผิด (ต้องใช้ `/api/tenant/devices` + ดึง active แยก), แก้อ่าน ThingsBoard attribute ผิด scope (ใช้ SHARED_SCOPE ทั้งที่บอร์ดเขียน CLIENT_SCOPE), แก้ RPC จาก two-way เป็น one-way (ลดดีเลย์การกดปุ่ม), แก้ telemetry ส่งทุก 30 วิ → ส่งทันทีเมื่อสถานะเปลี่ยน

### จอสัมผัส + หน้าตั้งค่าบนเครื่อง (technician tool)
- จอ ILI9341 + ทัช XPT2046 ใช้งานได้ (แก้ปัญหาจุดบัดกรี/FPC หลวม — ปัญหาฮาร์ดแวร์ ไม่ใช่โค้ด)
- แก้บั๊กโค้ด: touch calibration range ผิด (ใส่พิกเซลแทนค่าดิบ ADC), แกน X ของทัชกลับด้าน (กด ON โดนปุ่ม OFF), ลดดีเลย์การตอบสนองทัช
- เพิ่มหน้า **ตั้งค่าแบบใส่ PIN (1234)** บนจอเครื่อง — ปรับ emptyMin/graceSec/hot/autoOn/preOpenMin ได้โดยไม่ต้องเปิดเว็บ (ทดสอบแล้วว่าค่าที่ปรับขึ้นตรงกับเว็บ)

### ฮาร์ดแวร์ใหม่ที่เพิ่งต่อวันนี้ — ยืนยันทำงานจริงทุกตัว
- **DS3231 (นาฬิกาเรียลไทม์):** ตั้งเวลาระบบจาก RTC ได้ตั้งแต่บูต (ไม่ต้องรอ NTP) · ตารางเรียน/วันหยุดที่เคยเป็นแค่ placeholder ตอนนี้คำนวณจากเวลาจริงแล้ว
- **AMG8833 (กล้องความร้อน 8x8):** ตรวจจับคนในห้องแม่นขึ้น (เห็นคนนั่งนิ่งที่ PIR จับไม่ได้) — รวมกับ PIR แบบ OR
- **บัซเซอร์:** บี๊บเตือนตอนเริ่มนับถอยหลังปิดแอร์อัตโนมัติ, บี๊บตอนใส่ PIN ผิด · ปัญหาตอนแรกคือสาย GND หลุด ไม่ใช่โค้ด
- **Ai-Thinker RD-01 (เรดาร์ 24GHz mmWave):** ต่อ UART ที่ IO1(RX)/IO2(TX) @ 256000 baud (ตาม datasheet จริง ไม่ใช่ 115200 ตามโฆษณา) — เห็นคนนิ่งได้แม่นกว่า PIR อีกชั้น รวม OR กับ PIR + AMG8833 เป็นระบบตรวจจับคน 3 ชั้น

### หน้าจอเครื่องมือช่าง + ไฟสถานะ — เพิ่มใหม่
- **หน้า THERMAL/RADAR บนจอเครื่อง** (ใส่ PIN 1234 เข้าเหมือน SETTINGS): heatmap AMG8833 8x8 สี jet + MAX/MIN, หน้า radar Rd-01 (state/ระยะ/พลังงาน)
- **ขึ้นเว็บ:** การ์ด "เซนเซอร์เสริม" ในหน้า Room แสดง heatmap + radar เหมือนจอเครื่อง — แก้บั๊ก PubSubClient buffer เริ่มต้น 256 ไบต์เล็กไป (thermalPixels CSV ~320 ไบต์ ทำให้ publish ทั้งก้อน fail เงียบๆ) → เพิ่มเป็น 1024
- **ไฟสถานะ WS2812B (3 ดวง):** ซ้าย=เน็ต/ThingsBoard (เขียว/เหลือง/แดง), กลาง=แอร์เปิด/ปิด (ฟ้า/ดับ), ขวา=คนในห้อง/กำลังเรียนรู้ IR (เขียว/กะพริบม่วง/ดับ)

## ยังไม่ได้เริ่ม / ค้างอยู่
- **Telegram control channel** — ยังไม่เขียน
- **Web server ในตัวบอร์ด** (`autoac.local`) — ยังไม่เขียน
- **Factory reset** — ยังไม่เขียน
- **ยังไม่เคย push ขึ้น GitHub เลยทั้ง 2 repo** — มีแต่ local git, ยังไม่มี remote

## ตัดออกจากสโคป (ตัดสินใจแล้ว)
- **หน้าจอผู้ใช้ทั่วไปแบบเต็ม** (นอกจาก technician tool) — ไม่ทำ
- **ทดสอบ 24 ชม. ต่อเนื่อง** — ตัดออก
- **ทดสอบระยะยิง IR ≥ 8m** (NFR-09) — ตัดออก เพราะทดสอบจริงแล้วผ่าน

## สิ่งที่ควรรู้/ระวังต่อ
- บัฟเฟอร์เรียนรู้ IR (400 ช่อง) เผื่อไว้พอสำหรับรีโมทที่เจอจริง (230 ช่อง) แต่ถ้าเจอรีโมทที่ยาวกว่านี้มากอาจต้องขยายอีก
- หน้าจอ technician tool "ปุ่มทดสอบ IR ดิบ" (`HwIrTest`) ใช้โปรโตคอล `HAIER_AC176` เสมอ ไม่สนใจว่าห้องตั้ง Learned mode ไว้หรือไม่ — ใช้ไม่ได้กับแอร์ที่ไม่ใช่โปรโตคอลนี้ (พบจากการทดสอบจริง: แอร์เครื่องที่ทดสอบไม่รองรับ จึงต้องพึ่ง Learned mode แทน)
- สายไฟ GND ของอุปกรณ์เสริม (บัซเซอร์ เป็นต้น) ต้องเช็คให้ครบวงจรก่อนสรุปว่าโค้ด/GPIO มีปัญหา

## Commit log วันนี้

### Firmware (`Firmware/AutoAC`, branch `main`)
```
001a268 feat(hw): add Ai-Thinker RD-01 24GHz mmWave radar presence detection
fc5f596 fix(buzzer): replace tone()/noTone() with manual GPIO bit-banging
0227306 feat(hw): add AMG8833 thermal presence detection and buzzer alerts
dafc28d feat(rtc): add DS3231 real-time clock support for schedule/holiday logic
2957423 feat(ui): add PIN-gated on-device settings screen (1234)
5a75cc8 fix(ui): invert touch X axis and separate touch-poll/redraw cadence
d520bac fix(display): touch x_min/x_max/y_min/y_max were set to pixel coords instead of raw ADC range
046bf3c fix(ir-learn): publish telemetry immediately on AC state change instead of waiting up to 30s
b5650f3 feat(ir-learn): add calibrateTemp command to fix real-vs-tracked temperature drift
0eba6ee fix(ir-learn): expand raw capture buffer from 150 to 400 samples
5c3c086 fix(ir-learn): fix raw-code format, missing IRsend begin(), attribute scope/timing, NVS settings reset, button bounds check, and burst timing
90000fe feat(ir-learn): bridge learn commands and status to ThingsBoard
37cfb5d feat(ir-learn): wire learn dispatch and Learned-mode IR send into Control task
29e6f4a feat(ir-learn): add shared learned-IR-code state
bd563f7 feat(ir-learn): persist learned IR codes in NVS
02f3a64 feat(ir-learn): add non-blocking IR learn capture module
701bdde feat(ir-learn): add IrMode/IrButton types and learn command types
405cef4 chore: initial commit of existing firmware before IR-learning feature
```

### Web (`Wabapp`, branch `main`)
```
8530e3f feat(ir-learn): add sync-real-temperature control to Room page for learned-mode rooms
6344fb9 fix(ir-learn): read ThingsBoard CLIENT_SCOPE attributes instead of SHARED_SCOPE
0c2c7ae fix(ir-learn): admin-gate Cancel button and disable other Learn buttons while one is listening (review I3/M3)
623d78f feat(ir-learn): add IR learn UI and hide mode/fan for learned-mode rooms
965aeac feat(ir-learn): read IR learn attributes into telemetry hook
a7e4f17 chore: initial commit of existing web app before IR-learning feature
```
