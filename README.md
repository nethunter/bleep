# Shark Nano II Remote (CrowPanel 1.28)

A physical BLE remote control for the **iFootage Shark Nano II** camera slider,
running on the ESP32-C3 CrowPanel 1.28" round display (240x240 GC9A01 + CST816D
touch). The firmware is a C++/NimBLE port of the reverse-engineered Shark Nano
BLE protocol.

## What it does

- **Pairing + auto-reconnect.** Scans for the slider (custom GATT service
  `0xFFF0` or a `Nano`/`Shark` advertised name), connects, and remembers the
  device in NVS. On boot and after any drop it reconnects automatically; a
  `Re-pair` button on the connect screen forgets the saved device and scans
  again.
- **Keypoints (A-H).** Set, go-to, and delete keypoints. Positioning is done by
  hand using **manual tracking** (toggle on the Keypoints screen), so no
  slide/pan jog controls are needed on the small round UI. Deletes cascade
  through later configured slots, matching the slider's own behavior.
- **Per-keypoint speed/hold.** Tap a keypoint to open its modal and adjust
  travel speed (0-100%) and hold time (seconds) for destinations B-H via a
  read-modify-write of the device timing table. Keypoint A is the route start
  and has no timing. The device may quantize speed; the applied value is read
  back and shown.
- **Run controls.** Standby / Start / Stop, a loop toggle, and a route-direction
  (reverse) toggle, with a live run-progress bar driven by the slider's
  progress notifications.

## Controls

- **Touch:** primary UI (connect screen, Keypoints screen, Run screen, and the
  per-keypoint modal).
- **Button (GPIO 1):**
  - Short press: switch between the Keypoints and Run screens (closes an open
    modal first).
  - Long press: enter ESP32-C3 deep sleep; press again to wake.

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/shark_protocol.*` | Frame envelope (`AA BB <body> <crc32> BB AA`), IEEE CRC32, streaming frame scanner, command builders, and the run-progress parser. |
| `src/shark_client.*` | NimBLE central: scan/connect/auto-reconnect state machine, notification stream buffer, decoded device state, and high-level operator actions. |
| `src/ui.*` | LVGL screens for the 240x240 round panel. |
| `src/main.cpp` | Display/touch/IO bring-up, deep sleep, button, and the main loop. |

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
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e crowpanel_128 -t upload
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio device monitor
```

Built and compile-verified with espressif32 7.0.1 (arduino-esp32 3.x), LVGL
8.x, LovyanGFX, and NimBLE-Arduino 2.x, using the `huge_app.csv` partition
layout (BLE + LVGL + LovyanGFX exceed the default app partition).

## Safety

Movement, delete, standby/start, and go-to commands move real hardware. ACK
notifications are not proof of success; confirm with state notifications and
observed physical behavior.
