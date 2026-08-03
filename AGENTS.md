# AGENTS.md

Guidance for AI coding agents working in this repository.

## What this is

Firmware evolving from a physical **iFootage Shark Nano II** BLE remote into a
compile-time configurable studio controller. It runs on an **ESP32-C3 CrowPanel
1.28"** round display (240x240 GC9A01 LCD + CST816D touch). The current stack is
Arduino-ESP32, NimBLE-Arduino, LVGL 8.x, and LovyanGFX.

`README.md` describes current user-visible behavior. The versioned design,
accepted decisions, ordered implementation phases, and current handoff live
under `docs/`.

## Layout

| Path | Responsibility |
| --- | --- |
| `src/core/*` | Compile-time driver metadata, persistent runtime registry, typed commands/results, and `DeviceManager`. |
| `src/drivers/*` | Adapters from generic device infrastructure to protocol/transport implementations. |
| `src/shark_protocol.*` | Frame envelope, CRC32, frame scanner, command builders, run-progress parser. Pure logic, no Arduino/BLE deps. |
| `src/shark_state.*` | Pure Shark notification-to-state reduction. |
| `src/shark_client.*` | On-demand NimBLE central, notification parsing, decoded state, and Shark actions. |
| `src/ui.*` | Home, Devices, device management, and application navigation. |
| `src/shark_ui.*` | Specialized Shark connect, keypoint, positioning, and run screens. |
| `src/main.cpp` | Display/touch/IO bring-up, button, main loop. |
| `test/` | PlatformIO native tests for protocol and host-testable core logic. |
| `docs/` | Architecture, decisions, roadmap phases, device support, and progress handoff. |
| `include/lv_conf.h` | LVGL build configuration (fonts, widgets, theme). |
| `platformio.ini` | Target firmware and native-test build environments. |

## Roadmap and documentation discipline

Before planning or implementing a change:

1. Read `docs/README.md`, `docs/decisions.md`, and `docs/progress.md`.
2. Read the relevant sections of `docs/architecture.md`,
   `docs/implementation-roadmap.md`, and `docs/device-support.md`.
3. Identify the roadmap phase or explicitly documented spike/tranche that owns
   the work.

Follow these rules while building:

- Work within one phase or explicitly recorded tranche at a time. Do not begin
  dependent roadmap work until the prior completion gate passes, unless the
  deviation is recorded in `docs/decisions.md` and reflected in the roadmap.
- Treat completion gates as requirements, not suggestions. Do not mark a phase
  complete when hardware checks, measurements, or target-device behavior remain
  unverified.
- Preserve dormant configuration records for drivers omitted by a build. Keep
  compiled drivers separate from persistent runtime device instances.
- Follow accepted architecture decisions, especially main-loop ownership,
  neutral Home boot, on-demand device connections, compile-time driver
  selection, and dedicated Portal mode. Add a new ADR instead of silently
  changing an accepted decision.
- Label protocol assumptions as `Research`, `Hypothesis`, or `Blocked`.
  Captured ACKs are not proof of device state or physical success.
- Update `docs/progress.md` at the end of every implementation session with the
  exact build environment, results, measurements, hardware checks, blockers,
  and next safe task. Update `README.md` when current user-visible behavior
  changes.

## Conventions

- C++17, two-space indentation, `namespace`-scoped code. Match the existing
  style of the file you are editing.
- Keep comments focused on non-obvious intent, trade-offs, and hardware/protocol
  caveats. Do not narrate what the code plainly does.
- Threading rule (important): NimBLE callbacks run on the host task and may only
  push raw bytes into the stream buffer or flip flags. All frame parsing, state
  mutation, GATT writes, and **all LVGL access** happen from `loop()`. Do not
  call LVGL or GATT writes from a NimBLE callback.
- UI runs on a small round panel; verify text fits its widget and stays clear of
  the rounded edges. Set fonts/colors explicitly rather than relying on theme
  inheritance.

## Build, flash, and verify (do this after finishing a task)

After completing any code change, **always try to compile and flash to the
connected board**, then report the result. Treat a clean build as the minimum
bar; a successful flash is the goal whenever a board is attached.

Run host tests whenever host-testable protocol, state, registry, persistence,
driver-catalog, command-routing, or scene logic changes:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio test -e native
```

Build every affected firmware profile. At minimum, verify `crowpanel_128`; also
verify alternate profiles when shared configuration, fonts, drivers, or
transports change.

Use the workspace-local PlatformIO (preferred, matches `README.md`):

```sh
# Compile
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128

# Compile + upload to the board
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128 -t upload

# Serial monitor (115200)
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio device monitor
```

The global `pio` (`~/.platformio/penv/bin/pio`) also works if the local venv is
unavailable.

Flashing notes:

- The upload/monitor port is set in `platformio.ini`
  (`/dev/cu.usbserial-211240`). If upload fails because the port is missing,
  the board is likely unplugged or unavailable — report this instead of guessing
  a different port.
- If `-t upload` fails for a reason unrelated to your change (no port, busy
  monitor, permissions), still report that the **build succeeded** and clearly
  state that the flash could not complete and why.
- Do not start a long-running `device monitor` and block on it unless asked;
  prefer a bounded read if you need to confirm runtime behavior.

## Safety

Movement, delete, standby/start, and go-to commands move real hardware. ACK
notifications are not proof of success — confirm with state notifications and
observed physical behavior. Be cautious when changing protocol command builders
or run-state logic.
