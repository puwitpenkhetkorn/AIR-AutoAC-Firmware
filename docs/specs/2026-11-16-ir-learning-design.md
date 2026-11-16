# ฟีเจอร์เรียนรู้รหัส IR จากรีโมทจริง (Universal/Learned mode)

## ทำไมต้องทำ

ตอนนี้บอร์ดสั่งแอร์ผ่าน `IRac` + โปรโตคอล `HAIER_AC176` ซึ่งใช้ได้กับแอร์ Haier เท่านั้น
ห้องเรียนอื่นๆ ที่ใช้แอร์ยี่ห้อ/รุ่นที่ `IRac` ไม่รองรับ ต้องจับสัญญาณ IR ดิบจากรีโมทจริงของห้องนั้น
มาเก็บไว้ยิงซ้ำแทน ฮาร์ดแวร์ IR receiver (VS1838B, `PIN_IR_RX = 7`) มีอยู่แล้วบนบอร์ดทุกตัว
แค่ยังไม่เคยใช้งานจริงในโค้ด

## เป้าหมาย

- ผู้ดูแล (Admin) เข้าหน้า "ตั้งค่า" ในเว็บ เลือกโหมด IR ของห้องเป็น "เรียนรู้จากรีโมท" แล้วกดเรียนรู้ทีละปุ่ม
- เรียนรู้ 4 ปุ่มต่อห้อง: เปิด, ปิด, อุณหภูมิ+1, อุณหภูมิ-1
- หลังเรียนรู้ครบ ควบคุมเปิด/ปิด/ปรับอุณหภูมิผ่านเว็บได้เหมือนห้องที่ใช้โปรโตคอลมาตรฐาน (Haier)
- รหัสที่เรียนรู้แล้วเก็บถาวรใน NVS ของบอร์ด (รอดไฟดับ/รีสตาร์ท)

## ไม่ทำ (Non-goals)

- ไม่เรียนรู้โหมด (เย็น/พัดลม/ลดความชื้น/อัตโนมัติ) และความแรงพัดลม — ปุ่มพวกนี้ของรีโมทจริงมักเป็นปุ่มวนลูป
  ไม่มีทางรู้ step ปัจจุบันจริงของแอร์ (IR เป็นทางเดียว) ถ้าเรียนรู้แค่ "กด 1 ที" จะกดวนไม่ตรงเป้าหมาย
  → ห้องโหมดเรียนรู้จะซ่อนปุ่มโหมด/พัดลมในเว็บ ผู้ใช้ต้องเดินไปกดรีโมทจริงเองสำหรับ 2 อย่างนี้
- ไม่ทำ full-state capture (จับรหัสทุกชุดผสมอุณหภูมิ×โหมด×พัดลม) — เรียนรู้มากเกินความคุ้มค่าเวลา
- ไม่รองรับหลายรีโมท/driver ต่อห้อง (1 บอร์ด = 1 ห้อง = 1 ชุดรหัสเรียนรู้ ตามสถาปัตยกรรมเดิม)

## Anti-goals

- ห้ามกระทบห้อง Haier ที่ใช้งานได้แล้ว (โหมด `IRac` เดิมต้องยังทำงานเหมือนเดิมทุกกรณี)
- ห้าม task อื่นค้าง/บล็อกระหว่างรอจับสัญญาณ IR (ใช้ `IRrecv` แบบ interrupt-driven + poll ไม่ใช่ blocking wait)
- ห้ามผิดกติกาสถาปัตยกรรม: คำสั่งเรียนรู้ต้องเข้า `cmdQueue` เหมือนคำสั่งอื่น, มีแค่ Control task ที่แตะ IR จริง

## Constraints

- Settings ปัจจุบันเก็บผ่าน `Preferences` (NVS) คีย์ต่อค่า ไม่ใช่ blob ก้อนใหญ่ — ใช้แนวทางเดียวกัน
- RPC ปัจจุบันเป็น one-way (ไม่รอ ack จากบอร์ด) — ฟีเจอร์เรียนรู้ต้องใช้ telemetry/attribute polling
  เพื่อให้เว็บเห็นผลลัพธ์ ไม่ใช่รอ response ของ RPC call ตรงๆ
- ต้องมีทาง "ยกเลิก" การเรียนรู้ถ้าผู้ใช้กดผิด/ไม่อยากรอ

## แนวทางที่พิจารณา (เลือกไปแล้วในบทสนทนา)

| แนวทาง | คำอธิบาย | เลือก |
|---|---|---|
| A. Full-state capture | จับทุกชุดผสมอุณหภูมิ×โหมด×พัดลม | ❌ เรียนรู้เป็นร้อยรอบ ไม่คุ้ม |
| B. 6 ปุ่ม (เปิด/ปิด/อุณหภูมิ±/โหมด/พัดลม) | ใกล้เคียง Haier ที่สุด | ❌ โหมด/พัดลมวนลูปไม่รู้จุดเริ่ม เสี่ยงมั่ว |
| **B′. 4 ปุ่ม (เปิด/ปิด/อุณหภูมิ±)** | ตัดโหมด/พัดลมออก เหลือสิ่งที่นับจำนวนครั้งได้แม่นยำ | ✅ เลือก |

## การออกแบบ

### 1. Types (`lib/autoac_core/types.h`)

```cpp
enum class IrMode : uint8_t { Protocol = 0, Learned = 1 }; // ต่อห้อง เก็บใน Settings

enum class IrButton : uint8_t { PowerOn = 0, PowerOff = 1, TempUp = 2, TempDown = 3 };
constexpr int IR_BUTTON_COUNT = 4;

// เพิ่มใน Command::Type: IrLearnStart (intArg = IrButton), IrLearnCancel
```

เพิ่ม `IrMode irMode = IrMode::Protocol;` ใน `Settings` struct — ตั้งค่าได้ผ่าน `setSetting` key `"irMode"` (0/1)
เหมือนค่าตั้งอื่นๆ ที่มีอยู่แล้ว (`emptyMin`, `hot`, ฯลฯ) — ไม่ต้องเพิ่ม RPC method ใหม่สำหรับส่วนนี้

### 2. การจับสัญญาณ (ir_learn.h/.cpp ใหม่ ใน `src/`)

ใช้ `IRrecv` จาก IRremoteESP8266 (มีอยู่แล้วใน `lib_deps`) แบบไม่บล็อก:

```cpp
void irLearnBegin();                 // เรียกตอนบูต (ครั้งเดียว) — irrecv.enableIRIn()
void irLearnStart(autoac::IrButton b, uint32_t timeoutMs = 15000);
void irLearnCancel();
bool irLearnActive();
// เรียกทุก tick ใน control_task loop (ไม่บล็อก) — คืน true ถ้าจบ (สำเร็จ/timeout) รอบนี้
bool irLearnPoll(bool& outOk, autoac::IrButton& outButton);
```

ข้างใน: เก็บ `decode_results` เป็น raw timing (`results.rawbuf`, `results.rawlen`) ลง struct คงที่
`struct LearnedIrCode { uint16_t raw[150]; uint8_t len; bool learned; }` — ขนาด ~300 ไบต์/ปุ่ม
เก็บ 4 ปุ่ม = ~1.2KB รวม ไม่เกินลิมิต blob ของ `Preferences::putBytes`

### 3. NVS (`nvs_store.h/.cpp`)

เพิ่ม `nvsLoadIrCodes()` / `nvsSaveIrCode(IrButton)` — เก็บ blob ต่อปุ่มด้วยคีย์ `"ir0".."ir3"`
โหลดเข้า `app::irCodes[4]` (global array ใหม่ใน `app.h`, Control task เขียน/อ่านเท่านั้น ตามกติกา)

### 4. control_task.cpp

- `Command::Type::IrLearnStart/IrLearnCancel` ไม่ผ่าน `Controller` (เหมือน `HwIrTest`) — เรียก `irLearnStart/Cancel` ตรงๆ
- ใน loop หลัก เพิ่ม `if (irLearnPoll(ok, btn)) { nvsSaveIrCode(btn); publishEvent(...); }` ทุก tick
- `irSend()` แยก 2 ทาง ตาม `app::settings.irMode`:
  - `Protocol` (เดิม): เหมือนปัจจุบันทุกอย่าง (IRac + HAIER_AC176)
  - `Learned`: แปล `Decision` เป็นลำดับปุ่มเรียนรู้
    - `sendOn` → ยิง `PowerOn` 1 ครั้ง (ถ้ายังไม่เรียนรู้ → log แล้วข้าม ไม่ throw)
    - `sendOff` → ยิง `PowerOff` 1 ครั้ง
    - `updateSetTemp` → เทียบ `newTemp` กับ `ac_.setTemp` เดิมที่ track ไว้ใน software → ยิง `TempUp`/`TempDown`
      ตามจำนวนองศาที่ต่างกัน (ห่างกัน 400ms/ครั้งเหมือนเดิม), จำกัดสูงสุด 14 ครั้ง (ช่วง 16–30°C) กันสั่งเพี้ยนยิงรัว
  - โหมด/พัดลมของห้อง `Learned`: `applyDecision`/`Controller` ยังคำนวณปกติ (software state ยัง track ไว้)
    แต่ `irSend()` ฝั่ง `Learned` ไม่ส่งอะไรออกจริงเมื่อเจอ `updateMode`/`updateFan` (เงียบๆ ข้าม)

### 5. ThingsBoard / เว็บ

- ไม่มี RPC method ใหม่สำหรับคุมค่า — ใช้ `setSetting` เดิม ส่ง key พิเศษ:
  - `"irMode"` (0/1)
  - `"irLearnStart"` (value = IrButton index 0-3) → บอร์ดตีความเป็นคำสั่งเริ่มเรียนรู้ ไม่ใช่ setting จริง (คีย์พิเศษ ไม่ผ่าน `nvsSaveSettings`)
  - `"irLearnCancel"` (value ไม่สำคัญ)
- บอร์ด publish attribute เพิ่มใน `publishAttributes()`:
  - `irMode`, `irLearned` (bitmask/array 4 bool ว่าปุ่มไหนเรียนรู้แล้ว), `irLearnStatus` (`"idle"|"listening:<button>"|"ok:<button>"|"timeout:<button>"`)
- หน้า **ตั้งค่า** (`Settings.tsx`) เพิ่มส่วนใหม่ (admin เท่านั้น, ไม่ขึ้นถ้า `irMode` ยังไม่เปลี่ยนเป็นเรียนรู้):
  - toggle "โหมด IR: มาตรฐาน / เรียนรู้จากรีโมท"
  - ถ้าเรียนรู้: แสดง 4 แถว (เปิด/ปิด/อุณหภูมิ+/อุณหภูมิ-) แต่ละแถวมีปุ่ม "เรียนรู้" + badge (ยังไม่เรียนรู้ / เรียนรู้แล้ว)
    กดแล้ว poll `irLearnStatus` attribute ทุก ~1 วิ (เหมือน `useTelemetry` polling ที่มีอยู่) โชว์ "กำลังรอสัญญาณ... เล็งรีโมทแล้วกด" จนกว่าจะ ok/timeout

## รายละเอียดเพิ่มเติม (เติมหลัง builder check)

- **`irSend()` ฝั่ง Learned ต้องไม่บล็อก Control task เช่นกัน** (ไม่ใช่แค่ `irLearnPoll`) — การยิง TempUp/TempDown
  หลายครั้งติดกัน (เว้น 400ms/ครั้ง สูงสุด 14 ครั้ง ≈ 5.6 วิ) ต้องทำเป็น state machine ต่อ tick แบบเดียวกับ `irLearnPoll`
  (เก็บจำนวนครั้งที่เหลือ + เวลาที่ต้องกดครั้งถัดไป เป็น static state ใน `control_task.cpp`, เช็ค/ยิงทีละครั้งต่อ loop)
  ห้าม loop + `vTaskDelay` รัวๆ ภายในฟังก์ชันเดียวเหมือนโค้ด Protocol mode เดิม
- **ค่า `setTemp` หลังรีสตาร์ท**: ใช้พฤติกรรมเดิมของ `Controller::begin()` คือรีเซ็ตเป็นค่าเริ่มต้น (24°C, `ac_.on=false`)
  ทุกโหมดเหมือนกัน — ตรงกับสมมติฐานเดิมของระบบ (ไฟดับ/รีสตาร์ท = ถือว่าแอร์ปิด, UC-09) ไม่ต้อง persist `setTemp` เพิ่ม
- **การชนกันของ RX (เรียนรู้) กับ TX (ยิงคำสั่ง)**: ขณะ `irLearnActive()` เป็น true ให้ `irSend()` ข้ามการยิงทุกชนิดเงียบๆ
  (รอบ `Controller::tick()`/`applyCommand()` หน้าถัดไปจะคำนวณ decision ใหม่เองอยู่แล้วทุก 1 วิ ไม่ต้องรีบ) และห้ามรับคำสั่ง
  `IrLearnStart` ใหม่ถ้ามี burst TX ค้างอยู่ (เช็คจาก state mach��นข้อบน) — คืนค่า "กำลังส่งคำสั่งอยู่ ลองใหม่อีกครั้ง" ผ่าน `irLearnStatus`
- **บัฟเฟอร์ raw เกิน 150 ตัว**: ถ้า `results.rawlen > 150` ให้ถือเป็นเรียนรู้ล้มเหลว (`irLearnStatus = "too_long:<button>"`)
  ไม่บันทึกลง NVS — ไม่ขยายบัฟเฟอร์เพิ่มในรอบแรก (รีโมทแอร์ส่วนใหญ่อยู่ในช่วงนี้; ถ้าเจอปัญหาจริงค่อยขยาย)
- **`IrLearnStart`/`IrLearnCancel` เข้า `cmdQueue` เหมือนคำสั่งอื่นจริง** แต่ dispatch ในลูปหลักของ `control_task.cpp`
  ด้วยการเช็ค `cmd.type` ก่อนถึง `controller.applyCommand()` เหมือนที่ `HwIrTest` ทำอยู่แล้วตอนนี้ (ดู `control_task.cpp` บรรทัด
  "ปุ่มทดสอบ IR ดิบจากหน้าจอเครื่องมือช่าง — ไม่ผ่าน Controller") — รูปแบบเดียวกันเป๊ะ ไม่มีอะไรต่าง

## สิ่งที่ปล่อยให้ผู้ลงมือทำตัดสินใจเอง

- ชื่อฟังก์ชัน/ไฟล์ย่อยที่ไม่กระทบ interface ข้างบน
- ค่า timeout การเรียนรู้ที่แน่นอน (แนะนำ 15 วิ, ปรับได้)
- ข้อความ UI ที่แน่นอนในหน้าเว็บ (ยึดโทนเดิมของโปรเจกต์)
- จะ reset `irLearned`/ลบรหัสเดิมเมื่อเรียนรู้ปุ่มเดิมซ้ำหรือเขียนทับเลย (แนะนำ: เขียนทับเลย ไม่ต้องยืนยัน)
