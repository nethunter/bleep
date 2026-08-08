# Implementation Roadmap

Each phase has an explicit completion gate. Do not begin a dependent phase
until its gate passes or the deviation is recorded in
[Decisions](decisions.md).

## Active sequencing deviation

ADR-013 authorizes a bounded multi-device foundation tranche ahead of the
Phase 1 feasibility spikes. Implemented scope includes the driver catalog,
device manager, versioned device registry, Home/Devices UI, and on-demand Shark
lifecycle. It does not complete Phase 2, Phase 3, or Phase 7: Kconfig validation,
the GATT facade, groups, Portal mode, scenes, generic device modules, and the
physical Shark regression gate remain outstanding.

ADR-014 additionally authorizes a bounded Canon BR-E1 BLE sub-spike against the
EOS R6 Mark III. It adds on-demand pairing and a stateless movie record trigger
to the existing one-active-instance framework. It does not claim recording
state, concurrent Shark/Canon links, CCAPI support, or completion of Phase 1 or
Phase 5.

ADR-015 names the production-facing choices `Canon (Trigger)` and
`Canon (Smart)`. An EOS R6 Mark III host-HCI capture now identifies the
smartphone BLE request, responses, and credential-bearing characteristics used
for Wi-Fi handoff. Smart remains blocked on network-side DHCP/endpoint evidence
and the first successful CCAPI request.

ADR-019 and ADR-020 authorize a bounded on-device scene tranche ahead of full
Phase 3/6/7: panel-authored Start and Stop lists, persistent scene storage,
concurrent Canon Smart + Tascam links during a run, and Press Record / Press
Stop as the first scenario. Groups, Portal HTTP scene editing, lights,
generated reverse-Stop, Parallel steps, and Shark-in-scene remain deferred.

ADR-017 authorizes a BLE-only Camera Connect experiment as `Canon (Smart)`
beside the verified BR-E1 `Canon (Trigger)` driver. It tests captured pairing,
setup, explicit movie commands, shooting-state notifications, and lifecycle
controls without claiming the Smart Wi-Fi/CCAPI workflow. Both drivers may be
compiled together; ADR-022 supersedes the original single-transport limit.
ADR-018 makes wake
automatic on screen activation, keeps power-down explicit, and preserves
non-destructive Back behavior.

ADR-022 replaces screen-scoped teardown with retained logical sessions mapped
onto four physical BLE transport groups. Protocol-ready manual and sequence
sessions survive navigation, multiple instances may coexist, shared transports
such as the panel-owned Amaran/Aputure/Zhiyun mesh consume one slot, and safe group-aware LRU
eviction protects foreground, sequence, pending-command, and confirmed-recording
sessions.

ADR-023 authorizes a bounded local Home Assistant client on top of that
multi-instance baseline. The software tranche includes AP Wi-Fi bootstrap and
screen-scoped LAN Portal setup,
schema-v2 entity records, separate secret storage, four dynamic HA profiles, a
shared REST/WebSocket runtime, entity controls, and safe authored scene actions.
It remains `Experimental`: do not mark the tranche complete until the target
passes ten Portal and ten runtime lifecycle cycles with correct AP-to-LAN handoff,
responsive LVGL, authenticated subscription, correct actions/state, and
recovered sockets/tasks/heap. TLS, cloud/OAuth, HA devices/areas, additional
domains, and exposing Ble(e)p devices to HA remain deferred.

ADR-025 authorizes a battery-safety mitigation tranche without changing the
required BLE/Home Assistant concurrency or healthy retained-link behavior. It
adds conservative radio settings, parks ownerless dropped sessions, and enables
station modem sleep. Electrical protection of the
CrowPanel battery-input path remains a separate hardware requirement.

### Home Assistant feasibility gate

Status: `Software implemented; hardware gate open`.

Required target evidence:

- the setup AP accepts only Wi-Fi provisioning, hands off cleanly to the LAN
  listener, reports scan/join/failure state without blocking LVGL, and the
  displayed numeric address remains reachable only while Portal is open;
- REST `/api/`, bounded `/api/states` discovery, WebSocket authentication,
  selected-entity subscription, and state/service round trips all succeed;
- four entities persist across reboot while neutral Home boot starts no Wi-Fi;
- wrong token, missing entity, HA restart, Wi-Fi loss, reconnect, rebind, and
  explicit unlink recover without UI stalls;
- mixed HA/Canon/Tascam sequences prepare one shared HA session and execute in
  order;
- ten Portal and ten runtime connect/disconnect cycles recover heap, sockets,
  and tasks. If not, record the measured constraint and stop this tranche.

## Planned UI memory optimization

Status: `Implemented` in simulator and firmware build; physical navigation
regression remains operator-pending and does not supersede open hardware gates.

Work:

- keep only Home and Devices resident;
- lazily create and delete device screens, management overlays, and keyboards;
- share one recording-control view between Canon and Tascam through
  device-specific state/command adapters;
- lazily allocate Shark screens and overlays while preserving its specialized
  workflows;
- measure LVGL peak use and fragmentation across repeated navigation, then
  reduce the LVGL pool when the measured peak permits it.

Completion gate:

- maximum configured devices can repeatedly traverse every screen without
  allocation failure or growing fragmentation;
- simulator screenshots and physical navigation remain visually unchanged;
- connected free/minimum heap and LVGL peak/free memory are recorded;
- all device transport and command hardware regressions pass.

## Phase 0: Preserve and baseline the Shark remote

Work:

- review and preserve existing uncommitted Shark changes;
- build and flash the current firmware;
- capture firmware size, free heap, minimum free heap, and connection behavior;
- add host tests for the pure Shark protocol and extracted state reduction.

Completion gate:

- existing Shark pairing, keypoints, movement, run control, reconnect, and deep
  sleep still work on hardware;
- baseline measurements are recorded in [Progress](progress.md).

## Phase 1: Runtime feasibility spikes

### Bluetooth, Mesh, and Wi-Fi spike

Prove concurrent:

- Shark GATT;
- Canon BR-E1-compatible GATT;
- Amaran BLE Mesh traffic and PB-ADV provisioning;
- Canon HTTP over Wi-Fi;
- responsive LVGL rendering.

Measure connection latency, command latency, reconnect behavior, dropped
events, free heap, and repeated connect/disconnect stability.

### Dedicated Portal mode spike

Prove:

- Wi-Fi-only bootstrap through a temporary WPA2 SoftAP;
- clean handoff to a station-bound HTTP page at the displayed DHCP address,
  with `bleep.local` as a best-effort alias;
- the fixed initial setup password and active URL displayed on the panel;
- LAN reachability only while the Portal screen remains active;
- explicit Exit and inactivity-timeout teardown;
- return to Home and later device reconnection;
- full recovery of server tasks, sockets, buffers, and heap after repeated
  entry/exit cycles.

Completion gate:

- both spikes run reliably on the target ESP32-C3 with documented memory
  headroom;
- the selected Bluetooth and USB-network designs are recorded as decisions;
- a failed gate is reported as a hardware/stack constraint before refactoring.

### Multi-panel manufacturing tranche

Before producing multiple panels, complete the ordered plan in
`multi-bleep-manufacturing.md`: stable eFuse-derived identity, full-identity
open setup SSID, independent mesh-key validation, explicit same-room fixture
selection, and numeric-IP LAN disambiguation while retaining `bleep.local`.

Completion gate:

- two physical panels pass simultaneous AP, LAN Portal, independent-mesh,
  overlapping-scan, reboot, teardown, and factory-reset checks;
- the factory process flashes one common image and never clones configured NVS;
- build and simulator results remain labeled separately from two-panel proof.

## Phase 2: Kconfig and core driver framework

Work:

- add `Kconfig.projbuild`, dependency validation, and size-oriented defaults;
- provide example builds such as `shark_only`, `canon_ble`, and `full_studio`;
- add `DriverCatalog`, `DeviceManager`, typed capabilities, commands, state
  quality, and queued results;
- add the GATT transport facade and selected backend;
- adapt Shark behind the framework without changing user-visible behavior;
- add native tests for registration, dependency checks, command routing, and
  state reduction.

Implemented deviation (ADR-021): the first GATT facade tranche is shared by
Shark, Canon Trigger, Canon Smart, and Tascam. It provides lazy NimBLE lifetime,
one scanner with main-loop fan-out, bounded async connection slots, reconnect
backoff/watchdogs, address claims, serialized Canon security, bond deletion,
explicit protocol readiness, targeted discovery, timing telemetry, best-effort
connection parameters, and deterministic host tests. Build a fixed-capacity
per-link asynchronous GATT executor only if ten-cycle hardware measurements
show blocking GATT work reaches 25% of median readiness for a driver or the
Canon Smart + Tascam pair. The Phase 1 physical coexistence and heap gates
remain open; this implementation does not claim Mesh/GATT coexistence.

Implemented capacity tranche (ADR-034): saved-device capacity is 24, NimBLE
bond capacity is 16, and the application and NimBLE controller are explicitly
aligned at four physical links while retaining eight logical active instances.
The Devices UI pages records to bound LVGL use. A six-link profile remains a
separate measured experiment and must pass full-profile BLE, Mesh Proxy, Wi-Fi,
navigation, reconnect, latency, and contiguous-heap gates before adoption.

Completion gate:

- Shark passes Phase 0 behavior through the new interfaces;
- disabled drivers contribute no linked symbols or runtime allocations;
- invalid Kconfig combinations fail with actionable messages.

## Phase 3: Home, runtime registry, groups, and Portal mode

Work:

- boot to Home without scanning, pairing, re-pairing, or reconnecting Shark;
- initialize new and factory-reset panels with an empty device registry while
  preserving migration of an actually paired legacy Shark;
- request the first device connection only from a device screen or scene, then
  retain protocol-ready sessions until safe eviction or explicit release;
- add versioned persistence and migrations;
- create, rename, enable, disable, configure, and remove device instances;
- create groups and validate their shared capabilities;
- implement dedicated-mode SoftAP HTTP APIs and portal screens for device
  settings, studio Wi-Fi, groups, scenes, backup, restore, and reset;
- suspend normal control before Portal entry and tear down AP/server resources
  on Exit, timeout, or reboot;
- retain dormant records for drivers omitted by a later build.

Completion gate:

- configuration survives power cycles and compatible firmware rebuilds;
- startup remains on Home until the operator selects a device or scene;
- portal routes exist only in Portal mode, on the setup AP or its temporary
  station-bound LAN listener;
- repeated mode transitions recover their memory and normal device control;
- corrupted or old records fail safely or migrate predictably.

Implemented deviation (ADR-027): the LAN Portal now administers committed
device settings and the current authored Start/Stop scene model from responsive
phone/desktop views. Physical-device pairing, groups, backup, restore, reset,
and the remaining Phase 3 completion gates are not advanced by this tranche.

Implemented local tranche (ADR-031): the panel now provides radio-free Wi-Fi
status, persistent haptic enablement, build/support information, sanitized live
diagnostics, and warned full-NVS Factory Reset. Backup/restore, groups, and the
remaining repeated-lifecycle and hardware gates remain open.

## Phase 4: Amaran light driver

Status: Experimental bounded panel-owned-mesh tranche implemented under
ADR-024; target-light completion gate remains open.

Initial catalog entry: one generic `Amaran Light`. Pano 60c, Pano 120c, and
Ace 25c are validation fixtures, not choices the operator must make before
provisioning. Any compatible factory-reset fixture advertising Mesh
Provisioning may be added; support claims remain limited to hardware that has
passed this phase's completion gate.

Work:

- implement Telink opcode `0x26` commands;
- support power, brightness, CCT/tint, and RGB with validated 2300-10000 K
  model limits;
- provision factory-reset lights into a panel-owned mesh;
- persist pending configuration and reserve replay-safe sequence blocks;
- implement best-available state readback and mark optimistic state explicitly.

Deferred from this tranche: existing Sidus mesh import, user-authored native
groups, HSIC, interpolation, and confirmed color-property readback. The
panel-owned vendor-model group and physically correlated power status are now
research-confirmed for Ace 25c/MC Pro. Firmware now performs authenticated
group status polling with per-source freshness; enabling the correlated group
power Set path is integrated. Deterministic per-member vendor groups are also
integrated from the optically verified MC-red/Ace-green test. Decoded
configuration-status gating and the remaining real-panel checks stay open.

Completion gate:

- PB-GATT onboarding, interrupted configuration recovery, proxy fallback, and
  verified reset work on all three real target lights;
- commands and mixed-device sequences target individual lights;
- credentials never leak into normal logs or unprotected exports.

### Phase 4b: Zhiyun Light direct-control tranche

Status: Experimental generic multi-instance driver for MOLUS X100 and X60RGB
implemented under ADR-028/ADR-029. X100 has passed panel control checks;
X60RGB panel hardware verification remains open.

Implemented boundary:

- expose one `Zhiyun Light` Add light entry and create one normal persisted
  instance per selected X100 or X60RGB fixture;
- discover factory-reset `pl105` X100 or `plx104` X60RGB fixtures on `0x1827`
  and provision them into the shared panel-owned mesh with durable Device
  Key/unicast allocation;
- rediscover provisioned product-qualified fixtures advertising Mesh Proxy
  `0x1828`;
- initialize and validate the direct proprietary `0xFEE9` session;
- persist an ordinal member-routing selector instead of deriving it from the
  X100/X60RGB model; selector `0` is live-verified for an X60RGB that was the
  first Zhiyun member and third standards-mesh node;
- read power, float32 brightness, and CCT before reporting Ready, and expose
  power plus CCT/brightness with correlated confirmation on both models;
- expose X60RGB hue/saturation control using its captured correlated setter
  replies while keeping command completion non-optimistic;
- persist the normal BLE identity transactionally and attach saved sessions to
  the panel-owned mesh's one retained proxy client;
- constrain CCT to 2700-6500 K and brightness to 0-100%; X100 omits RGB at
  runtime while X60RGB converts the shared RGB action to captured HSI writes.

Deferred production work:

- interrupted-after-Provisioning-Data reconciliation and verified reset/retry;
- address-rotation recovery beyond service/product rescanning, firmware-version
  compatibility policy, effect modes, and service-change handling;
- physical boundary, reconnect, scene, retained-pool, coexistence, latency,
  and heap measurements listed in ADR-028/ADR-029, including two simultaneous
  Zhiyun instances.

Completion gate:

- one factory-reset X100 and one factory-reset X60RGB each provision through
  repeated use of the same Add light entry, reach confirmed Ready, control
  their captured capabilities with observed output, survive reboot, and
  complete scenes without optimistic command completion;
- failure and timeout paths preserve the last confirmed state and remain
  retryable;
- the full profile passes mixed-device coexistence and memory recovery checks.

## Phase 5: Canon camera drivers

Initial models:

- Canon EOS R6;
- Canon EOS R6 Mark II;
- Canon EOS R6 Mark III.

Work:

- implement BR-E1-compatible Bluetooth pairing and an honest record trigger;
- capture and implement smartphone-mode BLE pairing plus the Camera Connect
  Wi-Fi handoff as `Canon (Smart)`;
- implement bounded, non-blocking CCAPI discovery/configuration, record
  start/stop, state polling, and reconnect over the camera's direct access
  point;
- expose `Canon (Trigger)`, `Canon (Smart)`, and explicit Smart-to-Trigger
  fallback selection;
- leave BR-E1 recording state unknown; report smartphone BLE or CCAPI state as
  confirmed only after device-originated readback.

Completion gate:

- both transports operate on each available test camera;
- transport fallback is deterministic;
- HTTP success is not presented as confirmed recording until state readback.

## Phase 6: Scene engine

Work:

- implement ordered `Action`, `Wait`, and optional `Parallel` steps;
- target device instances or compatible groups;
- generate Stop by reversing and inverting Start by default;
- support an explicitly authored Stop override;
- journal completed actions and reverse only successful actions;
- add non-blocking timing, cancellation, progress, timeout, retry, abort, and
  continue policies;
- edit, validate, import, and export scenes through Portal-mode HTTP.

Active deviation (ADR-019 / ADR-020 / ADR-027): the first panel tranche ships authored
Start and Stop lists, prepare-on-open concurrent links (`Ready` before Start),
NVS persistence, Press Record / Press Stop for Canon Smart + Tascam, and
responsive Portal create/edit/duplicate/delete for that same persisted model.
Generated reverse-Stop, groups, Parallel, import/export, journaling, and broader
Phase 6 policy work remain later work.

Completion gate:

- the example start sequence works:
  1. lights on;
  2. wait one second;
  3. camera record start;
  4. wait one second;
  5. recorder record start;
- Stop runs the successful inverse actions in reverse order;
- partial failures and unavailable devices are visible and recoverable.

Bounded gate for the ADR-019/020 tranche:

- Press Record Start: Canon `RecordStart`, wait 500 ms, Tascam `RecordStart`;
- Press Stop: Canon `RecordStop`, then Tascam `RecordStop`;
- opening a sequence prepares all targets concurrently before Start;
- `Ready` requires every target's physical link and protocol initialization;
- links stay held while the run screen is open and through armed/Stop;
- target chips expose full controls without releasing the sequence's held
  links, and show per-target connection/protocol readiness;
- authored step order and the editable 500 ms wait remain scene data;
- partial failures and unavailable devices are visible and recoverable.

## Phase 7: Universal panel UI

Work:

- add home, devices, groups, and scenes navigation;
- add shared light, camera, motion, and recorder UI modules;
- preserve the specialized Shark keypoint/run experience;
- show only configured, enabled instances in operational menus;
- show pairing, connection, state quality, scene progress, and USB portal
  status within the round-display safe area;
- provide distinct, non-blocking haptic confirmation for accepted presses,
  connection readiness, Back navigation, and newly surfaced errors.

Completion gate:

- all text and controls fit the 240x240 round panel;
- disconnected or disabled devices do not block unrelated controls;
- all LVGL access remains on `loop()`.

## Phase 8: Future recorder drivers

Planned:

- Tascam Portacapture X8 Bluetooth: ADR-016 record-control tranche implemented
  from annotated AK-BT1 captures; bounded record-control hardware checks pass,
  while scene integration and broader recorder capabilities remain open;
- Deity PR4 remote control.

Work for each driver begins with protocol documentation or capture-based
research. Pairing, command encoding, state readback, and coexistence must be
verified before enabling its Kconfig option by default.

Completion gate:

- driver satisfies the common recorder capability contract;
- record start/stop can participate in scenes without brand-specific scene
  logic;
- hardware behavior and limitations are documented.

## Phase 9: Release hardening

Work:

- power-cycle, deep-sleep, reconnect, and failure-injection tests;
- minimal and full build size reports;
- static and runtime memory budgets;
- long-running Wi-Fi/Bluetooth coexistence tests;
- safety validation for physical movement and recording state;
- documentation and recovery procedures.

Completion gate:

- every published build profile compiles;
- relevant profiles flash and pass hardware checks;
- memory headroom and known limitations are recorded;
- no unsupported protocol behavior is presented as confirmed.
