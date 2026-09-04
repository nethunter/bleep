# Bluetooth Mesh lab probes

These host-side research tools exercise a panel-owned Bluetooth Mesh without
changing firmware. They are intentionally separate from the production
runtime and currently consume the JSON state created by the sibling
`studio-lighter` research client.

Keep the state file outside the repository with mode `0600`: it contains the
network key, application key, and node device keys. The tools never print
those keys, but raw proxy traffic can still contain stable mesh metadata.

Install the optional dependencies in a separate environment:

```sh
python3 -m pip install -r tools/mesh_lab/requirements.txt
```

The probe used for the 2026-08-07 MC Pro run supports the individual
configuration operations needed to diagnose provisioning independently:

```sh
PYTHONPATH=/path/to/studio-lighter/src python3 tools/mesh_lab/mesh_probe.py \
  --state /private/path/state.json --address COREBLUETOOTH-IDENTIFIER composition-get

PYTHONPATH=/path/to/studio-lighter/src python3 tools/mesh_lab/mesh_probe.py \
  --state /private/path/state.json --address COREBLUETOOTH-IDENTIFIER appkey-add

PYTHONPATH=/path/to/studio-lighter/src python3 tools/mesh_lab/mesh_probe.py \
  --state /private/path/state.json --address COREBLUETOOTH-IDENTIFIER \
  vendor-bind --company-id 0x03f6 --model-id 0x1000

PYTHONPATH=/path/to/studio-lighter/src python3 tools/mesh_lab/mesh_probe.py \
  --state /private/path/state.json --address COREBLUETOOTH-IDENTIFIER sig-bind

PYTHONPATH=/path/to/studio-lighter/src python3 tools/mesh_lab/mesh_probe.py \
  --state /private/path/state.json --address TARGET-IDENTIFIER \
  --proxy-address GATEWAY-IDENTIFIER onoff-get
```

`--address` selects the logical node and its unicast destination from the state
file. `--proxy-address` independently selects the physical BLE Mesh Proxy. Use
different values to prove that a node is reachable through another fixture's
single proxy connection.

Additional read-only operations are `level-get`, `transition-get`,
`onpowerup-get`, and `lightness-get`. `onoff-on`, `onoff-off`, and
`lightness-set --lightness VALUE` send acknowledged messages with a transaction
ID derived from the durably reserved sequence number. Always restore a fixture
to its intended state after a lab run.

`soak --other-address SECOND --count 20 --interval 1` alternates acknowledged
Generic OnOff Get messages between two logical nodes while retaining one BLE
connection to `--proxy-address`. It reserves every sequence number before the
connection attempt and prints the encrypted notifications for offline decode.

`vendor-power-soak --count 20 --interval 1` repeats the confirmed read-only
group power poll over one retained proxy connection. A healthy two-member mesh
should return two authenticated, independently sourced statuses per request.
It does not change emitter state.

`sig-subscribe` adds the target node's Generic OnOff Server to the mesh state's
group address. After subscribing each member, `group-onoff-get` sends one Get
to that group; each reachable member should return its own sourced status.
`group-onoff-on` and `group-onoff-off` send one acknowledged state change to
the group; use `group-onoff-get` afterward to resolve every member separately.

`vendor-subscribe --company-id ID --model-id ID` subscribes a
composition-verified vendor model to the mesh group. `raw-unicast --access HEX`
and `raw-group --access HEX` are restricted to short, already-understood lab
payloads; do not use them for destructive or unknown opcode fuzzing.

After subscribing each tested vendor model to the panel-owned group,
`vendor-power-get`, `vendor-power-on`, and `vendor-power-off` use the confirmed
Ace 25c/MC Pro group protocol. The Set operations change real emitter power;
`vendor-power-get` returns one authenticated, source-addressed status per
reachable member. These commands deliberately target the mesh group: the same
captured power payload sent to either node's unicast address had no observed
effect. Run a Get after a Set and treat missing members as stale, not Off.

`listen --listen 20 --proxy-address GATEWAY` opens a passive Mesh Proxy
notification window without sending an access message. Use it while operating
fixture-local controls to capture unsolicited model/vendor publications.

`appkey-add` uses segmented lower transport. This is the important difference
from the old research sender, which incorrectly emitted the 20-byte Config
AppKey Add access message as one unsegmented PDU.

Use `decode_notifications.py` with the same private state file and one or more
hex Mesh Proxy notifications to decrypt and label responses. It intentionally
prints decoded opcodes and parameters, but not keys. Known configuration,
Generic OnOff/Level/OnPowerUp/Default Transition Time, and Light Lightness
statuses also receive semantic present/target/transition labels.
The correlated Ace 25c/MC Pro vendor power status is labeled with emitter
power, stored intensity, product profile, and checksum validity.

`zhiyun_probe.py` performs the captured read-only `0xFEE9` initialization and
state sequence against a provisioned X100/X60RGB gateway. It reports only the
resolved model and decoded light values; raw identity payloads are intentionally
not printed. Pass the member's persisted selector. The first Zhiyun member in
the mixed panel-owned test mesh was an X60RGB and used selector `0`:

```sh
python3 tools/mesh_lab/zhiyun_probe.py \
  --address COREBLUETOOTH-IDENTIFIER --selector 0
```

Pass `--power on` or `--power off` only when a deliberate physical state
change is intended. The probe follows the write with an independent readback.
X60RGB also accepts `--hue`, `--saturation`, and `--brightness`; each setter is
followed by an independent readback.

`decode_zhiyun_capture.py` timelines a ZY Vega Android HCI snoop log (from an
`adb bugreport`) for Zhiyun mesh research. It requires `tshark` on `PATH` and
prints one line per event: LE connect/disconnect with the peer address, PB-GATT
provisioning PDUs, Mesh Proxy/Network PDUs (classified but left encrypted), and
the cleartext `0xFEE9` frames decoded into direction, sequence, command, member
selector, and payload. Pass `--video-start HH:MM:SS` (phone local time of the
screen recording's first frame) to print the matching recording offset beside
every event, and `--all-att` to include unrecognized ATT traffic.

```sh
python3 tools/mesh_lab/decode_zhiyun_capture.py /private/path/btsnoop_hci.log \
  --video-start 20:23:24
```

The tool prints raw radio addresses, so keep its output outside the repository
with the rest of the private capture material. The two-fixture capture decoded
with it proved that the vendor routes a second Zhiyun member's control through
the first fixture's retained `0xFEE9` gateway link, each member on its own
stable selector; see `docs/protocols/zhiyun-x100.md`.
