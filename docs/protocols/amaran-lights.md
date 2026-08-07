# Amaran and Aputure Bluetooth Mesh evidence

Status: `Research` with host-tested encoders, Sidus Link multi-fixture HCI
evidence, and one Ble(e)p-owned mesh verified on an Amaran Ace 25c and Aputure
MC Pro. Vendor control and broader target-fixture gates remain open.

## Reference boundary

The implementation was ported from the working
`/Users/nethunter/dev/hml/research/studio-lighter` tree, including its
uncommitted Home Assistant additions. Ble(e)p does not import Python or Home
Assistant code at runtime. Captures and successful writes in that reference are
evidence for packet construction, not confirmed fixture state.

An operator-recorded Sidus Link session added one amaran Ace 25c and one
Aputure MC Pro, then controlled both fixtures. The relevant rotated Android HCI
log has SHA-256
`342b8ebc77ee01c094f4bfbcfc8f5da16cf124430edb3049ef9a47499ebc0ce1`.
The companion video, raw bugreport, and snoop logs remain outside the repository
because they include stable radio identities, nearby devices, phone identity,
and encrypted Bluetooth Mesh provisioning traffic.

## Transport

- Provisioning service `0x1827`, Data In `0x2ADB`, Data Out `0x2ADC`.
- Mesh Proxy service `0x1828`, Data In `0x2ADD`, Data Out `0x2ADE`.
- Provisioning is P-256, no-OOB PB-GATT with secure random values.
- The userspace mesh implementation includes AES-CMAC `s1`/`k1`/`k2`/`k4`,
  AES-CCM upper/network encryption, privacy obfuscation, proxy framing, and
  segmented device-key configuration messages.
- Configuration sends Composition Data Get, AppKey Add, Generic OnOff Server
  bind, and group subscription. The 2026-08-07 MC Pro and Ace 25c runs decoded
  successful AppKey, model-bind, and subscription statuses.

## Panel-owned Ace 25c and MC Pro evidence

On 2026-08-07, a factory-reset Aputure MC Pro was provisioned over PB-GATT into
a new private test mesh whose credentials remain outside the repository. The
node received unicast address `0x0002` and reported one element. Its decoded
Composition Data Status reported:

- company ID `0x03F6`, product/version `0x0000`, replay-list size 20, and all
  four feature bits set;
- SIG Config Server `0x0000`, Health Server `0x0002`, and Generic OnOff Server
  `0x1000`; and
- vendor model company `0x03F6`, model `0x1000`.

This was the first Aputure-specific panel-owned-mesh evidence. The earlier
vendor command corpus was captured and tested against Amaran fixtures and was
not generalized to Aputure until the later group-addressed physical test below.

The factory-reset Ace 25c was then provisioned into the same test mesh at
unicast address `0x0003`, also with one element. Its composition is not the
same as the MC Pro:

- company ID `0x0211`, product `0x0000`, version `0x3333`, replay-list size
  105, and features `0x0007`;
- SIG Config Server `0x0000`, Health Server `0x0002`, Health Client `0x0003`,
  Generic OnOff Server `0x1000`, Generic Level Server `0x1002`, Generic Default
  Transition Time Server `0x1004`, Generic Power OnOff Server `0x1006`, Generic
  Power OnOff Setup Server `0x1007`, Light Lightness Server `0x1300`, and Light
  Lightness Setup Server `0x1301`; and
- vendor model company `0x0211`, model `0x0000`.

The MC Pro and Ace therefore require composition-driven model selection. The
Telink `0x0211:0x0000` assumption fits this Ace but was rejected as Invalid
Model by the MC Pro; the MC Pro instead requires `0x03F6:0x1000`.

The research sender initially emitted the 20-byte Config AppKey Add access
message as one oversized unsegmented lower-transport PDU. The MC Pro therefore
reported `Invalid AppKey Index` when the subsequent model bind arrived. Sending
AppKey Add as two 12-byte transport segments produced a Segment Acknowledgment
and Config AppKey Status success. The MC Pro then returned Config Model App
Status success for both vendor model `0x03F6:0x1000` and SIG Generic OnOff
Server `0x1000`.

An acknowledged Generic OnOff Get (`0x8201`) subsequently returned Generic
OnOff Status (`0x8204`) with present state `0x01`. This confirms that the MC Pro
was individually reachable through the proxy and that its standard model
reported On. It does not confirm emitter power: the operator later observed
the MC Pro physically dark while repeated authenticated Gets still returned
`0x01`. A standards-based per-node liveness path is established; the later
vendor `0x0E` group poll supplies the separately correlated physical-power path.

The Ace also acknowledged segmented AppKey Add and both its composition-derived
vendor bind and SIG Generic OnOff bind. A Get addressed to Ace node `0x0003`
while the host held only the MC Pro's BLE Proxy connection returned authenticated
Generic OnOff Status `0x01` from source `0x0003`. In the reverse direction, a
Get addressed to MC Pro node `0x0002` through only the Ace proxy returned status
`0x01` from source `0x0002`. Both fixtures advertised Mesh Proxy `0x1828` after
provisioning.

These cross-proxy responses prove both individual node state and bidirectional
proxy fallback for this two-member mesh. They also directly support charging
the complete mesh one physical BLE slot: changing which member supplies the
proxy bearer does not change the logical mesh identity or allocate a second
steady-state connection.

Proxy candidacy is power-state dependent. With both emitters physically Off,
only MC Pro continued advertising Mesh Proxy; Ace stopped advertising it. Both
nodes still answered authenticated group status through the MC Pro bearer.
Ble(e)p must therefore remember multiple candidates but choose among those
currently advertising, and it must not require every member to be a usable
proxy in order to treat the network as one device slot.

## Confirmed standard-model protocol behavior

The following Bluetooth Mesh Model messages produced authenticated responses
on the panel-owned mesh:

| Target | Request | Status | Confirmed protocol behavior |
| --- | --- | --- | --- |
| MC Pro | Generic OnOff Get `0x8201` | `0x8204` | Writable shadow state; reachability only |
| MC Pro | Generic OnOff Set `0x8202` | `0x8204` | Mutates shadow state; no emitter effect observed |
| Ace 25c | Generic OnOff Get/Set | `0x8204` | Transitioning shadow state; no emitter effect observed |
| Ace 25c | Generic Level Get `0x8205` | `0x8208` | Returned `0x7FFF` at full output |
| Ace 25c | Default Transition Time Get `0x820D` | `0x8210` | Returned `0x41`, one second |
| Ace 25c | Generic OnPowerUp Get `0x8211` | `0x8212` | Returned `0x01`, On |
| Ace 25c | Light Lightness Get/Set `0x824B`/`0x824C` | `0x824E` | Confirmed `0x8000`, then restored `0xFFFF` |

These are authenticated model responses, not physical-output observations.
Ace acknowledged an Off transition as `01 00 0A`: present On, target Off, with
approximately one second remaining. The settled Get returned `00`. The reverse
transition returned `00 01 0A`, followed by settled `01`. Lightness similarly
returned present `FFFF`, target `8000`, remaining `0A`, followed by settled
`8000`; the test then restored and confirmed `FFFF`.

Both nodes' Generic OnOff Servers were subscribed to group `0xC000`. One
Generic OnOff Get addressed to that group returned two statuses, sourced from
`0x0002` and `0x0003`. The same two-response result worked through either
fixture as the sole BLE proxy. This supports a low-traffic refresh strategy:
send one group Get, update each responding member by authenticated source
address, and expire only members that do not answer within the response window.

One group OnOff Set to Off through MC Pro produced immediate MC model status `00` and
Ace transition status `01 00 0A`; the following group Get returned `00` from
both sources. One group Set to On through Ace produced immediate MC status `01`
and Ace transition status `00 01 0A`; the final group Get returned `01` from
both. A mesh-level action can therefore use one command while still resolving
and displaying each member's model result independently. Physical emitter
effects were not observed during this unattended run.

A later operator-watched MC Pro test disproved physical correlation. With the
emitter physically Off, Generic OnOff Get returned On. Turning the emitter On
from the fixture did not change the model. Generic OnOff Set Off then returned
Off and a following Get remained Off, while the emitter visibly stayed On.
The MC Pro's Generic OnOff Server is therefore a writable shadow model. A later
operator-watched group Off also changed Ace's model from On toward Off, but
neither emitter changed. Generic OnOff is a shadow model on both tested
fixtures. It may prove node reachability but must be excluded from emitter
power display and physical group-control success.

Two retained-connection soaks alternated five unicast Gets per node. MC Pro as
the sole proxy returned 10/10 authenticated statuses; Ace as sole proxy also
returned 10/10. CoreBluetooth sometimes ended an attempted connection without
tool output before a usable session formed. Sequence numbers were reserved
before every attempt; no missing member responses were observed after a proxy
session successfully sent the requests.

## Sidus two-fixture connection evidence

The Sidus capture confirms the expected one-proxy-link architecture on real
Ace 25c and MC Pro fixtures:

- The Ace 25c used a temporary PB-GATT connection while being added. That link
  exposed Mesh Provisioning `0x1827` and closed after onboarding.
- Sidus opened a second connection to the MC Pro during the same setup flow.
  Its GATT database exposed Mesh Provisioning `0x1827` and Mesh Proxy `0x1828`.
  Sidus retained this connection after the Ace link closed.
- All later Ace and MC Pro controls used the retained MC Pro Mesh Proxy link:
  Write Without Response to Data In `0x2ADD`, with notifications from Data Out
  `0x2ADE`.
- The control portion contains 81 Mesh Proxy Data In writes and 34 Mesh Proxy
  Data Out notifications on that single retained connection. No dedicated Ace
  ACL connection remained while its control screen was exercised.
- Sidus closed the retained MC Pro connection after leaving the recorded
  control session. No second steady-state fixture link was required.

The Network PDUs are standard encrypted Bluetooth Mesh traffic, unlike
Zhiyun's proprietary cleartext gateway routing. Source, destination, opcode,
and vendor status fields cannot be recovered from HCI without the network and
application keys. Provisioning protects those keys, so they cannot be derived
from the snoop log alone. Ble(e)p provisions its own mesh and already has the
keys needed to authenticate and decode responses; importing an existing Sidus
network is outside the planned product scope.

The Sidus capture and panel-owned cross-proxy probes support accounting one
Sidus/amaran/Aputure mesh as one physical BLE session. The active proxy is a
replaceable bearer for the mesh, not a separate slot for each logical fixture.

A later panel-owned mixed-network probe added one Zhiyun X60RGB at unicast
`0x0004`. Using that X60RGB as the sole BLE Mesh Proxy, five vendor-power group
Gets produced five unique authenticated MC Pro Off statuses and three unique
Ace 25c Off statuses. Each status was forwarded twice with the same source and
network sequence, consistent with redundant relay paths; a receiver must apply
per-source replay protection before refreshing reachability. This proves the
standards-based proxy bearer can be shared across these brands when Ble(e)p
provisions them into the same network. It does not by itself merge the separate
Zhiyun `0xFEE9` application protocol into the current firmware runtime.

## Connection and state semantics

A connected Mesh Proxy proves that the controller has a bearer into the mesh;
it does not prove that every member is powered or reachable. The captured
traffic includes Data Out notifications correlated closely with some control
writes, including responses roughly 40-100 ms later, but encrypted HCI alone
cannot assign those replies to a node or decode their state.

Runtime state should therefore keep these facts separate:

- `meshConnected`: one selected proxy has a live BLE/GATT session;
- `nodeReachable`: that node recently produced an authenticated directed
  response;
- `modelPowerConfirmed`: decoded Generic OnOff model status reported on or off;
- `emitterPowerConfirmed`: a device-specific status path known to represent
  physical emitter output;
- `powerOptimistic`: a command was sent without matching status; and
- `lastSeen`: time of the last authenticated message from that member.

Reliable member-offline detection requires acknowledged Get/Set traffic,
decoded source addresses/statuses, and an explicit timeout. Group vendor-power
Get now returns separately sourced replies from both MC Pro and Ace 25c. A
proxy disconnect marks the bearer unavailable, not every fixture definitively
off.
Conversely, a silent member behind a healthy proxy must not remain indefinitely
"connected" merely because another fixture supplies the BLE bearer.

For this mesh, a group OnOff Get can refresh all members in one transmission.
Each authenticated source that answers becomes reachable with confirmed model
state; members absent after the bounded response window become stale/offline,
not implicitly Off. A three-byte OnOff Status carries model present state,
target state, and remaining transition time; the UI should show the target as
pending until a settled status or timeout arrives, but must not label it
physical power unless that model has been correlated with emitter behavior for
the fixture.

For both tested fixtures, ignore Generic OnOff as emitter state. A response
still refreshes `nodeReachable` and `lastSeen`, but only decoded vendor status
or another physically correlated source may update emitter power.

The tested vendor models provide that physical-power path. With both vendor
models subscribed to group `0xC000`, access payload
`26 8C 00 00 00 00 00 00 00 00 8C` physically turned both emitters Off and
`26 8D 00 00 00 00 00 00 00 01 8C` physically turned both On. Repeating the
Off command restored both fixtures to Off. These results were operator-watched;
the final Off state was also confirmed independently by authenticated vendor
status from both node addresses.

The physical-power Get is group access payload
`26 0E 00 00 00 00 00 00 00 00 0E`. Each fixture returns opcode `0x26` with
a checksum byte followed by nine status bytes. In the four correlated replies,
the first status byte was physical emitter power (`00` Off, `01` On), the
eighth of the nine status bytes was stored intensity `FA` (250, corresponding
to 100 percent) in both power
states, and the final byte distinguished Ace 25c (`01`) from MC Pro (`02`).
The intervening bytes remain unknown. Off readback was:

- MC Pro `E8 00 00 00 00 20 A4 28 FA 02`;
- Ace 25c `EB 00 00 00 00 80 56 1A FA 01`.

On readback was:

- MC Pro `E9 01 00 00 00 20 A4 28 FA 02`;
- Ace 25c `EC 01 00 00 00 80 56 1A FA 01`.

For each reply, the leading byte equals the additive checksum of the following
nine bytes. The `FA` value remaining unchanged while Off proves stored
intensity and emitter power are separate fields. The separate `0x0A` poll also
returned valid checksummed, source-specific but dynamically changing payloads;
its fields remain unresolved.

The group-Off test also proves that model `Off` is not `offline`: both nodes
reported settled Off while the same proxy session continued to route
authenticated statuses. Reachability must come from response freshness, never
from a power-like model value.

The production runtime now implements the read-only half of this path. It sends
the vendor Get to the panel-owned group every five seconds, authenticates and
decrypts complete unsegmented Proxy Network PDUs with the stored network and
application keys, maps each response source to its provisioned node, validates
the vendor checksum/profile, and records physical power plus `lastSeen`.
Per-source replayed sequence numbers are ignored. A node becomes stale after a
fifteen-second gap (three poll intervals); the shared Proxy connection remains
a separate bearer state. One missed group response therefore does not
immediately mark a fixture offline. Segmented Proxy SAR receive and IV Update
handling remain outside this bounded decoder.

## Captured vendor access payloads

The earlier Amaran capture corpus uses opcode `0x26`, followed by a one-byte
additive checksum and nine command bytes, for:

- power on/off;
- CCT from 2300 K through 10000 K, tint from -1000 through +1000 permille, and
  brightness from 0 through 100;
- packed `0xRRGGBB` color and brightness from 0 through 100;
- captured node-reset payload.

These vectors are not generically interchangeable across addressing modes or
models. Unicast power-off and brightness-50 writes to the Ace produced no
vendor response;
authenticated Generic OnOff and Light Lightness readback remained On and
`0xFFFF`. The same Amaran power-off vector produced no response or state change
on the MC Pro. However, the power vector sent to the subscribed vendor-model
group physically controlled both fixtures, and the `0x0E` group poll returned
correlated physical state. Ble(e)p may use only this confirmed group power
subset for the tested pair; CCT, RGB, brightness, and unicast equivalence remain
`Optimistic`. Standard model transactions remain useful for reachability but
cannot be treated as emitter state.

Native golden tests lock AES, CMAC, a known encrypted network packet, vendor power,
CCT, RGB, validation bounds, segmented configuration shape, checksummed store,
and durable sequence reservation. The group-addressed vendor power subset is
physically confirmed for Ace 25c and MC Pro; other vendor writes remain
`Optimistic`. Decoded standard status responses can confirm the corresponding
model field and node reachability, not automatically physical output.

## Hardware gate

For each Pano 60c, Pano 120c, and Ace 25c, verify provisioning, composition,
configuration statuses, several CCT/tint/RGB/brightness combinations, reboot
recovery, proxy fallback, interrupted configuration, sequence-number
continuity, mixed-device sequences, and reset followed by the return of
provisioning advertisements.
Record latency, dropped events, heap, and reconnect stability in
`docs/progress.md`.

The Sidus capture closes the external-app transport question for Ace 25c and
MC Pro. The panel-owned run closes PB-GATT provisioning, composition-driven
AppKey/model binding, group subscription, acknowledged standard-model
transactions, cross-member routing, and proxy fallback for this Ace/MC pair.
It closes group physical power Set/Get for this Ace 25c/MC Pro pair. It does
not close physical lightness/color behavior, the Pano target gate, or other
vendor-property control for these firmware versions.
