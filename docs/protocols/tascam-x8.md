# Tascam Portacapture X8 BLE protocol

Status: `Research`, with record start/stop and the transition events below
confirmed from an annotated over-the-air capture.

## Evidence

The primary fixture is `docs/protocols/dumps/tascam_x8.pcapng` (SHA-256
`115e77bcc91ca2c184439115df97ad0459ac8452018ce0e08bdde6568918fd51`).
It was captured with an nRF52840 sniffer while the official Portacapture
Control app operated a Portacapture X8 through an AK-BT1. Packet comments mark
record start/stop from both the phone and the recorder, input toggles, and mode
changes.

The controlled reconnect fixture is
`docs/protocols/dumps/tascam_x8_reconnect.pcapng` (SHA-256
`7d095c94a454827778f3ecc86778b70e2109269f2e47acd0383c997f019ec783`).
It contains annotated reconnects while recording and while stopped, both with
the recorder in Manual Mixer mode.

The capture identifies the AK-BT1's Device Information values as:

- manufacturer: `u-blox`;
- model: `ANNA-B1`;
- firmware revision: `4.0.0-004T`;
- software revision: `4.0.0-004`.

These describe the Bluetooth module, not the recorder firmware.

## Discovery and connection

- The scan response name is `Portacapture X8`.
- The captured address is public, but implementations must retain the
  advertised address type rather than assume it.
- No Security Manager Protocol exchange or encrypted ATT traffic appears in
  the capture. The official app connects directly instead of creating an
  operating-system bond.
- The custom primary service is
  `2456e1b9-26e2-8f83-e744-f34f01e9d701`.
- Data characteristic:
  `2456e1b9-26e2-8f83-e744-f34f01e9d703` (captured value handle `0x0019`,
  read/write/notify).
- Session characteristic:
  `2456e1b9-26e2-8f83-e744-f34f01e9d704` (captured value handle `0x001c`,
  write/notify).

Handles are included only to correlate the fixture. Code must discover and use
the UUIDs.

The official app enables notifications on both characteristics, writes `fe` to
the session characteristic, and receives `10`. It then sends a data snapshot
query bundle. While connected it writes `7f` to the session characteristic
roughly every 6-8 seconds. This keepalive is required by the initial driver
implementation; the exact timeout remains unmeasured.

## Data framing

The data characteristic carries a stream of zero-delimited COBS frames:

```text
00 <COBS-encoded payload> 00
```

One ATT value may contain multiple frames, and a frame may span ATT
notifications. Parsers therefore operate on a byte stream rather than treating
an ATT notification as a packet.

Decoded payloads begin with ASCII `DR` (`44 52`). Most observed command and
state payloads are 14 or 18 bytes after COBS decoding. There is no checksum in
the observed envelope.

Example:

```text
wire:    00 05 44 52 10 41 02 0b 01 01 01 01 01 01 01 01 00
decoded: 44 52 10 41 00 0b 00 00 00 00 00 00 00 00
```

COBS uses the standard rule: each nonzero code byte gives the distance to the
next inserted zero. Empty delimiter pairs are ignored.

## Record commands

Both commands are written with response to the data characteristic.

### Start

Confirmed at packets 27818, 30467, 47832, 58172, and 62670:

```text
decoded: 44 52 10 41 00 0b 00 00 00 00 00 00 00 00
wire:    00 05 44 52 10 41 02 0b 01 01 01 01 01 01 01 01 00
```

### Stop

Confirmed at packets 29550/29552, 32132/32184, 49149, 59348, and 63830:

```text
decoded: 44 52 10 41 00 08 00 00 00 00 00 00 00 00
wire:    00 05 44 52 10 41 02 08 01 01 01 01 01 01 01 01 00
```

Some app sessions issue the stop command more than once. The first driver
tranche sends it once and waits for recorder state instead of reproducing that
UI behavior.

## Recorder-confirmed transitions

Start actions from both the app and the recorder produce:

```text
decoded: 44 52 20 20 24 01 00 00 00 00 00 00 00 00 00 00 00 00
wire:    00 07 44 52 20 20 24 01 01 01 01 01 01 01 01 01 01 01 00
```

This is followed about 90-120 ms later by the corresponding release/clear
event with byte 5 changed from `01` to `00`. Confirmed examples are packets
27825/27833 for an app start and 33744/33750 plus 38492/38498 for physical
starts.

Stop actions from both the app and the recorder produce:

```text
decoded: 44 52 10 20 08 00 00 00 00 00 00 00 00 00 00 00 00 00
wire:    00 06 44 52 10 20 08 01 01 01 01 01 01 01 01 01 01 01 00
```

Confirmed examples are packet 29557 for an app stop and packets 36748 and
39875 for physical stops. Because physical controls emit the same events
without a preceding app write, these notifications—not the ATT Write
Response—are the authoritative transition evidence.

## Current recording status

The controlled reconnect capture identifies `DR 20 20 00` byte 5 as the
current transport state:

```text
recording: 44 52 20 20 00 81 00 00 00 00 00 00 00 00 00 00 00 00
stopped:   44 52 20 20 00 10 00 00 00 00 00 00 00 00 00 00 00 00
```

Recording reconnect packet 13038 reports `0x81`; stopped reconnect packet
16627 reports `0x10`. The same field changes `0x10 -> 0x82 -> 0x81` during the
captured start and `0x81 -> 0x82 -> 0x10` during the captured stop. `0x82` is
therefore treated as transitional and does not overwrite the last confirmed
state. The stable `0x81` and `0x10` values restore confirmed state after
reconnect.

## Initial snapshot queries

After session setup the app writes a bundle of COBS-framed `DR` queries,
beginning at packet 26202. The capture proves that these populate mixer,
transport, media, and device fields, but most field meanings are outside the
record-only tranche.

The implementation replays the captured bundle to put the AK-BT1 session into
the same reporting mode as the official app. Unknown responses are ignored.
The confirmed transition events and `DR 20 20 00` current-state values mutate
recording state.

The recurring decoded request `44 52 30 41 0f 76 ...` triggers a broad
multi-frame refresh. No direct matching response or stable recording field has
been proven, so it must not be described as a recording-state query.

## Confidence and limitations

Confirmed:

- AK-BT1 requirement and advertised X8 name;
- custom UUIDs and notification setup;
- COBS stream envelope;
- session open and keepalive bytes;
- record start/stop writes;
- recorder-originated start/stop transition events;
- current recording/stopped state after reconnect.

Research:

- whether the UUIDs and payloads are shared with other Portacapture models;
- session timeout and keepalive tolerance;
- behavior across other X8 and AK-BT1 firmware versions;
- complete meaning of snapshot, meter, media, battery, and mixer frames;
- failure responses for full media, write protection, or unavailable record
  mode.

An ATT write response is not proof of physical record success. Hardware
verification must confirm both a transition notification and the actual file
operation on the recorder.
