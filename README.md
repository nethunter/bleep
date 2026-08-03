# Studio Remote (CrowPanel 1.28)

A multi-device studio remote foundation running on the ESP32-C3 CrowPanel 1.28"
round display (240x240 GC9A01 + CST816D touch). The current build includes the
**iFootage Shark Nano II** driver and a research-stage **Canon EOS R6 family
BR-E1 BLE** driver plus research-stage **Tascam Portacapture X8** record
control through the AK-BT1 adapter.

## What it does

- **Home + persistent devices.** Boot opens a neutral icon Home (Devices,
  Groups, Scenes, Portal) without initializing Bluetooth. Only Devices is
  active today; the others are reserved. Devices are stored in a versioned
  registry and can be
  added, renamed, enabled, disabled, re-paired, and removed. Add device opens a
  category-grouped list of compiled Motion, Light, Camera, and Recorder drivers
  so the operator chooses the model; choices at their instance limit remain
  visible but unavailable. The current build permits one Shark, up to three
  Canon BLE instances, and one Tascam X8. Rename uses a
  round-native paged keypad with
  large character keys, A-I/J-R/S-Z/number-symbol pages, Space, backspace, and
  case controls.
- **On-demand pairing + reconnect.** Opening the enabled Shark device starts
  scan/connect for service `0xFFF0` or a `Nano`/`Shark` advertised name and
  remembers the pairing in NVS. Reconnect continues while the Shark screen is
  active; Back releases the connection and returns to Devices.
- **Canon BR-E1 record trigger (research).** Opening a Canon device scans for
  the BR-E1-compatible service, bonds as a remote, remembers the camera, and
  exposes one movie record trigger. The control screen uses the configured
  device name as its title. The camera must be in movie mode with remote
  control enabled. BLE cannot distinguish start from stop or read recording
  state, so the panel deliberately shows neither. Pairing, bonded reconnect,
  and the movie trigger are verified on the EOS R6 Mark II and Mark III;
  extended stability checks remain open.
- **Tascam X8 record control (research).** Opening a Tascam recorder scans for
  the `Portacapture X8` advertisement and connects through the required AK-BT1
  adapter. The control screen sends distinct record start/stop commands and
  changes between Ready and Recording only after recorder-originated transition
  events. The custom GATT UUIDs, COBS framing, session keepalive, commands, and
  transition vectors are documented in
  [`docs/protocols/tascam-x8.md`](docs/protocols/tascam-x8.md). Initial hardware
  verification from this controller remains open.
- **Keypoints (A-H).** Set, go-to, and delete keypoints. The Keypoints screen
  shows configured slots plus the next unset slot only, matching the slider's
  sequential route model. Tapping the next unset slot opens a positioning overlay:
  choose **Move by hand** to unlock the slider for hand positioning, or
  **Joystick** for slide/pan jogging; press **Save** to store the current slider
  position. Deletes cascade through later configured slots, matching the slider's
  own behavior.
- **Per-keypoint speed/hold.** Use the gear button on a configured keypoint to
  adjust travel speed (0-100%) and hold time (seconds) for destinations B-H via
  a read-modify-write of the device timing table. Keypoint A is the route start
  and has no timing. The device may quantize speed; the applied value is read
  back and shown.
- **Run controls.** Standby / Start / Stop, labeled Loop on/off and
  Forward/Reverse toggles, and a live run-progress bar driven by the slider's
  progress notifications.

## Controls

- **Touch:** Home, Devices and device management, connect, Keypoints, joystick
  positioning, Run, and per-keypoint settings.
- **Button (GPIO 1):**
  - Short press: navigate back outside device control or activate the active
    device's primary action. In Shark control it closes an open modal, opens Run
    from Keypoints, then advances Standby / Start / Stop on the Run screen. In
    connected Canon Trigger control it sends the record trigger. In connected
    Tascam control it explicitly starts from Ready/Unknown and stops from a
    recorder-confirmed Recording state.
  - Long press: power off the remote. When off, hold the button again to wake it;
    a short tap wakes briefly and goes back to sleep.

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/core/*` | Driver catalog, typed commands/results, persistent device registry, and loop-owned device manager. |
| `src/devices/<device>/*` | Per-device protocol, state, transport client, generic-driver adapter, and specialized UI. |
| `src/devices/shark_nano_ii/*` | Shark frame protocol, host-testable state reduction, on-demand NimBLE client, driver adapter, and specialized controls. |
| `src/devices/canon_ble/*` | Research-stage BR-E1 pairing/trigger protocol, on-demand NimBLE client, driver adapter, and camera screen. |
| `src/devices/tascam_x8/*` | Captured AK-BT1 protocol, COBS state parser, on-demand NimBLE client, driver adapter, and recorder screen. |
| `src/ui.*` | Home, Devices, and application navigation. |
| `src/main.cpp` | Display/touch/IO bring-up, button, and the main loop. |
| [`assets/icons/`](assets/icons/README.md) | Home mode source artwork and the prompt recipe for generating matching icons. |
| `src/assets/*` | Generated LVGL image arrays; rebuild them with `tools/gen_icons.py`. |

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
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e canon_ble
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/platformio run -e tascam_x8
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
