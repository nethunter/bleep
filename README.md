<p align="center">
  <img src="assets/bleep_logo.png" alt="Ble(e)p" width="680">
</p>

<p align="center">
  An open, community-built controller for the remotes scattered around a studio.
</p>

# Ble(e)p

Ble(e)p is firmware for a small, touch-first hardware remote that can control
many kinds of studio equipment from one place. It started as a replacement
remote for the iFootage Shark Nano II camera slider. The longer-term goal is
bigger: become the **Home Assistant of remote controls**.

That means a shared platform where the community can add device drivers,
support new controller hardware, combine equipment from different brands, and
build repeatable workflows without waiting for every manufacturer to cooperate
with every other one.

Today Ble(e)p runs on an ESP32-C3 CrowPanel 1.28-inch round display and controls
a small but growing set of motion, camera, and audio devices. It is active
firmware development, not a finished consumer product. Some integrations are
capture-backed experiments, and several roadmap features shown in the UI are
not implemented yet.

## Where it started

Ble(e)p began on the
[Hacking Modern Life YouTube channel](https://www.youtube.com/@hml) as a hands-on
project to turn a small round ESP32 panel into a better remote for the iFootage
Shark Nano II. Once that worked, the obvious question was: why stop at one
remote? That experiment grew into the larger open-source vision described here.

## The idea

Studios accumulate one remote per light, camera, slider, recorder, and app.
Ble(e)p aims to replace that pile with an open controller built around a few
principles:

- **One interface, many brands.** Drivers expose capabilities such as record,
  move, power, or brightness through a common model.
- **Local control.** Device links, state, and sequences run on the controller;
  normal operation does not depend on a cloud service.
- **Useful offline.** The panel boots to Home without starting Bluetooth and
  connects only when a device or sequence needs it.
- **Honest state.** A Bluetooth write or protocol ACK is not presented as proof
  that hardware moved, recorded, or powered down.
- **Community-extensible.** New devices live in isolated drivers with captured
  protocol evidence, host tests, and explicit confidence labels.
- **More than one panel.** The current CrowPanel is the first target, not the
  final form factor. Display, touch, transport, and device boundaries are being
  shaped so other hardware can follow.

## What works now

- A round, touch-first Home and Devices interface with persistent device
  records, enable/disable, rename, forget/re-pair, and delete.
- On-demand Bluetooth LE connections through one shared NimBLE central. Up to
  four protocol-ready device sessions stay connected across navigation and
  reconnect automatically until safely evicted or explicitly disconnected.
- On-device sequences with separately authored Start and Stop steps, waits,
  persistent storage, concurrent device preparation, and a hardware-button
  trigger. A partial Start failure can run Stop for cleanup and then retry
  Start; devices already confirmed stopped do not abort that cleanup.
- Experimental local Home Assistant control for four selected lights, switches,
  input booleans, buttons, scenes, or scripts through a temporary setup Portal
  and one shared on-demand Wi-Fi session.
- Specialized slider controls for keypoints A-H, joystick positioning,
  speed/hold settings, run direction, looping, and progress.
- A desktop LVGL simulator that renders the real 240x240 UI and captures PNGs
  without a connected board.
- Native tests for protocol parsing, state reducers, device/scene registries,
  command routing, persistence, and shared BLE scheduling.

Groups, Amaran lighting, generated reverse-Stop sequences, and full Canon
Wi-Fi/CCAPI control are roadmap work. Home Assistant is implemented as an
experimental bounded tranche whose target-server lifecycle gate is still open. See
[project progress](docs/progress.md) for the exact current gates and
[the implementation roadmap](docs/implementation-roadmap.md) for sequencing.

## Device support

| Device | Status | What Ble(e)p can do |
| --- | --- | --- |
| iFootage Shark Nano II | Current | Pair/reconnect, battery, keypoints, manual movement, timing, loop/direction, and run control. |
| Canon EOS R6 Mark II/III via BR-E1 mode | Current | Stateless movie-record trigger through `Canon (Trigger)`. There is no recording-state readback. |
| Canon EOS R6 Mark III smartphone mode | Experimental | Bonded BLE pairing, explicit movie start/stop, camera-reported recording state, automatic wake when reopening an offline camera, and explicit power-down through `Canon (Smart)`. |
| Tascam Portacapture X8 + AK-BT1 | Current, bounded scope | Record start/stop and recorder-confirmed state, including state restoration after reconnect. |
| Home Assistant local entities | Experimental | Four selected `light`, `switch`, `input_boolean`, `button`, `scene`, or `script` entities over local HTTP/WebSocket. |
| Amaran Pano/Ace lights | Research / planned | Power, brightness, CCT, and HSI are planned after Bluetooth Mesh feasibility work. |
| Deity PR4 | Later | Transport and protocol research have not started. |

Compatibility claims are deliberately narrow. Read
[device support](docs/device-support.md) for tested models, pairing modes,
limitations, and remaining hardware checks.

## Hardware

The current target is the **ESP32-C3 CrowPanel 1.28-inch round display**:

- 240x240 GC9A01 LCD;
- CST816D touch controller;
- PI4IOE5V6408 I/O expander;
- optional external button on GPIO 1, active low.

The board does not include battery-voltage sensing, so the firmware cannot show
the controller's own battery level without a hardware modification. Battery
values shown on the Shark screen come from the slider.

### Controls

- **Touch:** Home, device management, sequences, connection, keypoints,
  positioning, run controls, per-keypoint settings, and explicit Canon camera
  power-down. Reopening or preparing a Canon Smart camera that Ble(e)p powered
  off automatically reconnects and attempts the captured wake sequence.
- **Button (GPIO 1):** A short press activates the current primary action. On a
  sequence run screen it starts from Ready and stops once armed or while Start
  is running. Device screens similarly dispatch their primary Shark, Canon, or
  Tascam action. A 700 ms hold navigates Back, cancels, or closes the current
  overlay. The button has no power behavior; the hardware SPDT switch controls
  controller power.

### Pin assumptions

| Function | GPIO / address |
| --- | --- |
| LCD DC / CS / SCK / MOSI | GPIO 2 / 10 / 6 / 7 |
| I2C SDA / SCL | GPIO 4 / 5 |
| Touch interrupt | GPIO 0 |
| External button | GPIO 1, active low |
| I/O expander | I2C `0x43` |
| CST816D touch | I2C `0x15` |
| BM8563 RTC | I2C `0x51` |

## Build, test, and flash

You need a compatible Python 3 installation and PlatformIO. From the repository
root:

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements.txt

PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio test -e native
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e crowpanel_128
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e crowpanel_128 -t upload
```

The main profile compiles every current driver. Smaller driver-specific profiles
are also available:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e canon_ble
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e canon_trigger
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e tascam_x8
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e home_assistant
```

To render the UI on a desktop, install ImageMagick and run:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e ui_sim
.pio/build/ui_sim/program
```

Generated screenshots are written to `sim/screenshots/` and are intentionally
ignored by Git. The firmware uses the `huge_app.csv` single-app partition
layout because BLE, LVGL, and LovyanGFX exceed the default application
partition.

## Using the current build

The panel always boots to Home. Open **Devices**, add a device from its category,
then open that device to begin scanning or reconnecting. Pairing mode matters:

- Shark: make the slider discoverable; Ble(e)p matches service `0xFFF0` or a
  `Nano`/`Shark` advertised name.
- Canon Trigger: use the camera's Bluetooth remote / BR-E1 menu.
- Canon Smart: use **Connect to smartphone → Add a device to connect to**. A
  saved phone registration and a BR-E1 bond are different pairings.
- Tascam X8: install the AK-BT1 and make the recorder available to its remote
  app connection.

For first-time Home Assistant setup, open **Portal** from Home. Join the
temporary `Bleep-Setup-…` WPA2 network using password `12345678`, browse to the
setup address shown on the panel, scan for or manually enter the studio Wi-Fi,
and supply its password. The browser and panel show scanning, joining, and
failure feedback. After Ble(e)p joins, note the numeric LAN address, let the
setup AP close, and rejoin the normal local Wi-Fi. Open that numeric address
while the Portal screen remains active; `http://bleep.local` is also advertised
as a convenience but may not resolve on every client or network. Enter the local
`http://` Home Assistant URL and long-lived access token, then select at most
four supported entities. Exit Portal on the panel after saving; this stops the
LAN server and turns Wi-Fi off. Later Portal sessions join saved Wi-Fi directly
and show their current numeric LAN address; normal Home boot remains network-free.

Lights and switches expose explicit On/Off controls. Home Assistant
`input_boolean` entities use one context-sensitive action button: it shows ON
while the helper is off and OFF while it is on. Buttons use Press and HA
scenes/scripts use Activate.

This v1 path sends Portal and HA credentials over local plaintext HTTP. Use it
only on a trusted studio network; TLS,
Home Assistant Cloud, OAuth, and exposing Ble(e)p-controlled hardware back to
Home Assistant are not implemented.

Open **Scenes** to create ordered Start and Stop lists. A scene reaches `Ready`
only when every target has both a physical link and completed protocol setup.
The panel owns those links while the run screen is open. Circular target chips
show connection readiness and open each device's full controls without dropping
the other sequence links. Back or Done releases sequence ownership while ready
device sessions remain available for immediate reuse. Device management offers
an explicit Disconnect action.

## How the firmware is organized

| Path | Responsibility |
| --- | --- |
| `src/core/` | Driver metadata, persistent registries, typed commands/results, scene execution, `DeviceManager`, and the shared BLE central. |
| `src/devices/<device>/` | One device integration: protocol, state reducer, transport client, driver adapter, and optional specialized UI. |
| `src/ui.cpp`, `src/scene_ui.cpp`, `src/ui/` | Home, Devices, Scenes, shared pickers/record controls, and navigation. |
| `src/main.cpp` | Display, touch, I/O, power-button handling, and the Arduino main loop. |
| `test/` | PlatformIO native tests for host-testable logic. |
| `sim/` | Desktop LVGL harness and fake runtime. |
| `docs/` | Architecture, decisions, roadmap, device support, protocol research, and current handoff. |

All parsing, state mutation, GATT writes, scene transitions, and LVGL access
happen from Arduino `loop()`. NimBLE callbacks only enqueue bounded raw events
or notification bytes. This ownership rule is central to keeping the UI and
device state deterministic.

Drivers are selected at compile time through the current PlatformIO build
flags. Runtime device records are separate from compiled driver metadata, so a
record can remain dormant when a smaller firmware build omits its driver. A
future Arduino-as-component build is expected to expose these options through
Kconfig without changing driver code.

For the complete model, read [architecture](docs/architecture.md),
[architecture decisions](docs/decisions.md), and [scenes](docs/scenes.md).

## Adding support for a device

A useful driver contribution usually includes:

1. a stable driver/model ID and a clear pairing or discovery rule;
2. protocol types and golden vectors that can be tested without hardware;
3. a pure state reducer where the protocol exposes state;
4. a non-blocking client that obeys main-loop ownership;
5. capability and command descriptors used by the common UI and scenes;
6. hardware results that distinguish a link/ACK from observed physical action;
7. documentation with `Research`, `Hypothesis`, and `Blocked` labels where
   evidence is incomplete.

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and the
[device-driver checklist](docs/device-support.md#adding-a-future-driver). Raw
wireless captures often contain identifiers from unrelated nearby devices;
do not open a pull request with an unreviewed capture.

## Roadmap

The broad direction is:

- finish the shared multi-device foundation and physical regression gates;
- add capability-safe groups and reusable generic controls;
- prove Bluetooth Mesh, Wi-Fi/HTTP coexistence, and temporary Portal mode;
- add lighting and full camera integrations;
- harden scenes, recovery, configuration import/export, and release profiles;
- separate board support cleanly enough for community-maintained controller
  hardware.

The versioned [roadmap](docs/implementation-roadmap.md) and
[decision log](docs/decisions.md) are the source of truth. They intentionally
move more slowly than feature ideas: hardware verification gates are
requirements, not suggestions.

## Contributing

Device knowledge, protocol research, host tests, UI work, documentation, and
new hardware targets are all welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md)
before opening a pull request. Use [SECURITY.md](SECURITY.md) for vulnerability
reports, especially anything involving stored pairing material or future
network credentials.

## Safety and trademarks

This firmware can move physical equipment and start or stop real recordings.
Keep people, cables, and equipment clear of motorized hardware. Test protocol
changes at low speed and confirm physical behavior; an ACK alone is not proof
of success.

Ble(e)p is an independent community project and is not affiliated with or
endorsed by iFootage, Canon, Tascam, Amaran, Deity, Espressif, or Elecrow. Brand
and product names belong to their respective owners.

## License

Ble(e)p is licensed under the [Apache License 2.0](LICENSE), the same license
used by Home Assistant Core. It permits use, modification, and distribution
while preserving attribution and providing an explicit patent grant.
