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
- Protocol code: `src/shark_protocol.*`.
- Client code: `src/shark_client.*`.
- UI: existing keypoint, run, modal, and positioning workflows.

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

- Status: `Planned`.
- Bluetooth transport: emulate Canon BR-E1 behavior for record start/stop.
- HTTP transport: Canon Camera Control API (CCAPI) over the studio Wi-Fi.
- Runtime transport choices:
  - Bluetooth;
  - HTTP;
  - HTTP with Bluetooth fallback.
- Initial capabilities: record start, record stop, and recording state.

CCAPI can confirm recording state. BR-E1-style Bluetooth does not provide
equivalent state readback, so Bluetooth-only state remains optimistic.

Reference research:

- <https://developers.canon-europe.com/developers/s/article/Latest-CCAPI>
- <https://developercommunity.usa.canon.com/s/article/CCAPI-Function-List>
- <https://github.com/pklaus/canoremote>

## Audio recorders

### Tascam Portacapture X8 Bluetooth

- Status: `Later`.
- Expected device type: recorder.
- Required initial capabilities: record start and record stop.
- Desired state: recording, battery, and media status where the protocol
  exposes them.
- Protocol status: unverified. Research must identify the required Bluetooth
  adapter/model, pairing flow, services, command encoding, and readback.

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

