# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: Phase 0; software baseline complete, hardware behavior gate pending.
- Firmware state: Baseline preserved, host-tested, built, and flashed.
- Universal driver framework: Not started.
- Last updated: 2026-08-03.

## Completed planning

- Defined compile-time Kconfig/menuconfig driver selection.
- Separated compiled drivers from runtime device instances.
- Defined shared light, camera, motion, and recorder capabilities.
- Defined runtime enable/disable, configuration, and capability-safe groups.
- Selected direct control on the ESP32-C3 rather than an external gateway.
- Selected both panel-owned and imported Amaran mesh onboarding.
- Selected Canon BR-E1-compatible Bluetooth plus CCAPI HTTP.
- Defined ordered scenes with non-blocking waits, generated inverse Stop, and
  explicit Stop override.
- Reserved future recorder drivers for Tascam Portacapture X8 Bluetooth and
  Deity PR4.
- Selected a neutral Home screen with on-demand device connections and no
  automatic Shark pairing/reconnect at boot.
- Selected a dedicated Portal mode that temporarily runs a WPA2 SoftAP and HTTP
  server, then releases them completely on exit.

## Next task

Complete the Phase 0 hardware gate:

1. Exercise Shark pairing and saved-device reconnect.
2. Exercise keypoint set/go/delete and speed/hold readback.
3. Exercise manual movement, standby/start/stop, loop, and direction.
4. Exercise long-press sleep and long-press wake.
5. Record observed behavior and any minimum-heap reduction after the test.

## Measurements

Record values with the exact build environment and commit/worktree state.

### Baseline Shark build

- Date: 2026-08-03.
- PlatformIO environment: `crowpanel_128`.
- Baseline firmware flash usage: 807,946 / 3,145,728 bytes (25.7%).
- Baseline static RAM usage: 99,252 / 327,680 bytes (30.3%).
- Instrumented firmware flash usage: 808,962 / 3,145,728 bytes (25.7%).
- Instrumented static RAM usage: 99,276 / 327,680 bytes (30.3%).
- Free heap after BLE scan startup: 144,392 bytes at 922 ms.
- Minimum free heap after BLE scan startup: 144,348 bytes.
- Build result: Success with espressif32 7.0.1.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Host tests: 6/6 passed in the PlatformIO `native` environment.
- Worktree: Firmware sources initially matched `HEAD`; Phase 0 test,
  extraction, scanner-hardening, and telemetry changes were then applied.

### Full coexistence spike

- Date: Not started.
- Enabled drivers/transports: Not started.
- Flash usage: Not measured.
- Static RAM usage: Not measured.
- Free/minimum heap: Not measured.
- Stability duration: Not measured.
- Result: Not started.

### Dedicated Portal mode spike

- Date: Not started.
- SoftAP security and address: Not implemented.
- Portal entry/exit: Not tested.
- Studio Wi-Fi suspension/restoration: Not tested.
- Server/AP teardown: Not tested.
- Repeated-cycle memory recovery: Not measured.
- Result: Not started.

## Hardware verification

- Shark Nano II: Panel boots and begins scanning; movement and control checklist
  still requires operator verification with the slider powered and safe to move.
- Amaran Pano 60c: Not tested.
- Amaran Pano 120c: Not tested.
- Amaran Ace 25c: Not tested.
- Canon EOS R6: Not tested.
- Canon EOS R6 Mark II: Not tested.
- Canon EOS R6 Mark III: Not tested.
- Tascam Portacapture X8 Bluetooth: Future research.
- Deity PR4: Future research.

## Blockers and risks

- ESP32-C3 memory headroom for LVGL, Wi-Fi, BLE Mesh, and multiple GATT links is
  unknown.
- Direct BLE Mesh provisioning and custom GATT coexistence are not yet proven
  with the selected stack.
- SoftAP/HTTP Portal mode teardown and memory recovery are not yet proven.
- Amaran and recorder protocols rely on external or future research and require
  target-hardware validation.

## Session log

### 2026-08-03: Durable roadmap documentation

- Added the documentation index, architecture, implementation phases, device
  matrix, scene model, decision log, and progress handoff.
- No firmware code or build configuration was changed.
- No build or flash was run because this session documented the plan only.

### 2026-08-03: Startup and Portal-mode revision

- Changed startup to a neutral Home menu with connections initiated by device
  screens or scenes.
- Superseded USB serial networking with a dedicated temporary WPA2 SoftAP and
  HTTP server.
- Required Portal mode to suspend normal control and release all server/AP
  resources on exit or inactivity timeout.

### 2026-08-03: Phase 0 software baseline

- Built and flashed the preserved firmware before refactoring; recorded its
  flash and static RAM usage.
- Extracted notification state reduction into Arduino-independent
  `shark_state.*`.
- Added six native tests covering CRC/frame encoding, fragmented and malformed
  stream scanning, command builders, timing edits, state reduction, and reset.
- Fixed frame scanning across a notification split after the first `0xAA` byte
  and bounded malformed declared lengths.
- Added boot, link-transition, and periodic free/minimum-heap telemetry.
- Built and flashed the instrumented firmware and captured startup heap.
- Phase 0 remains open until the physical Shark behavior checklist passes.

