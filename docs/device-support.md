# Device Support

Status values:

- `Current`: implemented in this repository;
- `Planned`: requirements are defined but implementation has not started;
- `Research`: protocol or stack feasibility must be established;
- `Later`: architecture must allow it, but it is outside the first release.

## iFootage

### Shark Nano II

- Status: `Current`, to be preserved through the refactor.
- Transport: Bluetooth LE GATT.
- Capabilities: pairing/reconnect, battery, keypoints A-H, speed, hold,
  movement, run state/progress, loop, and direction.
- Device code: `src/devices/shark_nano_ii/`.
- Protocol: `protocol.*`; client: `client.*`; driver adapter: `driver.*`.
- UI: `ui.*`, preserving the existing keypoint, run, modal, and positioning
  workflows.
- Hardware trigger: from Keypoints, opens Run; on Run, advances the same
  Standby / Start / Stop action as the touch CTA.

Safety: movement and run commands affect physical hardware. ACKs are not proof
of successful movement.

## Amaran lights

### Pano 60c, Pano 120c, and Ace 25c

- Status: `Research` then `Planned`.
- Transport: Sidus/Telink Bluetooth Mesh.
- Initial capabilities: power, brightness, CCT, and HSI.
- Command family: proprietary Telink opcode `0x26`, based on public
  reverse-engineering that must be verified against the target lights.
- Onboarding:
  - provision factory-reset fixtures into a panel-owned mesh;
  - import an existing Sidus/amaran mesh through dedicated Portal mode.

The first release maintains one active studio mesh. Imported keys are secrets.
State may be optimistic where reliable readback is unavailable.

Reference research:

- <https://github.com/wesbos/amaran-BLE-control>
- <https://amarancreators.com/pages/amaran-pano-60c>
- <https://amarancreators.com/pages/amaran-pano-120c>

## Canon cameras

### EOS R6, EOS R6 Mark II, and EOS R6 Mark III

- Status: both ADR-015 Canon choices are compiled. Use the camera menu that
  matches the chosen driver.
- `Canon (Trigger)` (`DriverId::CanonTrigger = 4`): verified BR-E1-compatible
  BLE remote (`00050000-...`, movie-mode `0x88`/`0x08` press/release). Camera
  menu: Bluetooth remote / BR-E1. Capabilities: link + stateless record
  trigger. No recording-state UI.
- `Canon (Smart)` (`DriverId::CanonBle = 2`): BLE-only Camera Connect
  smartphone experiment (ADR-017). Camera menu: Connect to smartphone. Status
  for the production Wi-Fi/CCAPI path remains `Blocked` on network-side
  DHCP/endpoint and CCAPI evidence; the EOS R6 Mark III BLE-to-Wi-Fi handoff
  capture exists.
- Smart experimental transport: smartphone-mode BLE pairing and shooting
  services without starting Wi-Fi.
- Planned Smart transport: smartphone-mode BLE pairing and Wi-Fi handoff,
  followed by CCAPI HTTP over the camera's direct access point.
- Smart experimental capabilities: explicit record start, record stop, and
  camera-notification recording state.
- Capture-backed Smart capabilities: automatic wake from Bluetooth standby
  with mode `03` and explicit power down after shooting with mode `05`. The
  power control reconnects and wakes a camera powered down from that screen.
  Back only releases the panel connection and does not power down the camera.
- Smart hardware trigger: starts from Ready/Unknown and stops from a
  camera-confirmed Recording state. Touch exposes both commands while state is
  unknown.
- Planned Smart capabilities: record start, record stop, and confirmed
  recording state over CCAPI.

Public EOS M6 reverse-engineering reports `00 10` start, `00 11` stop, and
`01 01 02`/`01 01 01` recording-state notifications on the smartphone shooting
service. Sanitized Pixel 9 Pro XL host-HCI captures confirm those values, the
confirmation-first handshake, and mode command `03` with result `05` on the EOS
R6 Mark III. The panel still leaves state unknown until a matching camera
notification is observed and never promotes a GATT write ACK to confirmed
state.

The first bounded hardware tranche targeted the EOS R6 Mark III. Public
reverse-engineering supplied the pairing UUIDs and candidate command bytes.
The same implementation has since passed pairing, movie record triggering, and
bonded reconnect on the EOS R6 Mark II. EOS R6 support is not yet claimed.

EOS R6 Mark II and Mark III BR-E1 pairing, bonded reconnect, and the
movie-mode `0x88`/`0x08` press/release trigger have been functionally verified
for `Canon (Trigger)`. Extended cycle, forget/re-pair, latency, heap, and
coexistence checks remain open. Smartphone-mode pairing, explicit movie
control, state notifications, and reconnect are verified on the EOS R6 Mark III
for `Canon (Smart)`. The EOS R6 Mark II still needs a fresh camera-side
**Add a device** pair; reconnecting a body that still points at an old
smartphone registration produces Canon's **Connection target not found** error
even when the panel is scanning.

### Camera Connect handoff capture

The host-HCI handoff fixture now covers:

1. advertisement and pairing mode used by the EOS R6 Mark III;
2. ordered GATT reads, writes, indications, and notifications after pairing;
3. Wi-Fi handoff request `01` on `00020002-...`;
4. handoff indications `01 03` and `02 03` on `00020003-...`;
5. SSID-like and credential-like characteristic reads, with their values
   removed from the repository fixture.

Implementation still requires network-side evidence for security mode, DHCP,
camera IP and port, and the transition to the first successful CCAPI request.
Direct Camera Access Point setup plus CCAPI Auto Connect remains a valid manual
research path, but it is not equivalent to the automatic `Canon (Smart)`
workflow.

Reference research:

- <https://developers.canon-europe.com/developers/s/article/Latest-CCAPI>
- <https://developercommunity.usa.canon.com/s/article/CCAPI-Function-List>
- <https://github.com/pklaus/canoremote>

## Audio recorders

### Tascam Portacapture X8 Bluetooth

- Status: `Current` for the bounded record-control tranche; broader recorder
  capabilities remain `Research`.
- Expected device type: recorder.
- Required initial capabilities: record start and record stop.
- Desired state: recording, battery, and media status where the protocol
  exposes them.
- Required adapter: Tascam AK-BT1. The captured module identifies as a u-blox
  ANNA-B1 running `4.0.0-004T`.
- Pairing flow: the app connects directly to the `Portacapture X8` BLE
  advertisement; no SMP pairing or encrypted ATT exchange was observed.
- Protocol status: record start, record stop, COBS framing, custom GATT UUIDs,
  session keepalive, and recorder-originated start/stop transition events are
  confirmed from annotated captures. The current recording/stopped field is
  confirmed by controlled reconnects in both states. Extended reconnect
  behavior, battery, and media fields remain unverified.
- Hardware status: start/stop, persisted reconnect, state restoration after
  remote restart, stopping an existing recording, and media-file creation are
  verified on the target panel with the X8/AK-BT1.
- Evidence and golden vectors:
  [protocols/tascam-x8.md](protocols/tascam-x8.md).

### Deity PR4 remote control

- Status: `Later`.
- Expected device type: recorder.
- Required initial capabilities: record start and record stop.
- Desired state: recording, battery, and media status where available.
- Protocol status: unverified. Exact product naming, transport, pairing,
  commands, and readback must be confirmed before implementation.

## Adding a future driver

Every driver must define:

1. stable driver and model IDs;
2. Kconfig option and transport dependencies;
3. discovery and pairing behavior;
4. configuration schema with secret fields identified;
5. capability and command descriptors;
6. state fields and quality rules;
7. generic device-type UI compatibility;
8. protocol tests or captured golden vectors;
9. hardware verification notes and known limitations;
10. flash and runtime memory impact.

A new brand must not add conditional behavior to `SceneRunner`. It integrates
through capabilities and typed commands.

