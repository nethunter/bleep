# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: ADR-013 multi-device foundation tranche; physical Shark
  regression gate pending.
- Firmware state: Home-first, persistent device registry, and on-demand Shark
  firmware built, host-tested, and flashed.
- Universal driver framework: Bounded core implemented for one compiled Shark
  driver and one active Shark instance; broader transports/drivers not started.
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

Complete the combined Phase 0/foundation hardware gate:

1. Visually verify Home, Devices, rename keyboard, enable/disable, remove/add,
   and persistence across a power cycle.
2. Verify that boot and Home perform no BLE scan or connection.
3. Exercise on-demand Shark pairing and reconnect while its screen is active.
4. Exercise keypoints, timing, manual movement, run controls, and safe Back.
5. Exercise long-press sleep/wake and record the connected minimum heap.

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

### Multi-device foundation build

- Date: 2026-08-03.
- PlatformIO environment: `crowpanel_128`.
- Firmware flash usage: 822,672 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,284 / 327,680 bytes (35.8%).
- Roboto profile flash usage: 792,200 / 3,145,728 bytes (25.2%).
- Home boot free heap: 176,044 bytes at 915 ms.
- Home boot minimum free heap: 173,604 bytes.
- Boot link state: `disconnected`; NimBLE is not initialized until a device is
  opened.
- LVGL object heap: increased from 32 KiB to 48 KiB for Home, device management,
  and the rename keyboard. The first 32 KiB build exhausted the UI heap before
  boot telemetry and was rejected.
- Build result: `crowpanel_128` and `crowpanel_128_roboto` succeeded.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical device controls and persistence workflow: Operator verification
  pending.

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

- Shark Nano II: Panel firmware boots Home with link `disconnected`; on-demand
  pairing, persistence UI, movement, control, and sleep checklist still requires
  operator verification with the slider powered and safe to move.
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

### 2026-08-03: Multi-device Shark foundation

- Recorded ADR-013 to advance selected Phase 2/3/7 foundation work while Phase
  0 hardware and Phase 1 feasibility gates remain open.
- Added a compile-time Shark driver catalog, fixed-capacity runtime registry,
  versioned checked persistence, legacy pairing migration, typed command/result
  queues, and a loop-owned `DeviceManager`.
- Adapted Shark behind an on-demand driver lifecycle; boot and Home do not
  initialize, scan, pair, or reconnect BLE.
- Added Home, Devices, add/rename/enable/disable/forget/remove workflows and
  retained the specialized Shark connect, keypoint, positioning, and run UI.
- Expanded native coverage from 6 to 12 tests for catalog, registry, dormant
  records, corruption handling, migration, routing, disabled devices, and empty
  registry persistence.
- Built both panel profiles, flashed the default profile, and captured Home boot
  heap. Physical operator verification remains open.

### 2026-08-03: Desktop LVGL UI simulator

- Added PlatformIO `ui_sim` host environment that compiles the real `ui` /
  `shark_ui` sources against LVGL with Arduino/device stubs.
- Captures round 240x240 PNGs to `sim/screenshots/` via ImageMagick (`magick`)
  without flashing: Home, Devices, manage, rename keyboard, Shark connect,
  keypoints, and run.
- Simulator uses `LV_COLOR_16_SWAP=0` and a 128 KiB LVGL heap (firmware keeps
  48 KiB). Build and capture succeeded locally.

### 2026-08-03: Icon Home screen

- Generated Devices/Groups/Scenes/Portal glyphs with Gemini 2.5 Flash Image
  (Nano Banana) via Vertex AI after the AI Studio key used by the nano-banana
  MCP was blocked (`API_KEY_SERVICE_BLOCKED`).
- Embedded 48x48 `ALPHA_8BIT` icons (`tools/gen_icons.py`) and rebuilt Home as
  a 2x2 mode tile grid; Devices is active, other modes are disabled placeholders.
- Enabled `LV_USE_IMG`. Sim capture and `crowpanel_128` flash used for verify.

### 2026-08-03: Colorful Nano Banana Pro Home icons

- Regenerated fun colorful mode icons with `gemini-3-pro-image-preview`
  (Nano Banana Pro). The nano-banana MCP still calls retired
  `gemini-2.5-flash-image-preview`, so generation used the Gemini API directly.
- Switched embeds to `LV_IMG_CF_TRUE_COLOR_ALPHA` with swap0/swap1 branches so
  colors survive on both `ui_sim` and the panel. Disabled modes dim via opacity.
- Sim capture + `crowpanel_128` flash succeeded.

### 2026-08-03: Compact paged rename input

- Replaced the rectangular LVGL QWERTY keyboard with a round-native 3x3 keypad.
  Arrow buttons or horizontal swipes change between A-I, J-R, S-Z, and
  number/symbol pages.
- Character keys insert directly; Space, backspace, and case controls remain
  available. Icon Save/Cancel controls sit beside the name field.
- `ui_sim` rebuilt and captured `04_rename.png`; `crowpanel_128` built and
  flashed successfully (854,686 bytes flash, 117,644 bytes static RAM).
- `crowpanel_128_roboto` also built successfully (824,214 bytes flash, 117,644
  bytes static RAM). Physical keypad interaction remains operator-pending.

