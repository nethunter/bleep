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

- `Canon (Trigger)` status: EOS R6 Mark II and Mark III implemented and under
  extended hardware verification; EOS R6 remains `Planned`.
- `Canon (Trigger)` transport: BR-E1-compatible BLE with a stateless movie
  record trigger.
- `Canon (Smart)` status: `Blocked` pending an EOS R6 Mark III Camera Connect
  BLE-to-Wi-Fi handoff capture.
- `Canon (Smart)` transport: smartphone-mode BLE pairing and Wi-Fi handoff,
  followed by CCAPI HTTP over the camera's direct access point.
- Trigger capability: record trigger.
- Hardware trigger: while connected, invokes the same stateless record trigger
  as the touch CTA.
- Planned Smart capabilities: record start, record stop, and confirmed
  recording state.

CCAPI can confirm recording state. BR-E1-style Bluetooth uses the same trigger
for start and stop and does not provide equivalent state readback, so the panel
leaves Bluetooth-only recording state unknown rather than inferring it.

The first bounded hardware tranche targeted the EOS R6 Mark III. Public
reverse-engineering supplied the pairing UUIDs and candidate command bytes.
The same implementation has since passed pairing, movie record triggering, and
bonded reconnect on the EOS R6 Mark II. EOS R6 support is not yet claimed.

EOS R6 Mark II and Mark III pairing, bonded reconnect, and the BR-E1
movie-mode `0x88`/`0x08` press/release trigger have been functionally verified.
Extended cycle, forget/re-pair, latency, heap, and coexistence checks remain
open.

### Camera Connect handoff capture

The public Canon Smart implementations found during research cover
smartphone-mode pairing and BLE shutter control, but not the command that asks
the camera to start its Wi-Fi access point and returns the connection data.
Implementation of `Canon (Smart)` requires a capture containing:

1. advertisement and pairing mode used by the EOS R6 Mark III;
2. ordered GATT reads, writes, indications, and notifications after pairing;
3. the exact request that starts Wi-Fi;
4. SSID, BSSID, security mode, credential, IP, and port responses;
5. timing, reconnect behavior, and camera-screen prompts;
6. the transition from BLE handoff to the first successful CCAPI request.

Until that capture exists, direct Camera Access Point setup plus CCAPI Auto
Connect is a valid manual research path, but it is not equivalent to the
automatic `Canon (Smart)` workflow.

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

