# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: bounded on-device Scenes tranche (ADR-019/020) with the
  shared BLE central tranche (ADR-021), beside remaining Phase 0/foundation
  hardware gates.
- Firmware state: Home-first, persistent device registry, retained on-demand Shark,
  Canon (Trigger)/(Smart), Tascam X8, and panel Scenes with authored Start/Stop
  lists, prepare-on-open concurrent links (protocol-ready `Ready`), settings cog
  (rename/edit/delete), and NVS scene persistence. Lazy UI allocation keeps
  Home/Devices resident; scene UI loads on demand.
- Universal driver framework: Four-slot retained connection pool for manual and
  sequence sessions, including multiple instances of one Canon driver. All four GATT
  clients now share one lazy NimBLE scanner/runtime and async link slots,
  targeted discovery, explicit protocol readiness, and BLE timing telemetry.
- Last updated: 2026-08-04.

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

Exercise the ADR-021 transport and Press Record / Press Stop on hardware, then
remaining Canon/foundation gates:

1. With paired Canon Smart (R6 II or III) and Tascam X8 configured and initially
   disconnected, open a sequence; confirm the shared scanner discovers both,
   both async links reach Ready without starvation, Start records with the
   authored gap, and Stop confirms both stopped.
2. Confirm device screens refuse open while a sequence owns links; Back/Done
   releases sequence ownership while protocol-ready sessions remain connected.
3. Confirm scene persistence across power cycle.
4. Continue Canon Trigger/Smart and Shark foundation hardware gates as before.
5. Run ten initially disconnected cycles per driver and ten Canon Smart +
   Tascam sequence opens; record median/p95 readiness, per-stage blocking GATT,
   scan drops, retries, post-init heap, and post-teardown heap. Trigger the
   asynchronous GATT executor only if blocking GATT reaches the 25% gate.
6. Keep groups, lights, Portal scene editing, and generated reverse-Stop
   deferred.

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

### Canon smartphone-mode BLE experiment

- Date: 2026-08-03.
- Branch: `spike/canon-smartphone-ble`.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble`.
- Default firmware flash usage: 875,214 / 3,145,728 bytes (27.8%); static RAM:
  167,836 / 327,680 bytes (51.2%).
- Roboto firmware flash usage: 844,742 bytes; static RAM: 167,836 bytes.
- Canon profile flash usage: 876,014 bytes; static RAM: 166,524 bytes.
- Host tests: 18/18 passed.
- Simulator: Ready, Recording, and Unknown Canon screens captured; Unknown
  exposes separate Start and Stop controls. The maximum-device run completed
  with 8,072 bytes free after initialization and 10,048 bytes after removal.
- Build result: all affected environments succeeded.
- Flash result: `crowpanel_128` succeeded on
  `/dev/cu.usbserial-211240`.
- Hardware result: smartphone-mode pairing, explicit movie control,
  camera-originated state, and reconnect restoration remain operator-pending.
  No EOS R6-family protocol value is marked confirmed by this build/flash.

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

- Analyzed an annotated nRF52840 research capture (SHA-256
  `115e77bcc91ca2c184439115df97ad0459ac8452018ce0e08bdde6568918fd51`),
  later removed from the publishable tree,
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

- Analyzed a controlled recording/stopped reconnect capture (SHA-256
  `7d095c94a454827778f3ecc86778b70e2109269f2e47acd0383c997f019ec783`),
  later removed from the publishable tree.
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

### 2026-08-03: Tascam record-control hardware regression passed

- Operator verification passed for immediate Tascam screen opening, persisted
  reconnect, recording-state restoration after remote restart, stopping the
  pre-existing recording, and creation of the expected media file.
- The bounded ADR-016 record-control hardware tranche is verified. Battery,
  media-capacity reporting, mixer controls, and scene integration remain
  outside this tranche.
- Current Canon Trigger and Shark control were also reported working. Canon
  Smart remains blocked on its BLE-to-Wi-Fi handoff capture, and the exact
  extended Canon/combined foundation checks listed above remain open until
  individually measured or confirmed.

### 2026-08-03: Canon smartphone-mode BLE replacement experiment

- Created `spike/canon-smartphone-ble`, preserving the verified BR-E1 driver on
  the main branch, and recorded ADR-017 plus protocol confidence boundaries.
- Replaced the branch's Canon protocol with encrypted smartphone pairing,
  camera confirmation, stable controller identity, shooting-session
  subscriptions, explicit `00 10`/`00 11` movie commands, and
  camera-notification-only state reduction.
- Added asynchronous connect/security/setup phases and a loop-owned
  notification queue. NimBLE callbacks do not parse, mutate state, issue GATT
  writes, or access LVGL.
- Replaced the stateless trigger screen with Ready, Recording, transitional,
  and Unknown states. Unknown provides separate Start and Stop touch controls;
  the hardware button starts unless recording is camera-confirmed.
- Native tests passed 18/18. Simulator, default, Roboto, and Canon profile
  builds passed with the measurements above. The default profile flashed
  successfully to `/dev/cu.usbserial-211240`.
- Hardware pairing and camera behavior remain unverified. The branch must not
  replace the main BR-E1 implementation until the ADR-017 hardware gate passes.

### 2026-08-03: Canon R6 pairing-order correction

- First hardware attempt reached the camera confirmation screen and displayed
  `StudioRemote`, but the camera then reported `Connection target not found`.
- Comparison with newer Canon Camera Connect/furble/EOS RP research found that
  the implementation used the older EOS M6 ordering: it waited for camera
  confirmation before sending controller ID, name, and type. Newer clients send
  those identity records before waiting for the accept indication.
- Changed first-pair setup to subscribe with indications when supported, write
  the handshake request, immediately send stable ID/name/Android type, wait for
  `02`, and only then send the finish marker and open the shooting session.
- Native tests passed 18/18. `crowpanel_128`, `crowpanel_128_roboto`, and
  `canon_ble` rebuilt successfully with the updated measurements above.
- The corrected flash could not be uploaded because
  `/dev/cu.usbserial-211240` was no longer present. PlatformIO detected the
  unrelated nRF BLE sniffer at `/dev/cu.usbmodem101`, and the upload failed
  without modifying the panel. Reflash and hardware retry remain pending.

### 2026-08-03: Canon Camera Connect pairing and Wi-Fi handoff captures

- Analyzed Android host-HCI captures of fresh EOS R6 Mark III
  smartphone pairing, BLE movie control, bonded reconnect, and successful
  Camera Connect Wi-Fi offload.
- Added minimized ATT-only research sets (SHA-256
  `fac58a7277072f25b45c91f5051dae9c335d71ca9323e9388b69f6e3399cd08c`
  and `25e59aca42f47a9ca554fd85273f8bfe5b9f5577d96c9d59051838e407bf17ad`),
  later removed from the publishable tree.
  SMP keys, camera serial number, controller ID, SSID-like value, and
  credential-like value are excluded.
- Camera Connect waits for pairing indication `02` before writing controller
  ID, name, and Android type, disproving the branch's identity-first ordering.
  The camera accepts bonded legacy Just Works rather than the Secure
  Connections/MITM requested by Android.
- Confirmed shooting-session command/result `03`/`05`, movie commands
  `00 10`/`00 11`, and recording states `01 01 02`/`01 01 01`. The current
  branch's `02` then `06` session strategy does not match Camera Connect.
- Identified Wi-Fi handoff as write `01` to `00020002-...`, followed by
  indications `01 03` and `02 03` on `00020003-...`. Camera Connect acquired
  `camera_connect:CCBleHandOverWakeLock` at the same time.
- Confirmed camera power/session controls after shooting: `03` wakes from
  Bluetooth standby and receives `05`; `04` leaves shooting and receives `01`;
  `05` powers down and receives `01`, followed by camera-side BLE disconnects
  approximately 147-154 ms later.
- Network security mode, DHCP behavior, camera IP/port, and the first CCAPI
  request remain uncaptured. No build or flash was run because this session
  changed only protocol fixtures and documentation. The next safe task is to
  align the experimental BLE client with the captured confirmation-first
  handshake and `03` session command before another hardware retry.

### 2026-08-03: Canon captured BLE behavior implemented

- Aligned the ADR-017 client with the Android host-HCI research:
  bonding-only Just Works negotiation, request-before-subscribe pairing,
  confirmation-first identity, captured `06`/`07`/`08`/`0c` post-pair queries,
  and automatic `03` wake with required `05` session result. Bonded reconnect
  follows the same finish/query/wake path; the uncaptured `02`/`06` fallback
  was removed.
- Added ADR-018 and an explicit camera power command. The Canon screen disables
  power-down while recording or a record command is pending, sends mode `05`,
  waits for acknowledgement and the camera-side disconnect, and does not infer
  physical success from the acknowledgement alone. Back remains a local,
  non-destructive disconnect.
- Contradictory camera state now reports the requested record transition as
  failed while retaining the camera-confirmed steady state. Renamed the
  multi-device Unity runner from `test/test_shark.cpp` to
  `test/test_main.cpp`.
- Native tests passed 18/18. The UI simulator passed and generated
  `12_canon_ready.png` through `15_canon_powered_off.png`; LVGL memory had
  7,208 bytes free at the five-device maximum and 9,096 bytes after removal.
- Firmware builds passed: `crowpanel_128` used 876,724 bytes flash and 167,868
  bytes static RAM; `crowpanel_128_roboto` used 846,252/167,868; `canon_ble`
  used 877,528/166,548; and `tascam_x8` used 876,496/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. First pairing, bonded reconnect, automatic
  physical wake, record start/stop state, explicit physical power-down, and
  non-destructive Back remain operator-pending hardware checks; ADR-017 and
  ADR-018 are not hardware-complete.

### 2026-08-03: Canon discovery and stale-bond recovery

- Investigated an operator report that the camera no longer found the panel
  after the Pixel Camera Connect capture. The host-HCI log confirms that the
  camera sends `00010000-...` in its primary advertisement and
  `EOSR6m3_...` in a separate scan response.
- Canon discovery now accepts either the pairing service or an
  `EOS`/`PowerShot` advertised name, avoiding dependence on whether NimBLE
  merges the two reports before invoking the scan callback.
- A saved camera that fails encryption twice is now treated as a stale bond:
  the panel removes its local key, marks the record unpaired, and returns to
  discovery instead of retrying the invalid direct connection indefinitely.
  This is expected after the camera's smartphone registration is replaced or
  reset; the camera must still be in **Connect to smartphone** pairing mode.
- Documented a planned UI-memory optimization: retain only Home/Devices, lazily
  allocate other screens and overlays, share Canon/Tascam recording controls,
  and reduce the 96 KiB LVGL pool only after measured peak/fragmentation tests.
- Native tests passed 18/18. Builds passed: `crowpanel_128` used 877,114 bytes
  flash and 167,868 bytes static RAM; `crowpanel_128_roboto` used
  846,642/167,868; `canon_ble` used 877,922/166,556; and `tascam_x8` used
  876,882/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Boot telemetry from the preceding discovery build
  reported 125,180 bytes free heap and 122,740 minimum. The serial port was
  disconnected after the final flash, so physical rediscovery and pairing
  remain operator-pending rather than verified.

### 2026-08-03: Canon initial recording-state read

- Hardware verification confirmed smartphone-mode discovery, pairing,
  start/stop commands, and correct notification-driven state after each
  command. Initial state remained unknown until the first transition.
- The captured GATT declaration marks shooting-state characteristic
  `00030031-...` as Read + Notify. After wake result `05`, the client now reads
  that characteristic once and applies only the existing documented stopped
  or recording vectors; an empty, failed, or unfamiliar read leaves state
  unknown.
- Native tests passed 18/18. Builds passed: `crowpanel_128` used 877,240 bytes
  flash and 167,868 bytes static RAM; `crowpanel_128_roboto` used
  846,768/167,868; `canon_ble` used 878,048/166,556; and `tascam_x8` used
  877,008/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Correct Ready/Recording display immediately after
  connection remains operator-pending and is not yet marked verified.

### 2026-08-03: Canon power button wakes after power-off

- Confirmed the Canon power control stays enabled while the camera is powered
  off and routes `CameraPowerOn`, which reconnects and runs the captured wake
  sequence instead of leaving the control disabled.
- Native tests passed 18/18. Simulator captures include
  `15_canon_powered_off.png` and `16_canon_powered_on.png`.
- Firmware builds passed and the default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical power-off then power-on wake remains
  operator-pending.

### 2026-08-03: Canon R6 Mark II connection-target note

- Operator report: EOS R6 Mark II shows **Connection target not found** while
  the same panel build pairs and controls the EOS R6 Mark III. Removing the
  panel device record and the Mark III entry did not clear the Mark II error.
- Canon documentation treats that message as the camera failing to find its
  previously registered smartphone/app target. The panel is a BLE central and
  does not advertise, so the R6 II must use **Add a device to connect to**
  rather than a saved phone entry; camera-side smartphone registrations may
  still need deletion even after the panel forgets a body.
- Hardened discovery further: match Canon manufacturer ID `0x01A9` and names
  containing `EOS`/`R6`/`PowerShot`, not only an `EOS` prefix or service UUID.
  Device removal now also drops controller-side bonds. Scan hits log to serial
  as `canon scan hit ...` for the next hardware retry.
- Shortened failed bonded-reconnect retries so the client falls back to scan
  after one miss instead of repeatedly targeting a powered-down or different
  body.

### 2026-08-03: EOS R6 Mark II reaches Ready on mode `04`

- Serial on `EOSR6m2_D4D530` showed the panel bonding and opening the core
  session, then stalling in `OpeningSession` because the client only treated
  wake result `05` (R6 III Camera Connect) as Ready. The R6 II notifies `04`,
  matching public EOS M6 shooting-mode/wake behavior.
- `parseModeEvent` now accepts `04` and `05` as session-ready. Core subscribe
  prefers indications when offered; OpeningSession will retry shooting mode
  `02` if wake `03` times out. Multi-camera scan dwell prefers `m2` names and
  ignores bodies that remotely terminate during bonding.
- Native tests passed 18/18. Default firmware flashed to
  `/dev/cu.usbserial-211240`. Capture confirmed
  `canon mode notify ... byte0=04` then `canon session ready` with
  `link=connected`. Operator should verify record start/stop on the R6 II.
- R6 II GATT lacks pairing-info `0001000c` (has read-only `0001000b`); post-pair
  queries remain skipped on that body.

### 2026-08-03: Canon multi-instance address lock

- Operator report: controlling the second Canon instance (R6 III) sent record
  commands to the first body (R6 II). Root cause: bonded reconnect fell back to
  an open scan and could adopt a sibling camera, then persisted that address
  onto the active instance.
- Bonded instances now lock to their saved BLE address, skip other paired peer
  addresses, and do not rewrite pairing identity on ordinary reconnect.
  Confirmation timeouts still rotate during fresh pairing only.
- Native tests passed 18/18; default firmware flashed to
  `/dev/cu.usbserial-211240`. If an R6 III record was already overwritten with
  the R6 II address, Forget that instance and re-pair on Add-a-device.

### 2026-08-03: Dual Canon Trigger + Smart drivers

- Restored BR-E1 `Canon (Trigger)` into `src/devices/canon_trigger/` with
  `DriverId::CanonTrigger = 4`. Kept smartphone BLE as `Canon (Smart)` at
  `DriverId::CanonBle = 2` so existing NVS Smart records stay valid.
- Catalog labels: `Canon (Trigger)` / `canon.eos_r6.trigger` and
  `Canon (Smart)` / `canon.eos_r6.smartphone_ble`. Both compile by default;
  optional `canon_trigger` / `canon_ble` PlatformIO envs isolate each driver.
- ADR-017 updated: Smart no longer replaces Trigger; both ship, camera menu
  must match the chosen driver, one active transport remains.
- Dual Canon screens plus the five-instance sim seed exhausted the prior 96 KiB
  LVGL heap (freeze at Add device). Raised `LV_MEM_SIZE` to 128 KiB in
  firmware and `ui_sim`.
- Host tests: 20/20 passed (`native`).
- Simulator: captures through `20_tascam_recording.png`, including
  `03_add_device.png`, `17_canon_trigger_ready.png`, and
  `18_canon_trigger_sent.png`. Free after max-device init: 36,680 bytes.
- Firmware: `crowpanel_128` build succeeded (flash 888,272 / RAM 201,172 with
  128 KiB LVGL). An earlier dual-driver image flashed to
  `/dev/cu.usbserial-211240`; the post-heap-bump reflash could not run because
  that port was absent (only unrelated usbmodem devices present).

### 2026-08-03: UI memory optimization

- Implemented the planned UI allocation work: Home/Devices stay resident;
  Add/Manage/Rename overlays are created on open and deleted on close; Shark,
  Canon Trigger, Canon Smart, and Tascam screens are built on show and released
  after navigation leaves them.
- Extracted shared `src/ui/recorder_shell.*` for Canon (Smart) and Tascam, with
  optional power and unknown START/STOP controls owned by adapters.
- Simulator full-navigation peak LVGL use was 17,012 bytes (frag 45% at end).
  Reduced `LV_MEM_SIZE` from 128 KiB to 64 KiB in firmware and `ui_sim`.
- Host tests: 20/20 passed (`native`).
- Simulator: captures through `20_tascam_recording.png`. After max-device init
  with 64 KiB pool: 40,152 bytes free / 10,684 peak; after remove refresh:
  42,208 free / 17,012 peak.
- Firmware: `crowpanel_128` build succeeded (flash 889,324 / RAM 135,628) and
  flashed to `/dev/cu.usbserial-211240`. Physical navigation regression remains
  operator-pending.

### 2026-08-04: On-device Scenes (Press Record / Press Stop)

- Recorded ADR-019 (authored Start/Stop) and ADR-020 (concurrent sequence
  links). Extended `DeviceManager` with `activateHeld` / multi-active loop and
  dispatch while keeping exclusive `activate` for device screens.
- Added scene registry/store/runner/service, separate NVS `scenes` blob, panel
  Scenes UI (list/edit Start/edit Stop/run), and Press Record seed
  (Canon RecordStart → wait 500 ms → Tascam RecordStart; Stop: Canon then
  Tascam RecordStop).
- Host tests: 23/23 passed (`native`), including concurrent links, scene store
  round-trip/corruption, and Press Record Start/Stop ordering.
- Simulator: captures `21_scenes_list` through `27_scenes_stop_progress`; full
  run reached IdleArmed then Completed.
- Firmware: `crowpanel_128` build succeeded (flash 906,206 / RAM 137,404) and
  flashed to `/dev/cu.usbserial-211240`.
- Hardware gate still open: exercise Press Record / Press Stop on real Canon
  Smart + Tascam with concurrent GATT. Groups, lights, Portal editing, and
  generated reverse-Stop remain deferred.

### 2026-08-04: Sequence add-step Category → Device → Action

- Restructured Scenes `+ Step` picker into three levels: category (plus Wait),
  enabled device in that category, then Record Start / Record Stop.
- Back within the overlay returns one level; hardware short-press matches.
- Simulator captures: `22b_scenes_add_category`, `22c_scenes_add_device`,
  `22d_scenes_add_action`.
- Firmware rebuilt and flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Shared category-icon picker shell

- Extracted `src/ui/picker_shell.*` for Devices **Add device** and Scenes
  **+ Step**: Category icon grid → driver/device list → (scene) action.
  SceneStep keeps Wait 500 ms on the category screen.
- Added cute category icons (`icon_cat_{motion,lights,cameras,recorders}`) via
  Nano Banana / `tools/gen_icons.py` → `ui_icon_cat_*`.
- Picker overlay is deleted on close (not merely hidden) so LVGL heap stays
  stable across later Shark navigation in `ui_sim`.
- Simulator: full capture through `27_scenes_stop_progress`; peak LVGL use
  17,012 bytes after remove refresh.
- Firmware: `crowpanel_128` build succeeded (flash 935,596 / RAM 137,428) and
  flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Scenes settings + prepare-on-open

- Removed Scenes-list Press Record seed button; `+` names blank sequences
  `Sequence n` with `n = count + 1`. `seedPressRecord` remains for sim/tests.
- Added `ScenePhase::Ready` and `prepare()`; opening a run screen connects all
  Start/Stop targets and holds links; Start from Ready skips re-activate.
  Amended ADR-020. Run-screen settings cog: Rename / Edit Start / Edit Stop /
  Delete. Shared `ui::promptRename` for scene rename.
- Host tests: 24/24 including prepare→Ready→Start from held links.
- Simulator: list without seed; `23b_scenes_settings`; `24_scenes_run_ready` at
  Ready phase; peak LVGL use 17,012 bytes after remove refresh.
- Firmware: `crowpanel_128` build succeeded (flash 937,214 / RAM 137,444) and
  flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Shared BLE central manager

- Recorded ADR-021. Added `src/core/ble/`: backend-independent bounded central,
  fixed advertisement/link/security events, shared scan demand and main-loop
  fan-out, address claims/skip lists, async connection slots, watchdog/backoff,
  serialized security requests, bond deletion, fake native backend, and lazy
  NimBLE runtime teardown.
- Migrated Tascam, Canon Trigger, Shark, and Canon Smart away from independent
  NimBLE initialization, scanners, clients, link callbacks, retry timers, and
  teardown. Device-specific advertisement matching, Canon candidate dwell and
  ignored peers, GATT setup, handshakes, commands, and notification queues
  remain in each client.
- Native tests: 29/29 passed in `native`, including lifetime/slot exhaustion,
  scan fan-out and independent release, address claims, concurrent async links,
  retry/watchdog behavior, security requests, bond deletion, queue overflow,
  advertisement parsing, and all four device matchers.
- UI simulator: build and all captures through
  `27_scenes_stop_progress.png` succeeded; final LVGL report was 24,728 bytes
  free, 17,012-byte peak use, and 35% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 941,882 bytes flash / 137,884 bytes RAM;
  `canon_ble` 941,046 / 136,540;
  `canon_trigger` 938,140 / 136,172;
  `tascam_x8` 939,998 / 136,380.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240`. Bounded restart telemetry at 835 ms reported
  Home `disconnected`, 155,004 bytes free heap, and 152,564 minimum free heap,
  confirming boot remains BLE-free.
- Hardware gate remains open: no operator interaction with Shark, Canon
  Trigger, Canon Smart, or Tascam was performed in this session. Concurrent
  Canon Smart + Tascam Prepare, scan drops, per-link retries, prepare latency,
  post-BLE-init heap, post-teardown heap, and physical command confirmation
  therefore remain unmeasured and must not be inferred from the successful
  build/flash.

### 2026-08-04: Ble(e)p project rename

- Renamed the user-facing project identity from Studio Remote / Universal
  Studio Remote to **Ble(e)p** in the README, documentation index,
  architecture goal, agent guidance, and round-panel Home title.
- Changed the NimBLE local name and Canon Trigger/Smart pairing identity from
  `StudioRemote` to `Ble(e)p`. Persistent registry schemas, NVS namespaces,
  driver IDs, device names, and historical verification notes remain unchanged.
- Updated local PlatformIO examples to invoke `./.venv/bin/python -m platformio`;
  the generated `./.venv/bin/platformio` launcher retained an absolute shebang
  to the pre-rename workspace path.
- Native tests: 29/29 passed, including the updated Canon Smart handshake name
  and length vector and Canon Trigger pairing-name coverage.
- UI simulator: build and all captures through
  `27_scenes_stop_progress.png` succeeded. `01_home.png` visually confirms the
  Ble(e)p title fits the 240x240 round Home screen. Final LVGL report was 24,728
  bytes free, 17,012-byte peak use, and 35% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 941,866 bytes flash / 137,884 bytes RAM;
  `crowpanel_128_roboto` 911,402 / 137,884;
  `canon_ble` 941,030 / 136,540;
  `canon_trigger` 938,174 / 136,172;
  `tascam_x8` 940,050 / 136,380.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240`. Physical BLE-name and Canon re-pair display checks
  were not performed.

### 2026-08-04: Sequence hardware action button

- Changed the hardware short-press behavior on an open sequence run screen to
  invoke the same state-aware action as the touch controls: Start when ready or
  restartable, and Stop when armed or while Start is in flight. The button is
  inert while preparation or Stop is already in progress; touch Back/Unlink
  remains the way to leave and release held links.
- Updated the UI simulator regression to start and stop Press Record through
  `ui::handleShortPress()`. The simulator build and all captures through
  `27_scenes_stop_progress.png` succeeded; final LVGL reporting remained 24,728
  bytes free, 17,012-byte peak use, and 35% fragmentation.
- Native tests: 29/29 passed. `crowpanel_128` built successfully with 942,028
  bytes flash and 137,884 bytes static RAM, then flashed successfully to
  `/dev/cu.usbserial-211240`.
- Physical button operation against connected Canon Smart and Tascam targets
  was not exercised, so the existing scene hardware gate remains open.
- Renamed the prepared-link control from `Cancel` to `Unlink`, while retaining
  `Cancel` only during `Connecting`; the prepared sequence status remains
  `Ready`. Native tests remained 29/29, the simulator and all captures passed,
  and `24_scenes_run_ready.png` visually confirmed both labels fit. The updated
  `crowpanel_128` build used 942,078 bytes flash / 137,884 bytes static RAM and
  flashed successfully to `/dev/cu.usbserial-211240`.

### 2026-08-04: Protocol-ready sequence preparation and BLE timing

- Split physical `LinkState::Connected` from `DeviceRuntimeState::protocolReady`.
  Sequence preparation remains `Connecting` and rejects Start until every
  target reports both. Readiness clears on failure, retry, release, and
  reconnect.
- Canon Smart now uses targeted handshake then shooting-core discovery and
  becomes ready only on the camera's session-ready notification. Tascam waits
  for session-open and its initialization write. Canon Trigger waits for
  discovery and the pairing-identity write. Shark waits for subscription and
  every handshake/initial-refresh write; failed final writes cannot publish
  readiness.
- Removed both fixed 100 ms post-connect waits. Setup begins on the next main
  loop after queued BLE events drain. All four drivers request best-effort
  7.5–15 ms setup and 15–30 ms steady-state connection parameters; rejection
  is logged but non-fatal.
- Added stable serial diagnostics in the form
  `ble_timing driver=<id> link=<n> stage=<stage> elapsed_ms=<n> total_ms=<n> result=<status>`
  for central connection lifecycle, security, targeted GATT stages, protocol
  readiness, retries, teardown, and total sequence preparation.
- Native tests: 30/30 passed, including physical-versus-protocol readiness,
  Start rejection before readiness, connection-parameter fallback, steady-state
  timing, and readiness reset on release/reconnect.
- UI simulator: build and all captures succeeded; maximum-device initialization
  reported 40,128 bytes free, 10,694-byte peak use, and 0% fragmentation;
  remove/refresh reported 24,728 bytes free, 17,012-byte peak use, and 35%
  fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 945,250 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 914,786 / 138,028;
  `canon_ble` 944,310 / 136,668;
  `canon_trigger` 941,428 / 136,300;
  `tascam_x8` 943,328 / 136,508.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240` after granting serial-port access.
- Hardware benchmark remains open: no paired devices were operated, so the ten
  initially disconnected cycles per driver, ten concurrent Canon Smart +
  Tascam opens, median/p95 stage timing, disconnect/retry/drop rates, physical
  Start/Stop, and heap recovery are still pending. The 25% asynchronous GATT
  executor gate cannot be evaluated from builds and simulator results; ADR-021
  therefore remains unamended and the executor is intentionally deferred.

### 2026-08-04: Intermittent paired-sequence connection fix

- Captured a live Canon Smart + Tascam sequence attempt from the flashed board.
  Simultaneous controller connection initiations repeatedly failed with reason
  `574` (`0x23e`, HCI `0x3e` connection-establishment timeout). Canon connected
  when it received an uncontended attempt; Tascam later linked and completed
  GATT setup.
- Changed the central scheduler to keep per-link async slots and shared scan
  discovery, but run only one controller connection or security procedure at a
  time. After a link/security event clears the controller, the next queued
  target begins; protocol initialization on an established link can still
  overlap that connection.
- Fixed a Tascam readiness regression found in the same trace. Its initialization
  write had been moved before the client's physical `Connected` state, while
  the write helper correctly rejects pre-link commands. The client now exposes
  the physical state for the write but leaves `protocolReady` false until the
  initialization succeeds, and logs `session_initialization` separately.
- Native tests: 30/30 passed, including serialized connection initiation across
  Canon security. Firmware builds succeeded:
  `crowpanel_128` 945,752 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 915,288 / 138,028;
  `canon_ble` 944,810 / 136,668;
  `canon_trigger` 941,928 / 136,300;
  `tascam_x8` 943,826 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`. The board
  booted cleanly, but no post-fix sequence attempt occurred during the bounded
  serial verification window; repeated paired-device validation remains open.

### 2026-08-04: Post-Stop BLE teardown crash fix

- Investigated an operator-reported reset after Start, Stop, then opening scene
  Settings. The prior exception was lost before serial attachment, but code
  inspection found a matching use-after-free in shared BLE teardown: NimBLE
  client deletion is asynchronous, while the backend freed its
  `ClientCallbacks` immediately after requesting disconnect. A later GAP
  disconnect could therefore call through a freed pointer after Stop released
  the links.
- Changed callback ownership to the bounded backend-slot lifetime. Client
  teardown schedules NimBLE deletion but retains the callback until the entire
  BLE backend has deinitialized, so late disconnect events remain safe.
- Added a UI simulator regression that completes Start/Stop and immediately
  opens Settings. It passed and captured
  `27b_scenes_settings_after_stop.png`; LVGL reported 14,008 bytes free, 79%
  used, 17,012-byte peak use, and 1% fragmentation at that point.
- Native tests: 30/30 passed. Firmware builds succeeded:
  `crowpanel_128` 945,728 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 915,264 / 138,028;
  `canon_ble` 944,786 / 136,668;
  `canon_trigger` 941,904 / 136,300;
  `tascam_x8` 943,802 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Repeating the physical Start/Stop/Settings workflow remains the completion
  check for this crash fix.

### 2026-08-04: Final sequence action delivery and prepared-edit links

- Fixed a sequence pipeline bug where Canon Smart and Tascam reported command
  success when a request was only queued in the driver. If that action was the
  last Stop step, `finishStop()` released the link before the next driver loop,
  so the Tascam Stop write was never transmitted. Both clients now perform the
  GATT write during main-loop command dispatch and return success only when the
  write succeeds; notification-confirmed state remains asynchronous.
- Added prepared-scene reconciliation. Editing waits/order keeps all existing
  target links. Removing a target releases only that target; adding one keeps
  unchanged links and prepares the new target before returning to `Ready`.
  Amended ADR-020 so a successful Stop keeps prepared links while the run/edit
  screen remains open; Back or Unlink performs teardown. This allows immediate
  editing or restart without reconnecting unchanged targets.
- Native tests: 30/30 passed, including editing a prepared wait without link
  loss and removing/re-adding a target while preserving the other target.
- UI simulator build and all captures passed, including the post-Stop Settings
  regression. Firmware builds succeeded:
  `crowpanel_128` 946,576 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 916,112 / 138,028;
  `canon_ble` 945,630 / 136,668;
  `canon_trigger` 942,748 / 136,300;
  `tascam_x8` 944,650 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical Tascam Stop delivery and prepared-edit link retention remain the
  final operator checks.

### 2026-08-04: Sequence retry and immediate Relink lifecycle fix

- Captured an operator-reported sequence failure with both devices nearby.
  Canon Smart recovered to protocol-ready in 3.4 seconds, while Tascam's first
  two direct attempts returned HCI `0x3e`; the previous policy then paid about
  seven seconds to rediscover the already-saved peer and reached sequence
  `Ready` at 18.7 seconds. Saved targets now receive a third direct attempt
  before scan fallback. The terminal sequence preparation timeout is 30 seconds
  rather than 20 seconds; successful readiness remains immediate.
- Fixed immediate Unlink -> Relink activation failure. `deleteClient()` retains
  NimBLE's global client slot until an asynchronous disconnect callback, so two
  retiring sequence clients could exhaust capacity before Relink. The backend
  now accepts logical link creation while old clients retire, provisions the
  replacements from `pump()`, delays deinitialization until the client list is
  empty, and ignores final callbacks whose client pointer no longer owns the
  slot.
- Hardware verification on the flashed `crowpanel_128` completed two
  Unlink -> Relink cycles with Canon Smart + Tascam. Both reached true
  protocol-ready: 13.7 seconds and 9.1 seconds. One Tascam session-open attempt
  failed during the second cycle and recovered on its bounded per-link retry;
  no stale callback canceled either reacquisition.
- Native tests: 30/30 passed. UI simulator build and every capture through
  `27b_scenes_settings_after_stop.png` passed; the post-Stop settings capture
  reported 14,008 bytes free and 17,012-byte peak LVGL use.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 947,354 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 916,890 / 138,028;
  `canon_ble` 946,396 / 136,668;
  `canon_trigger` 943,514 / 136,300;
  `tascam_x8` 945,416 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  The ten-cycle median/p95 benchmark and physical Start/Stop checks remain
  open; two successful relinks are regression evidence, not tranche completion.

### 2026-08-04: GitHub publication preparation

- Reworked the public README around Ble(e)p's long-term goal: an open,
  community-built controller ecosystem for many devices and controller
  hardware targets. Documented current support, limitations, architecture,
  setup, contribution flow, roadmap, safety, and trademark independence.
- Added contribution, conduct, and security policies; GitHub issue forms, a
  pull-request template, and native-test/firmware-build CI.
- Audited tracked text and Git patches for common credential patterns. No
  embedded API key, password, authorization token, or private key was found.
- Found private publication risk in the tracked packet captures: nearby device
  names and stable suffixes, radio addresses, camera/phone identifiers, local
  capture-interface metadata, and unrelated traffic. Removed every raw pcapng
  from the current tree and added ignore/privacy rules. Extracted protocol
  vectors, confidence notes, and source hashes remain in the documentation.
- Removed the tracked macOS `.DS_Store` and ignored OS/editor/build artifacts.
- Verification: documentation links and GitHub YAML parsed successfully;
  native tests passed 30/30; `crowpanel_128` built with espressif32 7.0.1 at
  947,354 bytes flash / 138,028 bytes RAM and flashed successfully to the
  configured ESP32-C3 panel.
- At that point, publishing blockers included selecting an open-source license
  and scrubbing the capture blobs from existing Git history (or publishing a
  reviewed squashed history). Git author identity/email also needed review for
  intended public attribution.

### 2026-08-04: Apache-2.0 license and project origin

- Selected Apache License 2.0, matching Home Assistant Core, and added the
  standard license text at the repository root. Updated contribution terms and
  marked the license decision complete in the publishing checklist.
- Added the project's origin story to the README: Ble(e)p began as a Hacking
  Modern Life YouTube build for a better iFootage Shark Nano II remote, then
  grew into the broader open controller ecosystem.
- Documentation links and license-file structure validated. Native tests passed
  30/30; `crowpanel_128` built at 947,354 bytes flash / 138,028 bytes RAM and
  flashed successfully to the configured ESP32-C3 panel.
- The remaining publication blocker is Git history: removed capture blobs are
  still present in earlier commits and must be scrubbed or excluded through a
  reviewed squashed public history before pushing.
### 2026-08-04: Action-button long-press Back

- Split the GPIO 1 action button by intent: short press only dispatches the
  active device's primary action; a 700 ms hold navigates Back, cancels, or
  closes the current overlay. Releasing after a handled long press does not
  dispatch a short-press action.
- Removed deep-sleep, long-hold power-off, and button-wake handling. The
  hardware SPDT switch is now the only remote power control.
- Host tests passed 30/30. `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, `canon_ble`, `canon_trigger`, and `tascam_x8` builds
  succeeded with PlatformIO 7.0.1. The flashed default firmware uses 941,552
  bytes flash and 137,924 bytes static RAM; the Roboto build uses 911,128 bytes
  flash and 137,924 bytes static RAM.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical short-action / long-Back button timing remains operator-pending.
- Follow-up: sequence Run now dispatches its enabled Start/Stop control on a
  short press; overlays and non-run scene screens ignore short presses.
- Follow-up: reduced the long-press threshold from 1.6 seconds to 700 ms after
  physical use showed the original Back delay was too long.

### 2026-08-04: Sequence target controls and readiness chips

- Rebased `codex/add-sequence-device-controls` onto local `main` at `f9633b4`
  before implementation.
- The sequence run screen now deduplicates direct Start/Stop targets into
  circular category-icon chips above Start/Stop. Borders breathe cyan during
  connection/protocol initialization, stay green when ready, and turn red when
  disconnected. The compact sequence phase sits above Cancel/Unlink.
- A chip opens the target's full device UI using its sequence-held activation.
  Simulator regression verified Canon control entry/Back while Canon and
  Tascam both remained active. Manual device controls retain the sequence's
  logical phase; stable Start/Stop actions remain gated until every target is
  protocol-ready.
- Native tests passed 30/30. `ui_sim` built and all captures completed,
  including connecting, ready, borrowed Canon control, and disconnected-camera
  sequence states. The post-Stop settings capture had 14,416 bytes free,
  17,012-byte peak LVGL use, and 1% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 943,916 bytes flash / 137,996 bytes RAM;
  `crowpanel_128_roboto` 913,460 / 137,996;
  `canon_ble` 942,716 / 136,636;
  `canon_trigger` 939,882 / 136,268;
  `tascam_x8` 941,732 / 136,476.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical chip touch targets, border animation, camera power-off/recovery, and
  preservation of the second live BLE link remain operator-pending.
- Follow-up: the compact run status now uses semantic colors: blue for
  connection/transitions, green for Ready/Done, red for Recording/Failed/not
  connected, and muted text for Idle. Native tests remained 30/30 and the full
  simulator capture set passed. Updated firmware builds succeeded:
  `crowpanel_128` 944,044 bytes flash / 137,996 bytes RAM;
  `crowpanel_128_roboto` 913,588 / 137,996;
  `canon_ble` 942,844 / 136,636;
  `canon_trigger` 940,010 / 136,268;
  `tascam_x8` 941,860 / 136,476. The updated `crowpanel_128` flashed
  successfully to `/dev/cu.usbserial-211240`.
- Follow-up: red chip borders now mean terminal connection failure only.
  WaitingRetry/WaitingConnect backoff remains breathing blue even when a driver
  temporarily reports `Disconnected`; powered-off or otherwise idle-disconnected
  targets use muted gray. The simulator now captures an explicit 30-second
  connection timeout and recovery. Native tests passed 30/30; every simulator
  capture passed. Updated builds succeeded: `crowpanel_128` 944,166 bytes flash
  / 137,996 bytes RAM; `crowpanel_128_roboto` 913,710 / 137,996; `canon_ble`
  942,974 / 136,636; `canon_trigger` 940,140 / 136,268; `tascam_x8` 941,982 /
  136,476. The corrected default firmware flashed successfully to
  `/dev/cu.usbserial-211240`.
- Follow-up: fixed Delete silently returning while automatic sequence
  preparation was in `Connecting`. Delete now cancels connection-only
  preparation, releases its held links, and removes the sequence; it remains
  disabled during Start/Stop execution and while armed. The simulator includes
  a connecting-delete regression that verifies the scene record and every held
  activation are removed. Native tests passed 30/30 and all simulator captures
  passed. Updated builds succeeded: `crowpanel_128` 944,276 bytes flash /
  137,996 bytes RAM; `crowpanel_128_roboto` 913,820 / 137,996; `canon_ble`
  943,084 / 136,636; `canon_trigger` 940,250 / 136,268; `tascam_x8` 942,092 /
  136,476. The delete fix flashed successfully to
  `/dev/cu.usbserial-211240`.

### 2026-08-04: Persistent BLE connection pool

- Added ADR-022 and replaced screen-scoped teardown with a four-session
  retained pool. Foreground and sequence owners are tracked per instance;
  protocol-ready sessions survive navigation, unfinished attempts stop when
  their final owner leaves, and unexpected drops retain the driver's bounded
  reconnect policy.
- Added safe LRU eviction and explicit Disconnect. Foreground, sequence-owned,
  pending-command, and confirmed-recording sessions cannot be auto-evicted;
  confirmed recording requires a second confirmation before Disconnect.
- Refactored compiled drivers to route lifecycle, commands, runtime state, and
  pairing updates by instance. Canon Trigger and Canon Smart now have three
  fixed client sessions each, allowing same-driver connections without dynamic
  allocation. Sequence prepare reuses retained targets and Done/Back releases
  only sequence ownership.
- Native tests passed 32/32, including ready/unready Back behavior,
  same-driver sessions, safe LRU, recording protection, retained sequence
  cancellation, and reuse without reactivation. `ui_sim` built and completed
  every capture; the management and recording-confirmation layouts fit the
  240x240 panel. Peak LVGL use remained 17,012 bytes with 1% fragmentation at
  the post-Stop checkpoint.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 947,390 bytes flash / 139,244 bytes RAM;
  `crowpanel_128_roboto` 916,934 / 139,244;
  `canon_ble` 945,506 / 137,604;
  `canon_trigger` 941,852 / 136,612;
  `tascam_x8` 943,204 / 136,508. The default profile uses 1,248 bytes more
  static RAM than the preceding documented build because the Canon drivers now
  reserve per-instance client state.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240` after
  granting direct serial access. Physical retained reopen, off-screen retry,
  same-driver concurrency, safe eviction, recording protection, and multi-link
  heap behavior remain operator-pending.

### 2026-08-04: Experimental Home Assistant entity tranche

- Added ADR-023 and preserved the in-progress multi-instance manager as the
  baseline. `DriverId::HomeAssistant` supports four dynamic entity profiles for
  `light`, `switch`, `input_boolean`, `button`, `scene`, and `script`, with only
  explicit On/Off, Press, and Activate capabilities. Configured-device capacity
  is now 12; active instance capacity is eight and remains distinct from the
  four-link BLE central limit.
- Device persistence is schema v2 and decodes v1 BLE identity records unchanged.
  Canonical HA entity IDs/domains live in tagged device records. Wi-Fi SSID and
  password, local HA URL, and long-lived token use a separate checksummed NVS
  record; Portal config responses omit password/token.
- Implemented temporary-AP Portal setup with a WPA2 password shown on-panel,
  a Wi-Fi-only bootstrap page, then a station-bound LAN Portal for Home
  Assistant setup and bounded incremental `/api/states` summaries,
  four-slot atomic save/rebind, referenced-entity removal protection, explicit
  Exit, and ten-minute inactivity teardown. Portal entry cancels scenes and
  physical sessions; teardown turns Wi-Fi off.
- Added one lazy shared HA REST/WebSocket runtime using ArduinoJson 7.4.3 and
  arduinoWebSockets. It performs bearer auth, individual initial-state reads,
  active-entity `subscribe_trigger`, bounded queue-only frames, reconnect
  backoff, service calls, subscribed confirmation with five-second timeout, and
  REST refresh after malformed/oversized/dropped updates. Four HA instances
  reuse the session and final eviction/unlink tears it down.
- Added round-panel On/Off, Press, and Activate screens with offline, missing,
  unknown, unavailable, pending, and failure states. Scene pickers and validation
  now use dynamic instance profiles, so safe HA actions can mix with Canon and
  Tascam steps.
- Raising configured capacity exposed a deterministic 64 KiB LVGL allocation
  failure while constructing the Shark run screen. The pool is now 96 KiB. The
  full simulator ran once under AddressSanitizer and then in the normal profile;
  all captures completed, including `28_ha_light.png` through
  `28e_ha_error.png`, `29_ha_button.png`, and `30_portal.png`. With 12 records,
  it reported 60,496 bytes free after init, 34,688 bytes free at the post-Stop
  checkpoint, 20,896-byte peak use, and 24% fragmentation there.
- Native tests passed 35/35, including v1-to-v2 migration, separate checksummed
  secrets/corruption, four-entity capacity, dynamic profiles, HA command/service
  mapping, and HA scene capability validation. `ui_sim` built and executed
  successfully.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 1,644,674 bytes flash / 205,644 bytes RAM (52.3% / 62.8%);
  `crowpanel_128_roboto` 1,614,210 / 205,644 (51.3% / 62.8%); and
  `home_assistant` 1,636,214 / 202,532 (52.0% / 61.8%).
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`. A bounded
  serial read showed normal panel/touch initialization and
  `runtime event=boot ... link=disconnected free_heap=88328 min_free_heap=85888`;
  no Wi-Fi activity was initiated at boot.
- Hardware gate remains open because no local HA URL, token, or Wi-Fi credentials
  were supplied in this session. AP-to-LAN handoff/lifetime, real REST and
  authenticated WebSocket behavior, external state changes, every physical
  action, wrong-token/missing-entity/restart/loss recovery, mixed HA/BLE
  execution, rebind, and ten Portal plus ten runtime heap/socket/task cycles are
  therefore `Blocked` on operator-provided target-server testing. Do not promote
  ADR-023 beyond Experimental until those results are recorded.

### 2026-08-04: Simpler Portal setup password

- Replaced the generated setup password with the fixed WPA2 password
  `12345678`. It remains visible on the Portal screen. A user-configurable AP
  password is deferred; the setup AP exists only while Portal mode is active
  and is replaced by the LAN Portal after Wi-Fi joins.
- Native tests passed 35/35. The UI simulator rebuilt and completed every
  capture; `30_portal.png` confirms the shorter password fits the round panel.
  Firmware builds passed with `crowpanel_128` at 1,644,652 bytes flash /
  205,644 bytes RAM, `crowpanel_128_roboto` at 1,614,188 / 205,644, and
  `home_assistant` at 1,636,192 / 202,532.
- The updated `crowpanel_128` firmware flashed successfully to
  `/dev/cu.usbserial-211240`.

### 2026-08-04: Wi-Fi-first Portal with local-network access

- Split Portal into two listener lifetimes. With no working saved Wi-Fi, the
  SoftAP-bound page collects only SSID/password. After a successful join it
  saves Wi-Fi, returns handoff instructions, destroys the AP listener and AP,
  and starts a new listener bound to the station address.
- The LAN Portal contains Home Assistant URL/token/entity configuration. The
  panel shows the numeric DHCP address and advertises `http://bleep.local`
  through mDNS as a best-effort alias. The LAN listener exists only
  while the panel remains on Portal; Exit, timeout, or Wi-Fi loss tears it down.
  Wi-Fi loss falls back to the Wi-Fi setup AP so credentials can be repaired.
- The simulator now captures both `30_portal.png` Wi-Fi bootstrap and
  `30b_portal_lan.png` local-network states. Target AP-to-LAN handoff, mDNS
  resolution, request reachability, timeout, and heap recovery remain part of
  the open hardware gate.
- Final verification passed: native tests 35/35; `ui_sim` built and completed
  all captures; `crowpanel_128` used 1,675,488 bytes flash / 207,636 bytes RAM
  (53.3% / 63.4%); `crowpanel_128_roboto` used 1,645,032 / 207,636
  (52.3% / 63.4%); and `home_assistant` used 1,667,076 / 204,524
  (53.0% / 62.4%).
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Live AP-to-LAN browser and mDNS behavior still
  requires entering the target studio Wi-Fi on the panel and remains part of
  the open hardware gate.

### 2026-08-04: Wi-Fi discovery, join feedback, and numeric LAN handoff

- Added bounded asynchronous discovery of up to 16 unique visible SSIDs, with
  RSSI/security summaries and retained manual entry for hidden networks.
- Replaced the blocking setup join with a main-loop state machine. The setup
  browser polls connection state while the panel reports scanning, joining,
  connected, missing-network, rejected-password, timeout, and storage failure
  states. A failed attempt leaves the setup AP available for retry.
- Made the assigned DHCP address the authoritative Portal URL shown by the
  browser and panel. `http://bleep.local` remains advertised as a convenience
  alias because client and network mDNS support is not reliable. Successful
  setup allows an eight-second handoff window, shortened after the browser has
  received the numeric address, before destroying the AP and starting the
  station-bound listener.
- Native tests passed 35/35. `ui_sim` built and completed all captures,
  including `30a_portal_connecting.png`, `30aa_portal_wifi_failed.png`, and the
  numeric-address `30b_portal_lan.png`. Firmware builds passed with
  `crowpanel_128` at 1,682,826 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` at 1,652,370 / 207,828
  (52.5% / 63.4%), and `home_assistant` at 1,674,410 / 204,716
  (53.2% / 62.5%).
- The updated `crowpanel_128` firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Real network scan results, connection error
  classification, DHCP handoff, and numeric-address reachability remain target
  hardware checks requiring operator interaction with the studio Wi-Fi.

### 2026-08-04: Home Assistant input-boolean action

- Kept explicit TurnOn/TurnOff capabilities and service calls for safe authored
  scenes, but replaced the two-button entity screen with one context-sensitive
  action. With confirmed OFF state it shows ON and calls `turn_on`; with
  confirmed ON it shows OFF and calls `turn_off`. Unknown state disables it.
- Native tests cover both `input_boolean.turn_on` and `turn_off` payloads plus
  explicit On and Off scene validation. Simulator captures
  `28f_ha_input_boolean.png` and `28g_ha_input_boolean_on.png` verify that the
  single action changes from ON to OFF with confirmed state.
- Native tests passed 35/35 and the full UI simulator completed. Firmware builds
  passed with `crowpanel_128` at 1,683,014 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` at 1,652,558 / 207,828
  (52.5% / 63.4%), and `home_assistant` at 1,674,598 / 204,716
  (53.2% / 62.5%).
- The corrected `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Live HA confirmation of both entity-screen
  directions and explicit sequence On/Off remains part of target testing.

### 2026-08-04: HA command-confirmation reconciliation

- Corrected a false-negative path observed when an `input_boolean` changed in
  Home Assistant but Ble(e)p displayed `ACTION FAILED` or `MISSING`. The
  five-second subscription deadline now keeps the action pending while an
  individual REST state refresh runs. A matching refreshed state completes the
  command successfully; only a confirmed mismatch or refresh failure becomes
  an action failure.
- Restricted `MISSING` to an entity-state HTTP 404 or an actual removed-state
  event. Transport errors, malformed responses, and state events without a
  usable state now produce `UNKNOWN` and schedule reconciliation instead of
  claiming the entity does not exist. Event parsing accepts both the
  trigger-variable shape and a state-event data fallback.
- Native tests passed 35/35. Firmware builds passed with `crowpanel_128` at
  1,683,552 bytes flash / 207,828 bytes RAM (53.5% / 63.4%),
  `crowpanel_128_roboto` at 1,653,096 / 207,828 (52.6% / 63.4%), and
  `home_assistant` at 1,675,144 / 204,716 (53.3% / 62.5%).
- The reconciliation fix flashed successfully to
  `/dev/cu.usbserial-211240`; repeat the previously successful helper action on
  target hardware to close this observed confirmation issue.

### 2026-08-04: Mixed-sequence BLE allocation crash

- Reproduced the reported Sequence 1 reset under a live serial monitor. The
  board logged `BLE_INIT: Malloc failed`, then `assert emi.c 164` and an
  interrupt-watchdog panic. The requested contiguous allocation was `0x7800`.
  This occurred because the authored sequence encountered an HA target first,
  initialized Wi-Fi, and only then lazily initialized the shared BLE central.
- Changed preparation ownership acquisition—not authored action execution—to
  initialize every physical target before any HA target. Rollback now tracks
  the actual acquisition order. A native regression authors the HA action
  first, begins with a retained HA session, verifies that it is evicted, and
  verifies that the physical driver then activates before HA. Pending HA work
  aborts preparation rather than entering the unsafe allocator order. Native
  tests pass 36/36.
- `crowpanel_128` built at 1,683,794 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` built at 1,653,338 / 207,828
  (52.6% / 63.4%), and `home_assistant` built at 1,675,386 / 204,716
  (53.3% / 62.5%). The full UI simulator also completed. The target image
  flashed successfully to `/dev/cu.usbserial-211240`.
  A post-flash monitor remained stable at Home for two minutes, but the exact
  Sequence 1 reopen was not performed during that monitoring window. The
  physical-before-Wi-Fi mitigation therefore remains hardware-unverified and
  ADR-023 stays Experimental.

### 2026-08-04: Mixed-sequence HA connection follow-up

- Investigated the follow-up where the reordered mixed sequence no longer
  crashed but did not connect to Home Assistant. The HA WebSocket disconnect
  callback previously had no handling, so `websocketStarted_` could remain true
  while authentication and subscription could never restart. The callback now
  flips a flag only; `loop()` clears protocol state and schedules a bounded
  reconnect. Secret-free stage logs distinguish Wi-Fi start/timeout,
  WebSocket start/disconnect, authentication, and subscription result and
  include free/largest-allocation heap figures.
- Reduced the LVGL pool from 96 KiB to 80 KiB to return 16 KiB to concurrent
  BLE and Wi-Fi operation. The complete `ui_sim` capture run passed at 80 KiB;
  its most demanding sequence-stop screen retained 18,296 bytes free with a
  20,897-byte peak allocation and 1% fragmentation.
- Native tests passed 36/36. Builds passed with `crowpanel_128` at 1,684,552
  bytes flash / 191,444 bytes RAM (53.6% / 58.4%),
  `crowpanel_128_roboto` at 1,654,096 / 191,444 (52.6% / 58.4%), and
  `home_assistant` at 1,676,132 / 188,332 (53.3% / 57.5%). The corrected
  `crowpanel_128` image flashed successfully to `/dev/cu.usbserial-211240`.
  Boot reported about 102 KiB free heap and the panel remained stable at Home
  for 150 seconds. No sequence activation occurred during the live monitor
  window, so the exact mixed HA connection remains an open hardware check and
  ADR-023 stays Experimental.

### 2026-08-04: HA initial-state readiness recovery

- Found another permanent-wait path during the mixed-sequence audit. After
  WebSocket authentication, a transient failure of the one-time individual
  REST state request left the entity `present == false` forever. The
  subscription could be healthy, but sequence readiness could never complete.
- Initial and reconciliation REST transport errors, non-404 HTTP errors, and
  malformed state JSON now schedule a per-entity retry after two seconds. Only
  one eligible entity is refreshed per main-loop pass, refreshes require an
  authenticated WebSocket, successful state updates clear the retry, and HTTP
  404 remains the terminal missing-entity result. Added a secret-free
  `rest_retry` stage log with HTTP and heap diagnostics.
- Native tests passed 36/36; `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `home_assistant` built successfully. The main
  image uses 1,684,760 bytes flash / 191,460 bytes RAM (53.6% / 58.4%) and
  flashed successfully to `/dev/cu.usbserial-211240`. Live mixed-sequence
  confirmation remains open pending an operator run on this image.

### 2026-08-04: Mixed HA/Canon/Tascam hardware success and SRAM bound

- Captured the remaining failure with the operator reproducing while the UART
  monitor stayed attached. With the 40-row display buffers, ESP-IDF reported
  `Expected to init 4 rx buffer, actual is 2`, `esp_wifi_init 257`, and repeated
  STA-enable failures. The board had about 25 KiB free heap and reached an
  8,376-byte minimum: the blocker was Wi-Fi RX-buffer allocation after two BLE
  sessions, not HA authentication or entity state.
- Reduced each of the two DMA display strips from 40 rows to 20, returning
  19,200 bytes without removing double buffering. The next live run connected
  Wi-Fi, received `auth_required`/`auth_ok`, established the selected-entity
  subscription, brought Tascam and Canon to protocol-ready, and reported
  `all_targets_ready` in 8,569 ms. The operator confirmed the sequence worked.
- That successful run still reached only 1,248 bytes minimum free heap, so the
  final bounds reduce each HA WebSocket frame slot from 4 KiB to 2 KiB and the
  LVGL pool from 80 KiB to 76 KiB. Oversized state events already fall back to
  individual REST. A 72 KiB LVGL attempt stalled the simulator and was
  rejected; 76 KiB completed every capture with 14,192 bytes free on the most
  demanding sequence-stop screen.
- Final verification: native tests passed 36/36; full `ui_sim` passed;
  `crowpanel_128` built at 1,680,660 bytes flash / 164,068 bytes RAM
  (53.4% / 50.1%); `crowpanel_128_roboto` built at 1,650,204 / 164,068
  (52.5% / 50.1%); and `home_assistant` built at 1,672,256 / 160,956
  (53.2% / 49.1%). The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. One mixed run passes; the required ten-cycle
  teardown and heap-recovery gate remains open, so ADR-023 stays Experimental.
