# Zhiyun MOLUS X100 Bluetooth evidence

Status: `Experimental`. Transport and framing were decoded from one Android
host-HCI session; PB-GATT provisioning and deterministic direct power/
brightness/CCT readback were first reproduced from a laptop and are now
implemented in Ble(e)p. Panel-originated hardware verification remains open.

## Evidence boundary

The source was an Android Bluetooth HCI snoop log captured while ZY Vega added
and controlled one MOLUS X100. The relevant rotated snoop log has SHA-256
`2d677dac86f0896026add3ba1d41910ca663819291f50dd30491379caa6bab1f`.
The raw bugreport and snoop logs stay outside the repository because they also
contain stable radio addresses, nearby device names, phone identity, Bluetooth
Mesh provisioning material, and unrelated traffic.

The extracted vectors below remove the captured radio address, changing local
name suffix, product identifier response, provisioning keys, and encrypted
Mesh traffic. ZHIYUN's published specifications give the X100 a 0-100%
brightness range and 2700-6500 K CCT range:
<https://www.zhiyun-tech.com/en/product/param/768?page=second_nav&source=param&type=website>.

ZY Vega's captured direct CCT writes include 50 K boundaries such as 2950,
3150, 3900, and 5450 K, but live panel testing showed this X100 quantizing a
4550 K write to 4500 K on readback. Ble(e)p therefore snaps actual control
targets to the nearest 100 K before writing and verifies that canonical value
exactly. The captured 50 K writes remain transport evidence, not evidence that
the fixture retains those values.

## Advertising and onboarding

- The captured local name was `PL105_XXXX`; the suffix is device-specific.
- Manufacturer data used company ID `0x0905` and began with ASCII `pl105\0`.
  Treat the product marker plus an expected service as stronger identity than
  the local name suffix or a stable radio address.
- Before provisioning the light advertised Mesh Provisioning service `0x1827`.
- PB-GATT used Data In `0x2ADB` and Data Out `0x2ADC`.
- The capabilities PDU reported one element, P-256 support, no public-key,
  output-OOB, or input-OOB method, and advertised static OOB as available.
  Static-OOB capability does not require its use: ZY Vega's Provisioning Start
  selected no-OOB authentication, and a laptop provisioner reproduced that
  standard no-OOB path successfully.
- After provisioning the light disconnected, advertised Mesh Proxy service
  `0x1828`, and reconnected. Mesh Proxy used Data In `0x2ADD` and Data Out
  `0x2ADE`. ZY Vega enabled notifications and exchanged encrypted Network PDUs;
  their access-layer purpose is not decoded by this capture.

The proprietary control service was present alongside the standard Mesh
services in both GATT layouts:

| Role | UUID | Properties |
| --- | --- | --- |
| Service | `0xFEE9` | Primary service |
| App to light | `d44bc439-abfd-45a2-b575-925416129600` | Write, Write Without Response |
| Light to app | `d44bc439-abfd-45a2-b575-925416129601` | Notify |

ZY Vega negotiated an ATT MTU of 320 and used Write Without Response for the
vendor frames. No SMP pairing or encrypted ATT exchange was observed on the
post-provision control connection. The standard Mesh Proxy and `0xFEE9`
channels are distinct even though Wireshark may mis-dissect a `0x24` vendor
frame on the latter as an unknown Mesh Proxy PDU.

## Vendor frame

All extracted commands and replies use this envelope:

```text
offset  size  field
0       2     magic: 24 3c
2       2     body length, uint16 little-endian
4       2     request marker 00 01; response marker 01 00
6       2     sequence, uint16 little-endian
8       2     command, uint16 little-endian
10      n     command payload
10+n    2     CRC-16/XMODEM over the body only, stored little-endian
```

Total frame size is `body_length + 6`. The captured app increments the
16-bit sequence for each request. Duplicate-sequence behavior and wraparound
were not tested. Replies reuse the request sequence and command. Unlike some
published Zhiyun gimbal captures, X100 replies retained magic `24 3c`; the
two-byte marker distinguished the captured direction.

CRC example:

```text
frame:      24 3c 06 00 00 01 02 00 03 20 08 36
CRC input:              00 01 02 00 03 20
XMODEM:                                         0x3608
wire CRC:                                        08 36
```

This matches the CRC family independently documented for the same `0xFEE9`
Zhiyun envelope in
<https://petermaguire.xyz/posts/zhiyun-weebil-s-ble-protocol/>.

## Captured light controls

Multi-byte values below are little-endian. `00 80 00` is the captured read
prefix and `00 80 01` is the captured write prefix for the three light-state
commands.

| Command | Read request payload | Read response payload | Write payload | Evidence |
| --- | --- | --- | --- | --- |
| `0x1001` | `00 80 00` + four zero bytes | `00 80 00` + float32 brightness | `00 80 01` + float32 brightness | Initial captured read returned 3.0; slider writes encoded 8.6 through 54.4 and back down to 13.4 |
| `0x1002` | `00 80 00` + two zero bytes | `00 80 00` + uint16 CCT | `00 80 01` + uint16 CCT | Initial read returned 5600; slider writes covered 2950 through 6500 K |
| `0x1008` | `00 80 00` + one zero byte | `00 80 00` + one-byte power | `00 80 01` + `00`/`01` | Captured consecutive app power-off and power-on writes |

Golden setter vectors, including captured sequence and CRC:

```text
brightness 51.0%, seq 0x00d9
24 3c 0d 00 00 01 d9 00 01 10 00 80 01 00 00 4c 42 31 69

CCT 5600 K, seq 0x016b
24 3c 0b 00 00 01 6b 01 02 10 00 80 01 e0 15 41 16

power off, seq 0x0172
24 3c 0a 00 00 01 72 01 08 10 00 80 01 00 9b 6d

power on, seq 0x0173
24 3c 0a 00 00 01 73 01 08 10 00 80 01 01 69 3a
```

Golden initial-read replies:

```text
brightness 3.0, seq 0x0006
24 3c 0d 00 01 00 06 00 01 10 00 80 00 00 00 40 40 28 f3

CCT 5600 K, seq 0x0007
24 3c 0b 00 01 00 07 00 02 10 00 80 00 e0 15 9b 9c

power on, seq 0x0008
24 3c 0a 00 01 00 08 00 08 10 00 80 00 01 fc 53
```

The rapid setter writes have no matching per-write reply, so successful GATT
writes alone are not state confirmation. Live testing established a
deterministic path: write the requested values, then issue correlated reads for
`0x1001`, `0x1002`, and `0x1008` and require their device-originated replies to
match. One run read 13.0% / 5600 K / on, confirmed a change to 10.0% / 3200 K /
on, and then confirmed restoration to the original values. This verifies
protocol state, though independently observed optical output remains a
separate hardware check.

## Setup and unknown commands

ZY Vega queried these commands immediately after subscribing to `0xFEE9`.
Live testing also found that sending the same initialization sequence, starting
at sequence 2, was required before light-state queries produced notifications:

| Command | Captured result | Status |
| --- | --- | --- |
| `0x2003` | Product/model identity containing `pl105`; identifier bytes omitted | `Confirmed`; use to validate the fixture and initialize the direct-control session |
| `0x8001` | Prefix `80`, then firmware string `1.8.4\0` | `Research` |
| `0x2001` | Two-byte result ending in decimal 101 | `Unknown`; possibly power/battery telemetry |
| `0x0006` | One-byte zero result | `Unknown` |
| `0x1201` | Polled about every eight seconds; result `00 80 65` | `Hypothesis`: health, supply, or battery telemetry; do not expose yet |
| `0x1202` | Prefix `00 80`, then firmware string `1.8.4\0` | `Research` |
| `0x1101` | One request near the end of the session, with no captured reply | `Unknown` |

The light also sent one unsolicited pre-provision `0x2005` reply. Its purpose
is unknown.

## Implemented boundary

The driver accepts a factory-reset X100 advertising `0x1827`, provisions it
with the shared panel-owned no-OOB PB-GATT engine, stores its Device Key and
unicast allocation in the existing versioned mesh store, then rediscovers
`0x1828`. It discovers `0xFEE9` directly, subscribes to `...9601`, queries
power, brightness, and CCT, then writes only the three captured setters. It
does not decode or originate Bluetooth Mesh Network PDUs for direct controls.

Factory-reset onboarding has now been reproduced using standard no-OOB PB-GATT:
the one-element light received unicast address 2 in a fresh temporary mesh,
disconnected, and changed from `0x1827` to `0x1828`. AppKey/model configuration
was not needed for the separate direct `0xFEE9` control path. A production
The shared repository preserves the existing `AMSH` version-1 NVS schema while
making its multi-vendor ownership explicit. Durable save happens before the
provisioning link is closed. Recovery when the light accepts Provisioning Data
but completion or persistence is interrupted still requires reset/retry and
hardware verification back to `0x1827`. Before
enabling either tranche by default, verify advertisement matching without a
stable address, 0/100% brightness, 2700/6500 K bounds, power off/on, reconnect,
retention/eviction, multiple X100 instances, and coexistence with the existing
retained BLE and Home Assistant sessions.
