# Insta360 Mini Remote protocol notes

Status: **Implemented candidate**, capture-backed on Insta360 X5 and X6 cameras
with an Insta360 Mini Remote. Ble(e)p exposes Mini Remote separately from the
[GPS Remote](insta360-gps-remote.md), with shared GATT/session/state code and
profile-specific advertising and shutter vectors. The current flashed Mini
build uses the captured exact `Insta360 Mini Remote` name after the custom-name
compatibility probes documented below failed discovery. Native and target
builds pass, and exact-name discovery plus the X6 power-off/UI-off/wake
lifecycle are panel-confirmed. Explicit Start/Stop and reported status still
need a Ble(e)p panel run before X6 can be promoted from candidate. The X6
observations do not add X6 to the supported-device list.

## Evidence and confidence

The raw nRF Sniffer captures and synchronized phone video remain outside the
repository because they contain device identifiers and unrelated radio
traffic. These hashes identify the private source material used for this note:

| Source | Capture details | SHA-256 |
| --- | --- | --- |
| `x5_mini_control.pcapng` | 45,275 packets; 2026-08-11 16:45:30 to 16:54:43; annotated video start, stop, mode change, and photo actions | `388b7398e620e622dccbb01a17788f2592993507ea0f027c4043c4a511bede4f` |
| `x5_mini_control_on_off.pcapng` | 25,753 packets; 2026-08-11 17:00:46 to 18:15:31; annotated power-off and power-on actions | `8546844db121b490943a623de5fd372336b4de08bcfc69e7671627fbcf819742` |
| `PXL_20260811_204251224.mp4` | synchronized visible behavior; recording began before packet capture, so only the interval after capture start was correlated | `1cadb78adfd7b6c97b2b0f5f5777a1e56ec2071b4ff66bb6efc926ac61a97859` |
| `identify.pcap` | 63,671 packets; 2026-09-04 13:59:56 to 14:02:16; X6/Mini advertising identification | `11f7838b754f2ffca9eb471fbb53d87a18d531646def51463fae5f5921c265e4` |
| `x6-mini-channel37.pcap` | 4,224 packets; 2026-09-04 14:07:34 to 14:16:55; first followed X6 link, remote and camera-local recording actions | `40fba5154895a02079948ca3f79f99279905e37fbe49f57c27b083438d669c38` |
| `x6-mini-reconnect-3.pcap` | 3,686 packets; 2026-09-04 14:18:12 to 14:22:35; clean X6 reconnect, initialization, two remote shutter notifications, and camera state writes | `7a219f7ce7969c51636ec04627f41a37c2eca01d989bf1c6222d78882851a7d1` |
| `PXL_20260904_182751452.jpg` | 2268x4032 user-supplied third-party controller manual page; separate GPS/Mini mode selection and claimed applicable-model lists | `a4cba64e143ee9d57d5d856b33b75bf0f4564b5be83cbd590f13dffa3ea096bd` |

Confidence labels used below:

- **Confirmed (X5 capture):** observed repeatedly in the annotated X5/Mini
  Remote traffic and correlated with the camera or remote UI.
- **Operator-confirmed (X5):** already exercised successfully by Ble(e)p on
  the physical X5, but not introduced by these Mini Remote captures.
- **Captured (X6):** present in a valid-CRC passive X6/Mini packet trace.
- **Operator-correlated (X6):** captured while the operator performed the
  stated action and reported the physical X6 result. There is no synchronized
  video for this session.
- **Research:** capture-backed interpretation that still needs a controlled
  Ble(e)p panel test.
- **Unknown:** bytes or behavior for which the captures do not justify a
  meaning.

## Advertising identity

The Mini Remote uses connectable legacy advertising with these fields:

| Packet | Field | Captured value |
| --- | --- | --- |
| Advertising data | Flags | `0x06` (general discoverable, BR/EDR unsupported) |
| Advertising data | Appearance | `0x0180` (Generic Remote Control) |
| Advertising data | Complete name | `Insta360 Mini Remote` |
| Scan response | Complete 16-bit service list | `0x1812` (HID) |
| Scan response | TX power | `0 dBm` |

The X6 identification capture matched the same fields. The Mini does **not**
advertise `0xCE80`. Advertising that proprietary service with a GPS Remote name
causes the X5 to select its legacy GPS-remote behavior, which does not supply
the Mini capture-state stream. Ble(e)p therefore exposes `0xCE80` in its shared
GATT table while advertising the Mini's name, appearance, and HID service
identity.

### Custom-name compatibility probe

The captured `Insta360 Mini Remote` name occupies 21 payload bytes. A first
diagnostic build changed only the complete-name field to `Ble(e)p Remote`; the
X6 did not show it. That rejects the prefix-free custom name for discovery.

The second diagnostic build preserved the vendor prefix and used the 21-byte
name `Insta360 Bleep Remote`:

```text
02 01 06 03 19 80 01 16 09 49 6E 73 74 61 33 36 30 20 42 6C 65 65 70 20 52 65 6D 6F 74 65
```

Appearance `0x0180`, the HID scan response, GATT table, commands, and state
decoder were unchanged. The X6 also did not show this identity. Because the
complete-name field was the only changed discovery input in both probes, the
observed X6 firmware appears to require the exact `Insta360 Mini Remote` name,
not merely an `Insta360` prefix. Ble(e)p therefore reverted to the captured
29-byte advertising packet, which the operator confirmed restored X6 discovery
and connection. This conclusion is limited to tested X6 discovery; it does not
establish how every Insta360 model implements its filter.

## GATT roles

The camera acts as the BLE central and connects to a remote peripheral exposing
service `0xCE80`. This role reversal is important: Ble(e)p advertises as the
remote and hosts the GATT server; the X5 scans and initiates the connection.

The service contains:

- `0xCE82`: remote-to-camera notifications; Notify property;
- `0xCE81`: camera-to-remote ATT writes; Write and Write Without Response
  properties;
- `0xCE83`: read-only; its value and purpose remain **Unknown**.

The service and directions are **Confirmed (X5 capture)** and corroborated by
the X6 handle traffic. The X6 reconnect reused the bonded/cached GATT layout,
so that capture did not repeat full service discovery.

### X6 connection sequence

The X6 is also the BLE central. Its captured `CONNECT_IND` targeted the Mini's
public address on LE 1M with Channel Selection Algorithm #2, a 48.75 ms initial
connection interval, zero peripheral latency, and a 7 s supervision timeout.
Those timing values describe this connection, not required constants.

After the MTU exchange, the X6 enabled notifications by writing `01 00` to the
CE82 CCCD at handle `0x004D`. It then wrote these frames to CE81 at handle
`0x004F`:

```text
FE EF FE 50 00 03 01 4C 00
FE EF FE 07 00 06 II II II II II II
```

`0x004C` is the CE82 value handle. The second payload contained six ASCII bytes
that looked device-specific and are redacted as `II`; their meaning is
**Unknown**. Both initialization frames are **Captured (X6)**. The followed
link was not marked encrypted, but it reused an existing relationship; fresh
pairing, bond creation, and security requirements remain untested.

The Mini Remote shutter notification is **Confirmed (X5 capture)** and
**Captured (X6)**:

```text
FC EF FE 86 00 03 01 00 00
```

It is the same toggle for start and stop. In the clean X6 cycle, the two
notifications arrived on CE82 handle `0x004C` 9.019 seconds apart. The first
was followed 390 ms later by video-starting state and 1.852 seconds later by
video-recording state. The second was followed 3.023 seconds later by video
idle. The operator reported that the X6 physically started and stopped, making
that cycle **Operator-correlated (X6)**. Mode changes use another command
ending in `... 01 00 02`; that frame is not a shutter command and Ble(e)p does
not send it.

## Camera-reported capture state

The X5 and X6 write this 13-byte frame to `0xCE81`:

```text
FE EF FE 55 00 07 MM SS XX XX XX XX XX
```

The fixed prefix, `MM`, and `SS` interpretations are **Confirmed (X5 capture)**.
The X6 trace independently contained video states `00`, `01`, `02`, and `04`.
Bytes 8 through 12 (`XX`) were not needed to distinguish the observed state
transitions; their meanings remain **Unknown** and Ble(e)p does not parse them.

Observed meanings:

| `MM` | `SS` | State |
| --- | --- | --- |
| `00` | `00` | Video idle/stopped |
| `00` | `01` | Video starting |
| `00` | `02` | Video recording; repeats while active |
| `00` | `04` | Video stopping/saving |
| `01` | `00` | Photo idle |
| `01` | `01` | Photo starting/countdown |
| `01` | `02` | Photo capture active |
| `01` | `05` | Photo saving/post-capture |

The same transitions appeared when actions were initiated on the camera, not
only from the remote. The X6 session also captured camera-local and
remote-originated actions: camera-local Stop produced `SS=04` followed by
`SS=00`, while the final remote-only cycle produced `SS=01`, `SS=02`, and
`SS=00`. The operator confirmed the corresponding physical behavior. Ble(e)p
therefore treats these as camera-reported state. Unknown lengths, modes, or
phases are rejected rather than guessed.

The X6 also writes an 18-byte elapsed-time frame to CE81:

```text
FE EF FE 63 00 0C PP 00 00 00 TT TT TT TT 00 00 00 00
```

`TT TT TT TT` increased as a little-endian recording-seconds counter (`1`,
`2`, `4`, `7`, and `8` were captured in the clean cycle; intermediate packets
were missed). `PP` was `02` while recording and `04` during one stopping/saving
sample. The direction and bytes are **Captured (X6)**; the field meanings are
**Research** because only one camera, mode, and session were sampled. This
timer corroborates recording but does not replace the `0x55` transition frame
as the conservative state source.

An ATT write response or successful remote notification is only transport
acknowledgement. Ble(e)p changes confirmed recording state only from the `0x55`
camera-to-remote state frame. Repeated video `MM=00, SS=02` frames provide the
pre-command synchronization that distinguishes an already-recording camera
from an idle one.

Start and Stop are not distinct wire commands in this Mini Remote path. A safe
implementation can map them onto the shutter toggle only after a stable video
state confirms that the requested transition is necessary.

## Power

The captured remote shutdown notification is:

```text
FC EF FE 86 00 03 01 00 03
```

This vector is **Confirmed (X5 capture)**.

The X5 replied with `FE EF FE 56 00 01 13` and then disconnected. Disconnect is
used as the shutdown confirmation; the reply alone is not physical proof. The
meaning of `0x13` beyond its association with this exchange remains
**Unknown**.

The first flashed X6 Mini test physically powered the camera off but the camera
did not drop its controller link before Ble(e)p's ten-second deadline, producing
`POWER OFF FAILED`. Ble(e)p now recognizes the captured `0x56 ... 13` shutdown
acceptance on the Mini path and requests local link teardown; the resulting GAP
disconnect releases the stale link and completes the logical off transition.
The patched panel then completed physical X6 shutdown, displayed `CAMERA OFF`,
and successfully woke/reconnected the camera on the next power press. This is
**Operator-confirmed (X6 panel)**. The result is consistent with the X6 sending
the same acceptance frame, but no packet or serial trace was retained to prove
which disconnect branch completed the transition.

Wake is not another GATT command. The disconnected remote advertises this
26-byte manufacturer payload, where `SSSSSS` is the six-character serial from
the paired camera name:

```text
4C 00 02 15 09 4F 52 42 49 54 09 FF 0F 00
SS SS SS SS SS SS 00 00 00 00 E4 01
```

The camera reconnecting confirms wake. The manufacturer prefix, serial field,
and suffix are also independently documented in the MIT-licensed
[`pchwalek/insta360_ble_esp32`](https://github.com/pchwalek/insta360_ble_esp32)
implementation for older Insta360 cameras.

For the X5 capture, the advertisement shape and reconnect behavior are
**Confirmed (X5 capture)**. The operator also confirmed that powering the
physical Mini Remote off and on powered the X6 off and on. Ble(e)p's shared
Mini power path is now also panel-confirmed for X6, including correct off UI and
ORBIT wake/reconnect. The exact X6 shutdown response still needs a dedicated
packet capture. Compatibility of this wake payload with other accepted camera
names remains **Research**.

## Implementation and safety boundaries

- `RecordStart` is available from the fresh connection's provisional video-idle
  state or a camera-confirmed video-idle frame; it is a no-op success if
  confirmed recording is already active.
- `RecordStop` is available only after a stable video-recording frame; it is a
  no-op success if confirmed video idle is already active.
- Photo phases are kept separate from recording state; a photo transition does
  not imply video stopped.
- Power-off is rejected while confirmed recording or a photo operation is
  active. Shutdown acceptance plus link teardown completes the logical off
  state; the panel cannot independently observe physical power. Reconnect
  confirms the wake path restored the link.
- Malformed frames and unrecognized modes or phases do not update state.

## Unknowns and scope limits

- The checksum, counter, timestamp, or payload meanings of bytes 8 through 12
  in the `0x55` state frame are not decoded.
- No explicit state-query command has been identified. Synchronization relies
  on unsolicited/repeated camera writes or a subsequent mode transition.
- The captured mode-change command is deliberately not implemented; Mini mode
  selection remains camera-side.
- Pairing/security requirements and behavior after bond deletion are not
  characterized here. The X6 reconnect was unencrypted in the passive trace,
  but it used a cached GATT relationship and was not a fresh-pairing test.
- The meanings of the X6 `0x50`, `0x07`, and `0x63` fields beyond the narrow
  observations above are not established.
- Compatibility claims printed in the supplied third-party controller manual
  are routing candidates only. They do not replace per-model panel evidence.

## Current hardware gate

On each flashed panel/camera combination, including X5 and X6 separately,
verify:

1. idle status appears after connection or a mode change;
2. remote- and camera-originated video start/stop stay synchronized;
3. photo mode shows start/capture/save/idle without becoming video state;
4. shutdown disconnects and leaves the device in `CAMERA OFF`;
5. the power button wakes the saved camera and reconnects within the 60-second
   advertisement window;
6. another active peripheral camera remains connected while wake advertising
   is requested.

Items 4 and 5 are operator-confirmed on X6. The remaining X6 items and the full
X5 regression/coexistence matrix stay open.
