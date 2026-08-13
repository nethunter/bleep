<p align="center">
  <img src="assets/bleep_logo.png" alt="Ble(e)p" width="680">
</p>

<p align="center">
  Open, community-built control and automation for studio gear.
</p>

<p align="center">
  <a href="https://bleep.hml.tech">Website</a> ·
  <a href="docs/manual/manual.md">Owner's guide</a> ·
  <a href="docs/device-support.md">Device support</a>
</p>

# Ble(e)p

**Bluetooth Links Everything, Eventually, Probably.**

The serious expansion is **Bluetooth Low Energy Equipment Panel**: a compact
description of the hardware and its purpose when the joke needs a straight
face.

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
  records, enable/disable, rename, forget/re-pair, and delete. A new or
  factory-reset panel starts with an empty Devices list; add Shark or any other
  physical device explicitly through **+ Add device**. The registry holds up
  to 24 saved devices. Connected devices sort to the front while other devices
  retain their saved order. The Devices list remains unpaged through six
  configured devices, then pages six devices at a time. Long screen, device, and sequence
  titles stay on one line and scroll horizontally instead of covering content.
  Common pages and edit overlays share the same Home-aligned title, navigation
  controls, and content start rather than maintaining separate screen geometry.
  Removing a saved device or deleting a sequence always opens a named,
  cancellable confirmation before changing stored configuration.
- A Home-screen Settings cog with radio-free saved-Wi-Fi status, persistent
  haptic enable/disable, a first-position scrollable About/build page (including
  commit date), sanitized runtime diagnostics, and a normal Factory Reset menu
  item that opens a separate warning screen with a red three-second-hold button.
  Wi-Fi changes still enter the temporary Portal; normal use of Home and
  Settings leaves the radio off.
- On-demand Bluetooth LE connections through one shared NimBLE host. Central
  camera/light links and the Phone Camera HID peripheral share that runtime. Up to
  four physical BLE transport groups stay connected across navigation and
  remain immediately reusable while healthy. Ordinary devices consume one
  group each; every Aputure Light and Zhiyun member provisioned into the
  same panel-owned mesh shares one group and one proxy connection. An ownerless retained session is parked
  after an unexpected drop instead of reconnecting indefinitely. The controller
  retains up to 16 NimBLE bond records independently of the saved-device limit.
- A battery-conscious runtime policy uses compile-time configurable BLE
  transmit power (+6 dBm by default), bounded
  low-duty scan bursts, calmer ready-link intervals, Home Assistant Wi-Fi modem
  sleep, and ownerless reconnect suppression. The screen and backlight remain
  on continuously.
- On-device sequences with generated reverse/inverse Stop by default, an
  explicit Custom Stop override, waits, persistent storage, concurrent device
  preparation, and a hardware-button
  trigger. A partial Start failure can run Stop for cleanup and then retry
  Start; devices already confirmed stopped do not abort that cleanup. There is
  no configured sequence-count ceiling; the registry grows until the panel
  cannot safely allocate or persist another sequence.
- Experimental local Home Assistant control for four selected lights, switches,
  input booleans, buttons, scenes, or scripts through a temporary setup Portal
  and one shared on-demand Wi-Fi session. An authenticated event subscription
  is command-ready even when a memory-gated initial state read is deferred; the
  entity remains visibly `UNKNOWN` until Home Assistant confirms its state.
  A successful service response completes sequence delivery even when an
  idempotent command produces no state-change event.
- A responsive LAN Portal for phone or desktop administration of committed
  devices and current Start/Stop sequences. It can rename, enable, disable, or
  remove existing devices and create, duplicate, reorder, and edit sequences;
  physical pairing and runtime control remain on the panel.
- Experimental native `Aputure Light` support with panel-owned PB-GATT
  provisioning, one cross-brand shared Mesh Proxy connection, per-member
  unicast controls, optimistic state, and the common capability-driven light
  screen. Aputure exposes per-member power and `Set look + On`; ordinary power,
  CCT/tint/brightness, and RGB use the selected fixture's unicast address.
  Automatic group polling is disabled because the previously labeled status
  payload is actually a group power-on command. `0xC000` is never written by
  ordinary device controls and is reserved for future explicit group actions.
  Initial Composition Data, AppKey, model, and group configuration advances
  only after authenticated success statuses; the reported vendor model chooses
  MC Pro versus the shared Ace/Pano command model automatically. The MT Pro is
  operator-confirmed working through this control path, but currently shares
  the MC Pro label because its captured composition identity is identical to
  MC Pro's known tuple.
- Experimental `Zhiyun Light` support for multiple MOLUS X100 and X60RGB
  fixtures. Each Add light operation detects the model, provisions a reset
  fixture when needed, persists its mesh-routing selector, and uses confirmed
  proprietary controls through the mesh's retained gateway; X60RGB also
  exposes RGB hue/saturation. Aputure, X100, X60RGB, and Home Assistant lights
  use the same control screen; the old brand-specific light controls are not
  constructed. Unsupported controls are hidden and HA is power-only.
- Separate Camera-family entries for GoPro, Insta360, DJI Osmo, Sony Camera,
  and Phone Camera. GoPro uses the published Open GoPro BLE shutter API;
  Phone Camera advertises a bonded BLE HID volume-key shutter. Insta360
  emulates the captured X5 GPS Remote protocol as
  `Insta360 Remote (Bleep)`, while DJI
  implements the published Osmo controller handshake, shows the camera's
  four-digit verification code during first pairing, and supports start/stop
  plus status subscription. Toggle-only cameras expose an explicit `Shutter Toggle` scene
  action that repeats in generated Stop. Insta360 X5 GPS display state,
  state-aware Start/Stop, and power behavior await exact-identity hardware
  verification; Google Pixel 9 reconnect and shutter behavior is
  operator-confirmed. GoPro MAX2 connection, initial recording state, explicit
  Start/Stop, and camera-confirmed Encoding state are operator-confirmed on the
  panel. Published Sleep plus reconnect-to-wake controls are implemented and
  verified on that MAX2; broader model/platform coverage and the other families remain
  experimental. Sony
  still stops at an explicit protocol-research screen.
- Specialized slider controls for keypoints A-H, joystick positioning,
  speed/hold settings, run direction, looping, and progress.
- A desktop LVGL simulator that renders the real 240x240 UI and captures PNGs
  without a connected board.
- Native tests for protocol parsing, state reducers, device/scene registries,
  command routing, persistence, and shared BLE scheduling.

Groups, existing Sidus mesh import, success-journal rollback, and full
Canon Wi-Fi/CCAPI control are roadmap work. Home Assistant and Aputure Light
are experimental bounded tranches whose hardware gates remain open. See
[project progress](docs/progress.md) for the exact current gates and
[the implementation roadmap](docs/implementation-roadmap.md) for sequencing.

## Device support

| Device | Status | What Ble(e)p can do |
| --- | --- | --- |
| iFootage Shark Nano II | Current | Pair/reconnect, battery, keypoints, manual movement, timing, loop/direction, and run control. |
| Canon EOS R6 Mark II/III via BR-E1 mode | Current | Stateless movie-record trigger through `Canon (Trigger)`. There is no recording-state readback. |
| Canon EOS R6 Mark II/III smartphone mode | Current, bounded scope | Bonded BLE pairing, explicit movie start/stop, and camera-reported recording state through `Canon (Smart)` are verified on both models; automatic wake and explicit power-down are included in the workflow. |
| GoPro MAX2 (Open GoPro BLE) | Experimental, verified bounded path | Bonded pairing, explicit Start/Stop, confirmed recording state, Sleep, physical wake, and post-boot Ready are operator-confirmed on the panel. Other models and coexistence coverage remain open. |
| Phone Camera — Google Pixel 9 | Experimental; verified path | Bonded reconnect and mixed-sequence BLE HID volume-up shutter are operator-confirmed. Other models and multi-phone coverage remain open. |
| Insta360 X3 | Experimental, verified bounded path | Operation through Ble(e)p's GPS Remote path is operator-confirmed. Model-specific capture, full reconnect/power coverage, and coexistence remain open. |
| Insta360 X4 | Experimental, verified bounded path | Operation through Ble(e)p's GPS Remote path is operator-confirmed. Model-specific capture, full reconnect/power coverage, and coexistence remain open. |
| Insta360 X4 Air | Experimental, verified bounded path | Operation through Ble(e)p's GPS Remote path is operator-confirmed. Model-specific capture, full reconnect/power coverage, and coexistence remain open. |
| Insta360 X5 | Experimental, verified bounded path | Pairing, immediate sequence Start, camera-reported recording state, Start/Stop, shutdown, and physical wake are operator-confirmed. The returning wake connection has a new address-routing fix awaiting confirmation. |
| Insta360 GO 3 | Experimental candidate | GPS Remote compatibility has not been tested on this model. |
| Insta360 GO Ultra | Experimental probe | Separate target with no established GPS Remote compatibility or hardware result. |
| DJI Osmo Action 5 Pro | Current, bounded scope | On-panel first-pair verification, explicit recording start/stop, and camera-originated recording status are operator-confirmed. Reconnect, forget/re-pair, and coexistence remain open. |
| DJI Osmo 360 | Current, bounded scope | On-panel first-pair verification, explicit recording start/stop, and camera-originated recording status are operator-confirmed. Reconnect, forget/re-pair, and coexistence remain open. |
| Sony Camera | Research | Separate catalog entry with recoverable capture-required onboarding; no device record is committed until the peripheral-role protocol is verified. |
| Tascam Portacapture X8 + AK-BT1 | Current, bounded scope | Record start/stop and recorder-confirmed state, including state restoration after reconnect. |
| Home Assistant local entities | Experimental; mixed sequence verified | Four selected `light`, `switch`, `input_boolean`, `button`, `scene`, or `script` entities over local HTTP/WebSocket. Four-link BLE coexistence plus accepted Start/Stop delivery is hardware-verified; full domain/lifecycle coverage remains open. |
| Aputure Light | Experimental; Ray 60c and MT Pro verified paths | Automatically selects one stable candidate, or shows a bounded picker when multiple factory-reset Aputure/amaran fixtures are nearby, before provisioning onto the panel-owned mesh. Per-node power/look routing follows the working Studio Lighter unicast path. The exact amaran Ray 60c and Aputure MT Pro are operator-confirmed working. MT Pro is currently mislabeled as MC Pro because both report the known `0x03F6:0x1000` vendor tuple and the same composition header IDs. One MT/MC comparison found distinct Mesh Device UUID prefixes, but mapping from that single pair remains research. Ace 25c, Pano 60c, Pano 120c, and MC Pro identification is implemented; broader physical selection, multi-fixture isolation, recovery, and soak gates remain open. |
| Zhiyun Light (MOLUS X100 / X60RGB) | Experimental | Uses the same conditional onboarding picker, then adds the selected fixture as a logical member of the one-slot mesh. Ble(e)p persists a per-member proprietary routing selector and multiplexes `0xFEE9` control with the standard Mesh Proxy bearer on one retained gateway. X100 supports power and CCT/brightness; X60RGB adds RGB hue/saturation. |
| Deity PR4 | Later | Transport and protocol research have not started. |

Compatibility claims are deliberately narrow. Read
[device support](docs/device-support.md) for tested models, pairing modes,
limitations, and remaining hardware checks.

## Hardware

The current target is the **ESP32-C3 CrowPanel 1.28-inch round display**:

- 240x240 GC9A01 LCD;
- CST816D touch controller;
- PI4IOE5V6408 I/O expander;
- onboard vibration motor;
- optional external button on GPIO 1, active low.

The board does not include battery-voltage sensing, so the firmware cannot show
the controller's own battery level without a hardware modification. Battery
values shown on the Shark screen come from the slider.

### Controls

- **Touch:** Home, device management, sequences, connection, keypoints,
  positioning, run controls, per-keypoint settings, and explicit Canon/GoPro
  camera power controls. Reopening or preparing a Canon Smart or sleeping
  GoPro that Ble(e)p powered off reconnects and attempts its device-specific
  wake path for up to 30 seconds, then returns to a retryable asleep state. An
  accepted touch click produces a crisp haptic tap; a device becoming ready or
  opening an already-connected device uses two quick ticks; Back uses two
  uneven taps; and a newly surfaced error uses two strong pulses. Scrolling
  and canceled touches do not produce feedback.
- **Button (GPIO 1):** A short press activates the current primary action. On a
  sequence run screen it starts from Ready and stops once armed or while Start
  is running. Device screens similarly dispatch their primary Shark, Canon,
  GoPro, Phone Camera, or Tascam action. A 700 ms hold navigates Back, cancels,
  or closes the current
  overlay. Continuing the same hold to 2 seconds safely unwinds the remaining
  navigation path and returns directly to Home. Recognized short presses use
  the normal tap, while each handled hold stage uses the Back pattern. The
  button has no power behavior; the hardware SPDT switch controls controller
  power.

### Pin assumptions

| Function | GPIO / address |
| --- | --- |
| LCD DC / CS / SCK / MOSI | GPIO 2 / 10 / 6 / 7 |
| I2C SDA / SCL | GPIO 4 / 5 |
| Touch interrupt | GPIO 0 |
| External button | GPIO 1, active low |
| Vibration motor | I/O expander P0 |
| I/O expander | I2C `0x43` |
| CST816D touch | I2C `0x15` |
| BM8563 RTC | I2C `0x51` |

## Build, test, and flash

You need a compatible Python 3 installation and PlatformIO. From the repository
root:

This `0.2.0-dev` naming baseline intentionally does not migrate the former
Amaran driver IDs or `amaran_mesh` NVS key. Before flashing it over an earlier
development build, use **Settings > Factory Reset** on the currently installed
firmware. The reset clears saved configuration; it does not erase firmware.

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements.txt

PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio test -e native
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e bleep
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e bleep -t upload
```

The main Montserrat profile compiles every current driver and is the standard
local firmware check. GitHub Actions is the cross-profile testing ground: every
pull request and push to `main` builds the full Montserrat and Roboto profiles,
plus the isolated Shark, Canon Smart, Canon Trigger, Tascam X8, Home Assistant,
  Aputure Light, Zhiyun, GoPro, Phone Camera, Insta360, and DJI Osmo profiles.
Build an isolated profile locally only when diagnosing a profile-specific
failure.

After native tests and all firmware profiles pass on a push to `main`, GitHub
Actions also updates the **Latest development firmware** prerelease with the
full Montserrat `bleep` application image and its SHA-256 checksum. The
image is a development snapshot, flashes at offset `0x10000`, and preserves the
panel's existing NVS configuration partition; it has not necessarily passed
the physical hardware release gates.

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

The panel always boots to Home. Open **Devices** and choose a device from its
category to go straight into pairing. The device appears in the saved Devices
list only after pairing reaches protocol-ready state; Back or a failed attempt
does not leave an unpaired record behind. Pairing mode matters:

- Shark: make the slider discoverable; Ble(e)p matches service `0xFFF0` or a
  `Nano`/`Shark` advertised name.
- Canon Trigger: use the camera's Bluetooth remote / BR-E1 menu.
- Canon Smart: use **Connect to smartphone → Add a device to connect to**. A
  saved phone registration and a BR-E1 bond are different pairings.
- Tascam X8: install the AK-BT1 and make the recorder available to its remote
  app connection.
- Zhiyun Light: choose **Add light -> Zhiyun**, then add one nearby X100 or
  X60RGB. Factory-reset and previously provisioned fixtures are accepted.
  Ble(e)p matches the product-qualified `PL105_` or `X104_`/`plx104` identity,
  provisions `0x1827` when needed, then rediscovers `0x1828` and opens the
  direct control service. Repeat Add light for every fixture.

When adding another Canon camera in either mode, Ble(e)p ignores Canon bodies
that are already saved or bonded to it, including cameras advertising from
Bluetooth standby. After pairing, known `EOSR6m2_...` and `EOSR6m3_...`
identities are shown as **Canon EOS R6 Mark II** and **Canon EOS R6 Mark III**.
**Retry** stays locked to the saved body; use **Forget** before deliberately
pairing that entry to a different camera.

Open **Portal** from Home and join the open temporary `Bleep-Setup-XXXXX`
network by scanning the on-panel QR code or selecting the SSID whose
five-character suffix matches that panel. Browse to the setup address shown on
the panel; phones may open it automatically as a captive sign-on page. The
setup Portal provides **Overview**, **Devices**, **Sequences**, and **Wi-Fi**
without requiring studio Wi-Fi. Devices can be renamed, enabled/disabled, or
removed; removal is blocked while a sequence references the device. Add
physical devices on the panel so their pairing flow stays local.

Home Assistant is the only Portal function that requires studio Wi-Fi. The
nearby-network scan completes before the setup AP opens, avoiding unreliable
in-AP channel hopping. Choose a result or enter a hidden SSID manually. After
Ble(e)p joins, note the numeric LAN address, let the setup AP close, and rejoin
the normal local Wi-Fi. Open that address while Portal remains active;
`http://bleep.local` is a best-effort convenience. The Home Assistant URL starts
as `http://homeassistant.local:8123`. A saved token is reported only as stored
and is never returned to the browser; choose **Change stored token** to replace
it. Select at most four supported entities.

Sequences can be created, renamed, enabled/disabled, duplicated, and deleted.
**Add Sequence** guides creation through Start actions, generated Stop review,
and Name. Editing Start regenerates the read-only reverse/inverse Stop
preview. **Customize Stop** copies that preview into an independent editable
list; **Use generated Stop** discards the override after confirmation. Custom
lists support action and wait steps, in-place editing, and reordering. On the
panel, orphaned custom
rows remain deletable if their target device
was removed, so an invalid sequence can be repaired one step at a time. The
Portal changes configuration only: it does not connect to
devices or run sequences. Choose **Finish & Exit** in the Portal, or Exit on the
panel, after saving; this stops the server and turns Wi-Fi off. Later Portal
sessions join saved Wi-Fi directly and show their current numeric LAN address;
normal Home boot remains network-free.
In LAN mode, the on-panel QR code changes from the temporary open-network join
to the current numeric Portal URL.

Lights and switches expose explicit On/Off controls. Home Assistant
`input_boolean` entities use one context-sensitive action button: it shows ON
while the helper is off and OFF while it is on. Buttons use Press and HA
scenes/scripts use Activate.

This v1 path uses an open setup AP and sends Portal and HA credentials over
local plaintext HTTP. Open Portal only in a controlled location and use the
LAN handoff only on a trusted studio network; TLS,
Home Assistant Cloud, OAuth, and exposing Ble(e)p-controlled hardware back to
Home Assistant are not implemented.

Open **Scenes** to create ordered Start sequences with generated or Custom Stop.
New sequences open Start first: add steps, select the header arrow to review
Stop, optionally customize it, then use the checkmark and standard Rename keypad
to name and finish the sequence.
A scene reaches `Ready`
only when every target has both a physical link and completed protocol setup.
In Start or Custom Stop, tap an existing step to change its action or settings;
waits open a millisecond duration editor and **Set look + On** steps reopen with
their saved CCT/RGB parameters. New looks default to 5600 K, 50% brightness,
and neutral tint. While that editor is open, settled slider or color-wheel
changes preview the look on the selected light after a short debounce. Preview
uses **Set look + On**, so the fixture turns on to make the result visible.
Step rows place their reorder/delete controls below
the action name, and the scrollable list ends with **Add step**. Back from an
existing step's settings returns directly to that Start/Stop list. The Devices
list similarly places **Add device** after the configured devices.
The panel owns those links while the run screen is open. Circular target chips
show connection readiness and open each device's full controls without dropping
the other sequence links. Opening Settings cancels pending preparation and
releases sequence ownership so Rename and the Start/Stop editors are immediately
usable; it does not interrupt an active Start, armed recording, Stop, or a
partial action failure that still permits Stop cleanup. Back or Done also
releases sequence ownership while ready device sessions remain available for
immediate reuse. Mixed BLE/Home Assistant scenes give cold physical targets and
deferred HA connection separate bounded preparation windows. Switching to a
scene that uses the same HA entity transfers its ownership without an idle gap,
keeping the retained Wi-Fi/WebSocket session protected even while new physical
targets prepare. Old-only targets lose sequence ownership but remain retained
while the four-resource pool has capacity; normal LRU eviction reclaims one
only when a new acquisition actually needs room. A shared physical transport
group is likewise protected when any member remains in the next scene. Device
management offers an explicit Disconnect action.

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

BLE transmit power is selected at compile time with
`CONFIG_BLE_TX_POWER_DBM`. Its shared configuration default is `+6` dBm;
define the symbol as an integer from `-24` through `20` in a profile's build
flags when a different range/current trade-off is required. NimBLE maps the
request to a radio level supported by the ESP32-C3.

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
[device-driver checklist](docs/device-support.md#adding-a-future-driver). For
Android vendor-app research, follow the
[screen-recording and HCI capture workflow](docs/protocols/capture-workflow.md).
Raw wireless captures often contain identifiers from unrelated nearby devices;
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
endorsed by iFootage, Canon, GoPro, Insta360, DJI, Sony, Tascam, Amaran, Deity,
Espressif, or Elecrow. Brand
and product names belong to their respective owners.

## License

Ble(e)p is licensed under the [Apache License 2.0](LICENSE), the same license
used by Home Assistant Core. It permits use, modification, and distribution
while preserving attribution and providing an explicit patent grant.
