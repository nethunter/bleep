# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: bounded on-device Scenes tranche (ADR-019/020) beside dual Canon
  drivers, Tascam X8, and remaining Phase 0/foundation hardware gates.
- Firmware state: Home-first, persistent device registry, on-demand Shark,
  Canon (Trigger)/(Smart), Tascam X8, and panel Scenes with authored Start/Stop
  lists, prepare-on-open concurrent links (`Ready`), settings cog
  (rename/edit/delete), and NVS scene persistence. Lazy UI allocation keeps
  Home/Devices resident; scene UI loads on demand.
- Universal driver framework: Bounded multi-active links while a sequence run
  screen is open / running / armed (Canon Smart + Tascam); exclusive
  single-active activation remains for manual device screens.
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

Exercise Press Record / Press Stop on hardware, then remaining Canon/foundation
gates:

1. With paired Canon Smart (R6 II or III) and Tascam X8 configured, open a
   Press Record sequence, confirm both devices connect to Ready, Start, confirm
   both enter recording with the 500 ms gap, then Stop and confirm both stop.
2. Confirm device screens refuse open while a sequence holds links; Back/Cancel
   releases links.
3. Confirm scene persistence across power cycle.
4. Continue Canon Trigger/Smart and Shark foundation hardware gates as before.
5. Keep groups, lights, Portal scene editing, and generated reverse-Stop
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

- Analyzed Pixel 9 Pro XL host-HCI captures of fresh EOS R6 Mark III
  smartphone pairing, BLE movie control, bonded reconnect, and successful
  Camera Connect Wi-Fi offload.
- Added sanitized ATT-only fixtures
  `docs/protocols/dumps/canon-camera-connect-pairing.pcapng`
  (`fac58a7277072f25b45c91f5051dae9c335d71ca9323e9388b69f6e3399cd08c`)
  and `docs/protocols/dumps/canon-camera-connect-wifi-handoff.pcapng`
  (`25e59aca42f47a9ca554fd85273f8bfe5b9f5577d96c9d59051838e407bf17ad`).
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

- Aligned the ADR-017 client with the Pixel 9 Pro XL host-HCI fixtures:
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

