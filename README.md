# Studio Remote (CrowPanel 1.28)

A multi-device studio remote foundation running on the ESP32-C3 CrowPanel 1.28"
round display (240x240 GC9A01 + CST816D touch). The current build includes only
the **iFootage Shark Nano II** driver; its protocol remains a C++/NimBLE port of
the reverse-engineered Shark Nano BLE protocol.

## What it does

- **Home + persistent devices.** Boot opens a neutral icon Home (Devices,
  Groups, Scenes, Portal) without initializing Bluetooth. Only Devices is
  active today; the others are reserved. Devices are stored in a versioned
  registry and can be
  added, renamed, enabled, disabled, re-paired, and removed. The current build
  permits one Shark instance. Rename uses a round-native paged keypad with
  large character keys, A-I/J-R/S-Z/number-symbol pages, Space, backspace, and
  case controls.
- **On-demand pairing + reconnect.** Opening the enabled Shark device starts
  scan/connect for service `0xFFF0` or a `Nano`/`Shark` advertised name and
  remembers the pairing in NVS. Reconnect continues while the Shark screen is
  active; Back releases the connection and returns to Devices.
- **Keypoints (A-H).** Set, go-to, and delete keypoints. The Keypoints screen
  shows configured slots plus the next unset slot only, matching the slider's
  sequential route model. Tapping the next unset slot opens a positioning overlay:
  choose **Set manually** to unlock the slider for hand positioning, or
  **Joystick** for slide/pan jogging; press **Set** to store the current slider
  position. Deletes cascade through later configured slots, matching the slider's
  own behavior.
- **Per-keypoint speed/hold.** Use the gear button on a configured keypoint to
  adjust travel speed (0-100%) and hold time (seconds) for destinations B-H via
  a read-modify-write of the device timing table. Keypoint A is the route start
  and has no timing. The device may quantize speed; the applied value is read
  back and shown.
- **Run controls.** Standby / Start / Stop, a loop toggle, and a route-direction
  (reverse) toggle, with a live run-progress bar driven by the slider's
  progress notifications.

## Controls

- **Touch:** Home, Devices and device management, connect, Keypoints, joystick
  positioning, Run, and per-keypoint settings.
- **Button (GPIO 1):**
  - Short press: navigate back outside device control; switch between Keypoints
    and Run inside Shark control (closing an open modal first).
  - Long press: power off the remote. When off, hold the button again to wake it;
    a short tap wakes briefly and goes back to sleep.

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/core/*` | Driver catalog, typed commands/results, persistent device registry, and loop-owned device manager. |
| `src/drivers/shark_driver.*` | Adapter between generic device infrastructure and the Shark client. |
| `src/shark_protocol.*` | Frame envelope (`AA BB <body> <crc32> BB AA`), IEEE CRC32, streaming frame scanner, command builders, and the run-progress parser. |
| `src/shark_state.*` | Host-testable notification-to-state reduction. |
| `src/shark_client.*` | On-demand NimBLE central, notification stream, and Shark actions. |
| `src/ui.*` | Home, Devices, and application navigation. |
| `src/shark_ui.*` | Specialized Shark connect, keypoint, positioning, and run screens. |
| `src/main.cpp` | Display/touch/IO bring-up, button, and the main loop. |

NimBLE callbacks run on the host task and only push raw bytes into a FreeRTOS
stream buffer or flip flags; all frame parsing, state mutation, GATT writes, and
LVGL access happen from `loop()`.

The protocol model is documented in the companion reverse-engineering project
(`docs/protocol.md`). See it for command formats, confidence levels, and
caveats.

## Pin assumptions

| Function | GPIO / address |
| --- | --- |
| LCD DC | GPIO 2 |
| LCD CS | GPIO 10 |
| LCD SCK | GPIO 6 |
| LCD MOSI | GPIO 7 |
| I2C SDA | GPIO 4 |
| I2C SCL | GPIO 5 |
| Custom button | GPIO 1, active low |
| Touch INT | GPIO 0 |
| PI4IOE5V6408 expander | I2C `0x43` |
| CST816D touch | I2C `0x15` |
| BM8563 RTC | I2C `0x51` |
| Expander panel power | pin 4 |
| Expander touch/display enable | pin 3 |
| Expander backlight | pin 2 |

## Battery monitoring

This board has **no onboard battery-sense circuit**, so the firmware does not
display the remote's own battery. The battery percentage shown in the UI is the
**slider's** battery, read from its status notifications. (Reading the remote's
battery would require a hardware modification: an external divider from BAT+ to a
freed-up ADC GPIO.)

## Build and flash

Install PlatformIO, then:

```sh
pio run -e crowpanel_128
pio run -e crowpanel_128 -t upload
pio device monitor
```

This workspace also has PlatformIO in `.venv`, with a local core directory:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio test -e native
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128 -t upload
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio device monitor

# Desktop UI screenshots (no board; needs ImageMagick)
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e ui_sim
.pio/build/ui_sim/program   # writes round PNGs to sim/screenshots/
```

Built and compile-verified with espressif32 7.0.1 (arduino-esp32 3.x), LVGL
8.x, LovyanGFX, and NimBLE-Arduino 2.x, using the `huge_app.csv` partition
layout (BLE + LVGL + LovyanGFX exceed the default app partition).

## Safety

Movement, delete, standby/start, and go-to commands move real hardware. ACK
notifications are not proof of success; confirm with state notifications and
observed physical behavior.
