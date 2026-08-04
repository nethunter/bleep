# Studio Remote (CrowPanel 1.28)

A multi-device studio remote foundation running on the ESP32-C3 CrowPanel 1.28"
round display (240x240 GC9A01 + CST816D touch). The current build includes the
**iFootage Shark Nano II** driver, two Canon choices — verified **Canon
(Trigger)** BR-E1 BLE and experimental **Canon (Smart)** smartphone BLE — plus
research-stage **Tascam Portacapture X8** record control through the AK-BT1
adapter.

## What it does

- **Home + persistent devices.** Boot opens a neutral icon Home (Devices,
  Groups, Scenes, Portal) without initializing Bluetooth. Devices and Scenes
  are active; Groups and Portal remain reserved. Devices are stored in a
  versioned registry and can be added, renamed, enabled, disabled, re-paired,
  and removed. Add device and Scenes `+ Step` share one picker: a 2×2 category
  icon grid (Motion / Lights / Cameras / Recorders), then a device or driver
  list. Scene steps continue to an action (Record Start / Stop); Wait 500 ms is
  available on the category screen. Driver choices at their instance limit stay
  visible but unavailable. The current build permits one Shark, up to three
  Canon (Trigger) and three Canon (Smart) instances, and one Tascam X8. Rename
  uses a round-native paged keypad with large character keys,
  A-I/J-R/S-Z/number-symbol pages, Space, backspace, and case controls.
- **Scenes (sequences).** Create, edit, and run ordered Start/Stop sequences
  from the panel. The one-tap **Press Record** seed builds: Canon Smart
  `RecordStart`, wait 500 ms, Tascam `RecordStart` for Start; Canon
  `RecordStop` then Tascam `RecordStop` for Stop. Launching a sequence connects
  every target concurrently and holds those links until Stop finishes or Cancel.
  Device screens are blocked while a sequence holds links. Scenes persist in a
  separate NVS blob from the device registry.
- **On-demand pairing + reconnect.** Opening the enabled Shark device starts
  scan/connect for service `0xFFF0` or a `Nano`/`Shark` advertised name and
  remembers the pairing in NVS. Reconnect continues while the Shark screen is
  active; Back releases the connection and returns to Devices.
- **Canon (Trigger).** Opening a Trigger device uses the BR-E1-compatible
  remote service (`00050000-...`) and toggles movie record with `0x88`/`0x08`.
  Put the camera in Bluetooth remote / BR-E1 mode. Fast and stateless — no
  recording-state UI.
- **Canon (Smart) (experimental).** Opening a Smart device scans for the Camera
  Connect pairing service, completes bonded confirmation-first smartphone
  handshaking, runs the captured setup queries, wakes the camera from
  Bluetooth standby, subscribes to shooting state, and sends explicit movie
  Start/Stop commands. The camera screen has a separate power button that
  requests camera power-down and reconnects to wake it again while the screen
  remains open; Back only releases the panel's BLE connection. Use the
  camera's **Connect to smartphone → Add a device to connect to** path, not
  BR-E1 Remote mode and not a previously saved phone entry. If the camera
  shows **Connection target not found**, it is looking for an old smartphone
  registration: delete that connection on the camera, Forget pairing on the
  panel when switching bodies, then pair again while the Canon screen is open
  and scanning. Captured vectors are documented in
  [`docs/protocols/canon-smartphone-ble.md`](docs/protocols/canon-smartphone-ble.md).
  EOS R6 Mark III smartphone control is hardware-verified; EOS R6 Mark II still
  needs a fresh Add-a-device pair.
- **Tascam X8 record control (research).** Opening a Tascam recorder scans for
  the `Portacapture X8` advertisement and connects through the required AK-BT1
  adapter without blocking screen navigation. The control screen sends distinct
  record start/stop commands and changes between Ready and Recording only after
  recorder-originated transition or current-state packets. Confirmed state is
  restored after reconnect. The custom GATT UUIDs, COBS framing, session
  keepalive, commands, and state vectors are documented in
  [`docs/protocols/tascam-x8.md`](docs/protocols/tascam-x8.md). Hardware
  start/stop, persisted reconnect, state restoration after remote restart, and
  media-file creation are verified.
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

- **Touch:** Home, Devices and device management, Scenes create/edit/run,
  connect, Keypoints, joystick positioning, Run, per-keypoint settings, and
  explicit Canon camera power-down / wake.
- **Button (GPIO 1):**
  - Short press: navigate back outside device control or activate the active
    device's primary action. In Shark control it closes an open modal, opens Run
    from Keypoints, then advances Standby / Start / Stop on the Run screen. In
    connected Canon (Smart) control it starts from Ready/Unknown and stops from
    camera-confirmed Recording. In connected Canon (Trigger) control it fires
    the BR-E1 record toggle. In connected Tascam control it explicitly starts
    from Ready/Unknown and stops from recorder-confirmed Recording.
  - Long press: power off the remote. When off, hold the button again to wake it;
    a short tap wakes briefly and goes back to sleep.

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/core/*` | Driver catalog, typed commands/results, persistent device registry, and loop-owned device manager. |
| `src/devices/<device>/*` | Per-device protocol, state, transport client, generic-driver adapter, and specialized UI. |
| `src/devices/shark_nano_ii/*` | Shark frame protocol, host-testable state reduction, on-demand NimBLE client, driver adapter, and specialized controls. |
| `src/devices/canon_trigger/*` | Verified BR-E1-compatible pairing and movie trigger, on-demand NimBLE client, driver adapter, and Trigger screen. |
| `src/devices/canon_ble/*` | Experimental smartphone pairing/session protocol, explicit movie control, notification state reducer, on-demand NimBLE client, driver adapter, and Smart camera screen. |
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
