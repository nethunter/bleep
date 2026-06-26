# AGENTS.md

Guidance for AI coding agents working in this repository.

## What this is

Firmware for a physical BLE remote that controls the **iFootage Shark Nano II**
camera slider. It runs on an **ESP32-C3 CrowPanel 1.28"** round display
(240x240 GC9A01 LCD + CST816D touch). Stack: Arduino-ESP32, NimBLE-Arduino,
LVGL 8.x, LovyanGFX. See `README.md` for the full feature and protocol overview.

## Layout

| Path | Responsibility |
| --- | --- |
| `src/shark_protocol.*` | Frame envelope, CRC32, frame scanner, command builders, run-progress parser. Pure logic, no Arduino/BLE deps. |
| `src/shark_client.*` | NimBLE central: scan/connect/auto-reconnect, notification parsing, decoded state, operator actions. |
| `src/ui.*` | LVGL screens for the 240x240 round panel (connect / keypoints / run + per-keypoint modal). |
| `src/main.cpp` | Display/touch/IO bring-up, button, main loop. |
| `include/lv_conf.h` | LVGL build configuration (fonts, widgets, theme). |
| `platformio.ini` | Single build env: `crowpanel_128`. |

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
