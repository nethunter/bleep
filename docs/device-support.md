# Device Support

Status values:

- `Current`: implemented in this repository;
- `Planned`: requirements are defined but implementation has not started;
- `Research`: protocol or stack feasibility must be established;
- `Later`: architecture must allow it, but it is outside the first release.

All current GATT devices use the ADR-021 shared BLE central. They share one
active scanner and bounded asynchronous connection slots while retaining the
matching, pairing, GATT setup, and protocol rules documented below. This is
compile- and host-test-verified. Physical connection and protocol readiness are
tracked separately; Shark, Canon Trigger, Canon Smart, and Tascam publish ready
only after their required setup succeeds. Controller-level connection/security
procedures are queued one at a time, while established links may initialize as
the next target connects. Concurrent Canon Smart + Tascam scene
preparation, ten-cycle timing distributions, and post-teardown heap recovery
remain hardware gates.

ADR-022 retains protocol-ready sessions across navigation and sequences, up to
four active runtime instances. Multiple instances of the same Canon driver use
independent client state. Unexpected drops retry while retained; unfinished
first-time attempts stop when their last owner leaves.

## Home Assistant

- Status: `Experimental`; software/build/simulator gates pass, target HA
  feasibility gate remains open.
- Transport: local plaintext Wi-Fi using bearer-authenticated REST and the
  authenticated `/api/websocket` endpoint. No TLS, cloud, OAuth, or inbound HA
  integration.
- Supported entities: up to four canonical IDs in `light`, `switch`,
  `input_boolean`, `button`, `scene`, and `script` domains.
- Capabilities: power-only On/Off for lights, switches, and `input_boolean`;
  input booleans display these through one context-sensitive On/Off action
  button. Buttons use Press and HA scenes/scripts use Activate. Brightness,
  color, Toggle, arbitrary value actions,
  sensors, covers, climate, media, automations, HA devices, and areas are out of
  scope.
- Provisioning: the temporary WPA2 SoftAP scans nearby networks and collects
  only studio Wi-Fi, with manual SSID entry for hidden networks and visible
  connection progress/failure. Its on-panel QR code joins the setup AP, whose
  scoped wildcard DNS and redirects provide best-effort phone sign-on-screen
  discovery. After joining, the AP closes and the full Portal
  is reachable at the displayed numeric LAN address only while the panel remains
  on its Portal screen. `http://bleep.local` is a best-effort mDNS alias.
  Wi-Fi credentials and token are stored separately from ordinary device
  records and are not returned by the configuration endpoint.
- Runtime: four HA instances share one lazy retained session. Protocol-ready
  requires Wi-Fi, WebSocket authentication, selected-entity subscription, and
  initial REST state. Stateful actions wait for subscribed confirmation; a
  five-second miss reports failure and schedules REST refresh.
- Hardware gate: AP-to-LAN handoff and listener teardown, external state updates, all six domain
  mappings, failure recovery, mixed HA/BLE sequences, and ten lifecycle/heap
  cycles remain unverified on a real local Home Assistant installation.

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

### Generic Amaran Light

- Status: `Experimental`; compiled implementation, real-fixture gate open.
- Transport: PB-GATT provisioning and Mesh Proxy GATT over the shared NimBLE
  central, with one proxy link shared by logical lights.
- Initial capabilities: power, independently remembered 2300-10000 K
  CCT/tint/brightness and RGB/saturation/brightness looks. Sequence authoring
  exposes one `Set color` action with CCT and RGB modes.
- Command family: proprietary Telink opcode `0x26`, based on public
  reverse-engineering that must be verified against the target lights.
- Onboarding: choose `Amaran Light`; the first nearby factory-reset fixture
  advertising Mesh Provisioning is provisioned into the panel-owned mesh. The
  fixture model is not selected because onboarding and the supported Telink
  command family are mesh/protocol concerns rather than catalog concerns.
  Pano 60c, Pano 120c, and Ace 25c remain the initial validation set. Existing
  Sidus/amaran mesh import remains deferred.

The first release maintains one active studio mesh. Keys live in a separate
checksummed NVS record and are not logged or exported. Writes update optimistic
state only. Hardware verification is still required for provisioning and
configuration status responses, proxy fallback, reboot recovery, and safe
node reset before this becomes Current.

Reference research:

- <https://github.com/wesbos/amaran-BLE-control>
- `studio-lighter` working tree under the local research workspace
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
  camera-notification recording state. Record Stop is idempotent when the camera
  already confirms `Stopped`, which lets sequence cleanup continue after a
  partial Start failure.
- Capture-backed Smart capabilities: automatic wake from Bluetooth standby
  with mode `03` and explicit power down after shooting with mode `05`. The
  power control, reopening the saved device, or preparing it for a sequence
  reconnects and wakes a camera powered down from that screen. Back retains the
  panel connection and does not power down the camera.
- Smart hardware trigger: starts from Ready/Unknown and stops from a
  camera-confirmed Recording state. Touch exposes both commands while state is
  unknown.
- Planned Smart capabilities: record start, record stop, and confirmed
  recording state over CCAPI.

Public EOS M6 reverse-engineering reports `00 10` start, `00 11` stop, and
`01 01 02`/`01 01 01` recording-state notifications on the smartphone shooting
service. Minimized Android host-HCI research confirms those values, the
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
- Pairing flow: connect directly to an advertisement carrying the custom
  Tascam primary service UUID. Observed local names include `Portacapture X8`
  and the module-derived `ANNA-B1-…`; no SMP pairing or encrypted ATT exchange
  was observed.
- Protocol status: record start, record stop, COBS framing, custom GATT UUIDs,
  session keepalive, and recorder-originated start/stop transition events are
  confirmed from annotated captures. The current recording/stopped field is
  confirmed by controlled reconnects in both states. Extended reconnect
  behavior, battery, and media fields remain unverified.
- Hardware status: start/stop, persisted reconnect, state restoration after
  remote restart, stopping an existing recording, and media-file creation are
  verified on the target panel with the X8/AK-BT1. Record Stop is idempotent
  when the recorder already confirms `Stopped`, so a partial sequence Start can
  clean up other targets and become restartable.
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
