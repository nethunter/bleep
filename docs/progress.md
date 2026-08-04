# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: ADR-014 Canon Trigger verification, ADR-015 Canon Smart
  handoff research, and ADR-016 Tascam X8 record-control hardware verification;
  physical regression gates pending.
- Firmware state: Home-first, persistent device registry, on-demand Shark and
  Canon BLE, asynchronous on-demand Tascam X8/AK-BT1 record control with
  reconnect-state restoration, and device-specific hardware-trigger CTA
  routing built, host-tested, simulator-tested, and flashed.
- Universal driver framework: Bounded routing supports compiled Shark and Canon
  BLE and Tascam X8 drivers while preserving one active device instance at a
  time.
- Last updated: 2026-08-03.

## Completed planning

- Defined compile-time Kconfig/menuconfig driver selection.
- Separated compiled drivers from runtime device instances.
- Defined shared light, camera, motion, and recorder capabilities.
- Defined runtime enable/disable, configuration, and capability-safe groups.
- Selected direct control on the ESP32-C3 rather than an external gateway.
- Selected both panel-owned and imported Amaran mesh onboarding.
- Selected Canon BR-E1-compatible Bluetooth plus CCAPI HTTP.
- Split Canon UX into `Canon (Trigger)` and `Canon (Smart)`; Smart requires a
  captured smartphone BLE-to-Wi-Fi handoff before implementation.
- Defined ordered scenes with non-blocking waits, generated inverse Stop, and
  explicit Stop override.
- Reserved future recorder drivers for Tascam Portacapture X8 Bluetooth and
  Deity PR4.
- Selected a neutral Home screen with on-demand device connections and no
  automatic Shark pairing/reconnect at boot.
- Selected a dedicated Portal mode that temporarily runs a WPA2 SoftAP and HTTP
  server, then releases them completely on exit.

## Next task

Complete the Canon BLE and combined Phase 0/foundation hardware gates:

1. Verify the flashed Tascam update opens its UI immediately, restores Ready
   and Recording after reconnect, and can stop a recording started before the
   remote reboot. Recheck persisted reconnect and actual media files.
2. Capture the EOS R6 Mark III Camera Connect handoff from smartphone-mode BLE
   pairing through Wi-Fi AP startup and the first network request.
3. Annotate characteristic UUIDs, request/response bytes, timing, SSID/security
   data, camera prompts, and reconnect behavior.
4. Verify Trigger forget/re-pair and repeated screen entry/exit; record
   connection/command latency and free/minimum heap.
5. Visually verify Home, Devices, rename keyboard, enable/disable, remove/add,
   and persistence across a power cycle.
6. Verify that boot and Home perform no BLE scan or connection.
7. Exercise on-demand Shark pairing, controls, safe Back, and sleep/wake.

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

### Per-device source layout

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto`.
- Default profile flash usage: 854,674 / 3,145,728 bytes (27.2%).
- Roboto profile flash usage: 824,202 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,636 / 327,680 bytes (35.9%) in both firmware profiles.
- Build result: `ui_sim`, `crowpanel_128`, and `crowpanel_128_roboto`
  succeeded.
- Simulator result: all seven 240x240 PNG captures completed.
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical Shark and persistence regression: Not exercised; the existing
  operator hardware gate remains pending.

### Nano II UI polish

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto`.
- Default profile flash usage: 855,594 / 3,145,728 bytes (27.2%).
- Roboto profile flash usage: 825,122 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,652 / 327,680 bytes (35.9%) in both firmware profiles.
- Build result: `ui_sim`, `crowpanel_128`, and `crowpanel_128_roboto`
  succeeded.
- Simulator result: eleven 240x240 PNG captures completed, including keypoint
  settings and all three positioning states.
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical touch interaction and Shark movement: Not exercised; the combined
  Phase 0/foundation operator gate remains pending.

### Four-device LVGL capacity fix

- Date: 2026-08-03.
- Reproduction: the supported maximum of one Shark and three Canon instances
  exhausted the previous 48 KiB LVGL heap; an 80 KiB test reached Devices but
  failed when opening Settings.
- Fix: firmware and simulator LVGL heaps use 96 KiB. The simulator now seeds
  all four instances and exercises Settings, rename, Shark and Canon entry,
  and device removal/refresh.
- Simulator result: all twelve captures and the removal regression completed;
  17,640 bytes remained free after maximum-device initialization and 19,752
  bytes after removing one device.
- Build result: `native` passed 15/15 tests; `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto` succeeded.
- Default profile flash usage: 861,596 / 3,145,728 bytes (27.4%).
- Default and Roboto static RAM usage: 167,188 / 327,680 bytes (51.0%).
- Roboto profile flash usage: 831,124 / 3,145,728 bytes (26.4%).
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Physical four-device touch regression: Operator verification pending.

### Category-grouped add-device picker

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble`.
- Default profile flash usage: 862,696 / 3,145,728 bytes (27.4%).
- Default and Roboto static RAM usage: 167,196 / 327,680 bytes (51.0%).
- Roboto profile flash usage: 832,224 / 3,145,728 bytes (26.5%).
- Canon-only profile flash usage: 866,412 / 3,145,728 bytes (27.5%);
  static RAM usage: 166,308 / 327,680 bytes (50.8%).
- Simulator result: all thirteen captures completed, including the categorized
  picker; 15,648 bytes remained free after maximum-device initialization and
  17,760 bytes after the removal/refresh regression.
- Host tests: 15/15 passed.
- Build result: all affected environments succeeded.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Physical picker touch selection: Operator verification pending.

### Full coexistence spike

- Date: Not started.
- Enabled drivers/transports: Not started.
- Flash usage: Not measured.
- Static RAM usage: Not measured.
- Free/minimum heap: Not measured.
- Stability duration: Not measured.
- Result: Not started.

### Canon BR-E1 BLE sub-spike

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `canon_ble`,
  `crowpanel_128`, and `crowpanel_128_roboto`.
- Combined firmware flash usage: 861,590 / 3,145,728 bytes (27.4%).
- Combined static RAM usage: 118,036 / 327,680 bytes (36.0%).
- Canon-only firmware flash usage: 865,212 / 3,145,728 bytes (27.5%).
- Canon-only static RAM usage: 117,140 / 327,680 bytes (35.7%).
- Build result: all affected environments succeeded with espressif32 7.0.1.
- Flash result: combined `crowpanel_128` succeeded on
  `/dev/cu.usbserial-211240`.
- Host tests: 15/15 passed in the PlatformIO `native` environment.
- Simulator result: twelve 240x240 captures completed, including the Canon
  record-trigger screen.
- Hardware result: EOS R6 Mark III pairing, movie record start/stop, and bonded
  reconnect passed. The first `0x8c`/`0x0c` immediate-mode sequence took a
  still image in photo mode but did not record in movie mode; changing to the
  BR-E1 movie-mode `0x88`/`0x08` press/release sequence passed.
- Remaining hardware checks: forget/re-pair, five-cycle stability, connection
  and command latency, connected free/minimum heap, and Shark regression were
  not completed.
- Scope: This does not verify EOS R6/R6 II, concurrent links, CCAPI, fallback,
  or the full Phase 1 gate.

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
- Canon EOS R6 Mark II: Pairing, movie record trigger, and bonded reconnect
  passed; extended stability checks remain open.
- Canon EOS R6 Mark III: Pairing, movie record trigger, and bonded reconnect
  passed; extended stability checks remain open.
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

### 2026-08-03: Per-device source layout

- Consolidated the Shark protocol, state reducer, NimBLE client, driver adapter,
  and specialized UI under `src/devices/shark_nano_ii/`.
- Updated firmware, native-test, and simulator includes and source filters
  without changing namespaces, public APIs, runtime wiring, or behavior.
- Updated repository layout and device-support documentation for the
  per-device source convention.
- Native tests passed 12/12; `ui_sim` built and captured all seven screens;
  both firmware profiles built; the default profile flashed successfully.
- Physical Shark behavior was not exercised, so the existing combined
  Phase 0/foundation operator gate remains open.

### 2026-08-03: Nano II UI polish

- Polished Connect, Keypoints, Run, keypoint settings, and positioning views
  while preserving command routing, swipe navigation, and movement safeguards.
- Replaced cryptic keypoint markers and icon-only run toggles with readable
  labels, clarified action hierarchy, and prevented active-screen content from
  showing around modal edges.
- Expanded the simulator from seven to eleven captures with deterministic
  settings, positioning-choice, manual-positioning, and joystick states.
- Native tests passed 12/12; `ui_sim` and both firmware profiles built; the
  default profile flashed successfully.
- Physical touch and slider behavior remain operator-pending, so the existing
  combined Phase 0/foundation hardware gate remains open.

### 2026-08-03: Canon BR-E1 BLE sub-spike

- Recorded ADR-014: BLE exposes one honest record trigger and no inferred
  recording state.
- Added a compile-time Canon camera driver, asynchronous NimBLE connect/security
  progression, BR-E1 pairing identity, bonded reconnect/forget, and loop-owned
  press/release writes.
- Generalized `DeviceManager` to a bounded compiled-driver table and made
  Devices add/open behavior catalog-driven while preserving one active instance.
- Added the round Canon screen, native protocol/routing tests, simulator fake
  and screenshot, and a `canon_ble` firmware profile.
- Native tests, simulator capture, all affected firmware builds, and the
  combined firmware flash succeeded.
- Physical EOS R6 Mark III pairing and bonded reconnect passed. Hardware
  testing exposed an immediate-mode/photo command mismatch; the corrected
  `0x88`/`0x08` movie sequence then started and stopped recording successfully.
- Canon forget/re-pair, repeated-cycle measurements, and Shark regression
  remain operator-pending, so no roadmap phase gate is marked complete.

### 2026-08-03: Home icon prompt documentation

- Preserved the exact shared style prompt, per-mode subject prompts, successful
  Nano Banana Pro model/settings, visual review criteria, and LVGL conversion
  workflow in `assets/icons/README.md`.
- Linked the icon source and prompt guide from the repository README.
- Documentation only; firmware behavior and generated assets were unchanged,
  so no build, flash, simulator capture, or hardware check was run.

### 2026-08-03: Four-device UI heap fix

- Reproduced the reported freeze as LVGL allocation failure at the supported
  maximum of one Shark and three Canon device records.
- Increased the LVGL object heap from 48 KiB to 96 KiB and made the simulator
  use the same limit instead of masking firmware pressure with 128 KiB.
- Expanded simulator coverage to seed all four records and exercise management,
  rename, both device screens, and removal/refresh.
- Native tests, all simulator captures, both firmware builds, and the default
  profile flash succeeded. Physical touch confirmation remains pending.

### 2026-08-03: Canon Trigger/Smart split

- Recorded ADR-015 and the user-facing names `Canon (Trigger)` and
  `Canon (Smart)`.
- Defined Smart as smartphone-mode BLE pairing and automatic Wi-Fi handoff
  followed by direct-access-point CCAPI control; Trigger remains the verified
  BR-E1 fallback.
- Marked Smart blocked on an EOS R6 Mark III Camera Connect handoff capture and
  documented the required capture contents and research boundaries.
- Documentation only; firmware behavior was unchanged, so no build, flash,
  simulator capture, or hardware check was run.

### 2026-08-03: Category-grouped add-device picker

- Replaced automatic first-available-driver creation with an explicit,
  scrollable model picker grouped by Motion, Lights, Cameras, and Recorders.
- Kept compiled drivers visible when their instance limit is reached and
  disabled those choices with a clear status.
- Released picker rows when the overlay closes to preserve the bounded LVGL
  heap at the four-device maximum.
- Added a simulator capture for the picker. Native tests, simulator captures,
  Roboto, Canon-only, and default firmware builds passed; the default profile
  flashed successfully.
- Physical touch selection remains operator-pending.

### 2026-08-03: Canon EOS R6 Mark II verification

- Operator verified pairing, the BR-E1 movie record trigger, and bonded
  reconnect on the EOS R6 Mark II with the existing Canon Trigger driver.
- No model-specific protocol or firmware change was required.
- EOS R6 remains unverified; extended cycle, forget/re-pair, latency, heap, and
  coexistence checks remain open for the verified models.
- Documentation only; no build or flash was required.

### 2026-08-03: Canon device-name title

- Replaced the Canon control screen's hardcoded brand title with the runtime
  device instance name; long names remain bounded to one line.
- Simulator capture verified `EOS R6 Mark III` fits the round-screen header
  without overlapping connection status.
- Native tests passed 15/15; `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble` built successfully.
- Default firmware used 862,760 bytes flash and 167,196 bytes static RAM; the
  default profile flashed successfully to `/dev/cu.usbserial-211240`.

### 2026-08-03: Hardware trigger activates device CTA

- Routed GPIO 1 short presses through each active device UI's primary action:
  Canon sends its connected record trigger, while Shark opens Run from
  Keypoints and then advances Standby / Start / Stop.
- Touch and hardware activation share the same action helpers; disconnected
  CTAs remain inactive, and Shark modal/positioning dismissal is unchanged.
- Added simulator regressions for the full Shark run cycle and Canon trigger.
  Native tests passed 15/15, all UI captures completed, and the simulator
  finished with 17,728 bytes of LVGL memory free after device removal.
- `crowpanel_128` built at 862,762 / 3,145,728 bytes flash and 167,196 /
  327,680 bytes static RAM. `canon_ble` built at 866,478 bytes flash and
  166,308 bytes static RAM.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical button behavior on the Shark and Canon
  hardware remains operator-pending.

### 2026-08-03: Tascam X8 captured protocol and record-control driver

- Analyzed annotated nRF52840 fixture
  `docs/protocols/dumps/tascam_x8.pcapng`
  (`115e77bcc91ca2c184439115df97ad0459ac8452018ce0e08bdde6568918fd51`)
  and documented the AK-BT1 UUIDs, COBS stream, session open/keepalive, exact
  record start/stop writes, and recorder-originated transition events in
  `docs/protocols/tascam-x8.md`.
- Recorded ADR-016 and added the compile-time `tascam.portacapture_x8` recorder
  driver with explicit `RecordStart`/`RecordStop`, on-demand connection,
  persisted identity, and confirmed-transition-only Ready/Recording UI.
- A second capture pass rejected the earlier `0x81`/`0x10` steady-state
  interpretation. State reduction now uses the confirmed `DR 20 20 24 01`
  start and `DR 10 20 08` stop notifications, and initialization waits for the
  session characteristic's `10` open response. A stable reconnect-state field
  remains unproven.
- Added fragmented COBS/golden-vector/state/catalog/routing host tests and
  simulator fake state, hardware-button start/stop regression, and Ready plus
  Recording screenshots.
- Native tests passed 18/18. `ui_sim` built and completed all captures with
  10,024 bytes LVGL memory free at the five-device maximum and 12,032 bytes
  after removal.
- Firmware builds passed: `crowpanel_128` used 870,784 bytes flash and 167,692
  bytes static RAM; `crowpanel_128_roboto` used 840,312/167,692;
  `tascam_x8` used 872,144/166,532; and `canon_ble` used 871,426/166,388.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical X8/AK-BT1 connection, command, state,
  reconnect, and media-file checks remain operator-pending; the tranche is not
  hardware-complete.

### 2026-08-03: Tascam asynchronous connect and reconnect-state restoration

- Analyzed controlled recording/stopped reconnect fixture
  `docs/protocols/dumps/tascam_x8_reconnect.pcapng`
  (`7d095c94a454827778f3ecc86778b70e2109269f2e47acd0383c997f019ec783`).
  `DR 20 20 00` reports recording as `0x81`, stopped as `0x10`, and the
  transition between them as `0x82`.
- Added capture-backed current-state reduction so reconnect restores confirmed
  Ready/Recording instead of remaining unknown. Transitional `0x82` does not
  overwrite the last confirmed state.
- Changed the Tascam direct connection attempt to NimBLE's asynchronous mode;
  connect callbacks only set flags and service/session setup remains owned by
  `loop()`, allowing the Tascam screen to load before connection completes.
- Native tests passed 18/18. Firmware builds passed: `crowpanel_128` used
  870,986 bytes flash and 167,700 bytes static RAM;
  `crowpanel_128_roboto` used 840,514/167,700; and `tascam_x8` used
  872,334/166,532.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Immediate screen display and recording/stopped
  reconnect restoration remain operator-pending hardware checks.

### 2026-08-03: Tascam asynchronous session-loop regression fix

- Hardware exposed an infinite reconnect loop: BLE connected, but the panel
  remained in Waiting because the session-open timeout was evaluated before
  asynchronous link setup had started and therefore used its zero-initialized
  deadline.
- Added an explicit session-opening phase, delayed GATT/session setup briefly
  after the link callback, restored fresh attribute discovery on each attempt,
  and cancel an outstanding asynchronous attempt when leaving the screen.
- Native tests passed 18/18. Firmware builds passed: `crowpanel_128` used
  871,082 bytes flash and 167,700 bytes static RAM;
  `crowpanel_128_roboto` used 840,610/167,700; and `tascam_x8` used
  872,442/166,540.
- The corrected default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Hardware reconnection remains operator-pending.

