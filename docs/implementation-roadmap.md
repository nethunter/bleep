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

ADR-017 authorizes a BLE-only Camera Connect experiment as `Canon (Smart)`
beside the verified BR-E1 `Canon (Trigger)` driver. It tests captured pairing,
setup, explicit movie commands, shooting-state notifications, and lifecycle
controls without claiming the Smart Wi-Fi/CCAPI workflow. Both drivers may be
compiled together; only one transport is active at a time. ADR-018 makes wake
automatic on screen activation, keeps power-down explicit, and preserves
non-destructive Back behavior.

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

- transition from studio Wi-Fi/device control into a temporary WPA2 SoftAP;
- a bounded HTTP page reachable only through the temporary AP;
- per-session credentials displayed on the panel;
- explicit Exit and inactivity-timeout teardown;
- return to Home and later device reconnection;
- full recovery of server tasks, sockets, buffers, and heap after repeated
  entry/exit cycles.

Completion gate:

- both spikes run reliably on the target ESP32-C3 with documented memory
  headroom;
- the selected Bluetooth and USB-network designs are recorded as decisions;
- a failed gate is reported as a hardware/stack constraint before refactoring.

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

Completion gate:

- Shark passes Phase 0 behavior through the new interfaces;
- disabled drivers contribute no linked symbols or runtime allocations;
- invalid Kconfig combinations fail with actionable messages.

## Phase 3: Home, runtime registry, groups, and Portal mode

Work:

- boot to Home without scanning, pairing, re-pairing, or reconnecting Shark;
- request device connections only from a device screen or scene;
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
- portal routes exist only in Portal mode and only on its temporary AP;
- repeated mode transitions recover their memory and normal device control;
- corrupted or old records fail safely or migrate predictably.

## Phase 4: Amaran light driver

Initial models:

- amaran Pano 60c;
- amaran Pano 120c;
- amaran Ace 25c.

Work:

- implement Telink opcode `0x26` commands;
- support power, brightness, CCT, and HSI with model limits;
- provision factory-reset lights into a panel-owned mesh;
- import an existing Sidus/amaran mesh through Portal mode;
- implement best-available state readback and mark optimistic state explicitly.

Completion gate:

- both onboarding paths work on real target lights;
- commands can target individual lights and compatible groups;
- credentials never leak into normal logs or unprotected exports.

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

Completion gate:

- the example start sequence works:
  1. lights on;
  2. wait one second;
  3. camera record start;
  4. wait one second;
  5. recorder record start;
- Stop runs the successful inverse actions in reverse order;
- partial failures and unavailable devices are visible and recoverable.

## Phase 7: Universal panel UI

Work:

- add home, devices, groups, and scenes navigation;
- add shared light, camera, motion, and recorder UI modules;
- preserve the specialized Shark keypoint/run experience;
- show only configured, enabled instances in operational menus;
- show pairing, connection, state quality, scene progress, and USB portal
  status within the round-display safe area.

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

