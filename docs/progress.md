# Project Progress

Update this file at the end of every implementation session. Keep entries
short, factual, and reproducible.

## Current status

- Current phase: bounded action-camera tranche (ADR-036), beside the
  experimental Aputure Light Phase 4 tranche (ADR-024/ADR-038), existing Scenes/shared-BLE
  work, and remaining hardware gates.
- Firmware state: Home-first, persistent device registry, retained on-demand Shark,
  Canon (Trigger)/(Smart), Tascam X8, and panel Scenes with authored Start lists
  plus generated or explicitly customized Stop lists, prepare-on-open concurrent
  links (protocol-ready `Ready`), settings cog
  (rename/edit/delete) that cancels pending preparation before configuration,
  and NVS scene persistence. Lazy UI allocation keeps Home/Devices resident;
  scene UI loads on demand.
- Camera catalog/runtime: GoPro, Insta360, DJI Osmo, Sony Camera, and Phone
  Camera are separate Camera-family entries. GoPro has bonded, multi-instance
  Open GoPro shutter start/stop with response-gated optimistic state. Phone
  Camera exposes a bonded, multi-instance BLE HID volume-up shutter. Insta360
  now emulates the GPS Remote protocol for the X5 under the verified custom name
  `Insta360 Remote (Bleep)`, decodes GPS display frames for video/photo state, gates
  explicit Start/Stop on confirmed video state, and implements captured
  shutdown plus serial-specific `ORBIT` wake advertising;
  GO Ultra remains a distinct experimental probe. DJI now implements its
  published controller handshake, on-panel four-digit first-pair verification,
  explicit record control, and status push for Action 5 Pro/Osmo 360.
  Sony remains capture-required. An Insta360
  X5 has connected successfully to Ble(e)p as a GPS remote and worked in a
  mixed shutter sequence. A Google Pixel 9 also passed bonded reconnect and
  mixed-sequence shutter operation. DJI Osmo Action 5 Pro and Osmo 360 first
  pairing, explicit recording start/stop, and camera-confirmed recording status
  are now operator-confirmed.
- Universal driver framework: Up to 24 saved device records and 16 NimBLE bonds
  are independent of runtime concurrency. Eight logical active instances map
  onto four explicitly configured
  retained physical BLE transport groups for manual and sequence sessions,
  including multiple instances of one Canon driver. All members of the single
  panel-owned Aputure Light/Zhiyun mesh now share one cross-brand transport
  key and one retained proxy client. All four GATT
  clients now share one lazy NimBLE scanner/runtime and async link slots,
  targeted discovery, explicit protocol readiness, and BLE timing telemetry.
- One discoverable generic `Aputure Light` entry now uses the shared userspace
  PB-GATT/Mesh Proxy runtime. The clean-storage `0.2.0-dev` baseline removes
  prior hidden model IDs and uses the neutral `mesh` NVS key. Crypto, persistence, parameterized
  scenes, and UI are implemented. Panel-owned Ace 25c/MC Pro provisioning,
  composition, configuration status, standard OnOff model transactions, Ace
  Light Lightness model transactions, cross-proxy routing, and group messaging
  are host-proven. ADR-041 restores the working Studio Lighter per-node unicast
  route for ordinary power and looks and removes the unsafe `26 0E`
  command-as-poll behavior. Four-fixture isolation is awaiting physical
  validation. Per-member color integration is complete; decoded
  configuration-status enforcement is implemented and live-confirmed on MC
  Pro. Segmented Composition Data Status now drives automatic vendor-model
  selection in firmware; its new panel onboarding run and safe reset gate
  remain open.
- One discoverable, multi-instance `Zhiyun Light` entry now detects MOLUS X100
  and X60RGB profiles. Both share panel-owned PB-GATT onboarding and confirmed
  routed CCT/power control; X60RGB adds captured hue/saturation control. Saved
  nodes persist an ordinal routing selector and attach `0xFEE9` to the mesh
  proxy connection. X100 is panel-live-verified; X60RGB host-originated optical
  verification passes, while the flashed shared embedded path remains open.
- Last updated: 2026-08-12.

### 2026-08-12: Sequence look preview, RGB final-state ordering, and automatic composition identity

- The shared sequence look editor now retains foreground ownership and sends a
  debounced live `Set look + On` preview for CCT/tint/brightness or RGB while
  the controls move. Saving uses the same captured draft, so the previewed RGB
  mode and value are the values persisted into the scene.
- Added a scene-store RGB round trip and a runner regression proving a stored
  blue `SetLightRgbAndOn` reaches the driver without falling back to CCT. The
  Aputure compound transaction now follows the working Studio Lighter order:
  unicast power On first, requested look last. Pending remains asserted between
  stages; a failed look reports action failure while preserving the honest
  optimistic On state.
- Aputure configuration now starts at Composition Data Get instead of skipping
  directly to AppKey Add. The runtime reassembles authenticated segmented
  device-key replies, parses the reported vendor model, persists it before use,
  and automatically selects MC Pro `0x03F6:0x1000` or Ace/Pano
  `0x0211:0x0000`. Exact Ace/Pano names still come from a recognized advertised
  product label because those fixtures share one composition tuple. Manual
  identity remains only an unsupported/malformed-composition recovery path.
- Native tests passed 87/87. The complete `ui_sim` traversal passed, including
  interactive RGB preview and save under normal refresh ticks. Full Montserrat
  `bleep` compiled at 141,428 bytes static RAM (43.2%) and 1,930,326 bytes flash
  (61.4%). The final identity-corrected image was uploaded to the configured
  `/dev/cu.usbserial-211240` and reset the panel without erasing NVS. The
  integrated composition path, MC Pro RGB final output, and sequence preview
  still require physical observation.
- Corrected the untouched RGB default from white/zero saturation to red at
  100% saturation across normalized light state, Aputure, Zhiyun X60RGB, the
  shared control shell, and the sequence picker. Previously saved RGB values
  still derive and restore their actual saturation. Native tests remain 87/87,
  the complete `ui_sim` traversal passed, and `bleep` compiled at 141,428 bytes
  static RAM (43.2%) and 1,930,324 bytes flash (61.4%). The image uploaded
  successfully to `/dev/cu.usbserial-211240` without erasing NVS.
- Corrected Aputure/amaran control-screen entry when the remembered state is
  Off. The screen still restores the remembered sliders, but now sends only an
  explicit per-node Off after the proxy becomes ready; it no longer transmits
  a look packet that wakes the fixture while the button continues to display
  **Turn On**. If the remembered state is On, entry still reapplies its look.
  Native 87/87 and the complete `ui_sim` traversal pass. `bleep` compiled at
  141,428 bytes static RAM (43.2%) and 1,930,562 bytes flash (61.4%), then
  uploaded successfully without erasing NVS; physical confirmation on the
  Ace/Pano-class fixture remains required.
- Scene light authoring now resolves capabilities from each saved Zhiyun
  fixture instead of the shared driver's catalog-wide capability superset.
  MOLUS X100 therefore retains the unified light editor but exposes only CCT
  and brightness, while MOLUS X60RGB retains both CCT and RGB. Unknown Zhiyun
  identities fail conservatively to CCT-only. Native tests passed 88/88 and the
  complete `ui_sim` traversal passed with an interaction assertion that rejects
  an RGB editor for X100 while preserving X60RGB. Full Montserrat `bleep` built
  with 141,428 bytes static RAM (43.2%) and 1,930,924 bytes flash (61.4%), then
  uploaded successfully to `/dev/cu.usbserial-211240` without erasing NVS.
- Added bounded Scene diagnostics for stored step values, per-target
  acquisition/readiness, dispatch results, confirmation, and the exact failing
  step. A live Sequence 3 trace with four lights reached shared readiness in
  8.2 seconds after one recovered direct-connect miss, then completed two full
  Start/Stop cycles without a software failure. Aputure targets 19 and 18 used
  distinct unicasts `0x000B` and `0x000A`; both Zhiyun actions reported
  correlated confirmation. Native tests passed 88/88. Full Montserrat `bleep`
  built with 141,428 bytes static RAM (43.2%) and 1,932,130 bytes flash (61.4%)
  and uploaded successfully without erasing NVS. Physical output confirmation
  for all four fixtures is still required because dispatch/confirmation alone
  is not proof of the requested optical state.

### 2026-08-11: Light/mesh review corrections

- Live Aputure diagnostics separated a 43.6-second wait for the reset fixture's
  first compatible advertisement from the PB-GATT exchange, which completed in
  about 1.6 seconds. The actual completion failure was the expected fixture
  reboot followed by a 584 ms direct-connect miss: the runtime incorrectly
  rolled back immediately instead of waiting for the provisioned proxy.
- A single stable Aputure or Zhiyun candidate now auto-selects after a 750 ms
  discovery-settling window without displaying a one-row picker. Multiple
  candidates retain the stable explicit picker. Immediate auto-selection
  failure exposes the row for manual retry, and failed attempts clear stale
  candidates before scanning again.
- Aputure now distinguishes `Connecting to light`, PB-GATT `Provisioning`, and
  `Configuring mesh`. PB-GATT has a 30-second deadline. After provisioning, an
  expected reboot/connect miss preserves the provisional transaction, scans for
  the exact address or this mesh's standard Network ID, and resumes
  configuration through that proxy. A 60-second overall configuration deadline
  rolls back the provisional node and unicast allocation instead of waiting
  forever.
- Entering the unified Aputure control applies the displayed default look as
  intended. If the fixture was Off, the same per-node transaction follows the
  look with an explicit Off and keeps only that session pending, so opening the
  screen or changing look controls does not leave an Off fixture powered On.
- Fixed request ownership for sequence cancellation. `DeviceManager` now
  records the request ID that actually created each driver's asynchronous
  pending transaction. Removing a command that is still queued no longer
  cancels an unrelated manual transaction on the same fixture.
- Mesh onboarding rollback now persists a complete replacement blob before
  publishing it live. A failed rollback save retains the snapshot and pending
  add for retry instead of discarding recovery state and allowing a phantom
  node or consumed unicast address to return after reboot. Early Zhiyun PB-GATT
  disconnects return to the picker rather than remaining at Idle.
- Zhiyun post-provision scanning now accepts the exact selected address/type or
  a standards Mesh Proxy Network ID matching `k3(Network Key)`. Arbitrary
  same-model proxies are ignored. Physical rotating-address, cross-mesh, and
  competing-panel behavior remains unverified.
- Removed the superseded Aputure and Zhiyun control widget trees. Their UI
  modules now own onboarding only; Ready uses the single capability-driven
  light shell. Home Assistant lights also skip construction of the generic
  entity screen, while non-light HA domains keep it.
- Native passed 86/86. The complete `ui_sim` traversal and screenshots passed,
  including candidate scrolling/tapping during refresh and Aputure CCT/RGB,
  single-candidate auto-selection, X100, X60RGB, and HA power-only shared-shell
  views. Simulator LVGL reported
  42,848 bytes free after maximum-device initialization, 23,576 after sequence
  Stop/settings, and 26,480 after remove refresh. The full Montserrat `bleep`
  profile built with 141,420 / 327,680 bytes static RAM and 1,927,556 /
  3,145,728 bytes flash. The image uploaded to the configured
  `/dev/cu.usbserial-211240`; all written-region hashes verified and the board
  hard-reset. NVS was not erased or factory-reset.
- Still unverified: physical fixture selection, early-disconnect recovery,
  rollback under real NVS failure, cross-mesh rejection, reboot/fallback,
  two-panel/two-fixture behavior, all four Aputure fixtures together,
  Aputure/Zhiyun coexistence, phone/captive Portal behavior, and soak testing.

### 2026-08-10: Mesh isolation safety and recovery corrections

- Follow-up correction (ADR-041): comparison with the working Studio Lighter
  source showed ordinary `26 8D`/`26 8C` power is addressed to each light's
  persisted unicast node. The earlier failed private-group experiment did not
  disprove this route. Aputure Turn On/Off and compound **Set look + On** are
  restored, and ordinary power/CCT/RGB now share the target unicast address.
  `26 0E` is a captured group power-on command, not a read-only status query;
  the five-second automatic write and setup/config refresh writes were removed.
  Refresh is now a safe no-write operation pending a verified query.
- Recovery identification now offers and persists exact Ace 25c, Pano 60c,
  Pano 120c, and MC Pro names. Pano aliases share the known Amaran tuple without
  collapsing their display name to Ace. Native verification passes 78/78,
  including the exact encrypted source-1 to destination-2 power packet vector
  derived from the desktop reference. The complete `ui_sim` capture traversal
  passes with 42,848 bytes free after maximum-device initialization, 23,584
  bytes after sequence Stop/settings, and 26,472 after remove refresh. The full
  Montserrat `bleep` profile builds with 140,468 / 327,680 bytes static RAM and
  1,918,196 / 3,145,728 bytes flash. Flash and four-fixture hardware results
  are recorded below when completed. The approved upload attempt found
  `/dev/cu.usbserial-211240` initially, but the port disappeared before esptool
  could open it and did not re-enumerate during the following 30 seconds; the
  two remaining USB modem ports belong to NocFree peripherals and were not
  guessed as targets.

- Historical firmware, the desktop reference, and the retained hardware record
  confirm that Aputure vendor power is physically effective only at common group
  `0xC000`; unicast and private-group writes were inert on Ace 25c and MC Pro.
  Ordinary Aputure Turn On/Off and compound look-and-On capabilities are now
  hidden rather than presenting mesh-wide power as independent control. The
  shared shell hides its power button and sequence authoring exposes per-member
  **Set look**; Zhiyun retains confirmed **Set look + On** and generated Off.
- Scene commands now retain their assigned request ID. Stopping an in-flight
  Start removes that exact queued/result request and cancels the driver's
  asynchronous transaction before generated Stop begins. Zhiyun clears its
  selector-correlated pending operation and compound stage so a late look reply
  cannot schedule power-on after Stop. Result lookup now preserves unrelated
  replies, and a full fire-and-forget result queue evicts its oldest entry so a
  newly dispatched scene result cannot be silently lost.
- Shared-mesh scan fallback now accepts the BLE address of any persisted member,
  after the preferred direct gateway attempts fail. A powered-off Zhiyun member
  therefore cannot prevent fallback through a live Ace/MC proxy. Exact-address
  matching still excludes unrelated Mesh Proxy advertisers.
- A failed unconfigured Aputure node keeps exact Ace 25c/MC Pro identification
  available after configuration rejection. Correcting the tuple also corrects
  an automatically managed model name while preserving custom user names.
  Temporary global `CORE_DEBUG_LEVEL=4` was removed from normal firmware builds.
- Native verification passed 78/78, including in-flight compound Stop
  cancellation, exact result lookup under a full queue, Aputure capability
  boundaries, repeatable model assignment, and known-member gateway address
  matching. The `ui_sim` profile built and its complete capture traversal
  passed; LVGL reported 42,848 bytes free after maximum-device initialization
  and 23,544 bytes free after sequence Stop/settings. The full Montserrat
  `bleep` profile built with 140,460 / 327,680 bytes static RAM and 1,917,454 /
  3,145,728 bytes flash. After the panel was reconnected, the approved upload to
  the explicitly selected `/dev/cu.usbserial-211240` port succeeded; every
  written region passed hash verification and the board hard-reset through RTS.
  Hardware gateway/model recovery checks remain pending.

### 2026-08-11: Main-only owner's guide refresh

- Added repository guidance requiring each manual refresh to collect and merge
  post-generation implementation and supporting-documentation changes on a
  separate branch before the manual source or PDF is touched on `main`.
- After merging that preparatory branch with `--no-ff`, refreshed the Insta360
  X5 instructions for the final `Insta360 Remote (Bleep)` identity, state-aware
  Action Button and Scene behavior, removal of the raw Shutter fallback,
  provisionally available Start, confirmation-gated Stop, verified pairing,
  state, recording, shutdown, and physical wake, plus the remaining retained-
  session wake reconnect recheck.
- Rebuilt the 26-page owner's guide on `main`, rendered every page to PNG, and
  visually inspected the complete guide. No clipping, overlap, broken tables,
  missing images, or page-furniture defects were found. Documentation only;
  firmware build, simulator, flash, and hardware behavior were unchanged.

### 2026-08-11: Insta360 X5 GPS Remote control and reported state

- Direction correction: a new full GPS-mode capture shows that the X5 does
  report usable state to the GPS Remote. The implementation therefore replaces
  Mini emulation with the GPS protocol, advertises the captured name
  `Insta360 GPS Remote` with appearance `0x0180` and a `0xCE80` scan response,
  and sends the captured
  GPS shutter notification ending in `01 02 00`. After the custom `Ble(e)p`
  name did not appear in the X5 list, the primary advertisement was changed to
  match the vendor name byte-for-byte while deliberately retaining the service
  scan response. This isolates exact-name filtering; the full control flow
  needs post-flash X5 verification.
- Evidence: synchronized X5/GPS Remote capture established camera writes on
  `0xCE81`: `FE EF FE 10 80 07 ... m` identifies video idle/remaining time;
  `... 0D ... .HH:MM:SS` identifies active recording; `... 09 ... NNN+`
  identifies photo idle; and `... 05 ...` identifies post-capture saving.
  These updates also occur for camera-local actions, so they are reported state
  rather than command ACKs. Mini `0x55` state remains documented as research
  compatibility but is no longer the selected transport identity.
- Power: the capture established shutdown notification
  `FC EF FE 86 00 03 01 00 03`, followed by disconnect. The GPS Remote then
  resumes normal identity advertising; X5 reconnect confirms wake. The
  Mini-only serial-addressed `ORBIT` advertisement is not used by this path.
- Implementation: characteristic callbacks enqueue raw writes only. Main-loop
  decoding owns per-connection video/photo state, command completion, and UI.
  The Insta360 control uses the Canon-style recorder shell with confirmed
  Start/Stop, photo capture feedback, and a power button. Unknown state remains
  unknown and exposes only a raw Shutter fallback. The shared peripheral
  advertiser supports exact raw primary and scan-response payloads.
- Documentation: `docs/protocols/insta360-gps-remote.md` records the private
  capture hashes, GATT roles, golden command/display-state vectors, confidence
  boundaries, implemented safety rules, unknown fields, and remaining X5
  gates. The Mini capture remains a separate research note. The protocol index
  and repository agent guidance require protocol notes to stay synchronized
  with every research or implementation tranche.
- Verification before this GPS correction: native tests passed 76/76; full
  Montserrat `bleep`, `ui_sim`, screenshot traversal, and upload passed. Fresh
  correction results: native tests passed 76/76; full Montserrat `bleep` built
  with 140,532 / 327,680 bytes static RAM and 1,909,438 / 3,145,728 bytes
  flash; `ui_sim` built and its complete screenshot traversal passed. The
  configured `/dev/cu.usbserial-211240` upload completed, all written regions
  passed hash verification, and the panel hard-reset. The 26-page owner's guide
  was rebuilt and every rendered page was visually inspected. Live X5 pairing,
  status, Start/Stop, photo, shutdown, and wake remain the hardware gate.
- Exact-name diagnostic: after `Ble(e)p` did not appear in the X5 GPS Remote
  list, the advertised name was changed to the captured `Insta360 GPS Remote`
  value without changing the `0xCE80` scan response. Native tests again passed
  76/76; `bleep` built at 140,532 / 327,680 bytes static RAM and 1,909,454 /
  3,145,728 bytes flash; upload and flash hash verification passed on
  `/dev/cu.usbserial-211240`. The regenerated 26-page owner's guide passed a
  complete rendered-page inspection. Whether the exact name makes the remote
  discoverable is awaiting the immediate X5 check.
- Follow-up hardware result: with the exact vendor name, the X5 found the
  remote; shutter Start, Stop, post-trigger state, and power-off worked. Initial
  state remained unconfirmed before the first trigger, and power-on did not
  wake the camera. The idle decoder now accepts variable-length digit/`h`/`m`
  remaining-time text while retaining the captured fixed markers. A new
  diagnostic identity uses `Insta360 Remote (Bleep)` in the primary packet,
  moves appearance beside `0xCE80` in the scan response to fit the 31-byte
  legacy limit, and extends wake advertising from 30 to 60 seconds.
  Native tests passed 76/76; full `bleep` built at 140,532 / 327,680 bytes
  static RAM and 1,909,616 / 3,145,728 bytes flash. Upload and flash hash
  verification passed on `/dev/cu.usbserial-211240`. Per repository guidance,
  the owner's manual and generated PDF were not changed.
- Name-prefix diagnostic: `Insta360 Remote (Bleep)` was discoverable on the X5.
  The next flashed build removes only the `Insta360` prefix and advertises
  `Remote (Bleep)` with the same primary/scan-response split, state decoder,
  and 60-second wake window. Native tests passed 76/76; `bleep` built at
  140,532 / 327,680 bytes static RAM and 1,909,608 / 3,145,728 bytes flash;
  upload and hash verification passed on `/dev/cu.usbserial-211240`.
- Start/Stop-only correction: restored the working `Insta360 Remote (Bleep)`
  name after the prefix-removal experiment and removed Insta360's raw Shutter
  capability, dispatch, and UI fallback. Unknown or photo state now exposes no
  recording action; confirmed video idle exposes Start and confirmed recording
  exposes Stop. Capture review found unsolicited initial status: connection at
  frame 4268 / 29.390608 s, CCCD enable at frame 4347 / 31.051122 s, and the
  first video-idle write at frame 4725 / 35.251909 s, 5.861301 s after
  connection and well before the first shutter notification at frame 6750 /
  60.303646 s. No state-query command occurred in between. Native tests passed
  76/76; full `bleep` built at 140,532 / 327,680 bytes static RAM and
  1,909,612 / 3,145,728 bytes flash; `ui_sim` and complete screenshot traversal
  passed; upload and flash hash verification passed on
  `/dev/cu.usbserial-211240`. The manual/PDF were not updated.
- Capture-exact wake and initial-state diagnostics: corrected the GPS trace to
  show `ORBIT` advertising beginning 8.77 ms after shutdown disconnect and the
  X5 connecting about 25.09 seconds later. Wake now builds the captured 31-byte
  Apple manufacturer payload from the saved six-character camera serial, uses
  the three-byte `0 dBm` scan response, retains both buffers for the 60-second
  window, rejects invalid saved serials, and seeds peer acceptance from the
  saved address/name. The `0xCE82` subscription callback and `0xCE81` writes
  enqueue bounded events only; main-loop diagnostics log subscription, the
  first 16 writes, recognized state, and a 15-second timeout without printing
  non-state identity frames. Native tests passed 76/76, including the exact
  `X5 1HDKAB` wake vector and invalid identities. Full `bleep` built at 140,532
  / 327,680 bytes static RAM and 1,911,274 / 3,145,728 bytes flash; `ui_sim`
  and its complete screenshot traversal passed. The two additional opaque
  captured vendor services remain deliberately unemulated and are the leading
  hypothesis only if `0xCE82` subscribes without initial `0xCE81` writes. The
  configured `/dev/cu.usbserial-211240` upload completed and every written
  region passed hash verification. A first bounded 40-second serial window
  contained no connection diagnostics, so it did not establish an X5
  initial-sync or wake result; those physical checks remain open. The manual/PDF
  were not changed.
- Mac protocol-lab probe: added a PyObjC/CoreBluetooth `0xCE80` peripheral
  harness with interactive and automated state-gated Start, Stop, power-off,
  wake, private JSONL logging, and pure protocol tests. An nRF capture confirms
  the name-only request produces a full 31-byte `Insta360 GPS Remote`
  advertisement containing macOS-inserted `+12 dBm` TX power and `0xCE80`.
  The longer custom name was omitted when requested with the service. A cold
  ORBIT-only CoreBluetooth request reported advertising success but emitted no
  ORBIT packet in an eight-second nRF capture, so Mac-native wake is blocked on
  CoreBluetooth and requires a raw-HCI-capable adapter. A live X5 then selected
  the named Mac peripheral, subscribed to CE82, and sent its initialization
  stream without a remote query. Confirmed video-idle arrived 4.36 seconds
  after subscription; Start produced confirmed recording after 1.47 seconds;
  Stop produced confirmed idle after 3.52 seconds; and power-off produced CE82
  unsubscribe after 3.24 seconds. This CE80-only result disproves the missing
  opaque vendor services as an initial-sync blocker for X5. A subsequent
  128-second Mac ORBIT request produced no reconnect, consistent with the nRF
  evidence that CoreBluetooth did not emit ORBIT. The lab's three protocol
  tests passed and the full `bleep` profile built at 140,532 / 327,680 bytes
  static RAM and 1,911,274 / 3,145,728 bytes flash. Raw logs/captures remain
  under `/private/tmp`, outside the repository. The manual/PDF were not changed.
- Reusable desktop protocol harness documentation: recorded how the Mac X5 lab
  was built and how to adapt the pattern for other devices. The guide starts
  with the central-versus-peripheral role decision, then covers the PyObjC
  service and callback lifecycle, pure protocol/test separation, advertising
  verification with an independent sniffer, state-gated bounded scenarios,
  JSONL evidence and privacy, CoreBluetooth raw-advertising limits, and the
  firmware handoff boundary. The protocol index, Android capture workflow, and
  X5 lab README link to the guide. Documentation-only change; the owner's
  manual and generated PDF were not changed.
- Panel protocol correction from the desktop harness: capture reinspection
  showed the physical GPS Remote and successful Mac peripheral both declare
  CE82 Notify before CE81 Write/Write Without Response and CE83 Read, while the
  panel had created CE81 first. The panel now consumes a tested
  CE82/CE81/CE83 order constant. Normal advertising also returns to the exact
  captured vendor profile: `Insta360 GPS Remote` plus appearance `0x0180` in
  the 28-byte primary packet, and HID `0x1812` plus `0 dBm` TX power in the
  seven-byte scan response. The prior custom identity remains discovery-tested
  history, not the selected protocol profile. Native tests passed 76/76; full
  `bleep` built at 140,532 / 327,680 bytes static RAM and 1,911,698 /
  3,145,728 bytes flash; `ui_sim` and its complete screenshot traversal passed.
  Upload to `/dev/cu.usbserial-211240` completed, every written region passed
  hash verification, and the panel hard-reset. A bounded post-boot serial window
  showed a normal Home boot and no active Insta360 session; open the saved
  Insta360 control before the fresh X5 initial-state and wake checks. The
  owner's manual and generated PDF were not changed.
- X5 completion check and final usability corrections: the operator confirmed
  that the flashed panel pairs, receives initial and ongoing recording state,
  starts and stops recording, powers the X5 off, and physically wakes it. A new
  connection now begins in optimistic video-idle state so Scene Start can send
  immediately instead of waiting roughly five seconds for CE81; Stop remains
  confirmation-gated and the first CE81 state replaces the assumption. The
  advertised identity is restored to `Insta360 Remote (Bleep)`. The remaining
  observed defect was that a woken X5 did not attach to the retained session
  until the panel/runtime restarted. During the bounded ORBIT window the
  Insta360 listener now claims the returning central even when its unresolved
  address differs, and the main loop maps it to the sole `PoweringOn` session,
  preventing the generic Phone Camera fallback from owning that link. Native
  tests passed 76/76; full `bleep` built at 140,532 / 327,680 bytes static RAM
  and 1,911,904 / 3,145,728 bytes flash; `ui_sim` and its full traversal passed.
  Upload to `/dev/cu.usbserial-211240` completed, every written region passed
  hash verification, and the panel hard-reset. One wake/reconnect recheck
  remains. The owner's manual and generated PDF were not changed.
### 2026-08-11: Offline Portal administration and stable panel identity

- Fixed Portal sequence saves under ArduinoJson 7: action command fields now
  use explicit string extraction instead of the null-default conversion that
  rejected every action as `invalid_step`. Parser errors now identify the
  Start/Stop list, one-based step number, and reason.
- The open setup AP now serves the full Portal Overview, Devices, Sequences,
  and Wi-Fi views, so local configuration no longer depends on studio Wi-Fi.
  Home Assistant discovery, validation, and secret writes remain LAN-only.
- Nearby networks are scanned asynchronously before the SoftAP starts and the
  bounded cached results are served after the browser connects, avoiding the
  unreliable AP-client/channel-hopping path. Hidden SSIDs remain manually
  enterable.
- Added the immutable eFuse-derived `BLP-XXXXXXXXXXXX` identity to About and
  Portal overview. Setup SSIDs use the requested final-five-character form
  `Bleep-Setup-XXXXX`; the open-network QR no longer advertises a password.
- The Home Assistant URL defaults to `http://homeassistant.local:8123`.
  `/api/config` returns only a `token_stored` boolean; a stored token remains
  write-only and is preserved unless the operator explicitly changes it.
  Entity discovery matches both entity IDs and friendly names without regard
  to ASCII letter case, with mixed-case host regression coverage.
- Native tests passed 78/78, including the reported six-step Portal payload,
  identity formatting, and entity-search matching. The complete UI simulator
  traversal passed, and the full
  Montserrat `bleep` profile built
  successfully with 141,084 / 327,680 bytes static RAM and 1,905,974 /
  3,145,728 bytes flash. The worktree uploaded successfully to the configured
  ESP32-C3 on `/dev/cu.usbserial-211240`; every image hash verified and the
  board hard-reset. A post-reset request to the prior Portal address timed out,
  so reopening Portal and saving the sequence from a browser remain pending.
- Rebuilt the 26-page A4 owner's PDF. Expected-content and all-page nonblank
  extraction checks passed; every rendered page was visually inspected, with
  the changed Portal and troubleshooting pages checked at full resolution.

### 2026-08-11: Canon multi-body identity and automatic naming repair

- Reproduced the code path behind a Canon Smart report where an EOS R6 Mark III
  in Bluetooth standby could answer while a new EOS R6 Mark II entry was
  pairing. The Canon clients persisted the pre-bond advertisement address even
  though NimBLE exposes the bond-resolved identity address after security; a
  rotating private address could therefore bypass the saved-sibling filter.
- Canon Smart and Canon Trigger now persist the resolved identity address,
  reject already-saved and already-bonded bodies during fresh pairing, keep
  Retry locked to the saved camera, and require Forget before replacing it.
  Trigger also reports `ALREADY ADDED` when the only visible body is owned by
  another entry.
- Restored Canon Smart matching for captured `EOS`/`R6`/`PowerShot` names and
  merge same-address advertisement plus scan-response candidates. Generic
  Canon records now become `Canon EOS R6 Mark II` or `Canon EOS R6 Mark III`
  when the captured `EOSR6m2_...` or `EOSR6m3_...` identity is available.
- Native tests passed 77/77. The `ui_sim` build and complete capture run passed,
  including `18b_canon_trigger_already_added.png`; the new status fits the
  240x240 screen. The required full Montserrat `bleep` build passed at
  1,909,356 bytes flash and 140,340 bytes static RAM.
- Rebuilt the 26-page owner's guide and inspected every rendered page plus the
  affected Canon page. Hardware pairing of two simultaneous Canon bodies and
  the on-device naming result remain operator-pending. After explicit approval,
  the full profile uploaded to the auto-detected `/dev/cu.usbserial-211240`;
  esptool identified the ESP32-C3 and verified the written flash blocks.
- After rebasing onto `main` at `6880c44`, native tests passed 79/79, `ui_sim`
  built and completed its full screenshot traversal, and the required full
  Montserrat profile built at 1,915,074 bytes flash and 141,276 bytes static
  RAM. The merged 26-page PDF was regenerated and every rendered page was
  inspected; the Canon and Portal pages were also checked at full resolution.

### 2026-08-11: Owner-focused manual editorial pass

- Reframed the illustrated manual as an owner's guide for a creative operator:
  a seven-step quick start now leads into navigation, equipment setup, device
  control, repeatable workflows, studio services, recovery, function reference,
  exact-model compatibility, and safety.
- Consolidated electronics, capacity limits, pin assignments, enclosure parts,
  print settings, assembly, switched-battery wiring, the standard external D1
  battery-path diode installation, and firmware commands into one final
  advanced developer/builder section. Manual-maintenance instructions remain
  only in the source README.
- The rebuilt 26-page A4 PDF passed metadata and expected-content checks, text
  extraction on every page, and full rendered-page visual inspection. The full
  Montserrat `bleep` profile built successfully with 140,340 / 327,680 bytes
  static RAM and 1,906,428 / 3,145,728 bytes flash. This documentation-only
  change was not uploaded to hardware.
- Rebuilt the cover as separate text and illustration columns so the device art
  cannot obscure the title or subtitle. Owner-facing references now call the
  optional control the Action Button, and shared-light behavior is explained as
  one private light network/connection before the technical chapter introduces
  Bluetooth Mesh terminology.
- Added minimum remaining-page-space guards for every heading level so a title
  moves to the next page instead of becoming the last line on a page.
- Made removal of the original CrowPanel D1 and installation of an external
  1N5819 replacement part of the standard reference assembly in both the
  owner's manual and `hardware/README.md`; the two diodes must not be run in
  parallel.
  The hardware bill of materials now identifies the reference JLJLUP 3.7 V,
  1100 mAh protected 1S pack (ASIN B0GR14VMW5) and its listed dimensions,
  plug, weight, and discharge rate; the owner's PDF includes the specifications
  without a retailer link.
- Reduced compatibility status labels to Supported, Experimental, Candidate,
  Research, and Later. Canon EOS R6 Mark II and Mark III Smart Phone Mode are
  now Supported for the verified BLE recording-control path; the separate
  Wi-Fi/CCAPI omission remains explicit.
- Replaced the cover logo and controller artwork with alpha-channel PNGs. Both
  now render directly on the dark cover without black or white rectangular
  backgrounds; the title block remains unobstructed.
- Reordered the owner chapters as Cameras, Lights, Audio, Motion, then Studio
  Services, which contains Portal and Home Assistant setup. Workflow-building
  now follows those chapters, and the generated contents uses the same sequence.
- Made Audio and Motion category chapters, nesting Tascam Portacapture X8 and
  iFootage Shark Nano II beneath them so future devices can be added alongside
  the current models without changing the guide's top-level structure.
- Combined Canon guidance into one camera section with Trigger Mode and Smart
  Phone Mode explained as separate pairings with different controls and state
  feedback.
- Added a screen-by-screen Action Button reference covering every active camera,
  light, recorder, slider, Home Assistant entity, and scene screen, plus the
  navigation and research screens where a short press is intentionally ignored.
  Updated exact-model evidence: Canon EOS R6 Mark II now passes both Trigger
  Mode and Smart Phone Mode, and both DJI Osmo Action 5 Pro and Osmo 360 now
  report camera-confirmed recording status.
- Moved the Shark Nano II slider to the end of the compatibility list. The
  compatibility table now reserves a wider Status column and disables
  mid-word table wrapping so all five status labels remain intact.
- Re-edited every owner-facing chapter around visible actions, decisions, and
  outcomes. Removed implementation terms such as GPIO, service IDs, transport,
  provisioning, proxy, NVS, heap, WebSocket, and protocol acknowledgements from
  the owner's path. Hardware specifications, the controller battery-sensing
  limitation, capacity internals, wiring, repair, and firmware work now appear
  only under **Advanced: developers and builders**.

### 2026-08-10: Maintainable illustrated instruction manual

- Added a user-facing instruction manual with consistent technical line
  illustrations generated from the supplied hardware reference photos,
  including the five-prototype overview with every screen shown on,
  representative simulator captures, setup and operating procedures, a complete
  user-function reference, exact-model compatibility matrix, recovery guidance,
  and explicit Current/Experimental/Candidate/Research boundaries.
- The editable source is `docs/manual/manual.md`; a pinned ReportLab builder and
  short maintenance guide compile it to
  `output/pdf/bleep-instruction-manual.pdf`. The documentation index links the
  manual source.
- The final 23-page A4 PDF passed metadata/outline checks, text extraction for
  every page, expected-content assertions, and full-page rendered visual
  inspection with no blank pages, clipping, broken tables, or unreadable
  figures. The full Montserrat `bleep` profile also built successfully with
  140,340 / 327,680 bytes static RAM and 1,906,428 / 3,145,728 bytes flash.
  Firmware behavior did not change, so no board upload was attempted.
### 2026-08-09: Independent mesh lights, common controls, and compound looks

- Superseded ordinary common-group Aputure power under ADR-039. On, Off,
  refresh, CCT/tint/brightness, and RGB now use the selected instance's
  persisted control group; source-addressed status mutates only that session.
  `0xC000` remains reserved for a future explicit mesh/group action. Zhiyun
  keeps its persisted selector, reply correlation, gateway attachment retry,
  and the same one-proxy cross-brand transport.
- Added normalized `LightControlState` publication and one capability-driven
  control shell. Simulator captures cover Aputure CCT/tint and RGB, X100
  CCT-only, X60RGB CCT/RGB, and Home Assistant light power-only states,
  including pending/error presentation.
- Appended compound CCT/RGB-and-On commands. Scene validation requires color
  plus Turn On, the picker exposes one **Set look + On** action at 5600 K/50%
  brightness/neutral tint, editing restores stored values, and generated Stop
  creates one reverse-order Turn Off. Aputure keeps only the target session
  pending across its two writes; Zhiyun waits for correlated look confirmation
  before power confirmation. No old two-step scene migration was added for the
  clean-storage baseline.
- Host verification: native tests passed 76/76; `ui_sim` compiled and its full
  capture traversal completed. Simulator LVGL telemetry reported 42,848 bytes
  free after maximum-device initialization and 23,512 bytes free after sequence
  Stop/settings. The full Montserrat `bleep` profile built successfully with
  140,452 / 327,680 bytes static RAM and 1,913,148 / 3,145,728 bytes flash.
  This does not replace the existing pre-Wi-Fi hardware guidance of roughly
  40 KiB free heap and more than 36 KiB largest allocation.
- With explicit approval, the full `bleep` image uploaded successfully to
  `/dev/cu.usbserial-211240`; every written region passed hash verification and
  the panel hard-reset through RTS. Fixture validation remains Blocked/pending:
  no physical light output was observed. The required exact-model matrix is
  amaran Ace 25c, Pano 60c, Pano 120c, Aputure MC Pro, Zhiyun MOLUS
  X100, and X60RGB. Run factory-reset/reconnect and capability boundaries per
  model; 30 alternating per-target power/distinct-look isolation commands;
  every available same-brand and cross-brand pair through each gateway plus
  gateway loss/fallback; four logical mesh targets with camera/recorder/HA;
  compound Start/Stop failure/retry/reboot/scene-switch cases; then at least
  100 mixed cycles over two hours and 20 reboot/reconnect cycles. Record heap
  minima/largest block, latency, destination or selector, state quality, and
  physical output. An unavailable model stays Blocked and is not inferred.
- Live MC Pro onboarding after that upload reached pending mesh configuration
  with `Unknown vendor model`: its provisioning advertisement did not use the
  previously recognized literal `Mesh Device`, so the pending node had no
  vendor tuple. The failure screen now offers an explicit **MC PRO** recovery
  action. Confirming it transactionally persists the hardware-verified
  `0x03F6:0x1000` identity and resumes configuration without reprovisioning or
  guessing for other fixtures. Native tests passed 76/76; the full simulator
  traversal and new `20da_aputure_mc_pro_identify.png` capture passed. The full
  profile built with 140,452 / 327,680 bytes static RAM and 1,913,726 /
  3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240`; all written regions passed hash verification and
  the panel hard-reset. Live MC Pro recovery/configuration remains to be
  confirmed by the operator.
- Live follow-up found that switching the shared light shell between CCT and
  RGB restored the saved sliders on screen but did not dispatch that recalled
  look until a slider moved. Tab selection now marks the restored look dirty
  and sends it through the same 350 ms debounce. Simulator assertions cover
  both CCT-to-RGB and RGB-to-CCT application. Native tests passed 76/76 and the
  full simulator traversal passed. The full profile built with 140,452 /
  327,680 bytes static RAM and 1,913,752 / 3,145,728 bytes flash, then uploaded
  successfully to `/dev/cu.usbserial-211240`; all regions passed hash
  verification and the panel hard-reset.
- The MC Pro recovery also exposed that Aputure pairing updates left the
  registry record at the generic `Aputure Light` catalog name. Confirmed vendor
  tuples now assign the exact fixture name (`Aputure MC Pro` or
  `amaran Ace 25c`) and persist the proxy address metadata when adding or
  recovering a fixture. An operator-created custom display name is preserved.
  The recovery screen now states `Identify fixture` / `Confirm Aputure MC Pro`
  instead of presenting `Unknown vendor model` beside an ambiguous `MC PRO`
  button. Confirmation resumes at the already-reached vendor-bind stage and
  publishes the model-name update immediately while configuration continues.
  Native tests passed 76/76 and the full simulator traversal passed. The full
  profile built with 140,452 / 327,680 bytes static RAM and 1,913,968 /
  3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240`; every written region passed hash verification and
  the panel hard-reset. Live confirmation and configuration completion remain
  to be observed on the MC Pro.
- The full `bleep` profile now compiles with Arduino/NimBLE debug logging. The
  Aputure runtime additionally records the matched provisioning/proxy
  advertisement address, address type, and device name; the inferred vendor
  tuple after provisioning; and every explicit MC Pro identification outcome
  and resumed configuration stage. This is diagnostic evidence only and does
  not elevate an ACK or connection to physical fixture proof.
- The first live debug capture proved that explicit MC Pro confirmation saved
  `0x03F6:0x1000` and resumed at vendor bind, but the fixture returned Config
  Model App Status `0x02` (`Invalid Model`). A subsequent retry raced the still
  connected NimBLE client, left a stale Proxy Data In pointer, and crashed in
  `NimBLERemoteValueAttribute::writeValue`. Retry on an intact proxy now
  restarts the configuration transaction in place instead of reconnecting the
  live client, and raw configuration Proxy PDUs are logged for the next
  composition-driven model-selection implementation. That implementation and
  another live capture are still required before the MC Pro gate can pass. The
  corrected debug profile built with 140,460 / 327,680 bytes static RAM and
  1,954,444 / 3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240`; every written region passed hash verification and
  the panel hard-reset.
- A fresh live debug capture then provisioned another one-element Aputure node
  at unicast `0x0005`. The PB-GATT advertisement at
  `a4:c1:38:50:c6:22` had an empty GAP name; its service data exposed
  `400U5-50C62200fp`. The firmware therefore persisted company/model as zero,
  AppKey Add completed with status zero, and configuration stopped at the
  explicit unknown-vendor gate before any confirmation was pressed. The
  corrected retry build did not crash during this capture. The service-data
  identity and composition response now need to replace GAP-name inference.
- The operator confirmed that this empty-name fixture is an amaran Ace 25c and
  observed the mesh reject the UI's hardcoded MC Pro bind. Unknown-fixture
  recovery now presents separate **Ace 25c** and **MC Pro** choices; selecting
  Ace persists its confirmed `0x0211:0x0000` tuple and exact fixture name.
  Pano models remain blocked rather than inheriting either tuple. The complete
  simulator traversal passed and the round-screen capture fits both choices.
  The debug profile built with 140,468 / 327,680 bytes static RAM and
  1,954,734 / 3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240`; every written region passed hash verification and
  the panel hard-reset. Live Ace bind/configuration remains to be confirmed.
- The operator then reported a panel crash during the Ace recovery attempt.
  Opening the monitor rebooted the panel before the original panic could be
  captured; the post-reboot trace showed only an Aputure proxy scan and no
  `identify_requested` event or new panic. Because pending device-add state is
  volatile while the provisioned mesh node is durable, that reboot left the
  current reproduction unable to return to the model chooser without a
  deliberate recovery/reset step. Do not mark the Ace recovery safe until the
  exact tap-to-panic interval is captured and decoded.
- A later live control capture did not reproduce the crash: persisted instance
  3 connected to the shared proxy and reached protocol-ready, while the MC Pro
  continued to respond physically and the Ace did not. Framework logs showed
  GATT writes but not their mesh destination or access payload. Debug output
  now inventories each persisted Aputure node and logs every application
  access transmit (destination/sequence/payload/result) plus every decrypted
  access reply (source/destination/sequence/payload) for exact correlation.
- Correlated hardware evidence then showed MC Pro instance 2 at unicast
  `0x0004` / private group `0xC003` and Ace instance 3 at unicast `0x0006` /
  private group `0xC005`. Ace control and polling writes were sent only to
  `0xC005` and completed at GATT, but produced no decrypted access reply or
  physical response; the MC Pro remained functional. This rules out accidental
  fan-out for that reproduction and isolates the failure to the Ace's private
  subscription or command handling.
- Configuration ACK handling previously matched only source, destination, and
  opcode. Repeated Mesh Model Subscription Status messages can share opcode
  `0x801F`, so a retransmitted common-group ACK could incorrectly satisfy the
  following private-group step. Status correlation now also requires the exact
  element, AppKey/group, SIG/vendor model, company, and model fields. The mesh
  configuration revision is 3 so persisted nodes re-run the corrected bind and
  subscription transaction without an NVS reset or re-provisioning.
- Native passed 76/76, including duplicated-group and wrong-element rejection.
  The full Montserrat `bleep` profile built with 140,468 / 327,680 bytes static
  RAM (42.9%) and 1,955,840 / 3,145,728 bytes flash (62.2%), then uploaded to
  `/dev/cu.usbserial-211240` with hash verification and hard reset. Exact Ace
  subscription ACKs and physical power/color behavior remain the active
  hardware gate; do not claim Ace 25c support until they pass.
- The revision-3 Ace refresh then completed on hardware with exact success
  correlation for segmented AppKey Add, vendor bind, common vendor
  subscription, private `0xC005` vendor subscription, Generic OnOff bind, and
  Generic OnOff subscription. Subsequent vendor power Get/On/Off writes to
  `0xC005` still produced no authenticated access reply or physical response.
  This reproduces the existing protocol record that dedicated-group look
  writes work but this fixture's captured `0x26` power path responds only on
  the common group. Therefore correct subscription routing does not satisfy
  ADR-039's independent physical-power requirement; the protocol/behavior gate
  remains blocked rather than inferred from configuration ACKs.
- Source-history comparison identified the regression in `48b358c`: the
  unified-light tranche changed vendor power Set/Get and the power stage of
  compound look+On from the physically verified common group to each fixture's
  private look group. The desktop mesh probe and immediately preceding
  firmware both use private groups for CCT/RGB/look and `0xC000` for physical
  power/status. That proven split is restored while retaining the unified UI,
  shared proxy, exact model naming, and strict configuration-status matching.
- The restored firmware produced authenticated common-group power replies from
  both current nodes (`0x0004` and `0x0006`) while look writes remained isolated
  to `0xC003` or `0xC005`. Native passed 76/76; full `bleep` used 140,468 bytes
  static RAM and 1,955,854 bytes flash and uploaded with hash verification.
  The operator confirmed both the Ace and MC Pro physically turned Off and On.
  This restores the known-good baseline and proves the Ace is healthy, but the
  simultaneous output change also confirms it does not itself satisfy
  independent physical power; that remains a separate hardware-backed design
  requirement.

### 2026-08-09: Aputure Light control-state synchronization

- Aputure Light vendor status handling currently confirms power only; it cannot
  read back CCT/RGB mode, color, brightness, or tint. When the control reaches
  Ready, it now applies the displayed look once after the normal 350 ms
  debounce. Reopening the control does the same, keeping the fixture aligned
  with the values shown on screen without changing tab timing.
- Added a simulator case that starts the fixture in a mismatched RGB look and
  verifies the displayed CCT look is applied after Ready. The complete `ui_sim`
  capture traversal passed, and the full Montserrat `bleep` profile built
  successfully with 140,340 / 327,680 bytes static RAM and 1,906,428 /
  3,145,728 bytes flash. Hardware upload and optical behavior remain unverified
  for this change.

### 2026-08-09: Aputure Light RGB saturation default

- A fresh Aputure Light RGB draft now starts at 100% saturation instead of the
  zero-saturation value implied by the untouched white placeholder. Once an RGB
  look has been applied, reopening the control keeps the saturation encoded by
  that in-memory RGB value.
- Added a simulator assertion for the 100% initial saturation. The complete
  `ui_sim` capture traversal passed, and the full Montserrat `bleep` profile
  built successfully with 140,340 / 327,680 bytes static RAM and 1,906,362 /
  3,145,728 bytes flash. With explicit worktree approval, upload to
  `/dev/cu.usbserial-211240` succeeded; every written region passed hash
  verification and the panel hard-reset.
- Connection status remains evidence-scoped: proxy setup immediately sends the
  verified group physical-power Get and repeats it every five seconds. An
  authenticated per-source response confirms On/Off and reachability for the
  tested amaran Ace 25c/Aputure MC Pro path. CCT, tint, RGB, and brightness
  remain optimistic because no verified property-status decoder exists.

### 2026-08-09: Aputure Light canonical identity

- Unified the Amaran/Aputure logical driver under `Aputure Light` across the
  catalog, UI, Portal, source module and namespaces, compile flag, isolated
  profile, logs, simulator captures, tests, CI matrix, and current docs. Exact
  fixture evidence remains branded as amaran Ace/Pano or Aputure MC Pro.
- Removed the hidden Pano 120c and Ace 25c compatibility descriptors and their
  dormant adapter shells. The generic driver remains numeric ID 6; Zhiyun stays
  a separate logical driver while sharing the panel-owned mesh transport.
- Renamed the shared mesh NVS key from `amaran_mesh` to `mesh` for the selected
  clean-storage `0.2.0-dev` baseline. README and ADR-038 require an on-device
  Factory Reset before flashing over earlier development firmware; no automatic
  erase or migration is performed.
- Native tests passed 76/76. The complete `ui_sim` capture traversal passed;
  the renamed Aputure Light pairing, CCT, and RGB screens were visually checked
  with the existing marquee behavior and no content collision. The full
  Montserrat `bleep` profile built successfully with 140,340 / 327,680 bytes
  static RAM and 1,906,356 / 3,145,728 bytes flash. Driver-specific profiles
  remain assigned to GitHub Actions by repository policy. After the operator
  completed Factory Reset and explicitly approved the worktree upload, the
  configured `/dev/cu.usbserial-211240` upload succeeded; every written region
  passed hash verification and the panel hard-reset.

### 2026-08-09: Transparent UI icon sources

- Converted all nine Home and device-category source icons to RGBA PNGs with
  transparent backgrounds, removed the remaining black plates from the four
  Home icons, and regenerated the 48x48 LVGL true-color-alpha arrays.
- The complete `ui_sim` capture traversal passed. `01_home.png`,
  `03_add_device.png`, `22b_scenes_add_category.png`, and
  `24_scenes_run_ready.png` were visually checked; the cutouts render cleanly
  on the existing cards and circular sequence chips without clipping.
- The full Montserrat `bleep` profile built successfully with 140,364 / 327,680
  bytes static RAM and 1,900,774 / 3,145,728 bytes flash. After a sandboxed
  attempt was denied serial access, the direct upload to
  `/dev/cu.usbserial-211240` completed; every written region passed hash
  verification and the panel hard-reset.

### 2026-08-09: DJI multi-camera sequence addressing

- Diagnosed a two-DJI scene where individual camera controls worked but the
  scene's Start/Stop affected only the first camera. Instance routing and
  notification ownership were already per-client; every DJI handshake instead
  returned camera number `0`, which DJI defines as a single-camera connection.
- Each active DJI session now returns a distinct positive camera number from
  the bounded session slot, encoded across the protocol's full four-byte
  reserved field. The request-side `conidx` remains reserved and unchanged.
- Rebased again onto current `main` and audited the DJI view against the shared
  round-page layout. DJI already reaches `RoundPageHeader` through
  `recorder_shell`, so verification, ready, pending, recording, and failure
  states share the standard header anchors and marquee title. The complete
  `ui_sim` traversal passed, and `03_camera_dji_verification.png` was visually
  checked with no clipping or round-edge collision; no UI source change was
  needed.
- After rebasing the branch onto `main`, native tests passed 72/72, including a
  second-camera connection-response vector. The `dji_osmo` profile and renamed
  full `bleep` profile built successfully; `bleep` used 1,901,492 bytes flash
  and 140,364 bytes static RAM, then uploaded to `/dev/cu.usbserial-211240`
  with hash verification and hard reset. A live two-DJI Sequence 3 Start/Stop
  run remains the required confirmation.

### 2026-08-09: Resource-based scene growth

- Removed the configured four-scene ceiling. `SceneRegistry` now grows with
  checked dynamic allocation, and every service mutation snapshots the current
  registry before changing it so allocation or persistence failure preserves
  all existing scenes.
- Scene persistence schema v3 uses a 32-bit count and writes only authored
  Start/Stop steps instead of all reserved step slots. Existing v1/v2 blobs
  remain readable. Portal overview now reports the sequence count without a
  misleading capacity denominator; a real storage failure returns an explicit
  insufficient-storage response.
- Native tests passed 72/72, including a twelve-scene persistence round trip
  beyond the former limit. The complete `ui_sim` capture traversal passed.
  All 13 firmware profiles built sequentially. `crowpanel_128` used 1,901,304
  bytes flash and 140,364 bytes static RAM, then uploaded to
  `/dev/cu.usbserial-211240`; every region passed hash verification and the
  panel hard-reset. Creating and persisting a fifth scene then passed on the
  live panel, confirming the former four-scene boundary is removed.

## Completed planning

- Defined compile-time Kconfig/menuconfig driver selection.
- Separated compiled drivers from runtime device instances.
- Defined shared light, camera, motion, and recorder capabilities.
- Defined runtime enable/disable, configuration, and capability-safe groups.
- Selected direct control on the ESP32-C3 rather than an external gateway.
- Selected panel-owned Aputure Light mesh onboarding; existing Sidus-network
  import is outside the planned product scope.
- Selected Canon BR-E1-compatible Bluetooth plus CCAPI HTTP.
- Split Canon UX into `Canon (Trigger)` and `Canon (Smart)`; Smart requires a
  captured smartphone BLE-to-Wi-Fi handoff before implementation.
- Defined ordered scenes with non-blocking waits, generated inverse Stop, and
  explicit Stop override.
- Reserved future recorder drivers for Tascam Portacapture X8 Bluetooth and
  Deity PR4.
- Selected a neutral Home screen with on-demand device connections and no
  automatic Shark pairing/reconnect at boot.
- Selected a dedicated Portal mode that temporarily runs a WPA2 SoftAP and HTTP
  server, then releases them completely on exit.
- Accepted the multiple-panel manufacturing plan: stable eFuse-derived unit
  identity, full-identity open setup SSID, per-panel random mesh keys, explicit
  same-room fixture selection, shared `bleep.local` with numeric-IP fallback,
  and a two-panel physical release gate.

## Next task

First, continue the camera hardware matrix. Pair Insta360 X5 and GO 3
through their GPS Remote menu, probe GO Ultra separately without assuming it
shares that compatibility, and test DJI Osmo Action 5 Pro plus Osmo 360 through
their remote-controller flow. Action 5 Pro and Osmo 360 pairing, recording
start/stop, and camera-confirmed status have passed; continue their saved
reconnect, forget/re-pair, and two-camera concurrency checks. Then
pair one supported GoPro,
confirm reconnect and start/stop responses without claiming camera-reported
recording state, then pair representative iOS and Android phones to
`Ble(e)p Shutter` and confirm the system camera app responds to the physical and
on-screen shutter. Google Pixel 9 reconnect/shutter and Insta360 X5
mixed-sequence paths have passed. Exercise two simultaneous phone instances and mixed camera slot
accounting. Sony still requires a capture before enabling Add Device.

### 2026-08-09: DJI verification display and approval parsing repair

- Root cause: the DJI client generated and transmitted a four-digit code but
  discarded it before the UI could render it. It also accepted the
  camera-originated `00/19` approval only when the command type was exactly
  `0x00`; DJI response-required command frames such as `0x02` therefore timed
  out even after camera approval.
- Fix: first pairing now sends verification mode `1`, retained reconnects use
  mode `0`, and `VERIFY 0042`-style zero-padded codes remain visible until the
  handshake completes. Connection approval accepts any command frame with the
  response bit clear, rejects a denied result without retrying, and marks the
  record paired only after protocol readiness. A follow-up status repair delays
  the first `1D/05` subscription by 100 ms, retries it up to three times until
  a valid push arrives, and decodes DJI's documented screen-off, live-view,
  playback, recording, pre-recording, Action, and Osmo 360 mode combinations.
  Known Action/360 photo modes cannot be mislabeled as recording.
- Verification: native tests passed 71/71, including first-pair mode and
  camera-approval parsing. The full `ui_sim` capture run passed; the new
  `03_camera_dji_verification.png` confirms the code and instruction fit the
  240x240 layout. Both `dji_osmo` and `crowpanel_128` compiled successfully.
  Full firmware size after the status repair is 1,904,292 bytes flash and
  142,332 bytes static RAM.
- Hardware: `crowpanel_128` uploaded to `/dev/cu.usbserial-211240`; all written
  regions passed hash verification and the panel hard-reset. A live DJI camera
  test then confirmed that Osmo Action 5 Pro and Osmo 360 pairing plus explicit
  recording start/stop work. Both initially showed `STATUS PENDING`. The
  follow-up status firmware also uploaded with hash verification and hard-reset;
  Osmo 360 then passed the live `CAMERA CONFIRMED` status retest. Saved
  reconnect, Action 5 Pro status, forget/re-pair, and coexistence remain
  unverified.

Then exercise the newly flashed cross-brand mesh runtime from the panel: add or open
MC Pro, Ace 25c, and X60RGB together, confirm the Devices/sequence layer counts
them as one physical slot, and verify that the retained X60RGB proxy carries
both Mesh Proxy and selector-0 `0xFEE9` traffic. Repeat the observed MC-red,
Ace-green, X60-blue cycle, then test retained navigation, reconnect, fallback,
and a second Zhiyun member. Record physical output separately from protocol
confirmation.

After that, continue the existing production gates:

Integrate the amaran Ace 25c/Aputure MC Pro findings into the firmware before
promoting Aputure Light support:

1. Parse Composition Data in firmware, select each reported SIG/vendor model,
   require decoded configuration success, and complete fallback proxy
   selection. Core slot accounting and the saved-session runtime now charge the
   complete cross-brand panel-owned mesh one retained physical link.
2. Implement Generic OnOff only as Ace/MC reachability/shadow state, Ace Light
   Lightness as model state, and the confirmed group-addressed vendor power
   Set/Get as emitter control/state. Resolve each member by authenticated source
   with a response timeout; keep unverified Telink properties optimistic.
3. Provision Pano 60c and Pano 120c through the generic entry and capture their
   composition/configuration statuses before claiming shared behavior.
4. Verify CCT/tint/RGB at several brightness levels, reboot recovery,
   interrupted configuration, preferred/fallback proxy selection, and durable
   sequence continuity.
5. Implement and verify reset followed by the fixture returning to PB-GATT
   advertisements before deleting node secrets; retain a separately confirmed
   local-record fallback for unreachable factory-reset fixtures.
6. With paired Canon Smart (R6 II or III) and Tascam X8 configured and initially
   disconnected, open a sequence; confirm the shared scanner discovers both,
   both async links reach Ready without starvation, Start records with the
   authored gap, and Stop confirms both stopped.
7. Confirm device screens refuse open while a sequence owns links; Back/Done
   releases sequence ownership while protocol-ready sessions remain connected.
8. Confirm scene persistence across power cycle.
9. Continue Canon Trigger/Smart and Shark foundation hardware gates as before.
10. Run ten initially disconnected cycles per driver and ten Canon Smart +
   Tascam sequence opens; record median/p95 readiness, per-stage blocking GATT,
   scan drops, retries, post-init heap, and post-teardown heap. Trigger the
   asynchronous GATT executor only if blocking GATT reaches the 25% gate.
11. Keep existing-mesh import and broader Portal management deferred. The
   panel-owned Generic OnOff group used for mesh state/control is
   part of ADR-024, not the deferred user-authored native-groups feature.

## Measurements

Record values with the exact build environment and commit/worktree state.

### Rolling GitHub release repository context repair

- Date: 2026-08-09.
- The artifact-only publishing job now sets `GH_REPO` from
  `github.repository`, so GitHub CLI release commands can resolve the target
  repository without a redundant source checkout. This addresses the observed
  `fatal: not a git repository` failure after every test and firmware profile
  had already passed.
- Ruby/Psych parsed `.github/workflows/ci.yml`, Bash accepted the embedded
  publishing script, and `git diff --check` passed.
- The canonical full Montserrat `bleep` profile built successfully with
  142,316 / 327,680 bytes static RAM and 1,903,272 / 3,145,728 bytes flash.
  Upload found `/dev/cu.usbserial-211240` but could not open it because the port
  was busy, unavailable, or not permitted; no flash completed.
- The repaired publishing path remains pending one successful push-to-`main`
  GitHub Actions run because the release commands cannot be exercised locally
  without mutating the hosted rolling release.

### Rolling GitHub development firmware release

- Date: 2026-08-09.
- The canonical full Montserrat PlatformIO environment is now named `bleep`
  (`bleep_roboto` for the alternate-font full profile), replacing the
  board-centric `crowpanel_128` names. The underlying CrowPanel 1.28 hardware
  target and display variant are unchanged.
- On successful pushes to `main`, CI now waits for native tests and the full
  13-profile firmware matrix before updating a rolling **Latest development
  firmware** prerelease. Pull requests remain build-only.
- The release contains the canonical full Montserrat `bleep`
  application image under a stable filename plus its SHA-256 checksum. Release
  notes identify the source commit, the required `0x10000` application offset,
  NVS preservation, and the outstanding physical hardware gates.
- The renamed full `bleep` profile built successfully with 142,316 / 327,680
  bytes static RAM and 1,903,272 / 3,145,728 bytes flash. No flash was run for
  this naming-only configuration change.

### GitHub Actions firmware-variant matrix

- Date: 2026-08-09.
- CI now builds all 13 firmware environments independently on every pull
  request and push to `main`: full Montserrat, full Roboto, and all 11 isolated
  driver profiles. Matrix `fail-fast` is disabled so one failing variant does
  not hide results from the remaining profiles.
- The README and pull-request checklist now define `crowpanel_128` as the
  standard local firmware build and GitHub Actions as the cross-profile testing
  ground. No firmware source changed, so no local firmware build or flash was
  applicable to this CI-only update.

### Single-line marquee screen titles

- Date: 2026-08-09.
- Dynamic sequence, device-control, pairing, picker, and device-management
  titles now use one shared single-line circular marquee. Short titles remain
  centered and stationary; overflowing names scroll horizontally instead of
  wrapping into the content below.
- Devices, Scenes, Settings, and Settings subpage headers now share Home's
  vertical anchors (`y=28` title, `y=24` navigation control). Their page
  content moves upward with the header rather than leaving the title detached.
- Follow-up refactoring replaced the duplicated Home, Devices, Scenes,
  sequence editor/runner, Settings, pairing, picker, recorder, and generic
  device-control header construction with one `RoundPageHeader` layout. It
  owns the shared title width, marquee, navigation/action anchors, and the
  `y=58` common content start. Specialized Shark motion screens retain their
  intentionally different control geometry.
- The simulator fixture uses `HML Studio` for the sequence-name regression.
  The complete `ui_sim` capture traversal passed, including sequence run,
  Devices, Scenes, pickers, device controls, Settings, and About visual review.
  The completed traversal retained 20,392 bytes of LVGL memory after its final
  device refresh. Native tests passed 71/71.
- All 13 firmware profiles built sequentially: `crowpanel_128`,
  `crowpanel_128_roboto`, `canon_ble`, `canon_trigger`, `tascam_x8`,
  `home_assistant`, `shark_nano_ii`, `aputure_light`, `zhiyun_x100`, `gopro`,
  `phone_camera`, `insta360`, and `dji_osmo`. Default `crowpanel_128` used
  142,308 / 327,680 bytes static RAM and 1,897,212 / 3,145,728 bytes flash.
  It uploaded to `/dev/cu.usbserial-211240` with image hash verification and
  hard reset. Live panel title motion and alignment remain operator checks.

### Shared Device Edit and Sequence Settings layout

- Date: 2026-08-09.
- Device Edit and Sequence Settings now use the same reusable round-page menu
  body as their shared header: close control at `y=24`, entity marquee at
  `y=28`, and 168 px action rows beginning at `y=58`.
- Device Edit replaces its detached bottom Close button with the header close
  control, groups Enabled into a panel row, keeps Forget and Disconnect paired,
  and gives Rename and destructive Remove full-width rows. Sequence Settings
  uses the sequence name as its title and gives Rename, Edit Start, Edit Stop,
  and Delete the same row geometry.
- The complete `ui_sim` traversal passed, including the generic and active-
  recorder Device Edit states and Sequence Settings before and after a run.
  Native tests passed 71/71. All 13 firmware profiles built sequentially;
  default `crowpanel_128` used 142,308 / 327,680 bytes static RAM and
  1,897,326 / 3,145,728 bytes flash. The image uploaded to
  `/dev/cu.usbserial-211240` with hash verification and hard reset. Live touch
  targets and marquee motion remain operator checks.
- After the feature branch was rebased and merged into `main` with `--no-ff`,
  the complete simulator traversal passed again. The full Montserrat
  `crowpanel_128` profile built with 142,308 / 327,680 bytes static RAM and
  1,897,318 / 3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset.
  Alternate firmware profiles remain assigned to GitHub Actions.

### Destructive device and sequence confirmation

- Date: 2026-08-09.
- Removing a saved device and deleting a sequence now open dedicated in-place
  confirmation views. Each view names the affected record, warns that the
  action cannot be undone, and provides separate Cancel and explicitly labeled
  destructive actions. The initial Remove/Delete tap no longer mutates stored
  configuration.
- The simulator now asserts that the first destructive tap preserves the
  record and the second confirmed tap removes it. The complete `ui_sim`
  traversal passed, and both confirmation captures were visually checked for
  round-edge clearance. Native tests passed 71/71.
- Per the current local-build policy, only the full Montserrat `bleep` profile
  was built locally; GitHub Actions owns alternate-font and driver-profile
  verification. `bleep` used 142,316 / 327,680 bytes static RAM and
  1,903,264 / 3,145,728 bytes flash. It uploaded to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset.
  Physical touch confirmation remains an operator check.

### Dynamic 40-percent BLE discovery duty

- Date: 2026-08-09.
- Increased the shared NimBLE active-scan window from 20/100 to 40/100 to hear
  sparse advertisements sooner. The interval/window are compile-time settings
  with bounds validation.
- The coordinator now suspends the one shared scan before initiating a BLE
  connection and keeps it suspended through security. Per-link scan requests
  remain intact, so discovery resumes automatically for every other preparing
  device once the controller procedure completes. Established retained links
  are not disconnected.
- Native tests passed 71/71, including scan suspension across connection and
  security and automatic resume for another requester. All 13 firmware profiles
  built sequentially. Default `crowpanel_128` used 142,316 / 327,680 bytes
  static RAM and 1,903,824 / 3,145,728 bytes flash. It uploaded to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset.
- Hardware: a cold `HML Studio` run connected Canon physically at 2.13 seconds,
  completed its security at 2.68 seconds, then started X8. X8 connected
  physically in 0.83 seconds and both devices reached protocol-ready at about
  4.59 seconds total without NimBLE reason `520` or `574`. HA authenticated and
  subscribed roughly 0.65 seconds into its separate stage. Switching to
  Sequence 3 reused HA without another Wi-Fi/WebSocket startup and brought the
  Amaran proxy protocol-ready in 1.62 seconds. Repeat-run consistency and the
  higher discovery duty's battery endurance impact remain hardware gates.

### Mixed-scene staged timeout and retained HA reuse

- Date: 2026-08-09.
- A cold `HML Studio` capture kept Wi-Fi `Off` while Canon Smart reached
  protocol-ready at 8.2 seconds. X8 then reported NimBLE reasons `520`
  (connection timeout) and `574` (failed establishment), advertised again near
  40 seconds, and reached protocol-ready at 44.6 seconds. The old shared
  30-second sequence deadline had already failed, so deferred HA was never
  activated; the apparent fast second attempt reused the X8 link that continued
  preparing under retained sequence ownership.
- Mixed scenes now allow 60 seconds for cold physical preparation. Once every
  physical target is ready, deferred HA/Wi-Fi gets a new 30-second deadline
  rather than inheriting time spent waking BLE peripherals.
- Sequence switches now transfer ownership for shared targets without releasing
  them. Shared HA therefore stays authenticated even while new physical targets
  prepare. A follow-up hardware test showed that the mixed-scene preparation
  path still proactively disconnected every ownerless non-target before HA,
  bypassing the four-resource pool and dropping both Canon and Tascam even
  though HA, Canon, Tascam, and Amaran fit exactly. That cleanup is removed:
  old-only resources now remain retained until ordinary capacity-driven LRU
  actually needs room. The same owner protection applies to an entire shared
  BLE transport group when any logical member remains targeted.
- Native tests passed 70/70, including separate physical/HA deadlines, atomic
  shared-HA ownership across scene switches and edits, and the exact four-
  resource HML-to-Sequence-3 handoff retaining HA, Canon, Tascam, and Amaran.
  `crowpanel_128` built at 1,903,784 bytes flash and 142,316 bytes static RAM.
  The full `ui_sim` capture run passed after updating its terminal physical
  timeout fixture. The corrected firmware uploaded to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset. Live
  cold-boot `HML Studio` timing and the corrected same-HA four-resource scene
  switch remain pending operator verification.

### Tascam X8 bounded direct reconnect

- Date: 2026-08-09.
- Live serial diagnosis on the X8/AK-BT1 showed that successful physical links
  completed GATT setup, session initialization, and protocol readiness in about
  1.8-2.1 seconds. The slow cycle instead spent about 19 seconds on three blind
  saved-address attempts and their backoffs before advertisement rediscovery
  succeeded.
- A scan-first build was flashed and found slower by the operator. Saved X8
  activation therefore keeps one fast persisted-address attempt, then returns
  to scanning instead of spending two more backoff cycles on a silent address.
  Rediscovery filters on the saved address and address type, so it cannot select
  another X8 in the room; new-device scans remain service/name based.
- Retry and rediscovery phases now remain visible as connection progress rather
  than falling back to `Disconnected`. X8 connect-failure and disconnect events
  also log the NimBLE reason code for future diagnosis.
- Native tests passed 68/68, including single-direct retry and saved-peer
  matching regressions. `crowpanel_128` built at 1,903,768 bytes flash and
  142,332 bytes static RAM, then uploaded to `/dev/cu.usbserial-211240` with
  image hash verification and hard reset. Final live X8 connection timing is
  pending operator verification; the serial monitor was left detached after
  upload to avoid influencing the panel's reset lines.

### Generated sequence Stop authoring and persistence

- Date: 2026-08-09; feature branch `codex/autogenerate-sequence-stop`, rebased
  onto current local `main` at `c5cffb9` before integration.
- Added schema-v4 `Generated` and `Custom` Stop modes. Main already used schema
  v3 for compact, dynamically counted scene records, so v1/v2/v3 records all
  migrate to Generated mode and rewrite as v4. Generated Stop is
  materialized by reversing Start and applying the ADR-037 inverse mapping;
  color-setting actions are omitted. Start mutations regenerate atomically,
  while Customize copies the preview and Use generated discards the override.
- Every migrated record discards its legacy authored Stop and rebuilds it from
  Start. The panel and Portal use the same scene-core generation path; Portal
  generated PUTs do not trust a client-supplied Stop array.
- Add Sequence now guides both panel and Portal authoring through Start, Stop,
  and Name. The panel reuses the existing Rename overlay for the final stage;
  canceling Name returns to Stop without losing steps, while backing out of the
  initial Start removes the blank record. Empty Start cannot advance. On the
  round panel, the content area presents one centered authoring action while a
  header arrow/checkmark owns Next/Finish; the guided header control is
  allocated only while that flow is active.
- Native tests passed 76/76, including every mapping, reverse order, maximum
  length, customization/relinking, v1/v2/v3 migration, v4 round-trip, seeded
  Press Record order, and partial-Start-failure cleanup across every generated
  target.
- `ui_sim` built and completed its screenshot run. Generated preview, customize,
  two-step relink confirmation, empty generated text, Name cancel cleanup,
  guarded Add Start, and Add generated-Stop Finish were exercised. The new
  `23b0_scenes_add_start.png`, `23b1_scenes_add_generated_stop.png`, and
  `23b2_scenes_add_name.png` captures fit the round display. Portal
  assets passed Bun syntax checks; a live browser Portal session and physical
  touchscreen flow remain operator-unverified.
- Before the rebase, all 13 then-named firmware profiles built sequentially.
  After rebasing, the canonical renamed `bleep` profile built successfully at
  140,380 bytes static RAM (42.8%) and 1,906,570 bytes flash (60.6%). The
  rebased alternate profile matrix was not rerun to avoid competing with the
  operator's other build workflow.
- An earlier upload was stopped at the operator's request while another build
  owned the board workflow. After explicit approval, the canonical `bleep`
  image was later uploaded successfully to `/dev/cu.usbserial-211240`; esptool
  verified the image hash and hard-reset the panel via RTS. Migration-on-real-
  NVS, touchscreen, Portal-browser, peripheral, and physical sequence behavior
  remain operator-unverified.

### Mixed four-link Home Assistant readiness fix

- Date: 2026-08-08.
- Live Sequence 4 evidence showed that Wi-Fi joined and the Home Assistant
  WebSocket completed `auth_ok` plus a successful trigger subscription. At
  that point 29-31 KiB heap remained but the largest contiguous block was only
  9.2-11.8 KiB, so the guarded initial REST state read repeatedly logged
  `rest_deferred` and the HA target never became protocol-ready.
- A successful authenticated subscription now makes configured entities
  command-ready with an explicit unknown state when their initial REST read is
  memory-deferred. REST reconciliation and subscribed state events still
  provide confirmation later; no connected BLE camera, recorder, light, or
  peripheral session is released to achieve readiness.
- Native tests passed 67/67. `crowpanel_128` built at 1,903,000 bytes flash and
  142,324 bytes static RAM.
- The first live retry then reached `all_targets_ready`, and the operator
  reported that every physical action worked, but the sequence ended Failed.
  Inspection found that the nominal two-frame WebSocket ring used a sentinel
  slot and therefore held only one frame. A back-to-back service result and
  subscribed state event could drop the second frame and mark an otherwise
  successful command failed. The ring now tracks occupancy explicitly and uses
  both existing 2 KiB slots without increasing its payload allocation.
- The queue fix passed the 67/67 native suite. `crowpanel_128` built at
  1,903,234 bytes flash and 142,324 bytes static RAM; live Start/Stop
  confirmation remains the next check.
- A second live Start and Stop again completed every observed action and
  produced no `frame_fault`, but both ended Failed. This isolated the remaining
  false failure to state confirmation: Home Assistant may emit no state event
  for an idempotent service call, while REST is memory-deferred. Successful
  service results now complete delivery in both directions without claiming
  confirmed entity state.
- Final native tests passed 67/67. `crowpanel_128` built at 1,903,354 bytes
  flash and 142,324 bytes static RAM, then uploaded to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset.
- The final hardware retry reached `all_targets_ready`; both Start and Stop
  logged successful HA service results (`id=11` and `id=12`) while all physical
  actions completed. The operator confirmed that both directions finished
  normally with no Failed state.

### Sequence Settings preparation boundary and Wi-Fi reconnect timing

- Date: 2026-08-08.
- Opening a sequence's Settings overlay now cancels non-running preparation and
  releases every `ConnectionOwner::Sequence` target before showing Rename/Edit
  controls. Active Start, armed recording, and Stop phases are protected from
  this cancellation, as are partial action failures that still permit authored
  Stop cleanup.
- The simulator regression begins with disconnected Canon/Tascam targets,
  opens the real Settings event handler during `Connecting`, and verifies an
  idle runner with no held links or sequence owners. The full `ui_sim` build
  and capture run passed.
- Corrected Home Assistant reconnect timing in the same flashed image: the
  ten-second Wi-Fi restart threshold now measures one continuous disconnected
  interval instead of elapsed time since the original `WiFi.begin()`. Live
  Home Assistant/Wi-Fi recovery remains operator-unverified.
- Native tests passed 67/67. `crowpanel_128` built at 1,902,902 bytes flash and
  142,324 bytes static RAM and uploaded successfully to
  `/dev/cu.usbserial-211240`.

### Sequence 4 peripheral GATT mutation crash fix

- Date: 2026-08-08.
- Captured the reproducible hardware assertion when entering Sequence 4:
  `ble_svc_gap_init` failed while `PhoneCameraDriver::Runtime::begin()` called
  `NimBLEServer::start()`. The sequence had already activated Insta360, so its
  scan was an active GAP procedure while Phone Camera tried to extend the
  singleton peripheral GATT table.
- Peripheral service setup now stops advertising and temporarily pauses shared
  central discovery before a GATT-table mutation. It refuses the mutation when
  a peripheral peer is already connected, producing a normal activation
  failure instead of entering NimBLE's assertion path. Logical scan requests
  remain intact and resume on the next main-loop pass.
- Phone Camera retains and reuses its HID wrapper with NimBLE's retained
  singleton services, avoiding duplicate HID service registration after a
  radio lease is released and reacquired.
- Native tests passed 67/67, including a regression that verifies a requested
  scan pauses for the mutation and resumes afterward. `crowpanel_128` built at
  1,901,602 bytes flash and 142,028 bytes static RAM, then uploaded to
  `/dev/cu.usbserial-211240` with image hash verification and hard reset.
- Live verification entered Sequence 4 without a reset, passed the former
  Insta360-to-Phone-Camera activation point, and brought its Canon Smart target
  through `protocol_ready`. At 31 seconds the device reported 87,992 bytes free
  heap, an 87,604-byte minimum, a 69,620-byte largest block, and Wi-Fi Off.

### Sequence 4 parallel peripheral reconnect follow-up

- Date: 2026-08-08.
- Root cause: Insta360 and Phone Camera independently reset the ESP32-C3's one
  legacy advertising object. Phone Camera also treated any active advertising
  payload as its own HID identity, so its bonded phone received no reliable
  reconnect window.
- The first peripheral-camera activation now registers every compiled GATT
  service before accepting peers. A shared advertiser serializes only
  discovery/reconnect windows; established phone, Insta360, Canon, Tascam, and
  other BLE sessions remain concurrent within the four-link limit. Each bonded
  phone has its own candidate, with a short directed attempt followed by a
  longer normal HID window.
- Live Sequence 4 evidence showed valid Insta360 and Phone Camera advertising,
  Canon Smart on link 3, Tascam X8 on link 4, and the operator's phone connected
  after selecting `Ble(e)p Shutter`. The run then logged
  `sequence/all_targets_ready`, including its deferred Home Assistant target.
  This proves concurrent readiness, not synchronized physical shutter response.
- After the directed-to-discoverable fallback was flashed, the operator
  confirmed that the phone connects normally. Generic BLE HID reconnect timing
  remains ultimately controlled by the phone OS.

### Insta360 sequence visibility follow-up

- Date: 2026-08-08.
- Hardware evidence: the operator confirmed the tested camera was an Insta360
  X5. It connected successfully to Ble(e)p as a GPS remote and its shutter
  worked in the mixed Start/Stop sequence.
- Root cause: `RecordTrigger` was a runtime capability but was absent from both
  the scene-command traits and the sequence device/action filters. Insta360 was
  therefore intentionally omitted by the generic picker despite being saved.
- Added scene-safe `RecordTrigger`, exposed it as `Shutter Toggle`, and rendered
  it as `Shutter` in sequence rows. The same explicit toggle can be authored in
  Start and Stop; it is not mislabeled as confirmed recording state.
- Native tests passed 66/66, including validation and execution of trigger
  actions in both lists. The complete `ui_sim` capture run passed.
- `crowpanel_128` built at 1,901,000 bytes flash and 142,020 bytes static RAM,
  then uploaded to `/dev/cu.usbserial-211240` with written-image hash
  verification and hard reset.

### Camera Add-screen compiled-driver capacity fix

- Date: 2026-08-08.
- Rebased `codex/add-action-cameras` onto `main` at `e218613`; the uncommitted
  camera tranche was restored with both progress-log histories preserved.
- Root cause: the full firmware compiled 14 driver adapters, but
  `DeviceManager` retained only the first 10 pointers. The catalog still
  displayed Insta360, DJI Osmo, Sony Camera, and Phone Camera, while their Add
  flows could not resolve a runtime driver and immediately canceled back to
  Devices.
- Raised the adapter-pointer capacity to 16 and added production/simulator
  compile-time table guards. A native regression verifies the 14th compiled
  driver can begin and acquire a provisional Add session.
- Native tests passed 65/65. The complete `ui_sim` capture run passed and now
  explicitly opens and cancels provisional Add screens for GoPro, Insta360,
  DJI Osmo, Sony Camera, and Phone Camera.
- `crowpanel_128` built at 1,900,884 bytes flash and 142,020 bytes static RAM,
  then uploaded to `/dev/cu.usbserial-211240` with written-image hash
  verification and hard reset. Physical touch confirmation remains for the
  operator.

### Dynamic driver activation review fixes

- Date: 2026-08-08.
- Scene refresh now drops only `Sequence` ownership and reacquires the edited
  target set through the normal physical-before-Home-Assistant activation path.
  A native regression covers editing a prepared HA-only scene to add Canon
  Smart while HA remains first in authored order.
- Shark, Canon Smart, Canon Trigger, Tascam X8, and Zhiyun activation now
  propagate lazy client resource/link allocation failures to their driver
  adapters. Failed sessions are deleted, FreeRTOS notification buffers are
  released on deactivation, and shared Zhiyun gateway/repository ownership is
  rolled back.
- Amaran activation now deactivates the partially assigned session when its
  first `beginLink()` fails, releasing any acquired logical link and allowing
  the shared runtime to become idle.
- Native tests: 60/60 passed.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `shark_nano_ii`,
  `canon_ble`, `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`,
  and `zhiyun_x100` succeeded. The affected profiles were built sequentially;
  final full-profile size is 1,858,926 bytes flash and 141,676 bytes static
  RAM.
- Flash: the final `crowpanel_128` image uploaded successfully to
  `/dev/cu.usbserial-211240`. No live peripheral, forced allocator-failure, or
  mixed prepared-scene interaction was exercised, so those hardware/runtime
  checks remain open.
- Live follow-up on Sequence 3 with an Aputure mesh target plus HA reproduced a
  connect timeout. The proxy physically connected, but HA had already reduced
  `max_alloc` from 131,060 bytes at boot to 18,420-21,492 bytes before mesh
  GATT setup reached protocol readiness. Mixed preparation now defers HA until
  every physical target is protocol-ready, then gives asynchronous teardown a
  250 ms pump window before starting Wi-Fi. Native tests remain 60/60; the
  updated full profile builds at 1,859,188 bytes flash and 141,676 bytes static
  RAM. The follow-up uploaded successfully to `/dev/cu.usbserial-211240`;
  Sequence 3 requires a repeat live check.
- The repeat kept Wi-Fi off and preserved a 73,716-byte maximum allocation, so
  HA ordering is no longer the blocker. Aputure PB-GATT connected, Mesh Proxy
  service discovery completed in 233 ms, and notification subscription
  completed in 90 ms. The saved node is still marked unconfigured; readiness
  stalls in the subsequent configuration-send path. A diagnostic build now
  reports every configuration step and encode/write/save failure, but the
  instrumented Sequence 3 run is still pending.
- Coordinated Sequence 3 capture identified the exact configuration failure:
  steps 0 and 1 sent successfully, then step 2 repeated `unknown_vendor` because
  the schema-migrated `Aputure MC Pro` node had zero company/model IDs. The
  runtime now repairs only recognized persisted MC Pro/Ace identities to their
  confirmed vendor tuples, saves the repair transactionally, and stops
  configuration retries after a terminal model error. Native tests pass 60/60;
  the repaired full profile builds at 1,861,190 bytes flash and 141,676 bytes
  static RAM. Live Sequence 3 then repaired the MC Pro record to
  `0x03F6:0x1000`, sent configuration steps 0-6, persisted completion at step
  7, and reached mesh protocol readiness in 26,039 ms. HA started only after
  that gate and the complete mixed sequence reported all targets ready in
  26,673 ms. On-screen readiness and physical Start behavior remain for the
  operator to confirm.
- That earlier `Ready` result was disproved when the MC Pro did not physically
  react. Firmware schema 3 now invalidates legacy write-only configuration,
  paces segmented AppKey Add at 350 ms, decodes device-key Config AppKey,
  Model App, and Subscription statuses, retries bounded timeouts, and persists
  readiness only after all six success replies. After **Reset Sidus BT** and a
  clean panel re-add, the MC Pro returned success for steps 1-6, reached
  protocol readiness, and the operator confirmed physical power/color control.
  The temporary node-key recovery build was removed; raw encrypted packet
  tracing was also removed from the final source.
- Deleting the old device left orphaned Sequence 3 Start/Stop actions. Scene
  deletion previously routed through full-record validation, so removing one
  orphan failed while another remained. `SceneService::removeStep()` now saves
  deletions transactionally without requiring the intermediate scene to be
  runnable, cancels obsolete prepared ownership when needed, and lets the panel
  repair missing targets one row at a time. Native tests pass 61/61 and the
  desktop UI simulator builds/runs through all captures. Live testing then
  showed that the record was removed and persisted but the current editor kept
  rendering the deleted row until it was reopened: `refreshEdit()` was deleting
  the trash button from inside its own LVGL click dispatch. Deferring through
  `scene_ui::tick()` was still too early in LVGL's next input lifecycle and the
  operator observed a crash while continuing the same editing session. The
  editor now uses `lv_async_call()` so LVGL owns the safe redraw boundary. A UI
  simulator regression sends a real trash-button click, confirms the persisted
  count and rendered count both shrink, and confirms `+ Add step` remains
  usable immediately. Native tests remain 61/61 and the desktop simulator
  completes all captures; final flash and same-session panel confirmation
  remain pending.

### Insta360 GPS-remote and DJI Osmo controller implementation

- Date: 2026-08-08.
- Worktree/branch: `/Users/nethunter/.codex/worktrees/7525/bleep`,
  `codex/add-action-cameras`, with uncommitted tranche edits.
- Native tests: 62/62 passed. New coverage verifies DJI CRC/frame generation
  against the official 51-byte connection vector, record-control parsing, the
  exact Insta360 shutter notification, and separate X5/GO 3/GO Ultra matching.
- Firmware: `crowpanel_128` succeeded using the shared PlatformIO cache. Size:
  1,895,694 bytes flash and 141,988 bytes static RAM. The isolated `insta360`
  and `dji_osmo` profiles also built successfully and retained only their
  intended camera driver symbols.
- UI simulator: `ui_sim` built and the complete capture run succeeded;
  `03_camera_families.png` shows the expanded scrollable Cameras list. This is
  layout evidence, not live camera proof.
- Hardware: the combined image uploaded to `/dev/cu.usbserial-211240`, verified
  every written-region hash, and hard-reset. All X5, GO 3, GO Ultra, Action 5
  Pro, and Osmo 360 pairing/shutter/status checks remain operator-unverified.
- Boundary: DJI is based on DJI's public controller demo; Insta360 is based on
  a community MIT implementation plus vendor compatibility material. GO Ultra
  remains explicitly experimental because its documented accessory path
  differs from the legacy GPS remote.

### Action-camera catalog, GoPro control, and phone HID

- Date: 2026-08-08.
- Worktree/branch: `/Users/nethunter/.codex/worktrees/7525/bleep`,
  `codex/add-action-cameras`, based on `dac630d` with uncommitted tranche edits.
- Native tests: 60/60 passed. Coverage includes stable catalog IDs/capabilities,
  separate Camera-family descriptors, exact GoPro pairing/shutter packets,
  response parsing, and optimistic state reduction.
- UI simulator: `ui_sim` built and its complete capture run passed. The new
  `03_camera_families.png` shows the separate scrollable camera entries on the
  240x240 layout; no live touch or peripheral behavior is implied.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `shark_nano_ii`,
  `canon_ble`, `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`,
  `zhiyun_x100`, `gopro`, and `phone_camera` succeeded sequentially. The
  nine-profile driver-isolation audit passed.
- Full-profile size: 1,882,000 bytes flash and 141,876 bytes static RAM. Roboto
  size: 1,844,948 bytes flash and 141,876 bytes static RAM. The Phone Camera
  isolated profile uses 1,742,122 bytes flash and 141,076 bytes static RAM.
- Flash: the full profile uploaded successfully to
  `/dev/cu.usbserial-211240`; the uploader hard-reset the board afterward. GoPro
  shutter behavior, phone HID pairing/capture, multi-peer routing, and all
  Insta360/DJI/Sony protocols remain physical hardware gates.

### All-driver dormant-resource audit

- Date: 2026-08-08.
- Driver lifecycle audit: Shark Nano II and Tascam each hold one dynamically
  allocated session; Canon Smart and Canon Trigger allocate up to three session
  objects individually; Zhiyun allocates up to four individually; HA allocates
  its one shared client only for its first active entity; and Amaran allocates
  its shared mesh runtime/repository only for its first direct or Zhiyun gateway
  user. Every deactivation path releases its instance, and each shared runtime
  is deleted by its final user. Compiled driver shells remain guarded at 64
  bytes maximum; configured inactive records do not call driver activation.
- Finding and fix: the shared NimBLE backend still allocated its implementation
  object from the first main-loop tick. It now uses checked lazy allocation on
  first BLE activation and deletes the object after final asynchronous client
  teardown. Images with no BLE-backed driver compile the BLE pump out of the
  main loop.
- Finding and fix: non-trivial namespace-scope `NimBLEUUID` objects created
  startup constructors that forced disabled GATT clients and the BLE runtime
  into isolated images. UUIDs are now temporary activation-local values, so
  omitted driver client symbols and those startup constructors are absent.
- Profile audit: repaired `canon_ble`, `canon_trigger`, `tascam_x8`, and
  `home_assistant` so later-added HA/Amaran drivers are explicitly disabled;
  added `shark_nano_ii`. ELF symbol inspection confirms each of the seven
  isolated profiles contains only its selected driver. `zhiyun_x100`
  intentionally includes the shared Amaran mesh transport but not the Amaran
  driver adapter. The HA-only ELF contains no physical client or BLE-central
  symbol. `scripts/check_driver_isolation.py` preserves these checks as a
  seven-profile regression gate.
- Native tests: 59/59 passed.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `shark_nano_ii`,
  `canon_ble`, `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`,
  and `zhiyun_x100` succeeded sequentially. The existing full/Roboto caches
  resolved NimBLE-Arduino 2.5.0 and LovyanGFX 1.2.21; isolated profiles resolved
  NimBLE-Arduino 2.5.1 and LovyanGFX 1.2.26.
- Flash/static RAM bytes: full 1,858,600 / 141,676; Roboto 1,821,576 / 141,676;
  Shark 1,750,800 / 140,980; Canon Smart 1,757,658 / 141,060; Canon Trigger
  1,744,704 / 141,020; Tascam 1,751,472 / 141,052; HA 1,552,032 / 130,892;
  Amaran 1,766,984 / 141,140; Zhiyun 1,783,328 / 141,148. Removing dormant UUID
  objects recovered 480 bytes of full-profile static RAM. The now-genuinely
  isolated HA profile is 267,900 bytes smaller in flash and 10,696 bytes smaller
  in static RAM than its pre-audit build.
- Flash: the full profile uploaded successfully to
  `/dev/cu.usbserial-211240`. A forced cold-boot capture reported Wi-Fi `Off`,
  150,812 bytes free heap, 142,372 bytes minimum free heap, and a 131,060-byte
  maximum allocation before any device activation. Free/minimum heap are each
  480 bytes higher than the preceding full build, matching the recovered static
  UUID storage. Real peripherals are still required to exercise every driver's
  activation and post-deactivation heap return path.

### Scene-to-scene BLE slot handoff

- Date: 2026-08-08.
- Diagnosis: scene cancellation correctly removed `Sequence` ownership and the
  retained-pool LRU correctly selected safe idle transports. The failure was
  below that layer: a NimBLE control event could already be queued when an
  evicted client began asynchronous deletion, then be delivered to a new client
  after the same logical link handle was reused.
- Fix: each backend control event is tagged with the logical slot generation.
  Events from a released generation are dropped before central dispatch; final
  callbacks from a detached client remain rejected by client identity.
- Follow-up diagnosis: switching from the lights scene to HML Studio still
  retained the now-unowned light mesh transport while the new scene attempted
  to allocate Wi-Fi for Home Assistant. NimBLE client deletion is asynchronous,
  so starting Wi-Fi in the same preparation pass could race the returned heap.
- Follow-up fix: after acquiring the new scene's physical targets, preparation
  releases every idle active transport that is not a target. Shared targets are
  preserved, and foreground ownership, pending work, or confirmed recording
  prevents forced teardown. When cleanup releases a transport, HA activation is
  deferred for a non-blocking 250 ms so the main loop can pump NimBLE teardown
  before Wi-Fi starts.
- Native tests: 59/59 passed, including release/reacquire of one logical handle
  with a stale queued `Connected` event followed by a current-generation event,
  plus a lights-to-mixed-scene handoff that verifies the old light is released,
  the new physical target is preserved, and HA waits for the settle boundary.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `canon_ble`,
  `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`, and
  `zhiyun_x100` succeeded sequentially.
- Full profile size: 1,858,900 / 3,145,728 bytes flash (59.1%) and 142,156 /
  327,680 bytes static RAM (43.4%).
- Flash: `crowpanel_128` uploaded successfully to
  `/dev/cu.usbserial-211240`. The pre-fix diagnostic capture had confirmed
  normal boot with 150,332 bytes free heap, a 131,060-byte maximum allocation,
  and Wi-Fi off; the bounded post-flash read produced no new lines because
  opening the port did not reset the running panel. Physical lights-to-HML
  Studio verification remains open.

### Capacity alignment and conditional Devices pagination

- Date: 2026-08-08.
- Configuration: 24 saved device records, 16 NimBLE bonds, eight logical active
  instances, and four application/NimBLE physical links. A compile-time guard
  rejects an application link limit above the NimBLE connection pool.
- Persistence: schema 2 remains readable; device and mesh stores round-trip at
  the 24-record capacity. Large device-store scratch arrays moved off the task
  stack, and save allocates only its encoded length.
- Native tests: 57/57 passed in `native`, including full-capacity device and
  mesh-store round trips.
- Simulator: the maximum compiled-driver configuration uses 20 records. The
  Devices screen stays unpaged through six records and pages six at a time
  above that threshold; it releases rows before opening specialized screens.
  The full ASan capture suite completed with no sanitizer finding. At sequence
  stop the simulator reported 30,024 bytes free in the 76 KiB LVGL pool,
  10,906-byte peak allocation, and 13%
  fragmentation. A clean release-mode rebuild then completed the same capture
  suite with the same reported sequence-stop memory values; the 240x240
  Devices screenshot was visually checked for round-edge clearance.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `canon_ble`,
  `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`, and
  `zhiyun_x100` succeeded sequentially with NimBLE-Arduino 2.5.1.
- Full profile size: 1,861,386 / 3,145,728 bytes flash (59.2%) and 175,844 /
  327,680 bytes static RAM (53.7%). This is 46,350 bytes flash and 6,112 bytes
  static RAM above the preceding recorded full-profile build; the dependency
  resolver also advanced NimBLE-Arduino from 2.5.0 to 2.5.1.
- Flash: the first upload to `/dev/cu.usbserial-211240` lost its serial
  connection at 21% after the cable was accidentally disconnected. After
  reconnection, an explicit-port retry uploaded successfully. A bounded serial
  read confirmed normal ESP32-C3 boot, touch-controller detection, and the
  application `runtime event=boot` report with the BLE link disconnected. NVS
  was not explicitly erased.
- Open hardware gate: four simultaneous real links and BLE plus Home Assistant
  coexistence have not been exercised by this simulator/build evidence. Six
  links remain deliberately disabled pending measured target testing.

### Cross-brand panel-owned mesh routing

- Date: 2026-08-07.
- Hardware evidence: One host-controlled X60RGB proxy carried authenticated
  MC Pro/Ace 25c Mesh traffic and its own `0xFEE9` traffic. Dedicated groups
  `0xC001`/`0xC002` staged MC red/Ace green at 5%; selector-0 set X60RGB blue at
  5%. A camera frame showed three distinct lit fixtures. Common Sidus group
  Off plus confirmed X60 Off then produced a camera frame with all three dark.
- Firmware: mesh-store schema 2 persists per-node vendor group/model metadata
  and Zhiyun routing selectors; schema 1 remains readable. All three brands use
  `PanelOwnedMesh` slot key 1. Saved Zhiyun clients attach to the mesh runtime's
  retained native proxy client. Common-group Sidus power and per-node RGB/CCT
  routing are enabled; authenticated vendor status remains the power truth.
- Native tests: 51/51 passed in `native`, including shared cross-driver slot
  accounting, schema-1 migration, schema-2 routing persistence, and exact live
  5% RGB vectors.
- Simulator: `ui_sim` build succeeded; no layout changed in this tranche.
- Firmware builds: `crowpanel_128`, `crowpanel_128_roboto`, `canon_ble`,
  `canon_trigger`, `tascam_x8`, `home_assistant`, `aputure_light`, and
  `zhiyun_x100` succeeded sequentially.
- Full profile size: 1,815,036 / 3,145,728 bytes flash (57.7%) and 169,732 /
  327,680 bytes static RAM (51.8%).
- Flash: `crowpanel_128` uploaded successfully to auto-detected
  `/dev/cu.usbserial-211240` and reset.
- Open hardware gate: the light behavior above was driven from the host probe,
  not the newly flashed shared embedded runtime. Panel-originated shared-link
  control, reboot migration, fallback, two-Zhiyun routing, and coexistence soak
  remain unverified.

### Baseline Shark build

- Date: 2026-08-03.
- PlatformIO environment: `crowpanel_128`.
- Baseline firmware flash usage: 807,946 / 3,145,728 bytes (25.7%).
- Baseline static RAM usage: 99,252 / 327,680 bytes (30.3%).
- Instrumented firmware flash usage: 808,962 / 3,145,728 bytes (25.7%).
- Instrumented static RAM usage: 99,276 / 327,680 bytes (30.3%).
- Free heap after BLE scan startup: 144,392 bytes at 922 ms.
- Minimum free heap after BLE scan startup: 144,348 bytes.
- Build result: Success with espressif32 7.0.1.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Host tests: 6/6 passed in the PlatformIO `native` environment.
- Worktree: Firmware sources initially matched `HEAD`; Phase 0 test,
  extraction, scanner-hardening, and telemetry changes were then applied.

### Multi-device foundation build

- Date: 2026-08-03.
- PlatformIO environment: `crowpanel_128`.
- Firmware flash usage: 822,672 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,284 / 327,680 bytes (35.8%).
- Roboto profile flash usage: 792,200 / 3,145,728 bytes (25.2%).
- Home boot free heap: 176,044 bytes at 915 ms.
- Home boot minimum free heap: 173,604 bytes.
- Boot link state: `disconnected`; NimBLE is not initialized until a device is
  opened.
- LVGL object heap: increased from 32 KiB to 48 KiB for Home, device management,
  and the rename keyboard. The first 32 KiB build exhausted the UI heap before
  boot telemetry and was rejected.
- Build result: `crowpanel_128` and `crowpanel_128_roboto` succeeded.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical device controls and persistence workflow: Operator verification
  pending.

### Per-device source layout

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto`.
- Default profile flash usage: 854,674 / 3,145,728 bytes (27.2%).
- Roboto profile flash usage: 824,202 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,636 / 327,680 bytes (35.9%) in both firmware profiles.
- Build result: `ui_sim`, `crowpanel_128`, and `crowpanel_128_roboto`
  succeeded.
- Simulator result: all seven 240x240 PNG captures completed.
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical Shark and persistence regression: Not exercised; the existing
  operator hardware gate remains pending.

### Nano II UI polish

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto`.
- Default profile flash usage: 855,594 / 3,145,728 bytes (27.2%).
- Roboto profile flash usage: 825,122 / 3,145,728 bytes (26.2%).
- Static RAM usage: 117,652 / 327,680 bytes (35.9%) in both firmware profiles.
- Build result: `ui_sim`, `crowpanel_128`, and `crowpanel_128_roboto`
  succeeded.
- Simulator result: eleven 240x240 PNG captures completed, including keypoint
  settings and all three positioning states.
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Host tests: 12/12 passed in the PlatformIO `native` environment.
- Physical touch interaction and Shark movement: Not exercised; the combined
  Phase 0/foundation operator gate remains pending.

### Four-device LVGL capacity fix

- Date: 2026-08-03.
- Reproduction: the supported maximum of one Shark and three Canon instances
  exhausted the previous 48 KiB LVGL heap; an 80 KiB test reached Devices but
  failed when opening Settings.
- Fix: firmware and simulator LVGL heaps use 96 KiB. The simulator now seeds
  all four instances and exercises Settings, rename, Shark and Canon entry,
  and device removal/refresh.
- Simulator result: all twelve captures and the removal regression completed;
  17,640 bytes remained free after maximum-device initialization and 19,752
  bytes after removing one device.
- Build result: `native` passed 15/15 tests; `ui_sim`, `crowpanel_128`, and
  `crowpanel_128_roboto` succeeded.
- Default profile flash usage: 861,596 / 3,145,728 bytes (27.4%).
- Default and Roboto static RAM usage: 167,188 / 327,680 bytes (51.0%).
- Roboto profile flash usage: 831,124 / 3,145,728 bytes (26.4%).
- Flash result: Success on auto-detected `/dev/cu.usbserial-211240`.
- Physical four-device touch regression: Operator verification pending.

### Category-grouped add-device picker

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble`.
- Default profile flash usage: 862,696 / 3,145,728 bytes (27.4%).
- Default and Roboto static RAM usage: 167,196 / 327,680 bytes (51.0%).
- Roboto profile flash usage: 832,224 / 3,145,728 bytes (26.5%).
- Canon-only profile flash usage: 866,412 / 3,145,728 bytes (27.5%);
  static RAM usage: 166,308 / 327,680 bytes (50.8%).
- Simulator result: all thirteen captures completed, including the categorized
  picker; 15,648 bytes remained free after maximum-device initialization and
  17,760 bytes after the removal/refresh regression.
- Host tests: 15/15 passed.
- Build result: all affected environments succeeded.
- Flash result: Success on `/dev/cu.usbserial-211240`.
- Physical picker touch selection: Operator verification pending.

### Full coexistence spike

- Date: Not started.
- Enabled drivers/transports: Not started.
- Flash usage: Not measured.
- Static RAM usage: Not measured.
- Free/minimum heap: Not measured.
- Stability duration: Not measured.
- Result: Not started.

### Canon BR-E1 BLE sub-spike

- Date: 2026-08-03.
- PlatformIO environments: `native`, `ui_sim`, `canon_ble`,
  `crowpanel_128`, and `crowpanel_128_roboto`.
- Combined firmware flash usage: 861,590 / 3,145,728 bytes (27.4%).
- Combined static RAM usage: 118,036 / 327,680 bytes (36.0%).
- Canon-only firmware flash usage: 865,212 / 3,145,728 bytes (27.5%).
- Canon-only static RAM usage: 117,140 / 327,680 bytes (35.7%).
- Build result: all affected environments succeeded with espressif32 7.0.1.
- Flash result: combined `crowpanel_128` succeeded on
  `/dev/cu.usbserial-211240`.

- Host tests: 15/15 passed in the PlatformIO `native` environment.
- Simulator result: twelve 240x240 captures completed, including the Canon
  record-trigger screen.
- Hardware result: EOS R6 Mark III pairing, movie record start/stop, and bonded
  reconnect passed. The first `0x8c`/`0x0c` immediate-mode sequence took a
  still image in photo mode but did not record in movie mode; changing to the
  BR-E1 movie-mode `0x88`/`0x08` press/release sequence passed.
- Remaining hardware checks: forget/re-pair, five-cycle stability, connection
  and command latency, connected free/minimum heap, and Shark regression were
  not completed.
- Scope: This does not verify EOS R6/R6 II, concurrent links, CCAPI, fallback,
  or the full Phase 1 gate.

### Canon smartphone-mode BLE experiment

- Date: 2026-08-03.
- Branch: `spike/canon-smartphone-ble`.
- PlatformIO environments: `native`, `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble`.
- Default firmware flash usage: 875,214 / 3,145,728 bytes (27.8%); static RAM:
  167,836 / 327,680 bytes (51.2%).
- Roboto firmware flash usage: 844,742 bytes; static RAM: 167,836 bytes.
- Canon profile flash usage: 876,014 bytes; static RAM: 166,524 bytes.
- Host tests: 18/18 passed.
- Simulator: Ready, Recording, and Unknown Canon screens captured; Unknown
  exposes separate Start and Stop controls. The maximum-device run completed
  with 8,072 bytes free after initialization and 10,048 bytes after removal.
- Build result: all affected environments succeeded.
- Flash result: `crowpanel_128` succeeded on
  `/dev/cu.usbserial-211240`.
- Hardware result: smartphone-mode pairing, explicit movie control,
  camera-originated state, and reconnect restoration remain operator-pending.
  No EOS R6-family protocol value is marked confirmed by this build/flash.

### Dedicated Portal mode spike

- Date: Not started.
- SoftAP security and address: Not implemented.
- Portal entry/exit: Not tested.
- Studio Wi-Fi suspension/restoration: Not tested.
- Server/AP teardown: Not tested.
- Repeated-cycle memory recovery: Not measured.
- Result: Not started.

## Hardware verification

- Shark Nano II: Panel firmware boots Home with link `disconnected`; on-demand
  pairing, persistence UI, movement, control, and sleep checklist still requires
  operator verification with the slider powered and safe to move.
- Amaran Pano 60c: Not tested.
- Amaran Pano 120c: Not tested.
- Amaran Ace 25c: Not tested.
- Canon EOS R6: Not tested.
- Canon EOS R6 Mark II: Pairing, movie record trigger, and bonded reconnect
  passed; extended stability checks remain open.
- Canon EOS R6 Mark III: Pairing, movie record trigger, and bonded reconnect
  passed; extended stability checks remain open.
- Tascam Portacapture X8 Bluetooth: Future research.
- Deity PR4: Future research.

## Blockers and risks

- ESP32-C3 memory headroom for LVGL, Wi-Fi, BLE Mesh, and multiple GATT links is
  unknown.
- Direct BLE Mesh provisioning and custom GATT coexistence are not yet proven
  with the selected stack.
- SoftAP/HTTP Portal mode teardown and memory recovery are not yet proven.
- Amaran and recorder protocols rely on external or future research and require
  target-hardware validation.

## Session log

### 2026-08-03: Durable roadmap documentation

- Added the documentation index, architecture, implementation phases, device
  matrix, scene model, decision log, and progress handoff.
- No firmware code or build configuration was changed.
- No build or flash was run because this session documented the plan only.

### 2026-08-03: Startup and Portal-mode revision

- Changed startup to a neutral Home menu with connections initiated by device
  screens or scenes.
- Superseded USB serial networking with a dedicated temporary WPA2 SoftAP and
  HTTP server.
- Required Portal mode to suspend normal control and release all server/AP
  resources on exit or inactivity timeout.

### 2026-08-03: Phase 0 software baseline

- Built and flashed the preserved firmware before refactoring; recorded its
  flash and static RAM usage.
- Extracted notification state reduction into Arduino-independent
  `shark_state.*`.
- Added six native tests covering CRC/frame encoding, fragmented and malformed
  stream scanning, command builders, timing edits, state reduction, and reset.
- Fixed frame scanning across a notification split after the first `0xAA` byte
  and bounded malformed declared lengths.
- Added boot, link-transition, and periodic free/minimum-heap telemetry.
- Built and flashed the instrumented firmware and captured startup heap.
- Phase 0 remains open until the physical Shark behavior checklist passes.

### 2026-08-03: Multi-device Shark foundation

- Recorded ADR-013 to advance selected Phase 2/3/7 foundation work while Phase
  0 hardware and Phase 1 feasibility gates remain open.
- Added a compile-time Shark driver catalog, fixed-capacity runtime registry,
  versioned checked persistence, legacy pairing migration, typed command/result
  queues, and a loop-owned `DeviceManager`.
- Adapted Shark behind an on-demand driver lifecycle; boot and Home do not
  initialize, scan, pair, or reconnect BLE.
- Added Home, Devices, add/rename/enable/disable/forget/remove workflows and
  retained the specialized Shark connect, keypoint, positioning, and run UI.
- Expanded native coverage from 6 to 12 tests for catalog, registry, dormant
  records, corruption handling, migration, routing, disabled devices, and empty
  registry persistence.
- Built both panel profiles, flashed the default profile, and captured Home boot
  heap. Physical operator verification remains open.

### 2026-08-03: Desktop LVGL UI simulator

- Added PlatformIO `ui_sim` host environment that compiles the real `ui` /
  `shark_ui` sources against LVGL with Arduino/device stubs.
- Captures round 240x240 PNGs to `sim/screenshots/` via ImageMagick (`magick`)
  without flashing: Home, Devices, manage, rename keyboard, Shark connect,
  keypoints, and run.
- Simulator uses `LV_COLOR_16_SWAP=0` and a 128 KiB LVGL heap (firmware keeps
  48 KiB). Build and capture succeeded locally.

### 2026-08-03: Icon Home screen

- Generated Devices/Groups/Scenes/Portal glyphs with Gemini 2.5 Flash Image
  (Nano Banana) via Vertex AI after the AI Studio key used by the nano-banana
  MCP was blocked (`API_KEY_SERVICE_BLOCKED`).
- Embedded 48x48 `ALPHA_8BIT` icons (`tools/gen_icons.py`) and rebuilt Home as
  a 2x2 mode tile grid; Devices is active, other modes are disabled placeholders.
- Enabled `LV_USE_IMG`. Sim capture and `crowpanel_128` flash used for verify.

### 2026-08-03: Colorful Nano Banana Pro Home icons

- Regenerated fun colorful mode icons with `gemini-3-pro-image-preview`
  (Nano Banana Pro). The nano-banana MCP still calls retired
  `gemini-2.5-flash-image-preview`, so generation used the Gemini API directly.
- Switched embeds to `LV_IMG_CF_TRUE_COLOR_ALPHA` with swap0/swap1 branches so
  colors survive on both `ui_sim` and the panel. Disabled modes dim via opacity.
- Sim capture + `crowpanel_128` flash succeeded.

### 2026-08-03: Compact paged rename input

- Replaced the rectangular LVGL QWERTY keyboard with a round-native 3x3 keypad.
  Arrow buttons or horizontal swipes change between A-I, J-R, S-Z, and
  number/symbol pages.
- Character keys insert directly; Space, backspace, and case controls remain
  available. Icon Save/Cancel controls sit beside the name field.
- `ui_sim` rebuilt and captured `04_rename.png`; `crowpanel_128` built and
  flashed successfully (854,686 bytes flash, 117,644 bytes static RAM).
- `crowpanel_128_roboto` also built successfully (824,214 bytes flash, 117,644
  bytes static RAM). Physical keypad interaction remains operator-pending.

### 2026-08-03: Per-device source layout

- Consolidated the Shark protocol, state reducer, NimBLE client, driver adapter,
  and specialized UI under `src/devices/shark_nano_ii/`.
- Updated firmware, native-test, and simulator includes and source filters
  without changing namespaces, public APIs, runtime wiring, or behavior.
- Updated repository layout and device-support documentation for the
  per-device source convention.
- Native tests passed 12/12; `ui_sim` built and captured all seven screens;
  both firmware profiles built; the default profile flashed successfully.
- Physical Shark behavior was not exercised, so the existing combined
  Phase 0/foundation operator gate remains open.

### 2026-08-03: Nano II UI polish

- Polished Connect, Keypoints, Run, keypoint settings, and positioning views
  while preserving command routing, swipe navigation, and movement safeguards.
- Replaced cryptic keypoint markers and icon-only run toggles with readable
  labels, clarified action hierarchy, and prevented active-screen content from
  showing around modal edges.
- Expanded the simulator from seven to eleven captures with deterministic
  settings, positioning-choice, manual-positioning, and joystick states.
- Native tests passed 12/12; `ui_sim` and both firmware profiles built; the
  default profile flashed successfully.
- Physical touch and slider behavior remain operator-pending, so the existing
  combined Phase 0/foundation hardware gate remains open.

### 2026-08-03: Canon BR-E1 BLE sub-spike

- Recorded ADR-014: BLE exposes one honest record trigger and no inferred
  recording state.
- Added a compile-time Canon camera driver, asynchronous NimBLE connect/security
  progression, BR-E1 pairing identity, bonded reconnect/forget, and loop-owned
  press/release writes.
- Generalized `DeviceManager` to a bounded compiled-driver table and made
  Devices add/open behavior catalog-driven while preserving one active instance.
- Added the round Canon screen, native protocol/routing tests, simulator fake
  and screenshot, and a `canon_ble` firmware profile.
- Native tests, simulator capture, all affected firmware builds, and the
  combined firmware flash succeeded.
- Physical EOS R6 Mark III pairing and bonded reconnect passed. Hardware
  testing exposed an immediate-mode/photo command mismatch; the corrected
  `0x88`/`0x08` movie sequence then started and stopped recording successfully.
- Canon forget/re-pair, repeated-cycle measurements, and Shark regression
  remain operator-pending, so no roadmap phase gate is marked complete.

### 2026-08-03: Home icon prompt documentation

- Preserved the exact shared style prompt, per-mode subject prompts, successful
  Nano Banana Pro model/settings, visual review criteria, and LVGL conversion
  workflow in `assets/icons/README.md`.
- Linked the icon source and prompt guide from the repository README.
- Documentation only; firmware behavior and generated assets were unchanged,
  so no build, flash, simulator capture, or hardware check was run.

### 2026-08-03: Four-device UI heap fix

- Reproduced the reported freeze as LVGL allocation failure at the supported
  maximum of one Shark and three Canon device records.
- Increased the LVGL object heap from 48 KiB to 96 KiB and made the simulator
  use the same limit instead of masking firmware pressure with 128 KiB.
- Expanded simulator coverage to seed all four records and exercise management,
  rename, both device screens, and removal/refresh.
- Native tests, all simulator captures, both firmware builds, and the default
  profile flash succeeded. Physical touch confirmation remains pending.

### 2026-08-03: Canon Trigger/Smart split

- Recorded ADR-015 and the user-facing names `Canon (Trigger)` and
  `Canon (Smart)`.
- Defined Smart as smartphone-mode BLE pairing and automatic Wi-Fi handoff
  followed by direct-access-point CCAPI control; Trigger remains the verified
  BR-E1 fallback.
- Marked Smart blocked on an EOS R6 Mark III Camera Connect handoff capture and
  documented the required capture contents and research boundaries.
- Documentation only; firmware behavior was unchanged, so no build, flash,
  simulator capture, or hardware check was run.

### 2026-08-03: Category-grouped add-device picker

- Replaced automatic first-available-driver creation with an explicit,
  scrollable model picker grouped by Motion, Lights, Cameras, and Recorders.
- Kept compiled drivers visible when their instance limit is reached and
  disabled those choices with a clear status.
- Released picker rows when the overlay closes to preserve the bounded LVGL
  heap at the four-device maximum.
- Added a simulator capture for the picker. Native tests, simulator captures,
  Roboto, Canon-only, and default firmware builds passed; the default profile
  flashed successfully.
- Physical touch selection remains operator-pending.

### 2026-08-03: Canon EOS R6 Mark II verification

- Operator verified pairing, the BR-E1 movie record trigger, and bonded
  reconnect on the EOS R6 Mark II with the existing Canon Trigger driver.
- No model-specific protocol or firmware change was required.
- EOS R6 remains unverified; extended cycle, forget/re-pair, latency, heap, and
  coexistence checks remain open for the verified models.
- Documentation only; no build or flash was required.

### 2026-08-03: Canon device-name title

- Replaced the Canon control screen's hardcoded brand title with the runtime
  device instance name; long names remain bounded to one line.
- Simulator capture verified `EOS R6 Mark III` fits the round-screen header
  without overlapping connection status.
- Native tests passed 15/15; `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `canon_ble` built successfully.
- Default firmware used 862,760 bytes flash and 167,196 bytes static RAM; the
  default profile flashed successfully to `/dev/cu.usbserial-211240`.

### 2026-08-03: Hardware trigger activates device CTA

- Routed GPIO 1 short presses through each active device UI's primary action:
  Canon sends its connected record trigger, while Shark opens Run from
  Keypoints and then advances Standby / Start / Stop.
- Touch and hardware activation share the same action helpers; disconnected
  CTAs remain inactive, and Shark modal/positioning dismissal is unchanged.
- Added simulator regressions for the full Shark run cycle and Canon trigger.
  Native tests passed 15/15, all UI captures completed, and the simulator
  finished with 17,728 bytes of LVGL memory free after device removal.
- `crowpanel_128` built at 862,762 / 3,145,728 bytes flash and 167,196 /
  327,680 bytes static RAM. `canon_ble` built at 866,478 bytes flash and
  166,308 bytes static RAM.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical button behavior on the Shark and Canon
  hardware remains operator-pending.

### 2026-08-03: Tascam X8 captured protocol and record-control driver

- Analyzed an annotated nRF52840 research capture (SHA-256
  `115e77bcc91ca2c184439115df97ad0459ac8452018ce0e08bdde6568918fd51`),
  later removed from the publishable tree,
  and documented the AK-BT1 UUIDs, COBS stream, session open/keepalive, exact
  record start/stop writes, and recorder-originated transition events in
  `docs/protocols/tascam-x8.md`.
- Recorded ADR-016 and added the compile-time `tascam.portacapture_x8` recorder
  driver with explicit `RecordStart`/`RecordStop`, on-demand connection,
  persisted identity, and confirmed-transition-only Ready/Recording UI.
- A second capture pass rejected the earlier `0x81`/`0x10` steady-state
  interpretation. State reduction now uses the confirmed `DR 20 20 24 01`
  start and `DR 10 20 08` stop notifications, and initialization waits for the
  session characteristic's `10` open response. A stable reconnect-state field
  remains unproven.
- Added fragmented COBS/golden-vector/state/catalog/routing host tests and
  simulator fake state, hardware-button start/stop regression, and Ready plus
  Recording screenshots.
- Native tests passed 18/18. `ui_sim` built and completed all captures with
  10,024 bytes LVGL memory free at the five-device maximum and 12,032 bytes
  after removal.
- Firmware builds passed: `crowpanel_128` used 870,784 bytes flash and 167,692
  bytes static RAM; `crowpanel_128_roboto` used 840,312/167,692;
  `tascam_x8` used 872,144/166,532; and `canon_ble` used 871,426/166,388.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical X8/AK-BT1 connection, command, state,
  reconnect, and media-file checks remain operator-pending; the tranche is not
  hardware-complete.

### 2026-08-03: Tascam asynchronous connect and reconnect-state restoration

- Analyzed a controlled recording/stopped reconnect capture (SHA-256
  `7d095c94a454827778f3ecc86778b70e2109269f2e47acd0383c997f019ec783`),
  later removed from the publishable tree.
  `DR 20 20 00` reports recording as `0x81`, stopped as `0x10`, and the
  transition between them as `0x82`.
- Added capture-backed current-state reduction so reconnect restores confirmed
  Ready/Recording instead of remaining unknown. Transitional `0x82` does not
  overwrite the last confirmed state.
- Changed the Tascam direct connection attempt to NimBLE's asynchronous mode;
  connect callbacks only set flags and service/session setup remains owned by
  `loop()`, allowing the Tascam screen to load before connection completes.
- Native tests passed 18/18. Firmware builds passed: `crowpanel_128` used
  870,986 bytes flash and 167,700 bytes static RAM;
  `crowpanel_128_roboto` used 840,514/167,700; and `tascam_x8` used
  872,334/166,532.
- The default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Immediate screen display and recording/stopped
  reconnect restoration remain operator-pending hardware checks.

### 2026-08-03: Tascam asynchronous session-loop regression fix

- Hardware exposed an infinite reconnect loop: BLE connected, but the panel
  remained in Waiting because the session-open timeout was evaluated before
  asynchronous link setup had started and therefore used its zero-initialized
  deadline.
- Added an explicit session-opening phase, delayed GATT/session setup briefly
  after the link callback, restored fresh attribute discovery on each attempt,
  and cancel an outstanding asynchronous attempt when leaving the screen.
- Native tests passed 18/18. Firmware builds passed: `crowpanel_128` used
  871,082 bytes flash and 167,700 bytes static RAM;
  `crowpanel_128_roboto` used 840,610/167,700; and `tascam_x8` used
  872,442/166,540.
- The corrected default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Hardware reconnection remains operator-pending.

### 2026-08-03: Tascam record-control hardware regression passed

- Operator verification passed for immediate Tascam screen opening, persisted
  reconnect, recording-state restoration after remote restart, stopping the
  pre-existing recording, and creation of the expected media file.
- The bounded ADR-016 record-control hardware tranche is verified. Battery,
  media-capacity reporting, mixer controls, and scene integration remain
  outside this tranche.
- Current Canon Trigger and Shark control were also reported working. Canon
  Smart remains blocked on its BLE-to-Wi-Fi handoff capture, and the exact
  extended Canon/combined foundation checks listed above remain open until
  individually measured or confirmed.

### 2026-08-03: Canon smartphone-mode BLE replacement experiment

- Created `spike/canon-smartphone-ble`, preserving the verified BR-E1 driver on
  the main branch, and recorded ADR-017 plus protocol confidence boundaries.
- Replaced the branch's Canon protocol with encrypted smartphone pairing,
  camera confirmation, stable controller identity, shooting-session
  subscriptions, explicit `00 10`/`00 11` movie commands, and
  camera-notification-only state reduction.
- Added asynchronous connect/security/setup phases and a loop-owned
  notification queue. NimBLE callbacks do not parse, mutate state, issue GATT
  writes, or access LVGL.
- Replaced the stateless trigger screen with Ready, Recording, transitional,
  and Unknown states. Unknown provides separate Start and Stop touch controls;
  the hardware button starts unless recording is camera-confirmed.
- Native tests passed 18/18. Simulator, default, Roboto, and Canon profile
  builds passed with the measurements above. The default profile flashed
  successfully to `/dev/cu.usbserial-211240`.
- Hardware pairing and camera behavior remain unverified. The branch must not
  replace the main BR-E1 implementation until the ADR-017 hardware gate passes.

### 2026-08-03: Canon R6 pairing-order correction

- First hardware attempt reached the camera confirmation screen and displayed
  `StudioRemote`, but the camera then reported `Connection target not found`.
- Comparison with newer Canon Camera Connect/furble/EOS RP research found that
  the implementation used the older EOS M6 ordering: it waited for camera
  confirmation before sending controller ID, name, and type. Newer clients send
  those identity records before waiting for the accept indication.
- Changed first-pair setup to subscribe with indications when supported, write
  the handshake request, immediately send stable ID/name/Android type, wait for
  `02`, and only then send the finish marker and open the shooting session.
- Native tests passed 18/18. `crowpanel_128`, `crowpanel_128_roboto`, and
  `canon_ble` rebuilt successfully with the updated measurements above.
- The corrected flash could not be uploaded because
  `/dev/cu.usbserial-211240` was no longer present. PlatformIO detected the
  unrelated nRF BLE sniffer at `/dev/cu.usbmodem101`, and the upload failed
  without modifying the panel. Reflash and hardware retry remain pending.

### 2026-08-03: Canon Camera Connect pairing and Wi-Fi handoff captures

- Analyzed Android host-HCI captures of fresh EOS R6 Mark III
  smartphone pairing, BLE movie control, bonded reconnect, and successful
  Camera Connect Wi-Fi offload.
- Added minimized ATT-only research sets (SHA-256
  `fac58a7277072f25b45c91f5051dae9c335d71ca9323e9388b69f6e3399cd08c`
  and `25e59aca42f47a9ca554fd85273f8bfe5b9f5577d96c9d59051838e407bf17ad`),
  later removed from the publishable tree.
  SMP keys, camera serial number, controller ID, SSID-like value, and
  credential-like value are excluded.
- Camera Connect waits for pairing indication `02` before writing controller
  ID, name, and Android type, disproving the branch's identity-first ordering.
  The camera accepts bonded legacy Just Works rather than the Secure
  Connections/MITM requested by Android.
- Confirmed shooting-session command/result `03`/`05`, movie commands
  `00 10`/`00 11`, and recording states `01 01 02`/`01 01 01`. The current
  branch's `02` then `06` session strategy does not match Camera Connect.
- Identified Wi-Fi handoff as write `01` to `00020002-...`, followed by
  indications `01 03` and `02 03` on `00020003-...`. Camera Connect acquired
  `camera_connect:CCBleHandOverWakeLock` at the same time.
- Confirmed camera power/session controls after shooting: `03` wakes from
  Bluetooth standby and receives `05`; `04` leaves shooting and receives `01`;
  `05` powers down and receives `01`, followed by camera-side BLE disconnects
  approximately 147-154 ms later.
- Network security mode, DHCP behavior, camera IP/port, and the first CCAPI
  request remain uncaptured. No build or flash was run because this session
  changed only protocol fixtures and documentation. The next safe task is to
  align the experimental BLE client with the captured confirmation-first
  handshake and `03` session command before another hardware retry.

### 2026-08-03: Canon captured BLE behavior implemented

- Aligned the ADR-017 client with the Android host-HCI research:
  bonding-only Just Works negotiation, request-before-subscribe pairing,
  confirmation-first identity, captured `06`/`07`/`08`/`0c` post-pair queries,
  and automatic `03` wake with required `05` session result. Bonded reconnect
  follows the same finish/query/wake path; the uncaptured `02`/`06` fallback
  was removed.
- Added ADR-018 and an explicit camera power command. The Canon screen disables
  power-down while recording or a record command is pending, sends mode `05`,
  waits for acknowledgement and the camera-side disconnect, and does not infer
  physical success from the acknowledgement alone. Back remains a local,
  non-destructive disconnect.
- Contradictory camera state now reports the requested record transition as
  failed while retaining the camera-confirmed steady state. Renamed the
  multi-device Unity runner from `test/test_shark.cpp` to
  `test/test_main.cpp`.
- Native tests passed 18/18. The UI simulator passed and generated
  `12_canon_ready.png` through `15_canon_powered_off.png`; LVGL memory had
  7,208 bytes free at the five-device maximum and 9,096 bytes after removal.
- Firmware builds passed: `crowpanel_128` used 876,724 bytes flash and 167,868
  bytes static RAM; `crowpanel_128_roboto` used 846,252/167,868; `canon_ble`
  used 877,528/166,548; and `tascam_x8` used 876,496/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. First pairing, bonded reconnect, automatic
  physical wake, record start/stop state, explicit physical power-down, and
  non-destructive Back remain operator-pending hardware checks; ADR-017 and
  ADR-018 are not hardware-complete.

### 2026-08-03: Canon discovery and stale-bond recovery

- Investigated an operator report that the camera no longer found the panel
  after the Pixel Camera Connect capture. The host-HCI log confirms that the
  camera sends `00010000-...` in its primary advertisement and
  `EOSR6m3_...` in a separate scan response.
- Canon discovery now accepts either the pairing service or an
  `EOS`/`PowerShot` advertised name, avoiding dependence on whether NimBLE
  merges the two reports before invoking the scan callback.
- A saved camera that fails encryption twice is now treated as a stale bond:
  the panel removes its local key, marks the record unpaired, and returns to
  discovery instead of retrying the invalid direct connection indefinitely.
  This is expected after the camera's smartphone registration is replaced or
  reset; the camera must still be in **Connect to smartphone** pairing mode.
- Documented a planned UI-memory optimization: retain only Home/Devices, lazily
  allocate other screens and overlays, share Canon/Tascam recording controls,
  and reduce the 96 KiB LVGL pool only after measured peak/fragmentation tests.
- Native tests passed 18/18. Builds passed: `crowpanel_128` used 877,114 bytes
  flash and 167,868 bytes static RAM; `crowpanel_128_roboto` used
  846,642/167,868; `canon_ble` used 877,922/166,556; and `tascam_x8` used
  876,882/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Boot telemetry from the preceding discovery build
  reported 125,180 bytes free heap and 122,740 minimum. The serial port was
  disconnected after the final flash, so physical rediscovery and pairing
  remain operator-pending rather than verified.

### 2026-08-03: Canon initial recording-state read

- Hardware verification confirmed smartphone-mode discovery, pairing,
  start/stop commands, and correct notification-driven state after each
  command. Initial state remained unknown until the first transition.
- The captured GATT declaration marks shooting-state characteristic
  `00030031-...` as Read + Notify. After wake result `05`, the client now reads
  that characteristic once and applies only the existing documented stopped
  or recording vectors; an empty, failed, or unfamiliar read leaves state
  unknown.
- Native tests passed 18/18. Builds passed: `crowpanel_128` used 877,240 bytes
  flash and 167,868 bytes static RAM; `crowpanel_128_roboto` used
  846,768/167,868; `canon_ble` used 878,048/166,556; and `tascam_x8` used
  877,008/166,652.
- The final default firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Correct Ready/Recording display immediately after
  connection remains operator-pending and is not yet marked verified.

### 2026-08-03: Canon power button wakes after power-off

- Confirmed the Canon power control stays enabled while the camera is powered
  off and routes `CameraPowerOn`, which reconnects and runs the captured wake
  sequence instead of leaving the control disabled.
- Native tests passed 18/18. Simulator captures include
  `15_canon_powered_off.png` and `16_canon_powered_on.png`.
- Firmware builds passed and the default profile flashed successfully to
  `/dev/cu.usbserial-211240`. Physical power-off then power-on wake remains
  operator-pending.

### 2026-08-03: Canon R6 Mark II connection-target note

- Operator report: EOS R6 Mark II shows **Connection target not found** while
  the same panel build pairs and controls the EOS R6 Mark III. Removing the
  panel device record and the Mark III entry did not clear the Mark II error.
- Canon documentation treats that message as the camera failing to find its
  previously registered smartphone/app target. The panel is a BLE central and
  does not advertise, so the R6 II must use **Add a device to connect to**
  rather than a saved phone entry; camera-side smartphone registrations may
  still need deletion even after the panel forgets a body.
- Hardened discovery further: match Canon manufacturer ID `0x01A9` and names
  containing `EOS`/`R6`/`PowerShot`, not only an `EOS` prefix or service UUID.
  Device removal now also drops controller-side bonds. Scan hits log to serial
  as `canon scan hit ...` for the next hardware retry.
- Shortened failed bonded-reconnect retries so the client falls back to scan
  after one miss instead of repeatedly targeting a powered-down or different
  body.

### 2026-08-03: EOS R6 Mark II reaches Ready on mode `04`

- Serial on `EOSR6m2_D4D530` showed the panel bonding and opening the core
  session, then stalling in `OpeningSession` because the client only treated
  wake result `05` (R6 III Camera Connect) as Ready. The R6 II notifies `04`,
  matching public EOS M6 shooting-mode/wake behavior.
- `parseModeEvent` now accepts `04` and `05` as session-ready. Core subscribe
  prefers indications when offered; OpeningSession will retry shooting mode
  `02` if wake `03` times out. Multi-camera scan dwell prefers `m2` names and
  ignores bodies that remotely terminate during bonding.
- Native tests passed 18/18. Default firmware flashed to
  `/dev/cu.usbserial-211240`. Capture confirmed
  `canon mode notify ... byte0=04` then `canon session ready` with
  `link=connected`. Operator should verify record start/stop on the R6 II.
- R6 II GATT lacks pairing-info `0001000c` (has read-only `0001000b`); post-pair
  queries remain skipped on that body.

### 2026-08-03: Canon multi-instance address lock

- Operator report: controlling the second Canon instance (R6 III) sent record
  commands to the first body (R6 II). Root cause: bonded reconnect fell back to
  an open scan and could adopt a sibling camera, then persisted that address
  onto the active instance.
- Bonded instances now lock to their saved BLE address, skip other paired peer
  addresses, and do not rewrite pairing identity on ordinary reconnect.
  Confirmation timeouts still rotate during fresh pairing only.
- Native tests passed 18/18; default firmware flashed to
  `/dev/cu.usbserial-211240`. If an R6 III record was already overwritten with
  the R6 II address, Forget that instance and re-pair on Add-a-device.

### 2026-08-03: Dual Canon Trigger + Smart drivers

- Restored BR-E1 `Canon (Trigger)` into `src/devices/canon_trigger/` with
  `DriverId::CanonTrigger = 4`. Kept smartphone BLE as `Canon (Smart)` at
  `DriverId::CanonBle = 2` so existing NVS Smart records stay valid.
- Catalog labels: `Canon (Trigger)` / `canon.eos_r6.trigger` and
  `Canon (Smart)` / `canon.eos_r6.smartphone_ble`. Both compile by default;
  optional `canon_trigger` / `canon_ble` PlatformIO envs isolate each driver.
- ADR-017 updated: Smart no longer replaces Trigger; both ship, camera menu
  must match the chosen driver, one active transport remains.
- Dual Canon screens plus the five-instance sim seed exhausted the prior 96 KiB
  LVGL heap (freeze at Add device). Raised `LV_MEM_SIZE` to 128 KiB in
  firmware and `ui_sim`.
- Host tests: 20/20 passed (`native`).
- Simulator: captures through `20_tascam_recording.png`, including
  `03_add_device.png`, `17_canon_trigger_ready.png`, and
  `18_canon_trigger_sent.png`. Free after max-device init: 36,680 bytes.
- Firmware: `crowpanel_128` build succeeded (flash 888,272 / RAM 201,172 with
  128 KiB LVGL). An earlier dual-driver image flashed to
  `/dev/cu.usbserial-211240`; the post-heap-bump reflash could not run because
  that port was absent (only unrelated usbmodem devices present).

### 2026-08-03: UI memory optimization

- Implemented the planned UI allocation work: Home/Devices stay resident;
  Add/Manage/Rename overlays are created on open and deleted on close; Shark,
  Canon Trigger, Canon Smart, and Tascam screens are built on show and released
  after navigation leaves them.
- Extracted shared `src/ui/recorder_shell.*` for Canon (Smart) and Tascam, with
  optional power and unknown START/STOP controls owned by adapters.
- Simulator full-navigation peak LVGL use was 17,012 bytes (frag 45% at end).
  Reduced `LV_MEM_SIZE` from 128 KiB to 64 KiB in firmware and `ui_sim`.
- Host tests: 20/20 passed (`native`).
- Simulator: captures through `20_tascam_recording.png`. After max-device init
  with 64 KiB pool: 40,152 bytes free / 10,684 peak; after remove refresh:
  42,208 free / 17,012 peak.
- Firmware: `crowpanel_128` build succeeded (flash 889,324 / RAM 135,628) and
  flashed to `/dev/cu.usbserial-211240`. Physical navigation regression remains
  operator-pending.

### 2026-08-04: On-device Scenes (Press Record / Press Stop)

- Recorded ADR-019 (authored Start/Stop) and ADR-020 (concurrent sequence
  links). Extended `DeviceManager` with `activateHeld` / multi-active loop and
  dispatch while keeping exclusive `activate` for device screens.
- Added scene registry/store/runner/service, separate NVS `scenes` blob, panel
  Scenes UI (list/edit Start/edit Stop/run), and Press Record seed
  (Canon RecordStart → wait 500 ms → Tascam RecordStart; Stop: Canon then
  Tascam RecordStop).
- Host tests: 23/23 passed (`native`), including concurrent links, scene store
  round-trip/corruption, and Press Record Start/Stop ordering.
- Simulator: captures `21_scenes_list` through `27_scenes_stop_progress`; full
  run reached IdleArmed then Completed.
- Firmware: `crowpanel_128` build succeeded (flash 906,206 / RAM 137,404) and
  flashed to `/dev/cu.usbserial-211240`.
- Hardware gate still open: exercise Press Record / Press Stop on real Canon
  Smart + Tascam with concurrent GATT. Groups, lights, Portal editing, and
  generated reverse-Stop remain deferred.

### 2026-08-04: Sequence add-step Category → Device → Action

- Restructured Scenes `+ Step` picker into three levels: category (plus Wait),
  enabled device in that category, then Record Start / Record Stop.
- Back within the overlay returns one level; hardware short-press matches.
- Simulator captures: `22b_scenes_add_category`, `22c_scenes_add_device`,
  `22d_scenes_add_action`.
- Firmware rebuilt and flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Shared category-icon picker shell

- Extracted `src/ui/picker_shell.*` for Devices **Add device** and Scenes
  **+ Step**: Category icon grid → driver/device list → (scene) action.
  SceneStep keeps Wait 500 ms on the category screen.
- Added cute category icons (`icon_cat_{motion,lights,cameras,recorders}`) via
  Nano Banana / `tools/gen_icons.py` → `ui_icon_cat_*`.
- Picker overlay is deleted on close (not merely hidden) so LVGL heap stays
  stable across later Shark navigation in `ui_sim`.
- Simulator: full capture through `27_scenes_stop_progress`; peak LVGL use
  17,012 bytes after remove refresh.
- Firmware: `crowpanel_128` build succeeded (flash 935,596 / RAM 137,428) and
  flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Scenes settings + prepare-on-open

- Removed Scenes-list Press Record seed button; `+` names blank sequences
  `Sequence n` with `n = count + 1`. `seedPressRecord` remains for sim/tests.
- Added `ScenePhase::Ready` and `prepare()`; opening a run screen connects all
  Start/Stop targets and holds links; Start from Ready skips re-activate.
  Amended ADR-020. Run-screen settings cog: Rename / Edit Start / Edit Stop /
  Delete. Shared `ui::promptRename` for scene rename.
- Host tests: 24/24 including prepare→Ready→Start from held links.
- Simulator: list without seed; `23b_scenes_settings`; `24_scenes_run_ready` at
  Ready phase; peak LVGL use 17,012 bytes after remove refresh.
- Firmware: `crowpanel_128` build succeeded (flash 937,214 / RAM 137,444) and
  flashed to `/dev/cu.usbserial-211240`.

### 2026-08-04: Shared BLE central manager

- Recorded ADR-021. Added `src/core/ble/`: backend-independent bounded central,
  fixed advertisement/link/security events, shared scan demand and main-loop
  fan-out, address claims/skip lists, async connection slots, watchdog/backoff,
  serialized security requests, bond deletion, fake native backend, and lazy
  NimBLE runtime teardown.
- Migrated Tascam, Canon Trigger, Shark, and Canon Smart away from independent
  NimBLE initialization, scanners, clients, link callbacks, retry timers, and
  teardown. Device-specific advertisement matching, Canon candidate dwell and
  ignored peers, GATT setup, handshakes, commands, and notification queues
  remain in each client.
- Native tests: 29/29 passed in `native`, including lifetime/slot exhaustion,
  scan fan-out and independent release, address claims, concurrent async links,
  retry/watchdog behavior, security requests, bond deletion, queue overflow,
  advertisement parsing, and all four device matchers.
- UI simulator: build and all captures through
  `27_scenes_stop_progress.png` succeeded; final LVGL report was 24,728 bytes
  free, 17,012-byte peak use, and 35% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 941,882 bytes flash / 137,884 bytes RAM;
  `canon_ble` 941,046 / 136,540;
  `canon_trigger` 938,140 / 136,172;
  `tascam_x8` 939,998 / 136,380.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240`. Bounded restart telemetry at 835 ms reported
  Home `disconnected`, 155,004 bytes free heap, and 152,564 minimum free heap,
  confirming boot remains BLE-free.
- Hardware gate remains open: no operator interaction with Shark, Canon
  Trigger, Canon Smart, or Tascam was performed in this session. Concurrent
  Canon Smart + Tascam Prepare, scan drops, per-link retries, prepare latency,
  post-BLE-init heap, post-teardown heap, and physical command confirmation
  therefore remain unmeasured and must not be inferred from the successful
  build/flash.

### 2026-08-04: Ble(e)p project rename

- Renamed the user-facing project identity from Studio Remote / Universal
  Studio Remote to **Ble(e)p** in the README, documentation index,
  architecture goal, agent guidance, and round-panel Home title.
- Changed the NimBLE local name and Canon Trigger/Smart pairing identity from
  `StudioRemote` to `Ble(e)p`. Persistent registry schemas, NVS namespaces,
  driver IDs, device names, and historical verification notes remain unchanged.
- Updated local PlatformIO examples to invoke `./.venv/bin/python -m platformio`;
  the generated `./.venv/bin/platformio` launcher retained an absolute shebang
  to the pre-rename workspace path.
- Native tests: 29/29 passed, including the updated Canon Smart handshake name
  and length vector and Canon Trigger pairing-name coverage.
- UI simulator: build and all captures through
  `27_scenes_stop_progress.png` succeeded. `01_home.png` visually confirms the
  Ble(e)p title fits the 240x240 round Home screen. Final LVGL report was 24,728
  bytes free, 17,012-byte peak use, and 35% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 941,866 bytes flash / 137,884 bytes RAM;
  `crowpanel_128_roboto` 911,402 / 137,884;
  `canon_ble` 941,030 / 136,540;
  `canon_trigger` 938,174 / 136,172;
  `tascam_x8` 940,050 / 136,380.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240`. Physical BLE-name and Canon re-pair display checks
  were not performed.

### 2026-08-04: Sequence hardware action button

- Changed the hardware short-press behavior on an open sequence run screen to
  invoke the same state-aware action as the touch controls: Start when ready or
  restartable, and Stop when armed or while Start is in flight. The button is
  inert while preparation or Stop is already in progress; touch Back/Unlink
  remains the way to leave and release held links.
- Updated the UI simulator regression to start and stop Press Record through
  `ui::handleShortPress()`. The simulator build and all captures through
  `27_scenes_stop_progress.png` succeeded; final LVGL reporting remained 24,728
  bytes free, 17,012-byte peak use, and 35% fragmentation.
- Native tests: 29/29 passed. `crowpanel_128` built successfully with 942,028
  bytes flash and 137,884 bytes static RAM, then flashed successfully to
  `/dev/cu.usbserial-211240`.
- Physical button operation against connected Canon Smart and Tascam targets
  was not exercised, so the existing scene hardware gate remains open.
- Renamed the prepared-link control from `Cancel` to `Unlink`, while retaining
  `Cancel` only during `Connecting`; the prepared sequence status remains
  `Ready`. Native tests remained 29/29, the simulator and all captures passed,
  and `24_scenes_run_ready.png` visually confirmed both labels fit. The updated
  `crowpanel_128` build used 942,078 bytes flash / 137,884 bytes static RAM and
  flashed successfully to `/dev/cu.usbserial-211240`.

### 2026-08-04: Protocol-ready sequence preparation and BLE timing

- Split physical `LinkState::Connected` from `DeviceRuntimeState::protocolReady`.
  Sequence preparation remains `Connecting` and rejects Start until every
  target reports both. Readiness clears on failure, retry, release, and
  reconnect.
- Canon Smart now uses targeted handshake then shooting-core discovery and
  becomes ready only on the camera's session-ready notification. Tascam waits
  for session-open and its initialization write. Canon Trigger waits for
  discovery and the pairing-identity write. Shark waits for subscription and
  every handshake/initial-refresh write; failed final writes cannot publish
  readiness.
- Removed both fixed 100 ms post-connect waits. Setup begins on the next main
  loop after queued BLE events drain. All four drivers request best-effort
  7.5–15 ms setup and 15–30 ms steady-state connection parameters; rejection
  is logged but non-fatal.
- Added stable serial diagnostics in the form
  `ble_timing driver=<id> link=<n> stage=<stage> elapsed_ms=<n> total_ms=<n> result=<status>`
  for central connection lifecycle, security, targeted GATT stages, protocol
  readiness, retries, teardown, and total sequence preparation.
- Native tests: 30/30 passed, including physical-versus-protocol readiness,
  Start rejection before readiness, connection-parameter fallback, steady-state
  timing, and readiness reset on release/reconnect.
- UI simulator: build and all captures succeeded; maximum-device initialization
  reported 40,128 bytes free, 10,694-byte peak use, and 0% fragmentation;
  remove/refresh reported 24,728 bytes free, 17,012-byte peak use, and 35%
  fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 945,250 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 914,786 / 138,028;
  `canon_ble` 944,310 / 136,668;
  `canon_trigger` 941,428 / 136,300;
  `tascam_x8` 943,328 / 136,508.
- `crowpanel_128` flashed successfully to
  `/dev/cu.usbserial-211240` after granting serial-port access.
- Hardware benchmark remains open: no paired devices were operated, so the ten
  initially disconnected cycles per driver, ten concurrent Canon Smart +
  Tascam opens, median/p95 stage timing, disconnect/retry/drop rates, physical
  Start/Stop, and heap recovery are still pending. The 25% asynchronous GATT
  executor gate cannot be evaluated from builds and simulator results; ADR-021
  therefore remains unamended and the executor is intentionally deferred.

### 2026-08-04: Intermittent paired-sequence connection fix

- Captured a live Canon Smart + Tascam sequence attempt from the flashed board.
  Simultaneous controller connection initiations repeatedly failed with reason
  `574` (`0x23e`, HCI `0x3e` connection-establishment timeout). Canon connected
  when it received an uncontended attempt; Tascam later linked and completed
  GATT setup.
- Changed the central scheduler to keep per-link async slots and shared scan
  discovery, but run only one controller connection or security procedure at a
  time. After a link/security event clears the controller, the next queued
  target begins; protocol initialization on an established link can still
  overlap that connection.
- Fixed a Tascam readiness regression found in the same trace. Its initialization
  write had been moved before the client's physical `Connected` state, while
  the write helper correctly rejects pre-link commands. The client now exposes
  the physical state for the write but leaves `protocolReady` false until the
  initialization succeeds, and logs `session_initialization` separately.
- Native tests: 30/30 passed, including serialized connection initiation across
  Canon security. Firmware builds succeeded:
  `crowpanel_128` 945,752 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 915,288 / 138,028;
  `canon_ble` 944,810 / 136,668;
  `canon_trigger` 941,928 / 136,300;
  `tascam_x8` 943,826 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`. The board
  booted cleanly, but no post-fix sequence attempt occurred during the bounded
  serial verification window; repeated paired-device validation remains open.

### 2026-08-04: Post-Stop BLE teardown crash fix

- Investigated an operator-reported reset after Start, Stop, then opening scene
  Settings. The prior exception was lost before serial attachment, but code
  inspection found a matching use-after-free in shared BLE teardown: NimBLE
  client deletion is asynchronous, while the backend freed its
  `ClientCallbacks` immediately after requesting disconnect. A later GAP
  disconnect could therefore call through a freed pointer after Stop released
  the links.
- Changed callback ownership to the bounded backend-slot lifetime. Client
  teardown schedules NimBLE deletion but retains the callback until the entire
  BLE backend has deinitialized, so late disconnect events remain safe.
- Added a UI simulator regression that completes Start/Stop and immediately
  opens Settings. It passed and captured
  `27b_scenes_settings_after_stop.png`; LVGL reported 14,008 bytes free, 79%
  used, 17,012-byte peak use, and 1% fragmentation at that point.
- Native tests: 30/30 passed. Firmware builds succeeded:
  `crowpanel_128` 945,728 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 915,264 / 138,028;
  `canon_ble` 944,786 / 136,668;
  `canon_trigger` 941,904 / 136,300;
  `tascam_x8` 943,802 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Repeating the physical Start/Stop/Settings workflow remains the completion
  check for this crash fix.

### 2026-08-04: Final sequence action delivery and prepared-edit links

- Fixed a sequence pipeline bug where Canon Smart and Tascam reported command
  success when a request was only queued in the driver. If that action was the
  last Stop step, `finishStop()` released the link before the next driver loop,
  so the Tascam Stop write was never transmitted. Both clients now perform the
  GATT write during main-loop command dispatch and return success only when the
  write succeeds; notification-confirmed state remains asynchronous.
- Added prepared-scene reconciliation. Editing waits/order keeps all existing
  target links. Removing a target releases only that target; adding one keeps
  unchanged links and prepares the new target before returning to `Ready`.
  Amended ADR-020 so a successful Stop keeps prepared links while the run/edit
  screen remains open; Back or Unlink performs teardown. This allows immediate
  editing or restart without reconnecting unchanged targets.
- Native tests: 30/30 passed, including editing a prepared wait without link
  loss and removing/re-adding a target while preserving the other target.
- UI simulator build and all captures passed, including the post-Stop Settings
  regression. Firmware builds succeeded:
  `crowpanel_128` 946,576 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 916,112 / 138,028;
  `canon_ble` 945,630 / 136,668;
  `canon_trigger` 942,748 / 136,300;
  `tascam_x8` 944,650 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical Tascam Stop delivery and prepared-edit link retention remain the
  final operator checks.

### 2026-08-04: Sequence retry and immediate Relink lifecycle fix

- Captured an operator-reported sequence failure with both devices nearby.
  Canon Smart recovered to protocol-ready in 3.4 seconds, while Tascam's first
  two direct attempts returned HCI `0x3e`; the previous policy then paid about
  seven seconds to rediscover the already-saved peer and reached sequence
  `Ready` at 18.7 seconds. Saved targets now receive a third direct attempt
  before scan fallback. The terminal sequence preparation timeout is 30 seconds
  rather than 20 seconds; successful readiness remains immediate.
- Fixed immediate Unlink -> Relink activation failure. `deleteClient()` retains
  NimBLE's global client slot until an asynchronous disconnect callback, so two
  retiring sequence clients could exhaust capacity before Relink. The backend
  now accepts logical link creation while old clients retire, provisions the
  replacements from `pump()`, delays deinitialization until the client list is
  empty, and ignores final callbacks whose client pointer no longer owns the
  slot.
- Hardware verification on the flashed `crowpanel_128` completed two
  Unlink -> Relink cycles with Canon Smart + Tascam. Both reached true
  protocol-ready: 13.7 seconds and 9.1 seconds. One Tascam session-open attempt
  failed during the second cycle and recovered on its bounded per-link retry;
  no stale callback canceled either reacquisition.
- Native tests: 30/30 passed. UI simulator build and every capture through
  `27b_scenes_settings_after_stop.png` passed; the post-Stop settings capture
  reported 14,008 bytes free and 17,012-byte peak LVGL use.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 947,354 bytes flash / 138,028 bytes RAM;
  `crowpanel_128_roboto` 916,890 / 138,028;
  `canon_ble` 946,396 / 136,668;
  `canon_trigger` 943,514 / 136,300;
  `tascam_x8` 945,416 / 136,508.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  The ten-cycle median/p95 benchmark and physical Start/Stop checks remain
  open; two successful relinks are regression evidence, not tranche completion.

### 2026-08-04: GitHub publication preparation

- Reworked the public README around Ble(e)p's long-term goal: an open,
  community-built controller ecosystem for many devices and controller
  hardware targets. Documented current support, limitations, architecture,
  setup, contribution flow, roadmap, safety, and trademark independence.
- Added contribution, conduct, and security policies; GitHub issue forms, a
  pull-request template, and native-test/firmware-build CI.
- Audited tracked text and Git patches for common credential patterns. No
  embedded API key, password, authorization token, or private key was found.
- Found private publication risk in the tracked packet captures: nearby device
  names and stable suffixes, radio addresses, camera/phone identifiers, local
  capture-interface metadata, and unrelated traffic. Removed every raw pcapng
  from the current tree and added ignore/privacy rules. Extracted protocol
  vectors, confidence notes, and source hashes remain in the documentation.
- Removed the tracked macOS `.DS_Store` and ignored OS/editor/build artifacts.
- Verification: documentation links and GitHub YAML parsed successfully;
  native tests passed 30/30; `crowpanel_128` built with espressif32 7.0.1 at
  947,354 bytes flash / 138,028 bytes RAM and flashed successfully to the
  configured ESP32-C3 panel.
- At that point, publishing blockers included selecting an open-source license
  and scrubbing the capture blobs from existing Git history (or publishing a
  reviewed squashed history). Git author identity/email also needed review for
  intended public attribution.

### 2026-08-04: Apache-2.0 license and project origin

- Selected Apache License 2.0, matching Home Assistant Core, and added the
  standard license text at the repository root. Updated contribution terms and
  marked the license decision complete in the publishing checklist.
- Added the project's origin story to the README: Ble(e)p began as a Hacking
  Modern Life YouTube build for a better iFootage Shark Nano II remote, then
  grew into the broader open controller ecosystem.
- Documentation links and license-file structure validated. Native tests passed
  30/30; `crowpanel_128` built at 947,354 bytes flash / 138,028 bytes RAM and
  flashed successfully to the configured ESP32-C3 panel.
- The remaining publication blocker is Git history: removed capture blobs are
  still present in earlier commits and must be scrubbed or excluded through a
  reviewed squashed public history before pushing.
### 2026-08-04: Action-button long-press Back

- Split the GPIO 1 action button by intent: short press only dispatches the
  active device's primary action; a 700 ms hold navigates Back, cancels, or
  closes the current overlay. Releasing after a handled long press does not
  dispatch a short-press action.
- Removed deep-sleep, long-hold power-off, and button-wake handling. The
  hardware SPDT switch is now the only remote power control.
- Host tests passed 30/30. `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, `canon_ble`, `canon_trigger`, and `tascam_x8` builds
  succeeded with PlatformIO 7.0.1. The flashed default firmware uses 941,552
  bytes flash and 137,924 bytes static RAM; the Roboto build uses 911,128 bytes
  flash and 137,924 bytes static RAM.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical short-action / long-Back button timing remains operator-pending.
- Follow-up: sequence Run now dispatches its enabled Start/Stop control on a
  short press; overlays and non-run scene screens ignore short presses.
- Follow-up: reduced the long-press threshold from 1.6 seconds to 700 ms after
  physical use showed the original Back delay was too long.

### 2026-08-04: Sequence target controls and readiness chips

- Rebased `codex/add-sequence-device-controls` onto local `main` at `f9633b4`
  before implementation.
- The sequence run screen now deduplicates direct Start/Stop targets into
  circular category-icon chips above Start/Stop. Borders breathe cyan during
  connection/protocol initialization, stay green when ready, and turn red when
  disconnected. The compact sequence phase sits above Cancel/Unlink.
- A chip opens the target's full device UI using its sequence-held activation.
  Simulator regression verified Canon control entry/Back while Canon and
  Tascam both remained active. Manual device controls retain the sequence's
  logical phase; stable Start/Stop actions remain gated until every target is
  protocol-ready.
- Native tests passed 30/30. `ui_sim` built and all captures completed,
  including connecting, ready, borrowed Canon control, and disconnected-camera
  sequence states. The post-Stop settings capture had 14,416 bytes free,
  17,012-byte peak LVGL use, and 1% fragmentation.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 943,916 bytes flash / 137,996 bytes RAM;
  `crowpanel_128_roboto` 913,460 / 137,996;
  `canon_ble` 942,716 / 136,636;
  `canon_trigger` 939,882 / 136,268;
  `tascam_x8` 941,732 / 136,476.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`.
  Physical chip touch targets, border animation, camera power-off/recovery, and
  preservation of the second live BLE link remain operator-pending.
- Follow-up: the compact run status now uses semantic colors: blue for
  connection/transitions, green for Ready/Done, red for Recording/Failed/not
  connected, and muted text for Idle. Native tests remained 30/30 and the full
  simulator capture set passed. Updated firmware builds succeeded:
  `crowpanel_128` 944,044 bytes flash / 137,996 bytes RAM;
  `crowpanel_128_roboto` 913,588 / 137,996;
  `canon_ble` 942,844 / 136,636;
  `canon_trigger` 940,010 / 136,268;
  `tascam_x8` 941,860 / 136,476. The updated `crowpanel_128` flashed
  successfully to `/dev/cu.usbserial-211240`.
- Follow-up: red chip borders now mean terminal connection failure only.
  WaitingRetry/WaitingConnect backoff remains breathing blue even when a driver
  temporarily reports `Disconnected`; powered-off or otherwise idle-disconnected
  targets use muted gray. The simulator now captures an explicit 30-second
  connection timeout and recovery. Native tests passed 30/30; every simulator
  capture passed. Updated builds succeeded: `crowpanel_128` 944,166 bytes flash
  / 137,996 bytes RAM; `crowpanel_128_roboto` 913,710 / 137,996; `canon_ble`
  942,974 / 136,636; `canon_trigger` 940,140 / 136,268; `tascam_x8` 941,982 /
  136,476. The corrected default firmware flashed successfully to
  `/dev/cu.usbserial-211240`.
- Follow-up: fixed Delete silently returning while automatic sequence
  preparation was in `Connecting`. Delete now cancels connection-only
  preparation, releases its held links, and removes the sequence; it remains
  disabled during Start/Stop execution and while armed. The simulator includes
  a connecting-delete regression that verifies the scene record and every held
  activation are removed. Native tests passed 30/30 and all simulator captures
  passed. Updated builds succeeded: `crowpanel_128` 944,276 bytes flash /
  137,996 bytes RAM; `crowpanel_128_roboto` 913,820 / 137,996; `canon_ble`
  943,084 / 136,636; `canon_trigger` 940,250 / 136,268; `tascam_x8` 942,092 /
  136,476. The delete fix flashed successfully to
  `/dev/cu.usbserial-211240`.

### 2026-08-04: Persistent BLE connection pool

- Added ADR-022 and replaced screen-scoped teardown with a four-session
  retained pool. Foreground and sequence owners are tracked per instance;
  protocol-ready sessions survive navigation, unfinished attempts stop when
  their final owner leaves, and unexpected drops retain the driver's bounded
  reconnect policy.
- Added safe LRU eviction and explicit Disconnect. Foreground, sequence-owned,
  pending-command, and confirmed-recording sessions cannot be auto-evicted;
  confirmed recording requires a second confirmation before Disconnect.
- Refactored compiled drivers to route lifecycle, commands, runtime state, and
  pairing updates by instance. Canon Trigger and Canon Smart now have three
  fixed client sessions each, allowing same-driver connections without dynamic
  allocation. Sequence prepare reuses retained targets and Done/Back releases
  only sequence ownership.
- Native tests passed 32/32, including ready/unready Back behavior,
  same-driver sessions, safe LRU, recording protection, retained sequence
  cancellation, and reuse without reactivation. `ui_sim` built and completed
  every capture; the management and recording-confirmation layouts fit the
  240x240 panel. Peak LVGL use remained 17,012 bytes with 1% fragmentation at
  the post-Stop checkpoint.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 947,390 bytes flash / 139,244 bytes RAM;
  `crowpanel_128_roboto` 916,934 / 139,244;
  `canon_ble` 945,506 / 137,604;
  `canon_trigger` 941,852 / 136,612;
  `tascam_x8` 943,204 / 136,508. The default profile uses 1,248 bytes more
  static RAM than the preceding documented build because the Canon drivers now
  reserve per-instance client state.
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240` after
  granting direct serial access. Physical retained reopen, off-screen retry,
  same-driver concurrency, safe eviction, recording protection, and multi-link
  heap behavior remain operator-pending.

### 2026-08-04: Experimental Home Assistant entity tranche

- Added ADR-023 and preserved the in-progress multi-instance manager as the
  baseline. `DriverId::HomeAssistant` supports four dynamic entity profiles for
  `light`, `switch`, `input_boolean`, `button`, `scene`, and `script`, with only
  explicit On/Off, Press, and Activate capabilities. Configured-device capacity
  is now 12; active instance capacity is eight and remains distinct from the
  four-link BLE central limit.
- Device persistence is schema v2 and decodes v1 BLE identity records unchanged.
  Canonical HA entity IDs/domains live in tagged device records. Wi-Fi SSID and
  password, local HA URL, and long-lived token use a separate checksummed NVS
  record; Portal config responses omit password/token.
- Implemented temporary-AP Portal setup with a WPA2 password shown on-panel,
  a Wi-Fi-only bootstrap page, then a station-bound LAN Portal for Home
  Assistant setup and bounded incremental `/api/states` summaries,
  four-slot atomic save/rebind, referenced-entity removal protection, explicit
  Exit, and ten-minute inactivity teardown. Portal entry cancels scenes and
  physical sessions; teardown turns Wi-Fi off.
- Added one lazy shared HA REST/WebSocket runtime using ArduinoJson 7.4.3 and
  arduinoWebSockets. It performs bearer auth, individual initial-state reads,
  active-entity `subscribe_trigger`, bounded queue-only frames, reconnect
  backoff, service calls, subscribed confirmation with five-second timeout, and
  REST refresh after malformed/oversized/dropped updates. Four HA instances
  reuse the session and final eviction/unlink tears it down.
- Added round-panel On/Off, Press, and Activate screens with offline, missing,
  unknown, unavailable, pending, and failure states. Scene pickers and validation
  now use dynamic instance profiles, so safe HA actions can mix with Canon and
  Tascam steps.
- Raising configured capacity exposed a deterministic 64 KiB LVGL allocation
  failure while constructing the Shark run screen. The pool is now 96 KiB. The
  full simulator ran once under AddressSanitizer and then in the normal profile;
  all captures completed, including `28_ha_light.png` through
  `28e_ha_error.png`, `29_ha_button.png`, and `30_portal.png`. With 12 records,
  it reported 60,496 bytes free after init, 34,688 bytes free at the post-Stop
  checkpoint, 20,896-byte peak use, and 24% fragmentation there.
- Native tests passed 35/35, including v1-to-v2 migration, separate checksummed
  secrets/corruption, four-entity capacity, dynamic profiles, HA command/service
  mapping, and HA scene capability validation. `ui_sim` built and executed
  successfully.
- Firmware builds succeeded with espressif32 7.0.1:
  `crowpanel_128` 1,644,674 bytes flash / 205,644 bytes RAM (52.3% / 62.8%);
  `crowpanel_128_roboto` 1,614,210 / 205,644 (51.3% / 62.8%); and
  `home_assistant` 1,636,214 / 202,532 (52.0% / 61.8%).
- `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240`. A bounded
  serial read showed normal panel/touch initialization and
  `runtime event=boot ... link=disconnected free_heap=88328 min_free_heap=85888`;
  no Wi-Fi activity was initiated at boot.
- Hardware gate remains open because no local HA URL, token, or Wi-Fi credentials
  were supplied in this session. AP-to-LAN handoff/lifetime, real REST and
  authenticated WebSocket behavior, external state changes, every physical
  action, wrong-token/missing-entity/restart/loss recovery, mixed HA/BLE
  execution, rebind, and ten Portal plus ten runtime heap/socket/task cycles are
  therefore `Blocked` on operator-provided target-server testing. Do not promote
  ADR-023 beyond Experimental until those results are recorded.

### 2026-08-04: Simpler Portal setup password

- Replaced the generated setup password with the fixed WPA2 password
  `12345678`. It remains visible on the Portal screen. A user-configurable AP
  password is deferred; the setup AP exists only while Portal mode is active
  and is replaced by the LAN Portal after Wi-Fi joins.
- Native tests passed 35/35. The UI simulator rebuilt and completed every
  capture; `30_portal.png` confirms the shorter password fits the round panel.
  Firmware builds passed with `crowpanel_128` at 1,644,652 bytes flash /
  205,644 bytes RAM, `crowpanel_128_roboto` at 1,614,188 / 205,644, and
  `home_assistant` at 1,636,192 / 202,532.
- The updated `crowpanel_128` firmware flashed successfully to
  `/dev/cu.usbserial-211240`.

### 2026-08-04: Wi-Fi-first Portal with local-network access

- Split Portal into two listener lifetimes. With no working saved Wi-Fi, the
  SoftAP-bound page collects only SSID/password. After a successful join it
  saves Wi-Fi, returns handoff instructions, destroys the AP listener and AP,
  and starts a new listener bound to the station address.
- The LAN Portal contains Home Assistant URL/token/entity configuration. The
  panel shows the numeric DHCP address and advertises `http://bleep.local`
  through mDNS as a best-effort alias. The LAN listener exists only
  while the panel remains on Portal; Exit, timeout, or Wi-Fi loss tears it down.
  Wi-Fi loss falls back to the Wi-Fi setup AP so credentials can be repaired.
- The simulator now captures both `30_portal.png` Wi-Fi bootstrap and
  `30b_portal_lan.png` local-network states. Target AP-to-LAN handoff, mDNS
  resolution, request reachability, timeout, and heap recovery remain part of
  the open hardware gate.
- Final verification passed: native tests 35/35; `ui_sim` built and completed
  all captures; `crowpanel_128` used 1,675,488 bytes flash / 207,636 bytes RAM
  (53.3% / 63.4%); `crowpanel_128_roboto` used 1,645,032 / 207,636
  (52.3% / 63.4%); and `home_assistant` used 1,667,076 / 204,524
  (53.0% / 62.4%).
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Live AP-to-LAN browser and mDNS behavior still
  requires entering the target studio Wi-Fi on the panel and remains part of
  the open hardware gate.

### 2026-08-04: Wi-Fi discovery, join feedback, and numeric LAN handoff

- Added bounded asynchronous discovery of up to 16 unique visible SSIDs, with
  RSSI/security summaries and retained manual entry for hidden networks.
- Replaced the blocking setup join with a main-loop state machine. The setup
  browser polls connection state while the panel reports scanning, joining,
  connected, missing-network, rejected-password, timeout, and storage failure
  states. A failed attempt leaves the setup AP available for retry.
- Made the assigned DHCP address the authoritative Portal URL shown by the
  browser and panel. `http://bleep.local` remains advertised as a convenience
  alias because client and network mDNS support is not reliable. Successful
  setup allows an eight-second handoff window, shortened after the browser has
  received the numeric address, before destroying the AP and starting the
  station-bound listener.
- Native tests passed 35/35. `ui_sim` built and completed all captures,
  including `30a_portal_connecting.png`, `30aa_portal_wifi_failed.png`, and the
  numeric-address `30b_portal_lan.png`. Firmware builds passed with
  `crowpanel_128` at 1,682,826 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` at 1,652,370 / 207,828
  (52.5% / 63.4%), and `home_assistant` at 1,674,410 / 204,716
  (53.2% / 62.5%).
- The updated `crowpanel_128` firmware flashed successfully to
  `/dev/cu.usbserial-211240`. Real network scan results, connection error
  classification, DHCP handoff, and numeric-address reachability remain target
  hardware checks requiring operator interaction with the studio Wi-Fi.

### 2026-08-04: Home Assistant input-boolean action

- Kept explicit TurnOn/TurnOff capabilities and service calls for safe authored
  scenes, but replaced the two-button entity screen with one context-sensitive
  action. With confirmed OFF state it shows ON and calls `turn_on`; with
  confirmed ON it shows OFF and calls `turn_off`. Unknown state disables it.
- Native tests cover both `input_boolean.turn_on` and `turn_off` payloads plus
  explicit On and Off scene validation. Simulator captures
  `28f_ha_input_boolean.png` and `28g_ha_input_boolean_on.png` verify that the
  single action changes from ON to OFF with confirmed state.
- Native tests passed 35/35 and the full UI simulator completed. Firmware builds
  passed with `crowpanel_128` at 1,683,014 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` at 1,652,558 / 207,828
  (52.5% / 63.4%), and `home_assistant` at 1,674,598 / 204,716
  (53.2% / 62.5%).
- The corrected `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Live HA confirmation of both entity-screen
  directions and explicit sequence On/Off remains part of target testing.

### 2026-08-04: HA command-confirmation reconciliation

- Corrected a false-negative path observed when an `input_boolean` changed in
  Home Assistant but Ble(e)p displayed `ACTION FAILED` or `MISSING`. The
  five-second subscription deadline now keeps the action pending while an
  individual REST state refresh runs. A matching refreshed state completes the
  command successfully; only a confirmed mismatch or refresh failure becomes
  an action failure.
- Restricted `MISSING` to an entity-state HTTP 404 or an actual removed-state
  event. Transport errors, malformed responses, and state events without a
  usable state now produce `UNKNOWN` and schedule reconciliation instead of
  claiming the entity does not exist. Event parsing accepts both the
  trigger-variable shape and a state-event data fallback.
- Native tests passed 35/35. Firmware builds passed with `crowpanel_128` at
  1,683,552 bytes flash / 207,828 bytes RAM (53.5% / 63.4%),
  `crowpanel_128_roboto` at 1,653,096 / 207,828 (52.6% / 63.4%), and
  `home_assistant` at 1,675,144 / 204,716 (53.3% / 62.5%).
- The reconciliation fix flashed successfully to
  `/dev/cu.usbserial-211240`; repeat the previously successful helper action on
  target hardware to close this observed confirmation issue.

### 2026-08-04: Mixed-sequence BLE allocation crash

- Reproduced the reported Sequence 1 reset under a live serial monitor. The
  board logged `BLE_INIT: Malloc failed`, then `assert emi.c 164` and an
  interrupt-watchdog panic. The requested contiguous allocation was `0x7800`.
  This occurred because the authored sequence encountered an HA target first,
  initialized Wi-Fi, and only then lazily initialized the shared BLE central.
- Changed preparation ownership acquisition—not authored action execution—to
  initialize every physical target before any HA target. Rollback now tracks
  the actual acquisition order. A native regression authors the HA action
  first, begins with a retained HA session, verifies that it is evicted, and
  verifies that the physical driver then activates before HA. Pending HA work
  aborts preparation rather than entering the unsafe allocator order. Native
  tests pass 36/36.
- `crowpanel_128` built at 1,683,794 bytes flash / 207,828 bytes RAM
  (53.5% / 63.4%), `crowpanel_128_roboto` built at 1,653,338 / 207,828
  (52.6% / 63.4%), and `home_assistant` built at 1,675,386 / 204,716
  (53.3% / 62.5%). The full UI simulator also completed. The target image
  flashed successfully to `/dev/cu.usbserial-211240`.
  A post-flash monitor remained stable at Home for two minutes, but the exact
  Sequence 1 reopen was not performed during that monitoring window. The
  physical-before-Wi-Fi mitigation therefore remains hardware-unverified and
  ADR-023 stays Experimental.

### 2026-08-04: Mixed-sequence HA connection follow-up

- Investigated the follow-up where the reordered mixed sequence no longer
  crashed but did not connect to Home Assistant. The HA WebSocket disconnect
  callback previously had no handling, so `websocketStarted_` could remain true
  while authentication and subscription could never restart. The callback now
  flips a flag only; `loop()` clears protocol state and schedules a bounded
  reconnect. Secret-free stage logs distinguish Wi-Fi start/timeout,
  WebSocket start/disconnect, authentication, and subscription result and
  include free/largest-allocation heap figures.
- Reduced the LVGL pool from 96 KiB to 80 KiB to return 16 KiB to concurrent
  BLE and Wi-Fi operation. The complete `ui_sim` capture run passed at 80 KiB;
  its most demanding sequence-stop screen retained 18,296 bytes free with a
  20,897-byte peak allocation and 1% fragmentation.
- Native tests passed 36/36. Builds passed with `crowpanel_128` at 1,684,552
  bytes flash / 191,444 bytes RAM (53.6% / 58.4%),
  `crowpanel_128_roboto` at 1,654,096 / 191,444 (52.6% / 58.4%), and
  `home_assistant` at 1,676,132 / 188,332 (53.3% / 57.5%). The corrected
  `crowpanel_128` image flashed successfully to `/dev/cu.usbserial-211240`.
  Boot reported about 102 KiB free heap and the panel remained stable at Home
  for 150 seconds. No sequence activation occurred during the live monitor
  window, so the exact mixed HA connection remains an open hardware check and
  ADR-023 stays Experimental.

### 2026-08-04: HA initial-state readiness recovery

- Found another permanent-wait path during the mixed-sequence audit. After
  WebSocket authentication, a transient failure of the one-time individual
  REST state request left the entity `present == false` forever. The
  subscription could be healthy, but sequence readiness could never complete.
- Initial and reconciliation REST transport errors, non-404 HTTP errors, and
  malformed state JSON now schedule a per-entity retry after two seconds. Only
  one eligible entity is refreshed per main-loop pass, refreshes require an
  authenticated WebSocket, successful state updates clear the retry, and HTTP
  404 remains the terminal missing-entity result. Added a secret-free
  `rest_retry` stage log with HTTP and heap diagnostics.
- Native tests passed 36/36; `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, and `home_assistant` built successfully. The main
  image uses 1,684,760 bytes flash / 191,460 bytes RAM (53.6% / 58.4%) and
  flashed successfully to `/dev/cu.usbserial-211240`. Live mixed-sequence
  confirmation remains open pending an operator run on this image.

### 2026-08-04: Mixed HA/Canon/Tascam hardware success and SRAM bound

- Captured the remaining failure with the operator reproducing while the UART
  monitor stayed attached. With the 40-row display buffers, ESP-IDF reported
  `Expected to init 4 rx buffer, actual is 2`, `esp_wifi_init 257`, and repeated
  STA-enable failures. The board had about 25 KiB free heap and reached an
  8,376-byte minimum: the blocker was Wi-Fi RX-buffer allocation after two BLE
  sessions, not HA authentication or entity state.
- Reduced each of the two DMA display strips from 40 rows to 20, returning
  19,200 bytes without removing double buffering. The next live run connected
  Wi-Fi, received `auth_required`/`auth_ok`, established the selected-entity
  subscription, brought Tascam and Canon to protocol-ready, and reported
  `all_targets_ready` in 8,569 ms. The operator confirmed the sequence worked.
- That successful run still reached only 1,248 bytes minimum free heap, so the
  final bounds reduce each HA WebSocket frame slot from 4 KiB to 2 KiB and the
  LVGL pool from 80 KiB to 76 KiB. Oversized state events already fall back to
  individual REST. A 72 KiB LVGL attempt stalled the simulator and was
  rejected; 76 KiB completed every capture with 14,192 bytes free on the most
  demanding sequence-stop screen.
- Final verification: native tests passed 36/36; full `ui_sim` passed;
  `crowpanel_128` built at 1,680,660 bytes flash / 164,068 bytes RAM
  (53.4% / 50.1%); `crowpanel_128_roboto` built at 1,650,204 / 164,068
  (52.5% / 50.1%); and `home_assistant` built at 1,672,256 / 160,956
  (53.2% / 49.1%). The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. One mixed run passes; the required ten-cycle
  teardown and heap-recovery gate remains open, so ADR-023 stays Experimental.

### 2026-08-04: Canon Smart automatic wake on retained-session acquire

- Added a driver resume hook for already-active retained instances. Acquiring a
  Canon Smart session in `PoweredOff` now starts its existing reconnect path,
  which sends captured wake mode `03` after BLE setup. This applies when the
  saved device is reopened or prepared as a sequence target; other retained
  drivers remain unchanged.
- The simulator now powers Canon off, closes its screen, reopens the retained
  instance, and requires it to return to Ready without an explicit Power
  command. The complete simulator capture run passed. Native tests passed
  36/36, including successful and rejected retained-session resume ownership.
- All firmware profiles built successfully: `crowpanel_128` used 1,680,742
  bytes flash / 164,068 bytes RAM; `crowpanel_128_roboto` 1,650,286 / 164,068;
  `canon_ble` 1,678,876 / 162,444; `canon_trigger` 1,675,426 / 161,436;
  `tascam_x8` 1,676,848 / 161,348; and `home_assistant` 1,672,272 / 160,956.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Physical power-off, reopen, reconnect, and wake
  remain operator-pending and are not marked hardware-verified.

### 2026-08-04: Sequence recovery after partial Start failure

- Fixed authored Stop aborting when it reached a Canon Smart or Tascam target
  that already confirmed `Stopped`. Both clients now treat that Stop as a
  successful idempotent no-op and clear a stale command-failure indicator.
  Unknown or recording targets still receive the real Stop command and retain
  device-originated confirmation requirements.
- Added an end-to-end native regression that fails Tascam's first Record Start
  after Canon starts, verifies the sequence enters `Failed`, runs both authored
  Stop steps to `Completed`, and successfully starts the same sequence again.
  Native tests passed 37/37; the full `ui_sim` build and capture run passed.
- All firmware profiles built successfully: `crowpanel_128` used 1,680,778
  bytes flash / 164,068 bytes RAM; `crowpanel_128_roboto` 1,650,322 / 164,068;
  `canon_ble` 1,678,912 / 162,444; `canon_trigger` 1,675,462 / 161,436;
  `tascam_x8` 1,676,884 / 161,348; and `home_assistant` 1,672,308 / 160,956.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. The exact partial-failure Stop/restart workflow
  remains operator-pending on live Canon and Tascam hardware.

### 2026-08-04: Sequence long-press Back correction

- Corrected the sequence run-screen long press, which incorrectly invoked the
  enabled Start or Stop action. It now mirrors the visible Back button: Run
  returns to the sequence list, cancels the held run state, and releases
  sequence ownership. Settings still closes first, Edit returns to Run, and a
  long press from the sequence list returns Home.
- Added a simulator regression that closes sequence Settings, long-presses Back
  from Run, verifies the list is visible, and verifies Canon/Tascam sequence
  ownership was released. The complete `ui_sim` build and capture run passed;
  native tests passed 37/37.
- All firmware profiles built successfully: `crowpanel_128` used 1,680,616
  bytes flash / 164,068 bytes RAM; `crowpanel_128_roboto` 1,650,160 / 164,068;
  `canon_ble` 1,678,750 / 162,444; `canon_trigger` 1,675,300 / 161,436;
  `tascam_x8` 1,676,722 / 161,348; and `home_assistant` 1,672,146 / 160,956.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Physical 700 ms sequence Back behavior remains
  operator-pending.

### 2026-08-04: Home count footer removal

- Removed the Home footer that displayed device and sequence counts. Its
  middle-dot separator was unavailable in the embedded font and rendered as a
  square, while the count text extended into the round panel's cropped edge.
  Home now contains only the title and four mode tiles.
- The complete `ui_sim` capture run passed, and visual inspection of
  `01_home.png` confirmed the footer is absent with clear circular margins.
  Native tests passed 37/37.
- All firmware profiles built successfully: `crowpanel_128` used 1,680,404
  bytes flash / 164,068 bytes RAM; `crowpanel_128_roboto` 1,649,948 / 164,068;
  `canon_ble` 1,678,542 / 162,444; `canon_trigger` 1,675,092 / 161,436;
  `tascam_x8` 1,676,514 / 161,348; and `home_assistant` 1,671,938 / 160,956.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`. Physical Home-screen inspection remains
  operator-pending.

### 2026-08-05: Experimental native Amaran light tranche and pairing follow-up

- Added one discoverable generic `Aputure Light` driver backed by one shared
  runtime and one shared Mesh Proxy link. Hidden Pano 120c/Ace 25c IDs remain
  only so records created by the initial implementation can still load.
- Fixed first-use discovery: Mesh Provisioning and Mesh Proxy are advertised as
  16-bit service UUIDs (`0x1827`/`0x1828`), but discovery had compared them as
  full 128-bit GATT UUIDs and therefore never selected an advertisement.
- Extracted one round Bluetooth pairing screen for Shark and Amaran with a
  spinner, explicit scanning/connecting/error copy, Back, and Retry/Re-pair.
  Amaran now provisions the first nearby factory-reset Mesh Provisioning
  advertiser instead of asking the operator to identify a model.
- Split the live light draft into independent CCT and RGB look memories, added
  explicit RGB saturation, and made switching modes recall and apply the saved
  look without overwriting the inactive mode. Sequence authoring now presents
  one `Set color` action with CCT/RGB tabs; RGB saturation remains encoded in
  the existing packed RGB scene argument.
- Ported PB-GATT P-256/no-OOB provisioning, AES-CMAC mesh derivations, AES-CCM,
  privacy obfuscation, proxy framing, Telink opcode `0x26` power/CCT/RGB
  payloads, and segmented device-key configuration traffic from the working
  `studio-lighter` reference. Reference Python suite: 88 passed, 2 skipped.
- Added a separate checksummed mesh store, pending-configuration persistence,
  per-node metadata, and durable 256-number sequence reservations. Scene schema
  v2 carries three signed action arguments and loads v1 arguments as zero.
- Added parameterized CCT/RGB sequence authoring and a lazy CCT/tint/brightness
  plus RGB color-wheel screen with 350 ms coalescing and optimistic state.
- Native tests: 41/41 passed in `native`, including exact AES/CMAC/network and
  opcode vectors, segmentation, bounds, v1 scene migration, store corruption,
  and sequence restart reservation.
- UI simulator: `ui_sim` built and the complete capture program passed,
  including shared Shark pairing, `20d_amaran_pairing.png`,
  `20e_amaran_cct_optimistic.png`, `20f_amaran_rgb.png`, and the unified CCT/RGB
  sequence editors in `22e_scenes_set_color_cct.png` and
  `22f_scenes_set_color_rgb.png`. With 12
  seeded devices, 128 KiB simulator LVGL memory started with 86,592 bytes free;
  the measured sequence-settings high-water point retained 60,784 bytes free.
- A 76 KiB LVGL pool exhausted while constructing an existing Shark modal at
  the new maximum-device count. The target pool is now 96 KiB. Main firmware
  build: 188,556 / 327,680 bytes RAM (57.5%), 1,717,170 / 3,145,728 bytes flash
  (54.6%). Roboto: same RAM and 1,686,714 bytes flash. Amaran-only: 180,220
  bytes RAM (55.0%) and 1,673,138 bytes flash (53.2%). Native 41/41, `ui_sim`,
  `crowpanel_128`, `crowpanel_128_roboto`, and `aputure_light` all passed.
- Flashed `crowpanel_128` successfully to `/dev/cu.usbserial-211240`; image hash
  verification passed and the panel hard-reset.
- Hardware gate remains open: no target light was provisioned in this session.
  Configuration status-response decoding and reset-advertisement verification
  are not yet implemented, so support stays Experimental and local removal is
  not evidence that a fixture left the mesh.

### 2026-08-05: Mixed BLE/Home Assistant SRAM recovery

- Reproduced the Amaran-era mixed-sequence regression under UART. Starting
  Canon Smart, Tascam X8, and Home Assistant logged `BLE_INIT: Malloc failed`,
  timed Wi-Fi out, and reached only 2,972 bytes minimum free heap. The failure
  was SRAM exhaustion, not the four-link BLE slot limit.
- Restored the 76 KiB LVGL pool and changed the simulator to use that exact
  target allocation. Device rows now exist only while Devices is visible, and
  Shark creates only its active Keys or Run screen and destroys its inactive
  pairing/main view. The maximum twelve-device simulator completed every
  capture at 76 KiB, including all Shark overlays and sequence/device-control
  paths; sequence Stop settings retained 35,584 bytes free.
- Reduced the two DMA display strips from 20 to 15 rows, preserving double
  buffering and dividing the 240-line panel into sixteen equal flushes. The
  combined static SRAM recovery is 25,208 bytes: `crowpanel_128` fell from
  188,484 to 163,276 bytes RAM. Its flash use is 1,717,284 bytes.
- Native tests passed 41/41. `ui_sim`, `crowpanel_128`,
  `crowpanel_128_roboto`, `canon_ble`, `canon_trigger`, `tascam_x8`,
  `home_assistant`, and `aputure_light` all built. Their RAM results were
  163,276, 163,276, 161,628, 160,636, 160,548, 160,156, and 154,940 bytes,
  respectively.
- Flashed `crowpanel_128` successfully to `/dev/cu.usbserial-211240`; image
  hash verification passed. The optimized image booted at 130,372 bytes free
  heap with a 127,896-byte initial minimum, versus about 105 KiB free on the
  failing image. No sequence was opened during the post-flash monitor window,
  so the exact Canon + Tascam + HA readiness/command run and physical review of
  the 15-row display flush remain operator-pending hardware checks.

### 2026-08-06: Battery-conscious radio, reconnect, Wi-Fi, and display policy

- Added ADR-025 after three user-reported CrowPanels lost battery operation
  while still operating from USB. One failed board was reported at about 4.0 V
  on the battery side of D1 and 2.4 V on the board side. This remains a
  hardware-fault hypothesis, not proof that firmware or BLE damaged D1.
- Set NimBLE transmit power to 0 dBm, reduced the active scan window from
  80/100 to 20/100, and bounded shared discovery to four-second bursts with
  1.5-second pauses. Protocol-ready links now request 30-50 ms connection
  intervals rather than 15-30 ms.
- Preserved healthy retained links across navigation and Canon Smart's
  intentional `PoweredOff` wake path. An ownerless retained session that drops
  unexpectedly is now deactivated instead of continuing background reconnect.
  Foreground and sequence-owned links, pending commands, confirmed recording,
  and required BLE/Home Assistant concurrency remain protected.
- Enabled Wi-Fi station modem sleep for the retained Home Assistant client.
  The screen and backlight remain continuously on by operator choice; the
  PI4IOE5V6408 output has no hardware brightness level. No dimming, input wake
  suppression, deep sleep, or software power-off is added.
- Native tests passed 43/43, including bounded scan scheduling and ownerless
  drop parking while preserving intentional offline retention. All firmware
  profiles built: `crowpanel_128` used 1,718,036 bytes flash / 163,276 bytes
  RAM; `crowpanel_128_roboto` 1,687,572 / 163,276; `canon_ble` 1,716,214 /
  161,628; `canon_trigger` 1,712,738 / 160,636; `tascam_x8` 1,714,152 /
  160,548; `home_assistant` 1,709,620 / 160,156; and `aputure_light` 1,673,734 /
  154,940.
- Flashed `crowpanel_128` successfully to `/dev/cu.usbserial-211240`; image
  hash verification passed and the panel hard-reset. Physical scan latency,
  peripheral command/notification reliability, mixed BLE/HA operation, and a
  protected seven-day battery endurance run remain operator-pending. Firmware
  reduces load but is not electrical protection for D1, inrush, or USB reverse
  current.

### 2026-08-06: Sequence target status-ring layering

- Moved each sequence target's animated status ring from the icon button's
  parent border to a transparent foreground overlay. LVGL draws a parent's
  border before its children, so the square Home Assistant/category artwork
  could cover the ring corners while transparent Canon and Tascam artwork hid
  the same layering error.
- Added a simulator scene containing Canon Smart, Tascam, and Home Assistant
  targets. The complete `ui_sim` capture run passed, and
  `20g_sequence_ha_switch_status_ring.png` shows the connecting ring
  continuously above all three icons.
- `crowpanel_128` built with 163,308 / 327,680 bytes RAM (49.8%) and 1,718,124 /
  3,145,728 bytes flash (54.6%). It flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Opening a real mixed-device sequence for physical display inspection remains
  operator-pending.

### 2026-08-06: Home Assistant switch category icon

- Added `icon_cat_switches.png`, a cyan/yellow toggle-switch illustration, and
  regenerated the 48x48 LVGL icon arrays. `DeviceType::Switch` now uses the
  dedicated icon in shared category pickers and sequence target chips;
  `DeviceType::Action` retains the generic Devices artwork.
- The source asset was generated with the built-in image tool using the
  repository's documented playful glossy icon recipe, then normalized to
  1024x1024 before `tools/gen_icons.py --size 48` embedded it.
- The complete `ui_sim` run passed. Its mixed Canon/Tascam/HA regression now
  targets an HA `input_boolean`; `20g_sequence_ha_switch_status_ring.png`
  confirms the toggle remains readable at chip size with an uninterrupted
  foreground status ring.
- `crowpanel_128` built with 163,308 / 327,680 bytes RAM (49.8%) and 1,725,076 /
  3,145,728 bytes flash (54.8%). It flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Physical inspection of the configured `HML Shooting` sequence remains
  operator-pending.

### 2026-08-05: Sequence step settings editor

- Made every existing Start/Stop step label an edit control. Wait steps now
  open a prefilled millisecond spinbox, ordinary actions reopen the selected
  target's capability-safe action picker, and CCT/RGB actions reopen with their
  persisted parameters. Saving replaces the selected row; `+ Step` still adds
  a new row and now asks for a wait duration instead of inserting a fixed value.
- Added simulator regressions that change the seeded 500 ms wait to 750 ms and
  edit a CCT step to 4300 K, 72% brightness, and +120 tint while asserting the
  step count does not change. The complete simulator capture run passed, and
  `22a_scenes_edit_wait.png` was visually checked on the 240x240 round layout.
- Native tests passed 41/41. `ui_sim` and all seven firmware profiles
  (`crowpanel_128`, `crowpanel_128_roboto`, `canon_ble`, `canon_trigger`,
  `tascam_x8`, `home_assistant`, and `aputure_light`) built successfully.
  `crowpanel_128` uses 1,724,534 bytes flash / 188,596 bytes RAM (54.8% /
  57.6%); Roboto uses 1,694,070 / 188,596 (53.9% / 57.6%).
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240` with image hash verification. Physical touch
  editing of wait and color values remains operator-pending.

### 2026-08-06: Sequence/device list-space and step-row follow-up

- Increased sequence steps from 34 px single-line rows to 58 px bordered cards.
  The full-width editable action label sits above Up/Down controls grouped on
  the left and Delete anchored on the right, so action names are no longer
  covered by those buttons.
- Moved `Add step` into the step list as its final scrollable row and moved
  `Add device` into the Devices list after every configured device. Both lists
  now use the vertical area previously reserved for fixed bottom buttons. The
  add buttons remain horizontally centered with a small gap above them.
- Back from an existing step's wait/action/color editor now closes the picker
  directly to the Start/Stop step list. The category/device drill-down remains
  available only while adding a new step.
- Simulator regressions assert both Add controls are centered in their list's
  final row and that Back closes existing-step settings without leaving the
  step editor.
  The complete `ui_sim` capture run passed; `22_scenes_edit_start.png` and
  `23_scenes_edit_stop.png` were visually checked on the 240x240 layout.
- Native tests passed 43/43. `ui_sim` and all seven firmware profiles built
  successfully. `crowpanel_128` uses 1,732,622 bytes flash / 163,348 bytes RAM
  (55.1% / 49.8%). Physical touch scrolling and Back remain operator-pending.

### 2026-08-06: Portal QR join and phone sign-on discovery

- Added a 96 px on-panel QR code. During temporary-AP setup it contains the
  standard WPA2 `WIFI:` payload for the generated `Bleep-Setup-…` SSID and the
  fixed setup password; after LAN handoff it changes to the authoritative
  numeric Portal URL. The SSID, password or same-Wi-Fi hint, numeric address,
  and Exit action remain visible on the round display.
- Added AP-scoped wildcard DNS and unknown-path redirects so iOS, Android, and
  other clients can detect the temporary network as captive and offer the Wi-Fi
  setup page in their sign-on UI. DNS is serviced from the main loop and is
  destroyed with the AP before the LAN listener starts.
- `ui_sim` built and completed every capture. Payload assertions passed for the
  setup Wi-Fi QR and LAN URL QR; `30_portal.png` and `30b_portal_lan.png` were
  visually checked. The simulator finished with 11,424 bytes free and 1%
  fragmentation after its full regression run. Native tests passed 43/43.
- All seven firmware profiles built successfully. `crowpanel_128` uses
  1,739,896 bytes flash / 163,876 bytes RAM (55.3% / 50.0%); Roboto uses
  1,709,432 / 163,876; `canon_ble` 1,738,062 / 162,228; `canon_trigger`
  1,734,568 / 161,236; `tascam_x8` 1,736,000 / 161,140; `home_assistant`
  1,731,442 / 160,756; and `aputure_light` 1,695,532 / 155,548.
  `crowpanel_128` flashed successfully to `/dev/cu.usbserial-211240` with image
  hash verification and a hard reset. Scanning the displayed QR and confirming
  the phone's automatic sign-on sheet remain operator-pending hardware checks.

### 2026-08-06: Transactional Add-device pairing

- Added ADR-026 and changed **Add device** so choosing a driver immediately
  opens its pairing UI. The candidate record remains outside registry
  enumeration and the checked NVS blob until pairing identity exists, the
  driver is protocol-ready, and persistence succeeds. Back or hardware
  long-press cancels the candidate and runs driver-specific bond/peer/mesh
  cleanup; pairing and save errors remain retryable.
- Added native coverage for identity-before-readiness, successful commit,
  cancellation and restart, simultaneous-draft rejection, persistence-write
  failure, and retry. Native passed 45/45.
- The target-sized `ui_sim` build and complete capture program passed. Its new
  Canon Trigger regression verifies picker-to-pairing navigation, unchanged
  registry count during pairing, long-press cancellation, and protocol-ready
  commit into controls. `03a_add_device_pairing.png` and
  `03b_add_device_ready.png` were visually checked on the 240x240 round layout;
  the full run ended with 11,424 bytes free and 1% fragmentation.
- All seven firmware profiles built successfully. Flash/RAM bytes were:
  `crowpanel_128` 1,742,260 / 164,052; `crowpanel_128_roboto` 1,711,796 /
  164,052; `canon_ble` 1,740,030 / 162,420; `canon_trigger` 1,736,462 /
  161,412; `tascam_x8` 1,737,848 / 161,332; `home_assistant` 1,733,092 /
  160,932; and `aputure_light` 1,697,174 / 155,740.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Physical add/pair/success, pairing-error Retry, Back cleanup, and reboot
  during pairing remain operator-pending hardware checks.

### 2026-08-06: Tascam AK-BT1 service-based discovery fix

- Reproduced Add-device Tascam onboarding on the flashed panel. UART showed the
  Tascam client remaining in `scanning`; a simultaneous CoreBluetooth scan
  found the nearby adapter as `ANNA-B1-BC5A07` with primary service
  `2456e1b9-26e2-8f83-e744-f34f01e9d701`. The name-only matcher therefore
  ignored the real AK-BT1 advertisement.
- Changed Tascam discovery to accept that exact custom service UUID as the
  authoritative identity while retaining `Portacapture X8` as a compatibility
  name fallback. Added a native advertisement-vector regression; native passed
  45/45.
- `crowpanel_128` built at 1,742,276 bytes flash / 164,052 bytes RAM and
  `tascam_x8` at 1,737,864 / 161,332. The main image flashed successfully to
  `/dev/cu.usbserial-211240` with hash verification and a hard reset.
- Live post-flash onboarding discovered the adapter, queued a connection, and
  reached physical connection, successful GATT setup, session initialization,
  and `protocol_ready` in 4,304 ms. Three preceding controller connection
  attempts failed and recovered automatically. This verifies real Tascam
  Add-device pairing and transactional commit; pairing-error Retry, Back
  cleanup, and reboot-during-pairing checks remain open.

### 2026-08-06: Responsive Portal device and sequence administration

- Added ADR-027 and expanded the station-bound Portal into responsive Overview,
  Devices, Sequences, and Home Assistant views. Existing physical devices can
  be renamed, enabled/disabled, and removed; physical pairing and all runtime
  controls remain panel-only. Referenced-device removal is rejected with the
  referencing sequences identified, and dormant records remain visible.
- Added create, rename, enable/disable, duplicate, delete, and full in-place
  Start/Stop editing for the current scene model, including action/wait steps
  and reordering. Bounded stable command IDs cross the HTTP boundary, scene
  revisions reject stale updates, and normal scene validation remains
  authoritative.
- Made device and scene mutations transactional across checked NVS writes.
  Added a per-entry mutation nonce, no-store/frame-denial headers, request-size
  limits, and a safe deferred **Finish & Exit** route. Portal entry still
  cancels scenes and releases device links before Wi-Fi starts.
- Native tests passed 46/46, including persistence-failure rollback for device
  update/removal and scene duplicate/rename/enable/removal. The embedded
  JavaScript passed a Node syntax check. Mocked desktop and 390 px mobile Portal
  flows were visually inspected with no browser console errors or horizontal
  overflow; real phone/desktop interaction against the board remains pending.
- Native and UI simulator verification passed; the complete simulator capture
  ended with 11,424 bytes free and 1% fragmentation. All seven firmware profiles
  built successfully. Flash/RAM bytes were: `crowpanel_128` 1,781,886 / 164,084;
  `crowpanel_128_roboto` 1,751,414 / 164,084; `canon_ble` 1,779,660 / 162,436;
  `canon_trigger` 1,776,108 / 161,444; `tascam_x8` 1,777,506 / 161,348;
  `home_assistant` 1,772,738 / 160,964; and `aputure_light` 1,738,368 / 155,756.
  The final main image flashed to `/dev/cu.usbserial-211240`; hashes verified and
  the panel hard-reset. Live phone/desktop CRUD, reboot persistence, and ten
  Portal lifecycle heap/socket/task cycles remain operator-pending hardware
  gates.
- Replaced the ambiguous Overview metric `OFF / Runtime links in Portal` with
  `PAUSED / Device connections`. Replaced the text-only sidebar wordmark with a
  responsive embedded derivative of `assets/bleep_logo.png`; the Portal serves
  the 320 px WebP locally from program flash. Desktop and 390 px mobile browser
  checks confirmed the logo loaded at 178 px and 132 px respectively, with no
  page overflow.
- All seven firmware profiles rebuilt successfully after the branding change.
  Flash/RAM bytes were: `crowpanel_128` 1,792,140 / 164,124;
  `crowpanel_128_roboto` 1,761,668 / 164,124; `canon_ble` 1,789,916 / 162,484;
  `canon_trigger` 1,786,350 / 161,492; `tascam_x8` 1,787,762 / 161,388;
  `home_assistant` 1,782,996 / 161,012; and `aputure_light` 1,748,618 / 155,796.
  The final main image flashed to `/dev/cu.usbserial-211240`; hashes verified and
  the panel hard-reset.

### 2026-08-06: CrowPanel haptic feedback

- Added Phase 7 haptic feedback through the onboard vibration motor on
  PI4IOE5V6408 expander output P0. Accepted LVGL touch clicks and recognized
  short-button actions pulse for 25 ms; recognized long presses pulse for 50 ms.
  Scrolling and canceled touches do not trigger feedback.
- Motor timing is a rollover-safe main-loop deadline. Feedback never delays
  LVGL, scene, BLE, or Wi-Fi work, and a repeated request extends an active
  pulse. Expander initialization writes the output latch low before enabling
  the motor output so boot does not intentionally buzz.
- Native tests passed 46/46. `ui_sim` built and completed its full capture run,
  ending with 11,424 bytes free and 1% fragmentation. All seven firmware
  profiles built successfully. Flash/RAM bytes were: `crowpanel_128` 1,782,094
  / 164,092; `crowpanel_128_roboto` 1,751,622 / 164,092; `canon_ble` 1,779,868
  / 162,444; `canon_trigger` 1,776,316 / 161,452; `tascam_x8` 1,777,714 /
  161,356; `home_assistant` 1,772,946 / 160,972; and `aputure_light` 1,738,576 /
  155,764.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Physical confirmation of pulse feel and the chosen 25/50 ms strengths remains
  operator-pending.

### 2026-08-06: Semantic Press, Back, and Error haptics

- Replaced the single-duration motor pulse with a shared non-blocking pattern
  sequencer. Press is one 20 ms tap; Back is 15 ms on, 35 ms off, then 30 ms
  on; Error is 60 ms on, 45 ms off, then 60 ms on. Back replaces the generic
  Press generated by the same click, and Error takes priority over both.
- Routed touch and hardware-button Back paths through the Back pattern. Error
  feedback fires once when a foreground command, pending-add save, sequence, or
  terminal Portal state newly fails; an unchanged error does not keep buzzing.
- Added simulator pattern assertions for exact Press/Back/Error output changes,
  Back overriding Press, and Error suppressing weaker feedback. Native tests
  passed 46/46; `ui_sim` built and completed its full capture run with 11,424
  bytes free and 1% fragmentation.
- All seven firmware profiles built successfully. Flash/RAM bytes were:
  `crowpanel_128` 1,782,786 / 164,108; `crowpanel_128_roboto` 1,752,322 /
  164,108; `canon_ble` 1,780,566 / 162,468; `canon_trigger` 1,776,996 /
  161,476; `tascam_x8` 1,778,404 / 161,372; `home_assistant` 1,773,646 /
  160,996; and `aputure_light` 1,739,268 / 155,780.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Physical differentiation and strength checks for all three patterns remain
  operator-pending.

### 2026-08-06: Connection-ready haptic

- Added a Connected pattern: two quick 12 ms ticks separated by 24 ms. It fires
  when the foreground device becomes link-connected and protocol-ready, when an
  already-ready retained device is opened, and when sequence preparation enters
  `ScenePhase::Ready`. A readiness transition during Back or Error is queued
  until the stronger pattern completes.
- Extended simulator coverage to assert the exact Connected output sequence,
  delayed delivery after Error, a device becoming ready in place, reopening a
  retained ready device, and a sequence reaching Ready.
- Native tests passed 46/46. `ui_sim` built and completed its full capture run
  with 11,424 bytes free and 1% fragmentation.
- All seven firmware profiles built successfully. Flash/RAM bytes were:
  `crowpanel_128` 1,783,140 / 164,124; `crowpanel_128_roboto` 1,752,668 /
  164,124; `canon_ble` 1,780,916 / 162,484; `canon_trigger` 1,777,350 /
  161,492; `tascam_x8` 1,778,762 / 161,388; `home_assistant` 1,773,996 /
  161,012; and `aputure_light` 1,739,618 / 155,796.
- The final `crowpanel_128` image flashed successfully to
  `/dev/cu.usbserial-211240`; image hashes verified and the panel hard-reset.
  Physical differentiation of the Connected pattern remains operator-pending.

### 2026-08-07: CrowPanel watch-crown CAD prototype

- Used Elecrow's official `E5A5-2.35S10-12B15-F200` encoder drawing and board
  STEP as the mechanical references for a removable watch-crown prototype.
  The encoder drawing specifies a nominal 0.8 mm square drive socket and
  0.15 mm push-switch travel.
- Generated a Fusion 360-compatible STEP plus an STL and parametric CadQuery
  source under `output/cad/`. The crown is 11.5 mm diameter by 3.0 mm thick,
  with 40 straight rim flutes, a 0.75 mm clearance hub, and a relieved 0.76 mm
  square drive with a tapered lead-in.
- Re-imported the exported STEP and verified one valid solid with an
  11.495 x 11.495 x 5.350 mm envelope and 294.949 mm3 volume. The integrated
  sub-millimeter shaft requires a fine resin or machining process; physical
  encoder fit, neighboring-component clearance, rotation, and preserved button
  travel remain unverified. No firmware build or flash was applicable to this
  CAD-only artifact.

### 2026-08-07: Zhiyun MOLUS X100 Android HCI protocol research

- Generated and downloaded a Pixel Android bugreport through ADB after a ZY
  Vega add/control session. The bugreport and both HCI logs remain under a
  private temporary path and were not added to the repository.
- Isolated the relevant rotated snoop log (SHA-256
  `2d677dac86f0896026add3ba1d41910ca663819291f50dd30491379caa6bab1f`)
  and documented only sanitized transport facts and golden vectors in
  `docs/protocols/zhiyun-x100.md`.
- Confirmed the `pl105` product marker, no-OOB PB-GATT provisioning on `0x1827`,
  post-provision Mesh Proxy on `0x1828`, and a separate cleartext `0xFEE9`
  write/notify channel. Vendor frames use `24 3c`, a little-endian body length
  and sequence, and CRC-16/XMODEM over the body with little-endian CRC storage.
- Decoded captured power (`0x1008`), float32 brightness (`0x1001`), and uint16
  CCT (`0x1002`) read/write shapes. Setters had no per-write response; physical
  output and read-after-write confirmation remain hardware-unverified.
- No source, build configuration, persistent schema, or firmware behavior was
  changed. No build or flash was run for this documentation-only research
  session. A bounded already-provisioned direct-GATT driver is the next safe
  implementation candidate; factory-reset provisioning needs a separate
  architecture decision for mesh ownership and recovery.

### 2026-08-07: Zhiyun MOLUS X100 live provisioning and control

- Found the freshly reset `PL105_4BF3` advertising Mesh Provisioning `0x1827`
  near the laptop. Its live Provisioning Capabilities matched the capture:
  one element, P-256, static OOB available, and no input/output OOB.
- Corrected the research provisioner to treat advertised static-OOB capability
  as optional when Provisioning Start explicitly selects no-OOB. Provisioned
  the light into a fresh private mesh at unicast address 2; it disconnected and
  reappeared advertising Mesh Proxy `0x1828`. AppKey/model configuration was
  intentionally skipped because direct control uses the separate `0xFEE9`
  service. Mesh recovery material was retained outside the repository with
  owner-only permissions.
- Live GATT discovery confirmed both Mesh Proxy `0x1828` and proprietary
  `0xFEE9` with the captured write/notify characteristics. Direct state queries
  timed out until the captured `0x2003`, `0x8001`, `0x2001`, and `0x0006`
  initialization sequence was reproduced starting at sequence 2.
- The fixture identified as `pl105` with firmware `1.8.4`. Device-originated
  reads reported on / 13.0% / 5600 K. The laptop wrote on / 10.0% / 3200 K,
  read all three values back exactly, then restored and read back the original
  state. This confirms a non-optimistic command path based on correlated
  read-after-write; independently observed optical output is still an operator
  check.
- No firmware source or build configuration changed, so no PlatformIO build or
  panel flash was applicable. The next safe implementation task is the bounded
  already-provisioned direct-GATT driver; production provisioning still needs
  accepted ownership, durable secret storage, reset, retry, and rollback
  semantics.

### 2026-08-07: Zhiyun MOLUS X100 confirmed direct-control firmware

- Added the experimental `zhiyun.molus_x100` driver, a dedicated
  `zhiyun_x100` build profile, and panel/simulator UI. Add device matches an
  already-provisioned `pl105` advertiser on Mesh Proxy `0x1828`, opens the
  separate `0xFEE9` direct-control service, reproduces the captured
  initialization sequence, validates the product identity, and reads power,
  brightness, and CCT before reporting Ready or committing the record.
- Power and CCT/brightness commands are non-optimistic: the GATT setter remains
  pending, then correlated device reads must equal the requested values before
  the driver publishes them or completes a scene command. The initial tranche
  exposes 2700-6500 K, 0-100%, and no tint/RGB capability. CRC, captured golden
  vectors, fragmented notifications, identity, range, and advertisement
  matching have native coverage.
- Recorded ADR-028 and the Phase 4b completion gate. Still missing before
  production are panel-owned PB-GATT provisioning and a versioned secure mesh
  identity/store, unicast allocation, interrupted-provision recovery and
  rollback, verified factory reset, rotating-address and firmware-version
  policy, multiple fixtures, and the physical boundary/reboot/scene/retention/
  coexistence checks. Optical output must be observed independently from a
  matching protocol readback.
- Native tests passed 48/48. `ui_sim` built and completed its full capture run;
  the X100 screen is recorded as `20g_zhiyun_x100_confirmed.png`. The run ended
  with 11,472 bytes free, 20,788 bytes peak used, and 1% fragmentation.
- All eight firmware profiles built successfully. Flash/RAM bytes were:
  `crowpanel_128` 1,802,420 / 164,580; `crowpanel_128_roboto` 1,771,964 /
  164,580; `canon_ble` 1,796,016 / 162,548; `canon_trigger` 1,792,462 /
  161,540; `tascam_x8` 1,793,862 / 161,460; `home_assistant` 1,789,108 /
  161,060; `aputure_light` 1,754,718 / 155,868; and `zhiyun_x100` 1,730,502 /
  153,364.
- The laptop had already provisioned the fixture and verified the direct
  protocol by changing it to on / 10.0% / 3200 K, reading those values back,
  and restoring on / 13.0% / 5600 K. The final `crowpanel_128` image flashed
  successfully to `/dev/cu.usbserial-211240`, its hashes verified, and a bounded
  monitor showed normal display/touch initialization plus the boot heap line
  (`free_heap=129220`, `min_free_heap=126744`). Panel-originated add and
  physical light control remain the final operator-assisted hardware gate.

### 2026-08-07: Compile-time BLE transmit power

- Replaced the shared NimBLE backend's hard-coded 0 dBm transmit power with
  `CONFIG_BLE_TX_POWER_DBM`. Existing firmware profiles explicitly retain the
  0 dBm default; the configuration header supplies the same fallback and
  rejects values outside the ESP32-C3/NimBLE `-24` through `20` dBm input
  range at compile time. NimBLE maps the requested value to a supported radio
  level.
- Updated ADR-025, architecture, and README guidance to distinguish the
  battery-conscious default from compile-time overrides. Alternate values
  still require hardware range, reliability, and current measurements and do
  not change the battery-path safety boundary.
- Native tests passed 48/48. All eight firmware profiles built successfully at
  the existing sizes; `crowpanel_128` used 1,802,420 bytes flash / 164,580
  bytes RAM. The default combined image flashed successfully to
  `/dev/cu.usbserial-211240`, its hashes verified, and the board hard-reset.

### 2026-08-07: Shared X100 panel-owned provisioning

- Extracted the Amaran userspace no-OOB PB-GATT state machine into a shared
  provisioner and placed the existing version-1 `AMSH` store, panel-owned mesh
  identity, sequence allocator, and node records behind one neutral repository.
  Existing persisted Amaran data remains schema-compatible; Amaran and X100 no
  longer keep independent in-memory copies of the same NVS record.
- X100 Add device now accepts product-qualified `PL105_`/`pl105` advertisements
  on Mesh Provisioning `0x1827` as well as Mesh Proxy `0x1828`. A reset light is
  provisioned at the next durable unicast address, its Device Key/node record is
  saved before disconnect, then the driver rescans for `0x1828` and continues
  the existing confirmed `0xFEE9` initialization. It deliberately skips Mesh
  AppKey/model configuration because normal X100 control is direct GATT.
- The shared capability policy accepts the X100's advertised optional static
  OOB support while explicitly selecting no-OOB. Product matching rejects a
  generic Zhiyun company ID unless the manufacturer payload contains `pl105`.
  Failed node-store saves restore the previous in-memory node/allocation state.
- Native tests passed 49/49. `ui_sim` built and completed its full capture run,
  ending with 11,472 bytes free, 20,788 bytes peak used, and 1% fragmentation.
- All eight final-source firmware profiles built successfully. Flash/RAM bytes
  were: `crowpanel_128` 1,804,040 / 165,268; `crowpanel_128_roboto` 1,773,576 /
  165,268; `canon_ble` 1,797,548 / 162,564; `canon_trigger` 1,793,994 / 161,572;
  `tascam_x8` 1,795,394 / 161,484; `home_assistant` 1,790,640 / 161,092;
  `aputure_light` 1,755,770 / 155,876; and `zhiyun_x100` 1,742,864 / 156,748.
  The combined image flashed successfully to `/dev/cu.usbserial-211240`, every
  written region's hash verified, and the board hard-reset.
- Hardware gate remains open: reset the fixture back to `0x1827`, add MOLUS
  X100 from the panel, observe provisioning/rediscovery/confirmed Ready, then
  verify physical power and CCT/brightness output. An interruption after the
  fixture accepts Provisioning Data but before Complete/durable save cannot be
  reconciled automatically; factory reset and retry is the safe recovery.

### 2026-08-07: X100 CCT verification quantization fix

- Live panel testing confirmed that panel-owned provisioning, rediscovery,
  retained connection, power, and brightness all work, but arbitrary CCT
  slider values changed the light and then reported `NOT CONFIRMED`.
- Reinspection of the Android HCI capture found ZY Vega direct CCT writes at
  2950, 3150, 3900, and 5450 K: every value is on a 50 K boundary. Bleep had
  emitted every integer Kelvin and demanded exact equality from the quantized
  device readback. The client now rounds all UI and scene requests to the
  nearest 50 K, writes that canonical value, and still requires exact readback;
  the panel slider snaps to the same value when released.
- Added the captured 5450 K wire frame as a golden vector plus normalization
  boundary coverage. Native tests passed 49/49, the full `ui_sim` capture run
  completed, and all eight firmware profiles built. `crowpanel_128` used
  1,804,148 bytes flash / 165,268 bytes RAM. The corrected combined image
  flashed to `/dev/cu.usbserial-211240`, verified every written-region hash,
  and hard-reset. Live CCT confirmation after this flash remains the operator
  gate.

### 2026-08-07: X100 live control verification follow-up

- Panel-owned provisioning, rediscovery, retained connection, power, and
  brightness/CCT control were exercised on the physical X100. Live readback
  corrected the earlier 50 K hypothesis: a 4550 K request retained as 4500 K,
  so the final UI and command path canonicalizes to 100 K and still verifies
  exact device-originated state.
- Replaced immediate paired brightness/CCT writes with a deterministic staged
  transaction: write brightness, verify it, write CCT, then verify it. This
  avoids the fixture accepting the second setter while leaving the first state
  unchanged. Mismatch diagnostics identify the failing field and show requested
  versus readback values.
- The panel now uses a 350 ms trailing debounce without disabling either
  slider. Slider thumbs and labels remain optimistic at the latest requested
  values; command completion, scene status, and the status label remain
  non-optimistic and use correlated readback.
- With a 60 W USB-C source, 55% confirmed while 62% and 75% were rejected and
  the fixture retained 60%. This matches ZHIYUN's documented supply-dependent
  60% ceiling. Repeated stable readback is reported as `LIMIT 60%` rather than
  as a misleading CCT mismatch. The light was subsequently connected to a
  100 W source; above-60% live confirmation remains to be recorded.
- Final validation passed native 49/49 and the complete `ui_sim` capture run.
  All eight firmware profiles built successfully: `crowpanel_128` used
  1,805,322 bytes flash / 165,300 bytes RAM; `crowpanel_128_roboto` 1,774,850 /
  165,300; `canon_ble` 1,797,984 / 162,564; `canon_trigger` 1,794,430 / 161,572;
  `tascam_x8` 1,795,830 / 161,484; `home_assistant` 1,791,076 / 161,092;
  `aputure_light` 1,756,206 / 155,876; and `zhiyun_x100` 1,744,138 / 156,780.
  The final combined image flashed successfully to
  `/dev/cu.usbserial-211240`, verified all written-region hashes, and reset.

### 2026-08-07: BLE transmit-power default restored to +3 dBm

- `CONFIG_BLE_TX_POWER_DBM` now has one default definition in
  `include/driver_config.h`: +3 dBm. `platformio.ini` does not duplicate the
  default; individual profiles may override the symbol when needed. The
  existing compile-time `-24` through `20` dBm input-range guard remains.
- Updated README, architecture, and ADR-025 to describe +3 dBm as the shared
  default. Earlier 0 dBm progress entries remain as historical records of the
  battery-conscious experiment rather than current configuration.
- Native tests passed 49/49. All eight firmware profiles built successfully;
  `crowpanel_128` used 1,805,322 bytes flash / 165,300 bytes RAM. The combined
  +3 dBm image flashed successfully to `/dev/cu.usbserial-211240`, verified all
  written-region hashes, and hard-reset. RF range/current behavior at +3 dBm
  was not measured during this build-and-flash check.

### 2026-08-07: Generic Zhiyun Light driver and MOLUS X60RGB profile

- Generated an Android bug report over ADB and analyzed its rotated Bluetooth
  HCI snoop log outside the repository. The relevant sanitized evidence has
  SHA-256 `052ee361c6223db63951045c6318fb2a3a011176472cee76116e4d2f830dec12`.
  It identifies `X104_`/`plx104`, PB-GATT `0x1827`, post-provision Mesh Proxy
  `0x1828`, and the same direct `0xFEE9` transport used by X100.
- The X60RGB capture retains the X100 `24 3c` frame, CRC-16/XMODEM, and command
  IDs, with selector `01 80` instead of `00 80`. Added golden vectors for CCT,
  power, float32 hue, and float32 saturation, plus captured initialization
  order and firmware `1.7.0`. Effects and mode readback remain unimplemented.
- Replaced the catalog-visible X100-only entry with one `Zhiyun Light` driver
  while retaining numeric Driver ID 9 compatibility. Repeated Add light actions
  create up to four independent X100/X60RGB instances, infer their profile from
  product-qualified advertising and identity, exclude already saved peer
  addresses during subsequent scans, and automatically name committed records.
- Shared PB-GATT, mesh persistence, retained connections, frame parsing,
  power, CCT, and brightness paths remain common. X60RGB adds a debounced,
  optimistic-feeling RGB UI, while command completion waits for correlated
  device replies for hue, saturation, and brightness. X100 rejects RGB at
  runtime; the current catalog capability union is documented in ADR-029.
- Native tests passed 49/49. `ui_sim` built and completed its full capture run;
  `20g_zhiyun_x100_confirmed.png` and `20h_zhiyun_x60rgb_confirmed.png` show the
  two profiles, ending with 11,472 bytes free, 20,788 bytes peak used, and 1%
  fragmentation.
- All eight final-source firmware profiles built successfully. Flash/RAM bytes
  were: `crowpanel_128` 1,809,690 / 168,708;
  `crowpanel_128_roboto` 1,779,234 / 168,708; `canon_ble` 1,800,742 / 162,580;
  `canon_trigger` 1,797,188 / 161,588; `tascam_x8` 1,798,596 / 161,492;
  `home_assistant` 1,793,834 / 161,108; `aputure_light` 1,758,992 / 155,884;
  and `zhiyun_x100` 1,748,538 / 160,212. One first-pass Home Assistant link
  process crashed with host signal 11; the immediate individual retry and the
  final-source rerun both succeeded.
- The final combined image flashed to `/dev/cu.usbserial-211240`, every written
  region's hash verified, and the board hard-reset. X60RGB add/control from the
  panel and simultaneous X100/X60RGB retention remain operator-assisted
  hardware gates; build and capture evidence do not claim physical output.

### 2026-08-07: X60RGB swatch semantics and RGB write ordering

- Matched the operator-recorded swatch order to the chronological HCI writes:
  red 0 degrees, blue 240, magenta 300, cyan 180, orange 30, and green 120,
  each at 100% saturation, followed by a return to red. The later independent
  ramps confirm `0x1005` saturation and `0x1001` brightness before `0x1008`
  power off/on. This upgrades the hue/saturation interpretation from structural
  inference to operator-annotated capture evidence.
- Changed the combined X60RGB transaction from the provisional
  brightness/hue/saturation sequence to hue, saturation, then brightness. Each
  step still requires its same-sequence, same-command device reply; state is
  published as the requested RGB look only after the final brightness reply.
- Added all six captured swatches to the host RGB-to-HSV test. Native tests
  passed 49/49. All eight firmware profiles built successfully: `crowpanel_128`
  1,809,716 / 168,708 bytes flash/RAM; `crowpanel_128_roboto` 1,779,260 /
  168,708; `canon_ble` 1,800,768 / 162,580; `canon_trigger` 1,797,214 /
  161,588; `tascam_x8` 1,798,622 / 161,492; `home_assistant` 1,793,860 /
  161,108; `aputure_light` 1,759,018 / 155,884; and `zhiyun_x100` 1,748,564 /
  160,212.
- The updated combined image flashed successfully to
  `/dev/cu.usbserial-211240`, every written-region hash verified, and the board
  hard-reset. Cycling the annotated swatches from Bleep remains the physical
  output gate.

### 2026-08-07: BLE transmit-power default increased to +6 dBm

- Changed the single `CONFIG_BLE_TX_POWER_DBM` default in
  `include/driver_config.h` from +3 to +6 dBm. The shared NimBLE backend and
  compile-time range guard are unchanged, and `platformio.ini` still does not
  duplicate the default.
- Updated README, architecture, and ADR-025 to describe +6 dBm as the current
  default. Earlier 0/+3 dBm entries remain historical verification records.
- Native tests passed 49/49. All eight firmware profiles built successfully at
  unchanged sizes; `crowpanel_128` used 1,805,322 bytes flash / 165,300 bytes
  RAM. The +6 dBm combined image flashed successfully to
  `/dev/cu.usbserial-211240`, verified all written-region hashes, and
  hard-reset.
- This session used USB power only. Battery-path operation at +6 dBm remains
  unverified until D1 is replaced and its polarity, loaded voltage drop, and
  temperature are checked on the physical board.

### 2026-08-07: Zhiyun and Sidus multi-fixture HCI documentation

- Analyzed operator-recorded Android video and rotated HCI logs for one
  X100/X60RGB ZY Vega mesh and one Ace 25c/MC Pro Sidus network. Raw captures
  remain outside the repository; only sanitized evidence and HCI SHA-256
  identifiers were documented.
- ZY Vega retained one X100 `0xFEE9` connection after the X60RGB onboarding
  link closed, then routed both members' proprietary commands through it. This
  upgrades one-gateway/multiple-member routing to observed behavior and
  downgrades the existing model-derived `00`/`01` selector interpretation to
  an allocation hypothesis pending a same-model or reversed-order capture.
- Sidus retained one standard Mesh Proxy `0x1828` connection to the MC Pro and
  routed both the MC Pro and Ace 25c through it. The control interval contained
  81 Data In writes and 34 Data Out notifications; encrypted network/application
  data prevents per-node status decoding without the mesh keys.
- Updated `docs/protocols/zhiyun-x100.md`,
  `docs/protocols/zhiyun-x60rgb.md`, and
  `docs/protocols/aputure-lights.md` with the transport evidence, mesh-level
  slot implication, state-quality boundary, and remaining fallback/offline
  tests. No firmware source or user-visible behavior changed, so no build,
  flash, or hardware run was applicable to this documentation-only session.

### 2026-08-07: Aputure MC Pro panel-owned mesh probe

- Factory-reset one Aputure MC Pro and provisioned it over PB-GATT into a new
  private test mesh. Credentials and stable BLE identity remain outside the
  repository. The node received unicast address `0x0002` with one element.
- Decoded Composition Data Status: company `0x03F6`; SIG Config Server
  `0x0000`, Health Server `0x0002`, Generic OnOff Server `0x1000`; vendor model
  `0x03F6:0x1000`.
- Found a defect in the external `studio-lighter` research sender: its Config
  AppKey Add was emitted as an oversized unsegmented lower-transport message.
  A temporary standards-compliant two-segment sender received both Segment
  Acknowledgment and Config AppKey Status success.
- Received Config Model App Status success for the MC Pro vendor model and SIG
  Generic OnOff Server. Generic OnOff Get then returned authenticated Generic
  OnOff Status `0x01`, confirming that node `0x0002` was on and reachable
  through the proxy at query time.
- This is Aputure-specific evidence. The prior command corpus was tested only
  on Amaran fixtures; vendor-control equivalence is not assumed. Ace 25c
  provisioning, per-node status, shared-mesh routing, physical-output checks,
  and proxy fallback remain open.
- Only protocol documentation changed in this repository. No firmware build or
  flash was applicable; the live probe used the external research client.

### 2026-08-07: Retained host mesh-lab probes

- Added `tools/mesh_lab/mesh_probe.py` with the focused AppKey Add, vendor/SIG
  model bind, and Generic OnOff Get operations used for the MC Pro probe. Its
  AppKey path emits standards-compliant 12-byte lower-transport segments and
  persists sequence reservations before attempting BLE writes.
- Added `tools/mesh_lab/decode_notifications.py` to decrypt network, control,
  unsegmented access, and reassembled segmented access messages using a private
  mesh state file. Both tools reject a state file readable by group or world;
  credentials remain outside the repository and are never printed.
- Python syntax/help checks passed. The retained decoder reproduced the MC Pro
  Generic OnOff Status `0x8204 01` from the captured notification.
- `crowpanel_128` build succeeded: RAM 168,708 / 327,680 bytes (51.5%), flash
  1,809,716 / 3,145,728 bytes (57.5%). Upload to
  `/dev/cu.usbserial-211240` completed successfully. No firmware source changed.

### 2026-08-07: Ace 25c joined to the MC Pro test mesh

- Provisioned one factory-reset Amaran Ace 25c into the same private test mesh
  as the Aputure MC Pro. The Ace received unicast address `0x0003` with one
  element; credentials and stable BLE identities remain outside the repository.
- Decoded Ace Composition Data Status: company `0x0211`, version `0x3333`, ten
  SIG models including Generic OnOff/Level/Power OnOff and Light Lightness, and
  vendor model `0x0211:0x0000`. This differs from the MC Pro's
  `0x03F6:0x1000` vendor model and proves configuration must use composition
  rather than one product-family constant.
- Ace Config AppKey Status and vendor/SIG Config Model App Status all returned
  success. Generic OnOff Get addressed to Ace node `0x0003` over only the MC
  Pro BLE Proxy returned authenticated status `0x8204 01` from `0x0003`.
- Both fixtures advertised Mesh Proxy `0x1828`. Generic OnOff Get addressed to
  MC Pro node `0x0002` over only the Ace BLE Proxy returned authenticated status
  `0x8204 01` from `0x0002`. This verifies individual state, cross-member mesh
  routing, and bidirectional proxy fallback while using one physical BLE link.
- Extended the retained probe with Composition Data Get and a separate
  `--proxy-address`, allowing logical destination and physical proxy selection
  to be tested independently. One CoreBluetooth attempt ended silently; the
  retry succeeded and the pre-reserved sequence number prevented nonce reuse.

### 2026-08-07: Two-node standard-model automation and response soak

- Confirmed acknowledged Generic OnOff Set/Get model transactions on both
  nodes. MC Pro's model changed Off then On immediately through the Ace proxy.
  Ace's model reported one-second transitions (`01 00 0A`, then `00`;
  `00 01 0A`, then `01`). Physical output was unattended.
- Ace standard readback returned Generic Level `0x7FFF`, Light Lightness
  `0xFFFF`, Generic OnPowerUp `0x01`, and Default Transition Time `0x41` (one
  second). Light Lightness Set to `0x8000` returned present/target/remaining
  status, settled at `0x8000`, and was restored to model value `0xFFFF`.
- Bound both Generic OnOff Servers to group `0xC000`. One group Get returned
  separate authenticated On statuses from MC Pro `0x0002` and Ace `0x0003`;
  the same result worked through either fixture as the sole BLE proxy.
- One group Off through MC Pro returned immediate MC model Off and Ace's
  one-second transition, then a group Get reported both model Off. One group On
  through Ace returned immediate MC model On and Ace's reverse transition, then
  a final group Get reported both model On. Both nodes remained reachable while
  their model state was Off, proving model state and connectivity must be
  tracked independently.
- Ten alternating unicast Gets over one retained MC Pro proxy connection
  returned 10/10 statuses (five per node). The reverse Ace-proxy run also
  returned 10/10. No member response was lost after a usable proxy session
  formed.
- CoreBluetooth intermittently ended an attempted connection with no tool
  output. One silent attempt during full-lightness restoration may have sent:
  the next acknowledged Set found Ace already at `0xFFFF`, and the node's
  sequence advanced. Treat a silent host attempt as indeterminate until a
  directed Get resolves state, not automatically as unsent.
- Tested the prior Amaran vendor corpus separately. Ace unicast power-off and
  brightness-50 vectors produced no response and left authenticated standard
  state at On/`0xFFFF`; MC Pro likewise ignored the Amaran power-off vector.
  These successful proxy writes remain optimistic and are not valid evidence
  of fixture control for either tested firmware.
- Extended `tools/mesh_lab/mesh_probe.py` with acknowledged OnOff/Lightness
  control, standard state Gets, independent proxy selection, alternating-node
  soak, SIG group subscription, and group OnOff Get. Both standard models ended
  On; Ace's Light Lightness model ended at `0xFFFF`.
- Operator correction after returning: MC Pro was physically dark despite its
  authenticated Generic OnOff model reporting `0x01`. A repeated Get still
  returned model On, and a subsequent acknowledged standard On Set also
  returned model On. The standard MC Pro model therefore proves reachability
  but is not authoritative emitter state; the earlier restore claim is
  withdrawn.
- Operator-watched follow-up made the boundary definitive. After the fixture
  was turned On locally, standard Generic OnOff Set Off acknowledged `0x00` and
  the following Get persisted `0x00`, but the emitter visibly did not change.
  MC Pro Generic OnOff is a writable shadow model: retain it for authenticated
  reachability only and exclude it from emitter state/control claims.
- A subsequent watched group Off produced MC model Off and Ace's one-second
  model transition toward Off, but neither emitter changed. Generic OnOff is a
  shadow model on both tested fixtures and cannot implement physical group
  power. A 30-second passive proxy listen while requesting local fixture
  toggles captured no notifications; whether the controls were exercised
  during the exact window remained operator confirmation at capture end.
- Extended `decode_notifications.py` with semantic configuration status,
  OnOff present/target/remaining time, level, OnPowerUp, default-transition, and
  lightness decoding. Known captured transition and lightness responses were
  replayed successfully through the new labels.
- Final Python syntax checks and `git diff --check` passed. `crowpanel_128`
  built successfully at 168,708 / 327,680 bytes RAM (51.5%) and 1,809,716 /
  3,145,728 bytes flash (57.5%), then uploaded to
  `/dev/cu.usbserial-211240` with written-region hash verification. Firmware
  sources were unchanged by this host-tool/protocol session.

### 2026-08-07: Ace 25c/MC Pro physical vendor power correlation

- Subscribed the composition-selected vendor models on MC Pro `0x0002` and Ace
  25c `0x0003` to group `0xC000`. The previously captured power payload sent to
  this group physically turned both emitters Off, then On, then restored both
  Off. The same payload had produced no observed effect when sent unicast.
- Correlated group poll `26 0E 00 00 00 00 00 00 00 00 0E` with both physical
  states. Each node returned an authenticated opcode `0x26` status whose first
  data byte tracked real emitter power (`00` Off, `01` On); its leading checksum
  covered the following nine bytes. MC Pro ended in Off status
  `E8 00 00 00 00 20 A4 28 FA 02`; Ace ended in Off status
  `EB 00 00 00 00 80 56 1A FA 01`.
- Stored intensity `FA` remained unchanged across Off and On responses, so
  emitter power is not inferred from intensity. A separate `0x0A` vendor poll
  returned valid but dynamically changing fields that remain unresolved.
- With both emitters physically Off, only MC Pro continued advertising Mesh
  Proxy; Ace stopped advertising it. The mesh still occupied one BLE link and
  both nodes answered through MC Pro, but fallback candidate availability is
  power-state dependent.
- Added named `vendor-power-get`, `vendor-power-on`, and `vendor-power-off`
  operations to the retained mesh probe and semantic decoding for the
  correlated statuses. Final authenticated Get confirmed both fixtures Off.
- OBSBOT Center was running for independent webcam observation, but its
  accessibility state capture timed out twice; no camera-derived claim is
  recorded. Direct Python/OpenCV capture then reached AVFoundation but macOS
  denied Camera permission, so it also produced no frame. The physical
  transitions were operator-watched and final state was device-originated
  authenticated status.
- Python syntax/help checks, replay of both final vendor statuses through the
  semantic decoder, and `git diff --check` passed. `crowpanel_128` remained at
  168,708 / 327,680 bytes RAM (51.5%) and 1,809,716 / 3,145,728 bytes flash
  (57.5%); build and upload to `/dev/cu.usbserial-211240` succeeded with hash
  verification. Firmware sources were unchanged.

### 2026-08-07: Mesh-aware physical BLE slot accounting

- Split retained logical capacity from physical BLE capacity in
  `DeviceManager`. Each driver now exposes a `BleSlotKey`: ordinary GATT
  instances use one key each, non-BLE Home Assistant entities use no key, and
  all logical Amaran/Aputure members in the single panel-owned mesh share one
  key backed by the runtime's existing single Mesh Proxy connection.
- A second logical member can activate while all four physical keys are
  occupied without evicting anything. Acquisition of a genuinely new fifth key
  uses group-aware LRU: every member must be idle and unprotected, and the whole
  group is deactivated so its one central link is actually released. The
  separate eight-instance bound retains its single-instance LRU behavior.
- Added native coverage for the full four-key condition, same-mesh admission,
  whole-mesh eviction, Home Assistant's zero-BLE cost, and existing recording/
  owner protection. Native passed 50/50 after the refactor.
- Kept Zhiyun per-instance accounting for now. The mixed X100/X60RGB capture
  proves one proprietary gateway can route the mesh, but the firmware still
  owns one direct client per member and selector `00`/`01` covaries with model
  and onboarding order. Counting those clients as one before implementing the
  shared gateway would overbook the four-client central; a same-model or
  reversed-order capture remains the safe selector gate.
- Added the captured vendor physical-power Get builder and checksum/profile/
  state parser to the production Amaran protocol with MC Pro Off, Ace 25c On,
  and corrupt-checksum native vectors. Firmware transport integration remains
  separate because current notification handling does not yet decrypt Proxy
  Network PDUs.
- Added `vendor-power-soak` to the retained host probe. A read-only ten-poll run
  over one retained MC Pro proxy returned 10/10 authenticated Off statuses from
  MC Pro and 9/10 from Ace 25c. This observed miss requires bounded retry/
  freshness logic; a single absent group reply must not immediately mark a
  member offline. Both emitters remained Off and no state-changing command was
  sent.
- Final verification: native passed 50/50; `ui_sim` built and completed its
  full capture flow (20,788-byte LVGL peak, 1% final fragmentation); all eight
  firmware profiles (`crowpanel_128`, Roboto, Canon Smart, Canon Trigger,
  Tascam X8, Home Assistant, Amaran, and Zhiyun) built successfully. The first
  cross-profile attempt exposed an ESP32 GCC aggregate-initialization mismatch;
  an explicit constexpr `BleSlotKey` constructor fixed it before the successful
  rerun. Default firmware used 168,708 / 327,680 bytes RAM (51.5%) and
  1,810,744 / 3,145,728 bytes flash (57.6%). Upload to
  `/dev/cu.usbserial-211240` completed successfully with esptool verification.

### 2026-08-07: Authenticated mesh member status and cross-brand gateway proof

- Added AES-CCM decrypt/authentication and a bounded complete-unsegmented Proxy
  Network PDU decoder to the production Amaran transport. Native round-trip
  coverage encrypts a captured MC Pro status through the production encoder,
  recovers its source/destination/access payload, and rejects a corrupted tag.
- The runtime now sends the physically correlated vendor-power Get to the
  panel-owned group every five seconds. Authenticated replies are mapped by
  provisioned source address, vendor checksum/profile are validated, and power,
  reachability, and `lastSeen` update independently from the shared Proxy link.
  Per-source repeated sequence numbers are ignored; a member becomes stale only
  after a fifteen-second gap, accommodating the observed 9/10 Ace response run.
  Physical vendor power Set remains disabled in this tranche.
- Native passed 50/50. All eight firmware profiles rebuilt successfully and
  `ui_sim` completed its full screenshot flow with 20,788-byte peak LVGL use
  and 1% final fragmentation. Default `crowpanel_128` used 168,820 / 327,680
  bytes RAM (51.5%) and 1,812,058 / 3,145,728 bytes flash (57.6%), then uploaded
  successfully to `/dev/cu.usbserial-211240` with hash verification.
- macOS Camera permission was available on retry. Two direct Python/OpenCV
  1920x1080 captures succeeded. The operator identified the physical order as
  Zhiyun, MC Pro, then Ace 25c; the later frame showed the Zhiyun emitting while
  the two Sidus-network fixtures remained dark.
- Discovered the ready fixture as factory-reset X60RGB `X104_...`, not X100.
  Provisioned it with standard no-OOB PB-GATT into the same private test mesh as
  MC Pro and Ace at unicast `0x0004`, one element, and intentionally skipped
  Amaran AppKey/model configuration. Private keys and stable BLE identifiers
  remain outside the repository. It reappeared advertising Mesh Proxy.
- With X60RGB as the sole BLE gateway, five read-only group power polls returned
  five unique authenticated MC Pro Off statuses and three unique Ace Off
  statuses. Every response was delivered twice with the same source/sequence,
  demonstrating a redundant relay path and validating the runtime replay guard.
- Added retained `tools/mesh_lab/zhiyun_probe.py`. Its read-only captured
  `0xFEE9` initialization identified `plx104` and returned 4400 K, Power On,
  and 12% brightness from the newly provisioned X60RGB without printing raw
  identity payloads. Together with the Mesh Proxy poll, this proves one X60RGB
  BLE connection can carry its proprietary state and cross-brand standards
  mesh traffic. Firmware still needs one shared gateway owner before Zhiyun and
  Amaran logical members can honestly consume the same physical slot.

### 2026-08-07: Portal Home Assistant entity-picker repair

- Fixed the LAN Portal entity picker to address entity-rack fields in their
  actual sibling container instead of incorrectly searching beneath `haForm`.
  The rack controls are now explicitly associated with `haForm`, so selected
  entity IDs, names, and instance IDs are included when saving.
- Added `autocomplete="current-password"` to the long-lived token field and
  disabled autocomplete on generated entity/name fields.
- Focused Bun validation passed for JavaScript syntax, corrected selectors,
  form association, and token autocomplete. Native passed 51/51.
- `home_assistant` built at 161,380 / 327,680 bytes RAM and 1,797,980 /
  3,145,728 bytes flash. `crowpanel_128` built at 169,732 / 327,680 bytes RAM
  and 1,815,180 / 3,145,728 bytes flash, then uploaded successfully to
  `/dev/cu.usbserial-211240` with written-region hash verification.
- Live browser selection and save against the operator's Home Assistant remain
  to be exercised; compile and flash do not prove that external round trip.

### 2026-08-07: Project-name expansions

- Documented **Bluetooth Links Everything, Eventually, Probably** as Ble(e)p's
  playful expansion and **Bluetooth Low Energy Equipment Panel** as its serious
  descriptive expansion in the public README and documentation index.
- Documentation only; firmware behavior and BLE identity are unchanged. No
  build or flash was run for this wording-only update.

### 2026-08-08: Horizontal-stem watch-crown print variant

- Restored the watch-crown source and artifacts under the gitignored local
  `hardware/.workbench/watch-crown/` area. The canonical geometry remains an
  11.5 mm crown with the relieved 0.76 mm square drive for the CrowPanel
  encoder. Hardware experiments now stay in `.workbench` until promoted as
  reviewed deliverables.
- Added STEP and STL variants rotated 90 degrees and grounded on the crown rim,
  placing the 1.60 mm stem axis parallel to the build plate so its length is
  formed within each layer instead of across weak layer bonds. Added focused
  FDM orientation and support notes; the encoder fit remains unchanged.
- Re-imported both STEP files as one valid solid each. Canonical bounds were
  11.495 x 11.495 x 5.350 mm; horizontal-print bounds were
  5.350 x 11.495 x 11.495 mm. Both measured 294.949 mm3. Physical printing,
  support removal, encoder fit, and push travel remain unverified. No firmware
  build or flash was applicable to this CAD-only change.

### 2026-08-08: Android BLE capture workflow

- Added a reusable protocol-research guide for synchronized phone screen
  recording and Android HCI snoop capture through `adb bugreport`. It covers
  experiment design, deliberate action spacing, rotated-log discovery,
  video/ATT correlation, mesh proxy versus member identity, passive analysis,
  bounded active probing, privacy, and the durable evidence handoff.
- Linked the guide from the protocol index, public device-contribution section,
  and contributor privacy guidance. Documentation only; firmware behavior is
  unchanged, so no build or flash was run.

### 2026-08-08: Local Settings, build identity, diagnostics, and Factory Reset

- Added a round-safe Home cog while retaining all four mode tiles. The lazy
  Settings session contains Wi-Fi status/Portal entry, persistent haptic
  enablement, About, sanitized live System Info, and warned Factory Reset.
  Settings reads the saved SSID without starting Wi-Fi; normal Home boot remains
  radio-free.
- Added checked schema-1 `PSET` persistence for haptic enablement. Disabled
  feedback stops the current pattern and suppresses Press, Connected, Back, and
  Error requests. Missing or corrupt settings preserve the prior enabled
  default, and failed writes leave the last committed state unchanged.
- About shows firmware `v0.1.0-dev`, the seven-character Git commit with a
  dirty marker when applicable, its authored date, `crowpanel-1.28`, and the
  Apache-2.0 license. The generated 176x58 LVGL wordmark uses separately scaled
  mascot/lettering regions, authored transparency, Hamming resampling, and a
  small face-feature mask so both eyes and the smile survive panel scaling.
- System Info exposes only hardware/build identity, free/minimum/largest heap,
  active physical BLE groups, and Wi-Fi state. Factory Reset requires a
  continuous three-second touch hold, cancels scenes and transports, erases the
  complete NVS partition including bonds and mesh keys, and reboots without
  erasing firmware. The real destructive path was intentionally not triggered.
- Native passed 52/52. The complete `ui_sim` flow passed and captured
  `01_home.png` plus `31_settings.png` through
  `31e_settings_factory_reset.png`; reset assertions rejected a 2,999 ms hold
  and accepted one 3,001 ms hold. Visual review corrected the settings repaint
  lifetime, reset-warning spacing, and smile-preserving logo conversion.
- All eight firmware profiles built sequentially. Default `crowpanel_128` used
  169,828 / 327,680 bytes RAM (51.8%) and 1,850,856 / 3,145,728 bytes flash
  (58.8%); the other profiles remained between 47.7-51.8% RAM and 57.2-58.5%
  flash. One upload streamed to `/dev/cu.usbserial-211240`, but its final status
  was not captured; two confirmation retries then failed to open the same port
  with `Operation not permitted`. Treat the final flash and all physical UI,
  tactile persistence, Wi-Fi handoff, and reboot behavior as unverified.

### 2026-08-08: Settings hierarchy and scrolling follow-up

- Moved About to the first Settings row. The Settings list now has real vertical
  overflow and an automatic position indicator instead of exactly filling its
  viewport, and Factory Reset remains its own red top-level item and warned
  child screen.
- Made the About details independently scrollable below the project logo and
  added the project expansion above the firmware version, commit/date,
  hardware, and license details. Added simulator captures for both menu and
  About scrolled-to-end states so the overflow path is exercised directly.
- Native passed 52/52. The complete `ui_sim` flow passed with new
  `31_settings_scrolled.png` and `31c_settings_about_scrolled.png` captures;
  final LVGL peak remained 20,788 bytes with 1% fragmentation at completion.
- All eight profiles (`crowpanel_128`, Roboto, Canon Smart, Canon Trigger,
  Tascam X8, Home Assistant, Amaran, and Zhiyun) built sequentially. Default
  firmware used 169,852 / 327,680 bytes RAM (51.8%) and 1,851,496 / 3,145,728
  bytes flash (58.9%). Upload to `/dev/cu.usbserial-211240` completed with image
  hash verification and hard reset. Physical touch scrolling and navigation
  remain operator-unverified; Factory Reset was not triggered.

### 2026-08-08: Factory Reset menu presentation

- Changed the top-level Factory Reset row to the same neutral menu styling as
  the other Settings destinations. Selecting it still opens the dedicated
  warning screen; only that screen's hold-to-reset control is red.
- The full `ui_sim` capture flow passed and visually confirms the neutral row
  and separate warning screen. All eight firmware profiles rebuilt
  successfully; default used 169,852 / 327,680 bytes RAM (51.8%) and
  1,851,458 / 3,145,728 bytes flash (58.9%). Upload to
  `/dev/cu.usbserial-211240` completed with image hash verification and hard
  reset. The destructive reset action was not triggered.

### 2026-08-08: About logo scroll behavior

- Moved the project logo into the About page's scroll container so the logo,
  project expansion, firmware/build identity, hardware, and license move as one
  continuous document. The header remains outside the content region.
- The full `ui_sim` flow passed and its initial/scrolled About captures confirm
  that the smile remains visible while the logo moves with the document. All
  eight firmware profiles rebuilt successfully; default remained at 169,852 /
  327,680 bytes RAM (51.8%) and 1,851,458 / 3,145,728 bytes flash (58.9%). The
  board upload completed with image hash verification and hard reset. Physical
  touch scrolling remains operator-unverified.

### 2026-08-08: External 1N5819 battery-path repair documentation

- Documented the operator-installed through-hole 1N5819 replacement for a
  failed-open CrowPanel D1 in `hardware/README.md`, including applicability,
  polarity, solder points, strain relief, first-power checks, and the boundary
  between an initial repair and the open ADR-025 endurance gate.
- The repaired board ran from battery with 4.0 V measured on the battery side
  and 3.7 V on the board-load side, an observed 0.3 V forward drop. Diode
  temperature under representative BLE load and multi-day endurance remain
  unverified.
- Documentation only; firmware behavior did not change. No build or flash was
  run, avoiding deployment of unrelated user-owned firmware changes already
  present in the worktree.

### 2026-08-08: Remove the default Shark device

- Changed missing-registry initialization to persist an empty Devices list.
  A genuinely paired Shark in the pre-registry namespace still migrates, and
  upgrades remove only the exact untouched unpaired `Shark Nano II` placeholder
  with no BLE address or advertised name. Renamed, paired, and identified Shark
  records remain intact.
- Native passed 55/55, including empty first boot, one-time placeholder removal,
  paired legacy migration, restart persistence, and preservation of a renamed
  unpaired Shark. The complete `ui_sim` capture flow passed; its deliberately
  paired legacy fixture remains in place for Shark-screen regression coverage.
- All eight firmware profiles (`crowpanel_128`, Roboto, Canon Smart, Canon
  Trigger, Tascam X8, Home Assistant, Amaran, and Zhiyun) built sequentially.
  Default firmware used 169,852 / 327,680 bytes RAM (51.8%) and 1,851,688 /
  3,145,728 bytes flash (58.9%). Upload to `/dev/cu.usbserial-211240` completed
  with image hash verification and hard reset. NVS was not erased; the actual
  post-migration Devices screen remains operator-unverified.

### 2026-08-08: Two-stage action-button Back/Home hold

- Kept the 700 ms action-button hold as exactly one ordinary Back step. If the
  same press remains held to 2 seconds, the UI now follows each remaining
  screen's normal Back path and lands on Home, preserving provisional pairing,
  borrowed device-control, and sequence-link cleanup.
- The simulator regression begins in sequence Settings with links held, checks
  that the first stage only closes Settings, then checks that the continued
  stage reaches Home and releases both sequence owners. The complete `ui_sim`
  capture flow passed; native passed 52/52.
- All eight firmware profiles built sequentially. Default `crowpanel_128` used
  169,852 / 327,680 bytes RAM (51.8%) and 1,857,174 / 3,145,728 bytes flash
  (59.0%). It uploaded to `/dev/cu.usbserial-211240` with image hash
  verification and hard reset. Physical two-stage button timing remains
  operator-unverified.

### 2026-08-08: Dormant drivers and on-demand Wi-Fi memory recovery

- Diagnosed the simultaneous BLE/Home Assistant failure on the attached build
  as contiguous internal-SRAM exhaustion: live free heap was 7.3-9.2 KiB,
  largest allocation 5.1-7.7 KiB, minimum heap 340 bytes, and HA REST returned
  transport failures while NimBLE still requires roughly `0x7800` contiguous
  bytes to initialize.
- Implemented ADR-035 across Shark, Canon Smart, Canon Trigger, Tascam X8,
  Home Assistant, Amaran, and Zhiyun. Compiled driver globals are now pointer-
  sized shells with a 64-byte build guard; full clients/sessions allocate only
  when an instance activates. The shared mesh repository/runtime and HA client,
  WebSocket, and Wi-Fi runtime use first-user/last-user ownership. Configured HA
  entities alone do not start Wi-Fi; only active HA or Portal does, and final
  teardown sets `WIFI_OFF`.
- Changed the device registry from 24 permanent records to checked four-record
  heap blocks without changing persistence schema 2 or the 24-record ceiling.
  Split NimBLE callback traffic into a compact 16-entry control queue and an
  independent 8-entry advertisement queue. HA initial REST reads are serialized,
  heap-gated, use fixed URL/auth/subscription buffers, and back off after
  transient failures. Reduced LVGL from 76 to 64 KiB; the two 15-row display
  DMA strips were not changed.
- Native passed 57/57. The complete `ui_sim` capture flow passed with the 64 KiB
  pool; peak incremental LVGL use was 10,906 bytes and the tightest reported
  point retained 18,000 bytes free. All eight firmware profiles built
  sequentially. Full `crowpanel_128` uses 142,148 / 327,680 bytes static RAM
  (43.4%) and 1,846,218 / 3,145,728 bytes flash (58.7%), recovering 33,696 bytes
  of static RAM from the 175,844-byte failing build. Alternate profiles use
  141,612-142,148 bytes static RAM.
- The full image uploaded to `/dev/cu.usbserial-211240` with hash verification
  and hard reset. A bounded boot capture at neutral Home reported Wi-Fi `Off`,
  BLE disconnected, 151,052 bytes free heap, 142,612 bytes minimum free heap,
  and a 131,060-byte largest free block. Active-driver deltas, mixed BLE+HA
  operation, and post-deactivation recovery remain target-hardware gates.

### 2026-08-08: Canon Smart incomplete-discovery recovery

- Diagnosed an EOS R6 Mark II connection that appeared stuck. Serial evidence
  showed physical connection and encryption succeeded with 98,612 bytes free
  heap and a 77,812-byte largest block, but filtered GATT discovery returned
  pairing command `00010006` without the required pairing data `0001000a`.
  The subsequent clean disconnect exposed a UI bug: the disconnected Canon
  screen unconditionally replaced its retry state with a disabled `WAITING`
  action.
- Canon Smart now retries an incomplete pairing-service discovery once with a
  full characteristic refresh. If setup remains incomplete it stops automatic
  reconnect churn, reports `CONNECTION FAILED / CANON SETUP INCOMPLETE`, and
  enables `RETRY`. Retrying a saved camera preserves its address and bond;
  rejected or new pairing returns to discovery. Other drivers retain the shared
  BLE coordinator's existing automatic protocol-failure retry behavior.
- Native passed 58/58, including the no-reconnect terminal-failure path. The
  complete `ui_sim` capture flow passed with 18,000
  bytes free at its tightest reported point. All eight firmware profiles built
  sequentially. Default `crowpanel_128` used 142,148 / 327,680 bytes static RAM
  (43.4%) and 1,846,706 / 3,145,728 bytes flash (58.7%), then uploaded to
  `/dev/cu.usbserial-211240` with hash verification and hard reset.
- A bounded hardware capture reached the saved R6 II after three radio-level
  connection misses, discovered `00010006` and `0001000a`, received wake result
  `04`, and logged `protocol_ready`. The same capture subsequently brought up
  Tascam X8 and Home Assistant together and logged all sequence targets ready.
  Canon's forced full-refresh failure path remains intentionally difficult to
  reproduce, but it is bounded and its retry UI is compile/simulator verified.

### 2026-08-08: Muted Devices-list status typography

- Split each Devices row into independent name and runtime-status labels. The
  device name remains 14 px, while `connected`, `ready`, `connecting`,
  `scanning`, and `disabled` now use a 12 px muted-gray label. Added matching
  12 px Montserrat and generated Roboto faces so both supported UI font
  profiles retain the same hierarchy.
- The complete `ui_sim` capture flow passed. `02_devices.png` confirms the
  smaller gray status stays aligned and unclipped beneath each device name.
  Max-device initialization retained 43,048 bytes of LVGL memory, 344 bytes
  less than the preceding layout; the tightest reported point retained 17,984
  bytes.
- Native passed 58/58. All eight firmware profiles built sequentially. Default
  `crowpanel_128` used 142,156 / 327,680 bytes static RAM (43.4%) and 1,858,230
  / 3,145,728 bytes flash (59.1%). Upload to `/dev/cu.usbserial-211240`
  completed with image hash verification and hard reset. Physical-panel text
  appearance remains operator-verifiable.

### 2026-08-08: Canon Smart multi-instance notification routing

- Diagnosed Sequence 4 failing to prepare its first of two Canon Smart targets.
  Although the driver allocates an independent client/session per active camera,
  all Canon characteristic callbacks still forwarded through one global client
  pointer. Activating the second camera replaced that pointer, so the first
  camera's wake and shooting notifications entered the second camera's queue.
- Replaced the singleton callback destination with a bounded active-client
  registry. Each pairing, mode, and shooting notification now routes to the
  client that owns the originating `NimBLERemoteCharacteristic`; activation
  fails cleanly if the registry cannot accept another client.
- Native passed 67/67. `crowpanel_128` built with 142,332 / 327,680 bytes static
  RAM (43.4%) and 1,902,838 / 3,145,728 bytes flash (60.5%); `canon_ble` built
  with 141,084 bytes static RAM (43.1%) and 1,758,864 bytes flash (55.9%). The
  full image uploaded to `/dev/cu.usbserial-211240` with hash verification and
  hard reset. A live two-Canon Sequence 4 prepare and Start/Stop run remains the
  required peripheral verification.

### 2026-08-09: Ubuntu native-test compile repair

- Fixed the GitHub Actions native-test compile failure by explicitly including
  `<cstdio>` in `test/test_main.cpp`, which uses `std::snprintf`. The previous
  source relied on a transitive declaration available in the local macOS header
  graph but absent from the Ubuntu runner.
- Native passed 71/71. The full Montserrat `crowpanel_128` profile built with
  142,308 / 327,680 bytes static RAM (43.4%) and 1,897,428 / 3,145,728 bytes
  flash (60.3%). Alternate profiles were left to GitHub Actions.
- The full image uploaded successfully to `/dev/cu.usbserial-211240`; all
  written-region hashes verified and the board hard-reset. The next CI run
  remains the authoritative Ubuntu verification of the repaired include.

### 2026-08-09: DJI multi-camera sequence false-timeout repair

- Diagnosed Sequence 3 stopping after its first of two DJI cameras even though
  each camera's dedicated control screen worked. Instrumented hardware evidence
  showed the first scene command was marked timed out before its GATT write;
  the write and successful DJI ACK arrived afterward, when the scene had already
  entered `Failed`, so the second camera was never dispatched.
- DJI record commands now perform the pending GATT write and establish its
  five-second ACK deadline before evaluating expiration. This prevents a stale
  zero deadline from failing a newly queued command while retaining the same
  post-write timeout and ACK handling.
- On the connected hardware, Sequence 3 Start and Stop each dispatched in order
  to DJI instances 19 and 20. Both cameras returned ACK result 0 for both
  commands, every scene confirmation completed, and the operator confirmed both
  cameras started and stopped correctly. Temporary diagnostic logging was then
  removed.
- Native passed 72/72. The `dji_osmo` profile built with 139,132 / 327,680 bytes
  static RAM (42.5%) and 1,748,000 / 3,145,728 bytes flash (55.6%). The cleaned
  full `bleep` profile built with 140,364 bytes static RAM (42.8%) and 1,901,478
  bytes flash (60.4%), then uploaded to `/dev/cu.usbserial-211240`; all written
  regions passed hash verification and the board hard-reset.
### 2026-08-11: Transactional mesh creation and explicit light selection

- Ported only the still-relevant transaction/selection concepts from historical
  commit `8532ef3`; no wholesale cherry-pick was used. Current `panel_identity`,
  `Bleep-Setup-XXXXX`, offline Portal/security repairs, Aputure Light paths,
  shared Aputure/Zhiyun mesh transport, per-fixture unicast control, and compound
  scene behavior remain intact. `bdc9aaa` and the old identity/Portal code were
  not ported. The pre-existing `stash@{0}` safety stash remains untouched.
- Missing mesh initialization now fills Network Key and AppKey in temporary
  state through an injectable random seam, persists the existing schema before
  publishing it, and leaves live caller state unchanged on entropy or save
  failure. No panel identity participates in key generation.
- Fresh Aputure Light and Zhiyun adds now expose a four-entry scrollable picker
  with advertised name/model, address suffix, and RSSI. Address plus address
  type owns a stable selection token; duplicate updates stay in place and a
  stronger candidate replaces only the weakest full-list entry. Saved targets
  continue automatic reconnect. Immediate selection failure, connect/
  provisioning/configuration failure, competing loss, and Back/cancel return to
  Scanning without a normal registry commit. Provisional node/unicast state is
  restored without decreasing reserved sequence high-water.
- Native passed 84/84. The complete `ui_sim` traversal passed, including a real
  four-row candidate interaction that retained identical LVGL row objects over
  750 ms of normal refresh ticks, scrolled the list, tapped a stable-token row,
  and canceled the pending add. The full Montserrat `bleep` profile built with
  141,524 / 327,680 bytes static RAM (43.2%) and 1,931,720 / 3,145,728 bytes
  flash (61.4%). Per repository policy, alternate profiles remain CI-owned.
- The configured `/dev/cu.usbserial-211240` target was present. The validated
  `bleep` image uploaded successfully, every written-region hash verified, and
  the ESP32-C3 hard-reset. NVS at `0x9000` was not erased or factory-reset.
- Still unverified: two physical panels/two fixtures, phone and captive-Portal
  behavior, physical fixture selection and competing provisioning, cross-mesh
  rejection, reboot/fallback proxy ownership, all four Aputure fixtures,
  Aputure/Zhiyun coexistence, and the two-hour soak. Build, simulator, ACK, and
  proxy evidence do not satisfy these gates.
