# Zhiyun MOLUS X60RGB Bluetooth evidence

Status: `Experimental`. Advertising, PB-GATT onboarding, direct control
initialization, CCT, power, brightness, hue, and saturation were decoded from
one Android host-HCI session. Firmware use of the captured vectors is built and
host-tested; panel-originated control remains a hardware gate.

## Evidence boundary

The source was an Android Bluetooth HCI snoop log captured while ZY Vega added
and controlled one MOLUS X60RGB. The relevant rotated log has SHA-256
`052ee361c6223db63951045c6318fb2a3a011176472cee76116e4d2f830dec12`.
The raw bug report and snoop logs remain outside the repository because they
contain radio addresses, nearby device names, phone identity, and Bluetooth
Mesh provisioning material.

A later ZY Vega session added this X60RGB beside one X100 and then controlled
both. Its relevant rotated HCI log has SHA-256
`d61d04de34eec7d8dabc5d8284cf0e3ec3431299fbec0d58f3fc7a8c8af47430`.
It provides the shared-gateway evidence below; the raw companion video and
bugreport remain outside the repository.

The advertised product model is independently identified as PLX104 in the FCC
filing for the MOLUS X60RGB. ZHIYUN publishes a 60 W maximum output, 0-100%
brightness, and 2700-6500 K CCT range.

## Advertising and provisioning

- Factory-reset advertising used local name `X104_` plus a device-specific
  suffix, company ID `0x0905`, manufacturer marker `plx104`, and Mesh
  Provisioning service `0x1827`.
- ZY Vega provisioned the fixture through standard PB-GATT. It disconnected
  and reappeared with Mesh Proxy service `0x1828` while retaining the same
  product-qualified advertising fields.
- The same proprietary `0xFEE9` service and `...9600` write / `...9601` notify
  characteristics used by the X100 were present after provisioning.

## Shared vendor protocol

The X60RGB reuses the X100 `24 3c` envelope, little-endian body length,
sequence and command fields, and CRC-16/XMODEM. The captured X60RGB member used
the state payload prefix `01 80`, while the captured X100 member used `00 80`.
Reads end that prefix with `00`; writes and captured write replies use `01`.
The leading byte is no longer treated as proven model identity: the
multi-fixture capture shows that it routes a member through a shared gateway,
while its allocation rule remains a `Hypothesis` because model and add order
covary.

Initialization starts at sequence 2 and queries identity `0x2003`, firmware
`0x8001`, status `0x2001`, mode `0x0006`, CCT, power, and brightness. The
captured identity contained `plx104`, and firmware was `1.7.0`.

| Command | Value | Evidence |
| --- | --- | --- |
| `0x1001` | little-endian float32 brightness | Captured reads and writes; replies retained whole-percent values |
| `0x1002` | little-endian uint16 CCT | Captured writes and replies; retained values use 100 K steps |
| `0x1004` | little-endian float32 hue in degrees | Captured 0-300 degree writes and correlated replies |
| `0x1005` | little-endian float32 saturation percent | Captured 0-100% writes and correlated replies |
| `0x1008` | one-byte power | Captured off/on writes and correlated replies |

The operator-provided swatch order supplies direct semantic labels for the
chronological HSI writes:

| Swatch | Captured hue | Captured saturation |
| --- | ---: | ---: |
| Red | 0 degrees | 100% |
| Blue | 240 degrees | 100% |
| Magenta | 300 degrees | 100% |
| Cyan | 180 degrees | 100% |
| Orange | 30 degrees | 100% |
| Green | 120 degrees | 100% |

The capture then returns to red. Subsequent one-parameter ramps independently
identify `0x1005` as saturation and `0x1001` as brightness; the final `0x1008`
writes turn power off and on. This ordering rules out an RGB-channel or shared
intensity interpretation for `0x1004`/`0x1005`.

Sanitized golden writes copied exactly from the capture:

```text
CCT 5100 K, seq 0x000b
24 3c 0b 00 00 01 0b 00 02 10 01 80 01 ec 13 4d 26

hue 240 degrees, seq 0x013b
24 3c 0d 00 00 01 3b 01 04 10 01 80 01 00 00 70 43 b6 5e

power off, seq 0x020b
24 3c 0a 00 00 01 0b 02 08 10 01 80 01 00 a8 2a
```

Unlike the captured X100 session, X60RGB setters produced correlated replies
with the same sequence and command. Ble(e)p uses those captured replies to
confirm RGB hue, saturation, and brightness in that captured semantic order.
CCT and power retain the shared read-after-write path so both supported models
use the same conservative state publication rule.

## Routed control through an X100

In the later two-fixture session, the X60RGB's final direct onboarding/identity
connection closed before its control screen was exercised. No subsequent ACL
traffic used that connection handle. ZY Vega nevertheless sent X60RGB
brightness, CCT, hue, saturation, and power commands with the `01 80 01`
prefix through the X100's retained `0xFEE9` write characteristic. The X100's
own controls used `00 80 01` on that same characteristic.

This is direct evidence that a Zhiyun fixture can proxy proprietary control for
another mesh member without using standard Mesh Proxy Data In for each access
command. It also means the X60RGB radio address is an onboarding or gateway
candidate identity, not a requirement for a dedicated steady-state BLE link.

The capture does not yet prove a reliable routed online test for the X60RGB.
Initial state was obtained while its temporary direct link existed, and rapid
routed slider writes did not each have an immediately correlatable response.
Per-member reachability and confirmed power therefore require a decoded routed
reply or timeout policy rather than inference from the shared gateway link.

## Cross-brand panel-owned mesh probe

A factory-reset X60RGB was provisioned into the same private panel-owned test
network as one Aputure MC Pro and one amaran Ace 25c. It received unicast
address `0x0004`, reported one element, and reappeared with both Mesh Proxy
`0x1828` and proprietary control `0xFEE9` available. Sidus-family AppKey and
model configuration was deliberately skipped for the X60RGB.

The X60RGB then served as the sole BLE Mesh Proxy for five read-only
group-addressed vendor-power polls. The proxy returned five unique,
authenticated MC Pro Off statuses and three unique Ace 25c Off statuses, with
each response duplicated under the same source/sequence through redundant
relay paths. Separately, the captured `0xFEE9` initialization against that same
X60RGB connection identified `plx104` and read 4400 K, Power On, and 12%
brightness. A Python camera frame independently showed the identified Zhiyun
emitting while the two Sidus fixtures remained dark.

This proves that one X60RGB BLE connection can expose both the standard Mesh
Proxy bearer for cross-brand traffic and the proprietary Zhiyun control
service. It supports the one-slot-per-panel-owned-mesh architecture now used by
the shared embedded gateway runtime.

## Selector-zero and coordinated three-light proof

The same X60RGB was later controlled as the first Zhiyun member of the mixed
panel-owned mesh, after MC Pro and Ace 25c already occupied unicast addresses
`0x0002` and `0x0003`. Selector `1` did not retain the requested hue, while
selector `0` independently read back all four requested fields:

- hue 240 degrees;
- saturation 100%;
- brightness 5%;
- power On.

A camera frame showed the left X60RGB blue/violet while MC Pro was red and Ace
25c was cyan-green. After the observation interval, selector-0 power Off read
back Off and a second camera frame showed all three fixtures dark. This
disproves the earlier model-derived X60RGB selector assumption: selector `0`
can address an X60RGB, and the value is independent of standards-mesh unicast
address. It supports the narrower hypothesis that selectors are ordinal among
Zhiyun members. A second same-model fixture is still required to prove that
allocation rule.

The embedded driver now persists the selector in mesh-node schema 2. Existing
schema-1 Zhiyun records receive selectors in saved-node order during load.
Saved Zhiyun sessions attach `0xFEE9` to the mesh runtime's native proxy client,
so standard Mesh Proxy traffic and proprietary control share one retained BLE
connection. Onboarding still uses its temporary PB-GATT connection.

## Implemented boundary

`Zhiyun Light` is one multi-instance driver rather than separate X100 and
X60RGB catalog entries. Each Add Device operation accepts either product,
stores another normal instance, and selects its protocol profile from the
product-qualified advertisement and identity reply. Both models reuse the same
PB-GATT provisioner, mesh repository, retained BLE lifecycle, frame scanner,
and CCT/power client. The X60RGB additionally exposes RGB controls translated
to the captured HSI writes.

The prior model-derived selector and per-instance steady-client limitations are
removed. A second same-model fixture is still required to validate ordinal
selector allocation and simultaneous routed state handling.

The raw capture did not establish effect-mode commands, current-mode readback,
reset, interrupted provisioning recovery, multiple simultaneous fixtures, or
firmware-version compatibility. Those remain explicit hardware gates.
