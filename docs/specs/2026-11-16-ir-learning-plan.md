# IR Learning (Universal Remote) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a room whose AC isn't supported by `IRac` learn 4 raw IR codes (power on, power off, temp up, temp down) from its real remote, store them on-device, and control that room's AC from the web the same way Haier rooms already work (power + temperature only — mode/fan stay manual for these rooms).

**Architecture:** Add a parallel "Learned" IR send path alongside the existing `IRac`/`HAIER_AC176` "Protocol" path in `control_task.cpp`, selected per-device by a new `Settings.irMode` field. A new non-blocking `ir_learn` module owns the `IRrecv` receiver and a tick-polled learn/transmit state machine so the Control task never blocks. Learned codes persist in NVS. The web side reuses the existing `setSetting` RPC channel (no new RPC method) and reads new shared attributes to show per-button learn status.

**Tech Stack:** PlatformIO/Arduino C++ (ESP32-S3), IRremoteESP8266 (`IRrecv`/`IRsend`), Preferences (NVS), React + TypeScript (Vite), ThingsBoard REST.

**Spec:** `/Users/pp_.bear/Documents/AIR/Firmware/AutoAC/docs/specs/2026-11-16-ir-learning-design.md`

## Global Constraints

- Only the Control task may touch IR hardware (`CLAUDE.md` architecture rule) — all new IR learn/send code lives in `control_task.cpp` + the new `ir_learn.*` module, called only from the Control task.
- All commands flow through `app::cmdQueue` (`CLAUDE.md`) — `IrLearnStart`/`IrLearnCancel` are `Command` entries like every other command.
- No task may block waiting on IR RX or TX — both RX listening and multi-press TX bursts are tick-polled state machines, never `vTaskDelay`-loops longer than the existing single 400ms Protocol-mode gap.
- Never log tokens/passwords to Serial (`CLAUDE.md`) — not touched by this feature, but no new code may violate it.
- Raw capture buffer is capped at 150 samples (design doc "รายละเอียดเพิ่มเติม"); longer captures are a learn failure (`too_long:<button>`), not a crash or truncation.
- Mode/fan controls are hidden in the web UI for rooms in Learned mode (design doc Non-goals) — never sent as IR in that mode.
- `Settings`/NVS changes must not alter existing Protocol-mode (Haier) behavior in any way — Haier rooms keep `irMode = Protocol` and see zero behavior change.

## Review Focus

- **Learn request while a previous learn session is still active (same or different button):** a reasonable admin expects either a clear "busy" status or the second request replacing the first — not silent code corruption or two interleaved RX sessions.
- **`irMode` toggled to Learned before any button is learned:** pressing on/off/temp in the web must not crash or silently brick the room — `irSend()` must skip unlearned buttons and report nothing happened, not read garbage NVS data.
- **Temp jump larger than the 14-press cap (e.g. 16°C → 30°C in one web action):** must clamp to the cap, not try to send 14 presses then silently drop the rest without any indication, and must not wrap/underflow the press-count arithmetic.
- **`irLearnCancel` arriving with no learn session active:** must be a no-op, not a crash or an errant state transition.
- **Raw capture exactly at/over the 150-sample boundary:** off-by-one here corrupts NVS or accepts a code that will misfire later — needs an explicit boundary test, not just "large capture roughly works."

---

## File Structure

**Firmware (`/Users/pp_.bear/Documents/AIR/Firmware/AutoAC/`):**
- Modify `lib/autoac_core/types.h` — add `IrMode`, `IrButton`, new `Command::Type` values, `Settings.irMode`.
- Create `src/ir_learn.h` / `src/ir_learn.cpp` — `IRrecv` ownership, `LearnedIrCode` storage struct, non-blocking learn state machine.
- Modify `src/nvs_store.h` / `src/nvs_store.cpp` — add `nvsLoadIrCodes()`, `nvsSaveIrCode(IrButton)`.
- Modify `src/app.h` / `src/app.cpp` — add shared `app::irCodes[4]` array + getters (Control task writes, others read-only per existing convention).
- Modify `src/control_task.cpp` — dispatch `IrLearnStart`/`IrLearnCancel`, branch `irSend()` by `irMode`, non-blocking Learned-mode TX burst state machine, extend `applySetSetting` for `"irMode"`.
- Modify `src/net_task.cpp` — extend `onMqttMessage` to turn `setSetting` keys `"irLearnStart"`/`"irLearnCancel"` into the new `Command::Type`s instead of persisted settings; extend `publishAttributes()` with `irMode`, `irLearned`, `irLearnStatus`.

**Web (`/Users/pp_.bear/Documents/AIR/Wabapp/`):**
- Modify `src/types.ts` — extend `DeviceSettings` with `irMode?`, `irLearned?`, `irLearnStatus?`.
- Modify `src/hooks/useTelemetry.ts` — map the 3 new attributes into `DeviceSettings`.
- Modify `src/pages/Settings.tsx` — add IR mode toggle + 4-button learn UI (admin only); hide mode/fan controls on `Room.tsx` when `irMode === 'learned'`.
- Modify `src/pages/Room.tsx` — hide mode/fan `seg` groups when the room's `irMode` is Learned.

No native unit tests change behavior for `Controller.h`/`types.h`'s existing logic — `irMode` is read only inside `control_task.cpp`, never inside `Controller`, so `test/test_controller.cpp` needs no new cases for this feature. Firmware verification for the new module is manual (flash + serial log), documented as explicit steps in each task since there is no HIL rig in this project.

---

### Task 1: Core types for IR learning

**Files:**
- Modify: `lib/autoac_core/types.h`

**Interfaces:**
- Produces: `enum class IrMode : uint8_t { Protocol = 0, Learned = 1 }`; `enum class IrButton : uint8_t { PowerOn = 0, PowerOff = 1, TempUp = 2, TempDown = 3 }`; `constexpr int IR_BUTTON_COUNT = 4`; `Command::Type::IrLearnStart`, `Command::Type::IrLearnCancel` (both use `cmd.intArg` to carry the `IrButton` index for `IrLearnStart`; `IrLearnCancel` ignores `intArg`); `Settings.irMode` (default `IrMode::Protocol`).

- [ ] **Step 1: Add `IrMode` and `IrButton` enums + `IR_BUTTON_COUNT` to `lib/autoac_core/types.h`**, placed near the existing `AcMode`/`FanSpeed` enums (top of file).

- [ ] **Step 2: Add `IrLearnStart, IrLearnCancel` to `Command::Type`** in the existing enum list in `types.h`, right after `HwIrTest`, with a one-line comment matching the file's existing comment style (Thai, explaining `intArg` carries the `IrButton` for `IrLearnStart`).

- [ ] **Step 3: Add `IrMode irMode = IrMode::Protocol;` field to `Settings` struct** in `types.h`, after the existing `preOpenMin` field.

- [ ] **Step 4: Build the native test env to confirm nothing broke**

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio test -e native`
Expected: all 9 existing tests still PASS (no new tests needed — `Controller` never reads `irMode`).

- [ ] **Step 5: Commit**

```bash
git add lib/autoac_core/types.h
git commit -m "feat(ir-learn): add IrMode/IrButton types and learn command types"
```

---

### Task 2: `ir_learn` module — non-blocking RX capture + storage struct

**Files:**
- Create: `src/ir_learn.h`
- Create: `src/ir_learn.cpp`

**Interfaces:**
- Consumes: `PIN_IR_RX` from `pins.h` (value `7`); `autoac::IrButton`, `autoac::IR_BUTTON_COUNT` from Task 1.
- Produces:
  - `struct LearnedIrCode { uint16_t raw[150]; uint8_t len = 0; bool learned = false; };`
  - `void irLearnBegin();` — call once at boot, sets up `IRrecv` on `PIN_IR_RX`.
  - `bool irLearnActive();` — true while a learn session is in progress.
  - `void irLearnStart(autoac::IrButton b, uint32_t timeoutMs = 15000);` — no-op (and does not reset any existing session) if `irLearnActive()` is already true for a different session; starting with the same button again while idle is allowed and resets that button's session.
  - `void irLearnCancel();` — no-op if nothing is active.
  - `bool irLearnPoll(bool& outOk, autoac::IrButton& outButton, LearnedIrCode& outCode);` — call every Control task tick; returns `true` exactly once when a session ends (success, timeout, or `too_long`), filling `outOk`, `outButton`, and `outCode` (only meaningful when `outOk` is true). Returns `false` every other tick, including when idle.

- [ ] **Step 1: Write `src/ir_learn.h`** declaring the struct and the 5 functions above, with `#pragma once` and includes for `<cstdint>` and `"../lib/autoac_core/types.h"`.

- [ ] **Step 2: Implement `src/ir_learn.cpp`**
  - `#include <IRrecv.h>`, `#include <IRutils.h>`, `"ir_learn.h"`, `"pins.h"`.
  - Module-static `IRrecv irrecv(PIN_IR_RX, 1024, 15, true);` (buffer size/timeout per IRremoteESP8266 conventions) and module-static session state: `bool g_active`, `autoac::IrButton g_button`, `uint32_t g_deadlineMs`.
  - `irLearnBegin()` calls `irrecv.enableIRIn()`.
  - `irLearnStart()`: if `g_active` is already true, return immediately (busy — caller/`control_task.cpp` is responsible for surfacing "busy" via `irLearnStatus`, this function itself just refuses silently per its no-op contract). Otherwise set `g_active = true`, `g_button = b`, `g_deadlineMs = millis() + timeoutMs`, and call `irrecv.resume()` to clear any stale buffered signal.
  - `irLearnCancel()`: set `g_active = false` unconditionally (no-op if already false).
  - `irLearnPoll()`: if `!g_active`, return `false`. Else call `irrecv.decode(&results)`; on success, check `results.rawlen > 150` → `outOk = false` (caller treats as `too_long`), else copy `results.rawbuf`/`rawlen` into `outCode`, set `outCode.learned = true`, `outOk = true`; either way set `g_active = false`, `outButton = g_button`, call `irrecv.resume()`, return `true`. If decode did not succeed and `millis() >= g_deadlineMs`, treat as timeout: `g_active = false`, `outOk = false`, `outButton = g_button`, return `true`. Otherwise return `false`.

- [ ] **Step 3: Add `#include "ir_learn.h"` wiring into `platformio.ini` — no change needed** (IRremoteESP8266 already in `lib_deps`); confirm by building.

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio run -e autoac_bench`
Expected: SUCCESS (compiles; `ir_learn.cpp` not yet called from anywhere, that's Task 4).

- [ ] **Step 4: Commit**

```bash
git add src/ir_learn.h src/ir_learn.cpp
git commit -m "feat(ir-learn): add non-blocking IR learn capture module"
```

---

### Task 3: NVS persistence for learned codes

**Files:**
- Modify: `src/nvs_store.h`
- Modify: `src/nvs_store.cpp`

**Interfaces:**
- Consumes: `LearnedIrCode`, `autoac::IrButton`, `autoac::IR_BUTTON_COUNT` from Tasks 1–2.
- Produces: `void nvsLoadIrCodes(LearnedIrCode out[autoac::IR_BUTTON_COUNT]);` and `void nvsSaveIrCode(autoac::IrButton b, const LearnedIrCode& code);`, both declared in `nvs_store.h`.

- [ ] **Step 1: Declare both functions in `src/nvs_store.h`**, with a one-line Thai comment matching the file's existing style (e.g. "เก็บรหัส IR ที่เรียนรู้มา ต่อปุ่ม คีย์ ir0..ir3").

- [ ] **Step 2: Implement in `src/nvs_store.cpp`**
  - `nvsSaveIrCode`: open the existing `Preferences` namespace this file already uses, `putBytes("ir0"..."ir3", &code, sizeof(LearnedIrCode))` keyed by `(int)b`, close.
  - `nvsLoadIrCodes`: for each of the 4 buttons, `getBytes` into `out[i]` if the key exists and the stored size matches `sizeof(LearnedIrCode)`; otherwise leave `out[i]` as a default-constructed (`learned = false`) `LearnedIrCode`.
  - Call `nvsLoadIrCodes` from the existing `nvsLoadAll()` in the same file.

- [ ] **Step 3: Build to confirm it compiles**

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio run -e autoac_bench`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/nvs_store.h src/nvs_store.cpp
git commit -m "feat(ir-learn): persist learned IR codes in NVS"
```

---

### Task 4: Shared state for learned codes

**Files:**
- Modify: `src/app.h`
- Modify: `src/app.cpp`

**Interfaces:**
- Consumes: `LearnedIrCode`, `autoac::IR_BUTTON_COUNT` from Tasks 1–2.
- Produces: `app::irCodes` accessors — `const LearnedIrCode& app::irCode(autoac::IrButton b);` and `void app::setIrCode(autoac::IrButton b, const LearnedIrCode& code);`, following the existing read/write-ownership comment convention in `app.h` (Control task writes, `net_task`/`ui_task` read-only).

- [ ] **Step 1: Add the two function declarations to `src/app.h`**, next to the existing `setLastAcState`/`lastAcState` pair, with the same ownership comment style.

- [ ] **Step 2: Implement in `src/app.cpp`** backed by a module-static `LearnedIrCode g_irCodes[autoac::IR_BUTTON_COUNT];` array, populated once at boot by calling `nvsLoadIrCodes(g_irCodes)` from `app::initShared()` (existing function in this file).

- [ ] **Step 3: Build to confirm it compiles**

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio run -e autoac_bench`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/app.h src/app.cpp
git commit -m "feat(ir-learn): add shared learned-IR-code state"
```

---

### Task 5: Wire learn dispatch + Learned-mode `irSend()` into Control task

**Files:**
- Modify: `src/control_task.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–4 (`ir_learn.h`, `nvs_store.h`'s `nvsSaveIrCode`, `app::irCode`/`setIrCode`, `autoac::IrMode`/`IrButton`).
- Produces: nothing new consumed by later tasks — this task's effect is observable firmware behavior, verified by flashing.

- [ ] **Step 1: Call `irLearnBegin()` once** in `controlTaskLoop()`'s setup section (next to the existing `dht.begin()` call).

- [ ] **Step 2: Dispatch `IrLearnStart`/`IrLearnCancel` in the `cmdQueue` drain loop**, in the same `if (cmd.type == ...) { ...; continue; }` chain that already handles `HwIrTest` (same pattern — do not route through `controller.applyCommand()`):
  - `IrLearnStart`: if a Learned-mode TX burst (see Step 4) is currently pending, do nothing but `publishEvent`/log a "busy" reason; else call `irLearnStart(static_cast<autoac::IrButton>(cmd.intArg))`.
  - `IrLearnCancel`: call `irLearnCancel()`.

- [ ] **Step 3: Poll `irLearnPoll()` once per loop iteration** (after the `cmdQueue` drain, before the sensor-read block). On a `true` return: if `outOk`, call `app::setIrCode(outButton, outCode)` and `nvsSaveIrCode(outButton, outCode)`; either way, `publishEvent("ir_learn", "web", <status string>)` and `logEvent(...)` with the status (`"ok"`, `"timeout"`, or `"too_long"`) — this status is also what `net_task.cpp` (Task 6) will read back out via a new `app::` getter; add `const char* app::lastIrLearnStatus(); void app::setLastIrLearnStatus(const char*);` to `app.h`/`app.cpp` (small addition, same file-pair as Task 4) storing a format like `"ok:power_on"`/`"timeout:temp_up"`/`"too_long:temp_down"`/`"idle"` (default `"idle"`).

- [ ] **Step 4: Replace the single `irSend()` call sites with a mode branch.** Keep the existing `irSend(bool on, uint8_t setTemp, AcMode mode, FanSpeed fan)` function body exactly as-is (this is the Protocol path, used when `app::settings.irMode == autoac::IrMode::Protocol`). Add a new function in the same file:
  - `static void irSendLearned(const autoac::Controller::Decision& d, const autoac::AcState& ac);` — called instead of `irSend(...)` when `app::settings.irMode == autoac::IrMode::Learned`, from `applyDecision()`.
  - Inside `irSendLearned`, maintain module-static burst state: `static int g_pendingPresses = 0; static autoac::IrButton g_pendingButton; static uint32_t g_nextPressMs = 0;`. On a `d.sendOn`/`d.sendOff`, if a previous burst is still pending, drop the new request (log it) rather than interrupting the burst. On `d.sendOn`: if `app::irCode(PowerOn).learned`, transmit it once immediately via `IRsend::sendRaw()` (module-static `IRsend irSendDev(PIN_IR_TX);` mirroring the existing `irAc` pattern) and log; else log "ยังไม่เรียนรู้ปุ่มเปิด — ข้าม". Same for `d.sendOff`/`PowerOff`.
  - On `d.updateSetTemp`: compute `delta = (int)d.setTemp - (int)ac.setTemp` using the Controller's *previous* tracked value (pass the pre-update `ac_` snapshot in — `applyDecision()` already has `controller.state()` before and after; capture the "before" value before calling into Controller in this call site) — actually simpler: `applyDecision()` already calls `controller.state()` *after* the decision; add a second AC-state parameter to `applyDecision()`/`irSendLearned()` carrying the delta directly as an `int8_t tempDelta` computed by the caller (see Step 5 below) so `irSendLearned` does not need to re-derive it. Set `g_pendingPresses = clamp(abs(tempDelta), 0, 14)`, `g_pendingButton = tempDelta > 0 ? TempUp : TempDown`, `g_nextPressMs = millis()`. If either temp button isn't learned, log and set `g_pendingPresses = 0`.
  - Add a tick function `static void irSendLearnedTick();` called once per Control task loop (same place `irLearnPoll()` is polled): if `g_pendingPresses > 0 && millis() >= g_nextPressMs`, transmit `app::irCode(g_pendingButton)` once, decrement `g_pendingPresses`, set `g_nextPressMs = millis() + 400`.
  - `irLearnActive()` must return `false`-equivalent gating: at the top of `irSendLearnedTick()` and before honoring any new `d.sendOn`/`sendOff`/temp request, if `irLearnActive()` is true, skip entirely for this tick (RX/TX never run in the same tick) — this satisfies the design doc's concurrency rule.

- [ ] **Step 5: Update `applyDecision()`** to branch on `app::settings.irMode` instead of calling `irSend()` unconditionally, and to compute the `tempDelta` described in Step 4 before calling `irSendLearned` (using the `AcState` snapshot `applyDecision()` already takes).

- [ ] **Step 6: Flash and manually verify on hardware** (no automated test — documented per Global Constraints there is no HIL rig):
  - Set a room's `irMode` to `Learned` via a temporary `setSetting` RPC (or hardcode `true` briefly) — confirm via serial log that `irSend()`'s Protocol path is NOT invoked, `irSendLearned` IS, and unlearned-button presses log "ยังไม่เรียนรู้..." without crashing.

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio run -e autoac --target upload`
Expected: SUCCESS; serial monitor shows the expected skip-log lines when toggling power with no codes learned yet.

- [ ] **Step 7: Run native tests once more to confirm `Controller.h` itself is unaffected**

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio test -e native`
Expected: all 9 tests still PASS.

- [ ] **Step 8: Commit**

```bash
git add src/control_task.cpp src/app.h src/app.cpp
git commit -m "feat(ir-learn): wire learn dispatch and Learned-mode IR send into Control task"
```

---

### Task 6: ThingsBoard bridge — learn commands via `setSetting`, new attributes

**Files:**
- Modify: `src/net_task.cpp`

**Interfaces:**
- Consumes: `Command::Type::IrLearnStart/IrLearnCancel` from Task 1; `app::settings.irMode`, `app::irCode(...)`, `app::lastIrLearnStatus()` from Tasks 4–5.
- Produces: shared attributes `irMode` (0/1), `irLearned` (string, e.g. `"1,1,0,0"` for power_on,power_off,temp_up,temp_down learned/not), `irLearnStatus` (string) — consumed by the web in Task 7.

- [ ] **Step 1: In `onMqttMessage`'s `setSetting` branch**, before the generic key/value handling, special-case `key == "irLearnStart"` → build a `Command` with `type = Command::Type::IrLearnStart`, `intArg = (int)params["value"]`, push to `app::cmdQueue`, return (skip the normal `applySetSetting` path entirely since this must not be persisted as a setting). Same for `key == "irLearnCancel"` → `Command::Type::IrLearnCancel`.

- [ ] **Step 2: Extend `publishAttributes()`** to add 3 fields to the existing `StaticJsonDocument`: `doc["irMode"] = (int)app::settings.irMode;`, `doc["irLearned"] = <4-char "0"/"1" CSV built from app::irCode(b).learned for b in 0..3>;`, `doc["irLearnStatus"] = app::lastIrLearnStatus();`.

- [ ] **Step 3: Build to confirm it compiles**

Run: `cd /Users/pp_.bear/Documents/AIR/Firmware/AutoAC && python3 -m platformio run -e autoac_bench`
Expected: SUCCESS.

- [ ] **Step 4: Flash and manually verify**: toggle `irMode` via web (Task 7, once built) or a raw RPC, confirm the 3 new attributes appear and update in the ThingsBoard device's attribute viewer.

- [ ] **Step 5: Commit**

```bash
git add src/net_task.cpp
git commit -m "feat(ir-learn): bridge learn commands and status to ThingsBoard"
```

---

### Task 7: Web — types + telemetry hook for the 3 new attributes

**Files:**
- Modify: `src/types.ts`
- Modify: `src/hooks/useTelemetry.ts`

**Interfaces:**
- Consumes: ThingsBoard attributes `irMode`, `irLearned`, `irLearnStatus` from Task 6.
- Produces: `DeviceSettings.irMode?: 'protocol' | 'learned'`, `DeviceSettings.irLearned?: boolean[]` (length 4, indexed by `IrButton`), `DeviceSettings.irLearnStatus?: string` — consumed by Task 8.

- [ ] **Step 1: Extend `DeviceSettings` in `src/types.ts`** with the 3 fields above.

- [ ] **Step 2: In `useTelemetry.ts`'s `refresh()`**, map `attrs.irMode` (`0`/`1`) to `'protocol'`/`'learned'`, parse `attrs.irLearned` (CSV string) into a 4-element `boolean[]` (`split(',').map(c => c === '1')`, defaulting to `[false,false,false,false]` if absent), and pass through `attrs.irLearnStatus` as a plain string (default `'idle'`).

- [ ] **Step 3: Build to confirm no TS errors**

Run: `cd /Users/pp_.bear/Documents/AIR/Wabapp && npm run build`
Expected: builds cleanly.

- [ ] **Step 4: Commit**

```bash
git add src/types.ts src/hooks/useTelemetry.ts
git commit -m "feat(ir-learn): read IR learn attributes into telemetry hook"
```

---

### Task 8: Web — Settings page learn UI + hide mode/fan on Learned rooms

**Files:**
- Modify: `src/pages/Settings.tsx`
- Modify: `src/pages/Room.tsx`

**Interfaces:**
- Consumes: `DeviceSettings.irMode/irLearned/irLearnStatus` from Task 7; existing `useRpc().send('setSetting', { key, value })` pattern (already used for `emptyMin` etc.).

- [ ] **Step 1: In `Settings.tsx`, add an "โหมด IR" toggle** (admin-only, same `disabled={!isAdmin || sending}` pattern as the existing `autoOn` toggle) calling `send('setSetting', { key: 'irMode', value: s.irMode === 'learned' ? 0 : 1 })`.

- [ ] **Step 2: When `s.irMode === 'learned'`, render 4 rows** (เปิด/ปิด/อุณหภูมิ+/อุณหภูมิ-, in `IrButton` order `0..3`), each with:
  - a badge: "เรียนรู้แล้ว" (green) if `s.irLearned?.[i]`, else "ยังไม่เรียนรู้" (เหมือน badge แดง pattern ที่มีอยู่แล้วในหน้า `Rooms.tsx`)
  - a "เรียนรู้" button calling `send('setSetting', { key: 'irLearnStart', value: i })`
  - while `s.irLearnStatus` starts with `'listening:'` and its suffix matches this button's name, show "กำลังรอสัญญาณ... เล็งรีโมทแล้วกด" and a "ยกเลิก" button calling `send('setSetting', { key: 'irLearnCancel', value: 0 })`
  - rely on the existing `useTelemetry` 4-second poll (`refresh` is already called automatically) to pick up status changes — no new polling logic needed.

- [ ] **Step 3: In `Room.tsx`, wrap the existing `AC_MODES`/`AC_FANS` `control-group` blocks** in `{settings?.irMode !== 'learned' && (...)}` — `Room.tsx` currently gets `telemetry`/`active`/etc. from `useTelemetry`; add `settings` to its existing destructure of that hook's return value.

- [ ] **Step 4: Build**

Run: `cd /Users/pp_.bear/Documents/AIR/Wabapp && npm run build`
Expected: builds cleanly.

- [ ] **Step 5: Manual verification in dev server**: `npm run dev -- --port 5181 --host`, log in as Admin, open Settings for a room, toggle IR mode to Learned, confirm the 4 learn rows appear with "ยังไม่เรียนรู้" badges and Room.tsx's mode/fan rows disappear.

- [ ] **Step 6: Commit**

```bash
git add src/pages/Settings.tsx src/pages/Room.tsx
git commit -m "feat(ir-learn): add IR learn UI and hide mode/fan for learned-mode rooms"
```

---

## Self-Review

1. **Spec coverage:** 4-button learn (Task 2/5/8) ✓, per-button NVS persistence (Task 3) ✓, `irMode` toggle (Task 1/6/8) ✓, non-blocking RX (Task 2) and non-blocking TX burst (Task 5) per Anti-goals ✓, mode/fan hidden for Learned rooms (Task 8) ✓, no new RPC method / reuse of `setSetting` (Task 6) ✓, buffer-overflow → `too_long` (Task 2/5) ✓, RX/TX mutual exclusion (Task 5) ✓, setTemp-after-reboot behavior unchanged (no task touches `Controller::begin()`, confirmed intentional) ✓.
2. All steps carry an exact signature, file, and command/assertion; no "TBD"/"handle edge cases" lines remain.
3. Type names checked consistent across tasks: `LearnedIrCode`, `IrButton`, `IrMode`, `app::irCode`/`app::setIrCode`, `nvsLoadIrCodes`/`nvsSaveIrCode`, `irLearnBegin/Start/Cancel/Active/Poll` — same names used wherever referenced in later tasks.
4. Review Focus items each map to an explicit step: busy-session (Task 2 Step 2 + Task 5 Step 2), unlearned-button safety (Task 5 Step 4), temp-jump clamp (Task 5 Step 4), cancel-when-idle (Task 2 Step 2), 150-sample boundary (Task 2 Step 2, Task 5 Step 6 manual check — flagged as the one item that is a manual/hardware check rather than an automated test, since there is no native-testable path for raw IR capture).
5. Proportion: plan is comparable in length to the design doc it implements (one is prose/behavior, the other is file/task breakdown); no step duplicates full function bodies — algorithmic bodies are left as prose per Task 2/5's existing-pattern callouts.
