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

ADR-022 retains protocol-ready sessions across navigation and sequences. Up to
eight logical instances map onto four physical BLE transport groups. Ordinary
GATT devices consume one group per instance; every Aputure Light and Zhiyun
member in the panel-owned mesh consumes one group together, and Home Assistant
consumes none. Multiple Canon instances retain independent client state.
Unexpected drops retry while retained; unfinished first-time attempts stop
when their last owner leaves.

## Home Assistant

- Status: `Experimental`; mixed four-link BLE plus local-HA readiness and
  Start/Stop action delivery are hardware-verified. Broader lifecycle and
  entity-domain gates remain open.
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
- Provisioning: **Settings > Wi-Fi** can explicitly scan and join a visible
  studio network from the panel without configuring Home Assistant. The
  temporary open SoftAP remains the hidden-SSID and phone-entry path, with
  manual SSID entry and visible connection progress/failure. Its on-panel QR
  code joins the setup AP, whose
  scoped wildcard DNS and direct non-empty responses to phone connectivity
  probes provide best-effort sign-on-screen discovery. After joining, the AP
  is reachable at the displayed numeric LAN address only while the panel remains
  on its Portal screen. `http://bleep.local` is a best-effort mDNS alias.
  Wi-Fi credentials and token are stored separately from ordinary device
  records and are not returned by the configuration endpoint.
- Runtime: four HA instances share one lazy retained session. Wi-Fi, WebSocket
  authentication, and the selected-entity subscription establish command
  readiness. A memory-deferred initial REST read leaves state explicitly
  unknown. Successful service results complete delivery even when an
  idempotent action produces no state-change event; only REST or subscribed
  events confirm entity state.
- Hardware evidence: a local-HA Sequence 4 reached Ready while Insta360, Phone
  Camera, Canon Smart, and Tascam occupied the four BLE links. HA Start and Stop
  returned successful service results and the operator confirmed both complete
  mixed sequences worked without a false Failed state.
- Remaining gate: AP-to-LAN handoff/listener teardown, external state updates,
  every supported entity domain, failure recovery, and ten lifecycle/heap
  cycles on a real local Home Assistant installation.

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

## Aputure Light

### Generic Aputure Light

- Status: `Experimental`; compiled implementation, real-fixture gate open.
- Transport: PB-GATT provisioning and Mesh Proxy GATT over the shared NimBLE
  central, with one proxy link shared by logical lights across the three tested
  brands.
- Initial capabilities: independently remembered 2300-10000 K
  power, CCT/tint/brightness, and RGB/saturation/brightness controls. Sequence
  authoring exposes `Set look + On` with generated Off.
- Command family: proprietary Telink opcode `0x26`, based on public
  reverse-engineering that must be verified against the target lights.
- Exact-model evidence: the amaran Ray 60c and Aputure MT Pro are
  operator-confirmed working with Ble(e)p. The MT Pro currently appears as
  `Aputure MC Pro`; its control path is verified, but the captured composition
  header and vendor tuple match MC Pro's known values, so its distinct product
  discriminator remains under research. One exact MT/MC comparison found
  different five-character ASCII prefixes in their Mesh Device UUIDs, but that
  one-pair correlation is not yet a production identification rule. These
  results do not close
  multi-fixture isolation, recovery, or soak gates for the generic driver.
- Supported exact models: amaran Ray 60c, amaran Ace 25c, Aputure MC Pro, and
  Aputure MT Pro. “Supported” records the operator-approved physical control
  path for those exact products; it does not close the separately listed
  multi-fixture, recovery, identification, or endurance gates. The amaran Pano
  60c and Pano 120c remain candidates until those exact fixtures are tested.
- Onboarding: choose `Aputure Light`. One compatible candidate is selected
  automatically after a 750 ms settling window; two to four nearby candidates
  use the explicit picker before PB-GATT begins. The selected factory-reset
  fixture is provisioned into the panel-owned mesh. The runtime then reads the
  authenticated Composition Data Status and selects MC Pro
  (`0x03F6:0x1000`) or the shared Ace/Pano model (`0x0211:0x0000`)
  automatically. Exact advertised product naming is kept independently of the
  shared Amaran vendor tuple. Those four fixtures remain the original
  validation set; the separately verified Ray 60c and MT Pro extend the
  exact-model compatibility evidence. Existing
  Sidus/amaran mesh import remains deferred.
  If a provisioned-but-unconfigured fixture returns an unsupported or malformed
  composition, onboarding stops instead of guessing and the recovery screen
  offers all four exact choices. Model-specific support remains blocked until
  each fixture passes its physical gate.
  After PB-GATT, the panel waits for either the selected address or this mesh's
  Network ID instead of treating the fixture's expected reboot as an immediate
  failure. Provisioning and post-provision configuration have bounded rollback
  deadlines and cannot remain pending indefinitely. Saved-mesh reconnect uses
  the same Network ID after one failed direct-address attempt, so a rotating
  proxy address does not make a powered fixture invisible to a sequence.

The first release maintains one active studio mesh. Keys live in a separate
checksummed NVS record and are not logged or exported. The complete mesh is
charged as one physical BLE slot. When any active target is a Zhiyun light, the
shared bearer prefers a same-mesh, product-qualified Zhiyun proxy so its local
`0xFEE9` service and standard Mesh Proxy service are both available. Ace 25c
and MC Pro provisioning, composition evidence, cross-node routing, proxy
fallback, and group-addressed power experiments are confirmed. Standard Generic OnOff
is only a writable shadow/reachability model on both fixtures. Firmware sends
ordinary power, CCT/tint/brightness, and RGB through each fixture's node
unicast address, matching Studio Lighter. Common group `0xC000` is never
written by ordinary device control. Automatic polling is disabled because
`26 0E` is a captured group power-on command, not a read query. MC red and
Ace green were previously optically
observed at 5% after separate group writes, followed by the now-superseded
common-group On/Off test. Four-member power isolation, CCT, property
readback, decoded configuration-status enforcement, reboot/interruption
recovery, Pano fixtures, and safe node reset remain open before this becomes
Current. Source-addressed replies update only the matching session; absent a
verified read-only query, optimistic control state is kept distinct from the
shared proxy bearer state.

Reference research:

- <https://github.com/wesbos/amaran-BLE-control>
- `studio-lighter` working tree under the local research workspace
- <https://amarancreators.com/pages/amaran-pano-60c>
- <https://amarancreators.com/pages/amaran-pano-120c>

## Zhiyun lights

One discoverable `Zhiyun Light` driver owns the shared protocol family. Choose
the entry once for each nearby fixture; the saved records remain independent,
and each retained session selects its X100 or X60RGB profile from the
product-qualified advertisement and identity response.

### MOLUS X100

- Status: `Experimental`; the compile-time shared driver, panel UI, panel-owned
  PB-GATT provisioning, direct GATT initialization, and deterministic command/
  readback path are implemented. Panel-originated hardware verification is open.
- Product identity: internal BLE model marker `pl105`; captured local names use
  `PL105_` plus a device-specific suffix. The first Zhiyun member in the tested
  mixed mesh uses routing selector `0`, independent of product model.
- Transport: standard no-OOB PB-GATT (`0x1827`) for factory-reset onboarding,
  Mesh Proxy (`0x1828`) after provisioning, and direct proprietary control on
  service `0xFEE9`.
- Captured capabilities: power, float32 brightness, and uint16 CCT. ZHIYUN's
  published device limits are 0-100% and 2700-6500 K. Although captured ZY
  Vega writes include 50 K boundaries, live device readback quantizes them to
  100 K; the driver canonicalizes to 100 K before exact verification.
- State quality: setters have no per-write acknowledgement, but a write followed
  by correlated reads of power, brightness, and CCT was live-confirmed. A
  driver can therefore remain non-optimistic by publishing the value only
  after matching device-originated readback.
- Implemented tranche: Add light lists compatible advertisements and requires
  an explicit stable-token selection before connecting to a factory-reset
  `pl105` on `0x1827` or a provisioned one on `0x1828`. A reset light receives the shared
  panel-owned network and a durable Device Key/unicast allocation, then is
  rediscovered and validated on `0xFEE9`. The normal device record commits only
  after confirmed Ready; failed or canceled onboarding restores the provisional
  mesh allocation without rewinding reserved sequence high-water. A failed
  rollback save keeps the pending add and snapshot retryable. Post-provision
  discovery accepts only the selected BLE identity or a Mesh Proxy Network ID
  matching this panel's Network Key. Power and CCT/
  brightness commands remain pending until matching correlated replies arrive;
  scenes therefore wait for confirmation instead of treating the write as
  success. X100 exposes no tint or RGB capability at runtime.
- Missing before production: power-loss reconciliation after a light accepts
  Provisioning Data, physical picker selection and competing-panel recovery,
  verified reset/retry,
  boundary and power-cycle checks, rotating-address recovery, firmware
  compatibility policy, multiple live fixtures, retained/session and mixed-device
  coexistence measurements, plus independently observed optical output.
- Evidence and golden vectors:
  [protocols/zhiyun-x100.md](protocols/zhiyun-x100.md).

### MOLUS X60RGB

- Status: `Experimental`; Android HCI evidence, protocol builders/parsers,
  shared-driver model selection, host tests, and panel UI are implemented.
  Panel-originated control is the remaining immediate hardware gate.
- Product identity: internal BLE marker `plx104`; captured local names use
  `X104_` plus a device-specific suffix.
- Transport: the same no-OOB PB-GATT `0x1827`, post-provision Mesh Proxy
  `0x1828`, and proprietary `0xFEE9` direct-control service as the X100.
- Captured capabilities: power, float32 brightness, uint16 CCT, float32 hue in
  degrees, and float32 saturation percent. Selector `0` was live-verified when
  X60RGB was the first Zhiyun member but the third standards-mesh node; the
  selector is persisted as a member route rather than derived from model.
- State quality: X60RGB setters returned replies correlated by sequence and
  command. Ble(e)p confirms RGB hue, saturation, and brightness in the
  capture-backed order;
  shared power/CCT control keeps the conservative read-after-write path.
- Implemented tranche: the same bounded Add-light picker detects X60RGB only
  after operator selection and then opens CCT and RGB tabs. RGB UI values remain responsive and debounced, while command
  completion waits for matching device-originated replies.
- Missing before production: physical panel verification, mode/effect command
  research, reset and interrupted-provisioning recovery, firmware compatibility,
  simultaneous-X100/X60RGB coexistence, and optical output checks.
- Evidence and golden vectors:
  [protocols/zhiyun-x60rgb.md](protocols/zhiyun-x60rgb.md).

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
- Both Canon drivers persist the bond-resolved identity address rather than a
  temporary private address. New pairing ignores bodies already owned by a
  saved Canon entry or an existing panel bond, including standby advertising;
  Retry remains locked to the saved body and Forget is the explicit path to a
  replacement body.
- Captured `EOSR6m2_...` and `EOSR6m3_...` names are canonicalized to
  `Canon EOS R6 Mark II` and `Canon EOS R6 Mark III`. Existing generic Canon
  records are repaired when their saved BLE name already identifies the model.
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
control, and recording-state notifications are verified on both the EOS R6
Mark II and EOS R6 Mark III for `Canon (Smart)`. Broader reconnect-cycle,
forget/re-pair, latency, heap, and coexistence checks remain open. A body that
still points at an old smartphone registration can produce Canon's
**Connection target not found** error; remove the old registration before
pairing Ble(e)p.

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

## Action cameras and phone shutters

Compatibility evidence is deliberately split from protocol availability:

| Target | Implementation | Hardware evidence in this tranche |
| --- | --- | --- |
| GoPro MAX2 | Implemented, supported | Desktop harness and flashed panel confirmed connection, initial Encoding state, explicit Start/Stop, state transitions, physical recording, Sleep, physical wake, and post-boot Ready. |
| Other GoPro models in the current Open GoPro support table | Implemented candidates | No model-specific result; the MAX2 result is not inherited. |
| Google Pixel 9 phone camera over BLE HID | Implemented, verified path | Bonded reconnect and mixed-sequence shutter operation are operator-confirmed. |
| Other iOS/Android/HarmonyOS phones | Implemented candidates | Generic BLE HID volume-key transport is implemented; model-specific and multi-phone verification remains open. |
| Insta360 X3 | Implemented, supported | Operation through the GPS Remote path is operator-approved. No model-specific capture, complete reconnect/power matrix, or coexistence result is recorded. |
| Insta360 X4 | Implemented, supported | Operation through the GPS Remote path is operator-approved. No model-specific capture, complete reconnect/power matrix, or coexistence result is recorded. |
| Insta360 X4 Air | Implemented, supported | Operation through the GPS Remote path is operator-approved. No model-specific capture, complete reconnect/power matrix, or coexistence result is recorded. |
| Insta360 X5 | Implemented, supported | Pairing, initial state, Start/Stop, reported recording status, shutdown, and physical wake are operator-confirmed. Immediate optimistic Start and wake-return address routing have been added; the latter needs one fresh reconnect check. |
| Insta360 X6 | Implemented candidate; discovery and power verified | Exact-name Mini Remote discovery/connection, shutdown, `CAMERA OFF`, and wake/reconnect are operator-confirmed. Shutter toggle and camera-reported video state are capture-backed; explicit Start/Stop and reported status still need a Ble(e)p panel run. |
| Insta360 GO 3 | Experimental candidate | No model-specific GPS Remote result recorded. |
| Insta360 GO Ultra | Experimental candidate | The supplied controller manual assigns this model to Mini Remote mode. No model-specific connection or shutter result is recorded. |
| DJI Osmo Action 5 Pro | Implemented, verified bounded path | Pairing, explicit recording start/stop, and camera-originated recording status are operator-confirmed. Reconnect, forget/re-pair, and coexistence remain open. |
| DJI Osmo 360 | Implemented, verified bounded path | Pairing, explicit recording start/stop, and camera-originated recording status are operator-confirmed. Reconnect, forget/re-pair, and coexistence remain open. |
| Sony RMT-P1BT-compatible cameras | Research only | No savable driver or camera test yet. |

### GoPro

- Status: GoPro MAX2 is `Supported`; its desktop protocol, physical recording,
  and repaired panel connection/state/Start/Stop/Sleep/wake path are verified.
  Other GoPro models remain candidates until tested individually.
- Driver: `GoPro` (`DriverId::GoPro = 10`), up to four instances.
- Transport: Ble(e)p is the central and the camera is the peripheral. Discovery
  requires advertised service `0xFEA6`; pairing is bonded without MITM.
- Initialization: subscribe Command Response and Query Response on every
  connection, poll Get Hardware Info until BLE-ready, then register Encoding
  status 10. Fragmented Hardware Info responses are reassembled in the main loop.
- Commands: Set Pairing State plus Set Shutter on/off. A successful shutter
  response is acceptance only; `0x93` Encoding updates confirm recording state.
  While a transition is pending, mismatched old state is ignored and bounded
  Get Status Values polling confirms the requested target or times out.
- Power: the header power button sends published Sleep command `0x05` only
  while safely idle. Ble(e)p requires both a successful command response and
  its controller-side BLE disconnect before showing **Camera asleep**; leaving
  the link connected wakes the camera again. Power-on is a
  reconnect to the saved peer, which wakes a GoPro during its documented BLE
  wake-advertising window and repeats the full readiness/state query on a fresh
  post-boot link. The MAX2 visibly slept, woke, reconnected, and returned to
  Ready through this path.
- Compatibility claim: only models covered by the current Open GoPro supported
  camera table should be treated as candidates. HERO8, legacy MAX/MINI, or any
  retailer-only claim remains unverified until tested.
- Evidence: [GoPro Open GoPro BLE](protocols/gopro-open-gopro.md), plus the
  published setup, data-protocol, query, and control documentation linked there.

### Phone Camera

- Status: `Experimental`; Google Pixel 9 bonded reconnect and shutter behavior
  in a mixed sequence are operator-confirmed. Other phone models and multi-phone
  verification remain pending.
- Driver: `Phone Camera` (`DriverId::PhoneCamera = 14`), up to four bonded
  phones and four concurrent physical links within the global limit.
- Transport: Ble(e)p advertises one lazy BLE HID Consumer Control peripheral.
  Each instance binds to the authenticated peer identity. Shutter sends Volume
  Increment press/release only to that peer, matching the common phone-camera
  hardware-button convention.
- Reconnect: the saved identity receives a short directed-advertising attempt
  followed by a normal discoverable window. The phone is the BLE central, so
  its OS ultimately decides whether to reconnect automatically. Multiple saved
  phones receive separate windows and remain concurrently connected afterward,
  subject to the global physical-link limit.
- Boundary: camera-app response to the volume key is not observable over HID,
  so the panel reports only that the report was sent.

### Insta360

- Status: `Supported` on the exact X3, X4, X4 Air, and X5 models. The Cameras
  picker now exposes `Insta360 GPS Remote` and `Insta360 Mini Remote` as
  distinct drivers over one shared runtime. X6 is an implemented Mini
  candidate, not yet Supported.
- Drivers: `Insta360 GPS Remote` preserves `DriverId::Insta360 = 11` and stable
  ID `insta360.gps_remote`; `Insta360 Mini Remote` uses
  `DriverId::Insta360Mini = 15` and stable ID `insta360.mini_remote`. Each
  catalog entry permits four instances; their lazy shared runtime holds the
  combined eight session pointers and one advertiser/GATT server.
- Mode-routing candidates: the user-supplied third-party controller manual
  lists GPS Remote for X5, X4 Air, X4, X3, ONE X2, Ace Pro 2, Ace Pro, Ace,
  ONE RS, ONE R, GO 3S, and GO 3; it lists Mini Remote for X5, X4 Air, X4, X3,
  Ace Pro 2, GO Ultra, and GO 3S. Those lists choose which profile Ble(e)p
  offers; they are not model-specific test evidence. X6 is routed to Mini from
  the live capture, despite not appearing on the photographed list.
- Exact-model hardware evidence: the operator confirms the GPS Remote path
  works with Insta360 X3, X4, X4 Air, and X5. Only X5 currently has the
  detailed capture and feature-by-feature evidence below; do not infer that
  the other three models passed every X5 lifecycle check.
- Transport: GPS Remote's primary packet contains the operator-confirmed name
  `Insta360 Remote (Bleep)`; its scan response contains appearance `0x0180` and
  proprietary service `0xCE80`. Mini Remote uses the captured complete name
  `Insta360 Mini Remote`, appearance `0x0180`, and HID service `0x1812` in its
  scan response. On X6, changing only that name to `Ble(e)p Remote` or
  `Insta360 Bleep Remote` prevented discovery; restoring the exact captured
  name restored discovery and connection. Both profiles host service `0xCE80`,
  which declares CE82 Notify, CE81 Write/Write Without Response, then CE83
  Read, matching both the physical captures and the working Mac harness. The
  camera scans and connects to the panel. GPS Start/Stop notifies
  `FC EF FE 86 00 03 01 02 00`;
  Mini Start/Stop notifies `FC EF FE 86 00 03 01 00 00` on `0xCE82`.
- Reported state: X5/GPS Remote captures show the camera writing `FE EF FE 10
  80 ...` display updates to `0xCE81`; its video idle frames carry remaining
  time, recording frames carry an elapsed timer, and photo frames distinguish
  idle from post-capture saving. X5/X6 Mini captures instead use 13-byte `FE EF
  FE 55 00 07 MM SS ...` mode/phase frames. Ble(e)p exposes explicit Start/Stop
  only after a
  video state has been confirmed. A fresh connection provisionally assumes
  video idle so a Scene can send Start immediately; the first camera state
  upgrades or replaces that optimistic state. Stop still requires confirmed
  recording.
- Initial sync: the X5 enables `0xCE82` notifications and then writes its state
  to `0xCE81` without a remote query. Ble(e)p logs subscription, the first 16
  writes, decoded state, and a 15-second timeout from the main loop. It does
  not print non-state identity payloads. A live CE80-only Mac peripheral test
  received initialization immediately after subscription and confirmed idle
  4.36 seconds later, proving that the two other captured vendor services are
  not required for X5 initial state.
- Power: shutdown notifies `FC EF FE 86 00 03 01 00 03`. Wake advertises the
  captured Apple manufacturer `ORBIT` beacon containing the saved camera
  name's six-character serial, plus the captured `0 dBm` scan response, for up
  to 60 seconds. Missing or malformed serials fail clearly. Reconnect is the
  wake confirmation. The command and wake payload are capture-backed on X5.
  The operator confirmed that the physical Mini Remote also powers X6 off and
  on. Ble(e)p's patched Mini path then physically shut down the X6, displayed
  `CAMERA OFF`, and successfully woke/reconnected it on the next power press.
  The exact X6 shutdown response remains uncaptured.
- Candidates: GO 3 remains an untested GPS candidate and is not an alias for GO
  Ultra. GO Ultra is now an untested Mini candidate; current evidence does not
  establish GPS compatibility. A passive X6/Mini Remote capture confirms the
  Mini shutter toggle, camera-reported video transitions, and elapsed timer,
  and that profile is implemented. Its power lifecycle passed on the Ble(e)p
  panel, but its control/status and coexistence matrix remains incomplete, so
  X6 is not in the supported list.
- Boundary: Start/Stop remain a toggle at the transport layer. Immediate Start
  may use the fresh connection's provisional idle state; Stop requires
  camera-confirmed recording. Unknown state is otherwise not inferred. The GPS
  state, power-off, and wake path is capture-backed for X5. Two
  additional captured vendor services remain intentionally unemulated; the
  Mac test disproved their absence as an initial-sync blocker on X5. Pairing,
  initial state, Start, Stop, state updates, shutdown, and physical wake are
  operator-confirmed on the panel. The wake-return address-routing correction
  needs one fresh check; GO 3 and GO Ultra remain unverified. X6 Mini power is
  verified, while its explicit Start/Stop and status panel checks remain open.
- Research references:
  <https://github.com/theserialhobbyist/insta360_m5StickC_remote> and
  <https://github.com/pchwalek/insta360_ble_esp32>.

### DJI Osmo

- Status: verified bounded path; Osmo Action 5 Pro and Osmo 360 pairing,
  explicit recording start/stop, and camera-originated recording status are
  operator-confirmed. Saved reconnect, forget/re-pair, and coexistence remain
  pending.
- Transport: central-role service `0xFFF0`, notifications on `0xFFF4`, and
  write-without-response on `0xFFF5`.
- Protocol: DJI's connection request/camera approval handshake, explicit
  record start/stop (`1D/03`), and 2 Hz status subscription (`1D/05`). Valid
  camera status pushes (`1D/02`) are the only source of confirmed recording.
  Concurrent sessions return distinct positive camera numbers in the
  connection response; DJI reserves camera number `0` for a single-camera
  connection.
  First pairing sends verification mode `1` and displays the same zero-padded
  four-digit code that the operator must match on the camera; saved reconnects
  use verification mode `0`.
- Candidates: Osmo Action 5 Pro and Osmo 360, both listed by DJI's reference.
- Evidence: <https://github.com/dji-sdk/Osmo-GPS-Controller-Demo>.

### Sony Camera

- Status: `Research`; the catalog entry is visible but cannot be saved.
- Sony's RMT-P1BT behavior has a usable Apache-licensed independent protocol
  description, including Sony company ID `0x012D`, service
  `8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF`, and shutter press sequencing. Because
  the camera connects to the remote, implementation requires the same
  peripheral-role boundary as Phone Camera plus real-camera validation. It
  remains blocked until that gate can be exercised.
- Research references: <https://github.com/coral/freemote>,
  <https://helpguide.sony.net/ilc/1820/v1/en/contents/TP1000770077.html>,
  <https://onlinemanual.insta360.com/onex2/en-us/specs/bluetooth>, and
  <https://repair.dji.com/help/content?customId=01700008289&lang=en&paperDocType=ARTICLE&re=US&spaceId=17>.

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

It must also pass the dormant-resource checklist:

- the compiled driver object contains no full client/session arrays or large
  mutable buffers; immutable descriptors and protocol tables stay in flash;
- driver translation units contain no non-trivial namespace-scope objects such
  as constructed UUID wrappers or strings, because their startup constructors
  force an otherwise disabled driver into the linked image; construct temporary
  transport values only inside the activated session and confirm omitted-driver
  symbols are absent from an isolated profile's ELF with
  `scripts/check_driver_isolation.py`;
- configured but inactive instances consume registry metadata only;
- activation uses checked `nothrow` allocation and rolls back cleanly on
  failure; deactivation frees the session;
- shared transports/repositories use first-user/last-user ownership;
- callbacks only enqueue compact data or flags, with parsing and mutation on
  the main loop;
- Wi-Fi starts only for an active network-backed instance, Portal, bounded
  updater check, or explicit Settings scan and returns to `WIFI_OFF` after its
  last owner;
- the full profile records static RAM plus inactive, configured, active, and
  post-deactivation heap/maximum-allocation measurements.

A new brand must not add conditional behavior to `SceneRunner`. It integrates
through capabilities and typed commands.
