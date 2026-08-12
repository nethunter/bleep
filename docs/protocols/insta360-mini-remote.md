# Insta360 Mini Remote protocol notes

Status: **Research only**, capture-backed on an Insta360 X5 with an Insta360
Mini Remote. Ble(e)p currently selects the separately documented GPS Remote
path; this note preserves the decoded Mini protocol and is not the active
implementation identity. Do not extend these results to GO 3 or GO Ultra
without testing.

## Evidence and confidence

The raw nRF Sniffer captures and synchronized phone video remain outside the
repository because they contain device identifiers and unrelated radio
traffic. These hashes identify the private source material used for this note:

| Source | Capture details | SHA-256 |
| --- | --- | --- |
| `x5_mini_control.pcapng` | 45,275 packets; 2026-08-11 16:45:30 to 16:54:43; annotated video start, stop, mode change, and photo actions | `388b7398e620e622dccbb01a17788f2592993507ea0f027c4043c4a511bede4f` |
| `x5_mini_control_on_off.pcapng` | 25,753 packets; 2026-08-11 17:00:46 to 18:15:31; annotated power-off and power-on actions | `8546844db121b490943a623de5fd372336b4de08bcfc69e7671627fbcf819742` |
| `PXL_20260811_204251224.mp4` | synchronized visible behavior; recording began before packet capture, so only the interval after capture start was correlated | `1cadb78adfd7b6c97b2b0f5f5777a1e56ec2071b4ff66bb6efc926ac61a97859` |

Confidence labels used below:

- **Confirmed (X5 capture):** observed repeatedly in the annotated X5/Mini
  Remote traffic and correlated with the camera or remote UI.
- **Operator-confirmed (X5):** already exercised successfully by Ble(e)p on
  the physical X5, but not introduced by these Mini Remote captures.
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

The Mini does **not** advertise `0xCE80`. Advertising that proprietary service
with a GPS Remote name causes the X5 to select its legacy GPS-remote behavior,
which does not supply the Mini capture-state stream. An implementation of this
Mini path would need to expose `0xCE80` in its GATT table while advertising the
Mini's name, appearance, and HID service identity.

## GATT roles

The camera acts as the BLE central and connects to a remote peripheral exposing
service `0xCE80`. This role reversal is important: Ble(e)p advertises as the
remote and hosts the GATT server; the X5 scans and initiates the connection.

The service contains:

- `0xCE82`: remote-to-camera notifications; Notify property;
- `0xCE81`: camera-to-remote ATT writes; Write and Write Without Response
  properties;
- `0xCE83`: read-only; its value and purpose remain **Unknown**.

The service and directions are **Confirmed (X5 capture)**.

The Mini Remote shutter notification is **Confirmed (X5 capture)**:

```text
FC EF FE 86 00 03 01 00 00
```

It is the same toggle for start and stop. The X5 followed it with the expected
video transition writes in each direction. Mode changes use another command
ending in `... 01 00 02`; that frame is not a shutter command and Ble(e)p does
not send it.

## Camera-reported capture state

The X5 writes this 13-byte frame to `0xCE81`:

```text
FE EF FE 55 00 07 MM SS XX XX XX XX XX
```

The fixed prefix, `MM`, and `SS` interpretations are **Confirmed (X5
capture)**. Bytes 8 through 12 (`XX`) were not needed to distinguish the
observed state transitions; their meanings remain **Unknown** and Ble(e)p does
not parse them.

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
only from the remote. Ble(e)p therefore treats them as reported state. Unknown
lengths, modes, or phases are rejected rather than guessed.

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
**Confirmed (X5 capture)**. Compatibility of this wake payload with the other
camera names accepted by the driver remains **Research**.

## Safety boundaries for a future implementation

- `RecordStart` is available only after a stable video-idle frame; it is a
  no-op success if confirmed recording is already active.
- `RecordStop` is available only after a stable video-recording frame; it is a
  no-op success if confirmed video idle is already active.
- Before the first valid state frame, only the raw Shutter action is safe
  because the protocol does not expose distinct start and stop notifications.
- Photo phases are kept separate from recording state; a photo transition does
  not imply video stopped.
- Power-off is rejected while confirmed recording or a photo operation is
  active. Disconnect confirms off; reconnect confirms on.
- Malformed frames and unrecognized modes or phases do not update state.

## Unknowns and scope limits

- The checksum, counter, timestamp, or payload meanings of bytes 8 through 12
  in the `0x55` state frame are not decoded.
- No explicit state-query command has been identified. Synchronization relies
  on unsolicited/repeated camera writes or a subsequent mode transition.
- Mode-change and other Mini Remote commands have not been generalized.
- Pairing/security requirements and behavior after bond deletion are not
  characterized here.
- X3, X4, RS, ONE, GO 3, and GO Ultra compatibility is not established by the
  X5 captures. GO Ultra remains explicitly experimental in the driver.

## Hardware gate if this path is restored

On the flashed panel/X5 combination verify:

1. idle status appears after connection or a mode change;
2. remote- and camera-originated video start/stop stay synchronized;
3. photo mode shows start/capture/save/idle without becoming video state;
4. shutdown disconnects and leaves the device in `CAMERA OFF`;
5. the power button wakes the saved X5 and reconnects within the 30-second
   advertisement window;
6. another active peripheral camera remains connected while wake advertising
   is requested.
