# Insta360 GPS Remote protocol notes

Status: **Research**, capture-backed on an Insta360 X5 with an Insta360 GPS
Remote. Ble(e)p implements the captured GATT declaration order under the
operator-confirmed custom identity. X5 control and physical wake are confirmed;
the returning wake connection has one address-routing correction to recheck.

## Evidence and confidence

The raw capture and synchronized video remain outside the repository because
they contain stable device identifiers and unrelated radio traffic.

| Source | Capture details | SHA-256 |
| --- | --- | --- |
| `x5_mini_control_gps_full.pcapng` | 32,711 packets; 2026-08-11 17:37:03 to 17:43:07; annotated remote/camera start, stop, mode, photo, off, and on actions | `898c24d3c7c1d3ce4a0b36669b032075e1e8799e399fa6b1e2af3fd000fe026e` |
| `PXL_20260811_213726419.mp4` | synchronized visible remote and camera behavior | `f961e7a0f1b62b0ec5d1b16accc72c796ef64cc82a292be299f93f99562921da` |

`Confirmed (X5 capture)` below means a packet was correlated with the visible
camera/remote transition. `Research` means the interpretation is capture-backed
but still needs a Ble(e)p hardware test.

## Advertising and GATT identity

The captured GPS Remote's primary legacy advertisement is:

```text
02 01 06 03 19 80 01 14 09 49 6E 73 74 61 33 36 30 20 47 50 53 20 52 65 6D 6F 74 65
```

It contains general-discoverable flags, appearance `0x0180`, and the complete
name `Insta360 GPS Remote`. Its captured scan response is:

```text
03 03 12 18 02 0A 00
```

That contains the HID service UUID `0x1812` and `0 dBm` TX power. Neither the
primary packet nor scan response advertises proprietary service `0xCE80`.

After the exact vendor name passed discovery, `Insta360 Remote (Bleep)` also
passed discovery. Removing the `Insta360` prefix was tested separately and then
reverted. The working custom name requires appearance and the complete 16-bit
service UUID `0xCE80` in the scan response to fit legacy packet limits:

```text
ADV:  02 01 06 18 09 49 6E 73 74 61 33 36 30 20 52 65 6D 6F 74 65 20 28 42 6C 65 65 70 29
SCAN: 03 19 80 01 03 03 80 CE
```

This custom name and field placement were **Operator-confirmed (X5)** for
discovery and commands. The implementation uses them together with the corrected
GATT declaration order after the desktop harness established that GATT ordering,
not the two opaque services, was the material initialization difference.

The camera is the BLE central and connects to the remote peripheral. The remote
hosts service `0xCE80` in this declaration order:

- `0xCE82`: remote-to-camera Notify;
- `0xCE81`: camera-to-remote Write and Write Without Response;
- `0xCE83`: Read; value and purpose unknown.

The physical remote exposes CE82 at value handle `0x004C`, CE81 at `0x004F`,
and CE83 at `0x0051`. The working CoreBluetooth harness constructs the same
CE82, CE81, CE83 order. The earlier panel implementation reversed the first two
declarations; this is now corrected rather than adding the opaque services.

## Shutter and reported state

The GPS shutter notification on `0xCE82` is **Confirmed (X5 capture)**:

```text
FC EF FE 86 00 03 01 02 00
```

It is one toggle command, not distinct start and stop commands. Safe explicit
Start and Stop therefore depend on camera-originated state written to `0xCE81`.
These display frames were seen after both remote-originated and camera-local
actions, so they are state reports rather than command acknowledgements.

| Meaning | Captured shape |
| --- | --- |
| Video idle; remaining time | `FE EF FE 10 80 07 01 XX 46 01 TT TT 6D` (`TT...m`) |
| Video recording; elapsed timer | `FE EF FE 10 80 0D 01 XX 46 01 2E HH HH 3A MM MM 3A SS SS` (`.HH:MM:SS`) |
| Photo idle; remaining shots | `FE EF FE 10 80 09 01 XX 46 01 20 NN NN NN 2B` (` NNN+`) |
| Photo post-capture/saving | `FE EF FE 10 80 05 01 XX 2C 02 NN` |

The changing counter/value bytes marked `XX`, `TT`, or `NN` are not assigned a
broader meaning by the implementation. Fixed markers and timer punctuation are
validated. Video remaining-time text accepts variable-length digit/`h`/`m`
forms, and photo capacity accepts variable-length digit/`+` forms; malformed or
unknown display frames are ignored. The wider length handling was added after
the X5 remained unconfirmed before the first trigger with the original
capture-specific `35m` parser.

Ble(e)p synchronizes recording status from these frames. On connection it
provisionally assumes video idle and exposes Start immediately so a Scene need
not wait for the initial display write. The first recognized CE81 frame upgrades
or replaces that optimistic state. Stop remains available only while recording
is confirmed. This deliberately accepts that sending immediate Start while the
camera was already recording would toggle recording off. Photo mode exposes no
capture command after it is reported.

The connection trace confirms that synchronization does not require a shutter
notification. The BLE connection appears at frame 4268 / 29.390608 seconds,
the X5 enables the remote notification CCCD at frame 4347 / 31.051122 seconds,
and the first video-idle display write appears at frame 4725 / 35.251909
seconds. That is 5.861301 seconds after connection and precedes the first
remote shutter notification at frame 6750 / 60.303646 seconds. No explicit
state-query command appears between connection and the idle write.

Ble(e)p records bounded connection diagnostics in the main loop: connection,
`0xCE82` subscription changes, the first 16 `0xCE81` writes, the first decoded
state, and a 15-second initial-state timeout. Only `FE EF FE 10 80 ...` state
candidates are printed in full; other writes report length only so identity
frames containing stable camera data are not copied into logs. NimBLE
callbacks only enqueue these events.

## Power off and wake

The captured GPS power-off notification is:

```text
FC EF FE 86 00 03 01 00 03
```

The camera disconnected about three seconds later. Ble(e)p treats disconnect,
not successful notification delivery, as confirmation that the camera is off.
The related `... 01 00 00` command appeared without a disconnect and must not
be treated as power off.

The GPS Remote switches to a serial-addressed `ORBIT` manufacturer beacon
immediately after the shutdown disconnect. In the capture, link termination is
frame 28433 / 313.448590 seconds, the first `ORBIT` advertisement is frame
28435 / 313.457360 seconds, and the X5 connects at frame 30568 / 338.544040
seconds: about 25.09 seconds after advertising begins.

For captured camera name `X5 1HDKAB`, the complete 31-byte primary payload and
three-byte scan response are:

```text
ADV:  02 01 06 1B FF 4C 00 02 15 09 4F 52 42 49 54 09 FF 0F 00 31 48 44 4B 41 42 00 00 00 00 E4 01
SCAN: 02 0A 00
```

The six ASCII bytes beginning at primary offset 19 are copied from the final
six-character alphanumeric token in the saved camera name. Wake is rejected if
that serial is missing or invalid. Ble(e)p retains these raw buffers for a
60-second wake window and treats reconnect, not successful advertising, as
wake confirmation. The saved address and name seed peer acceptance before a
new scan result, allowing the powered-off X5 to reconnect after firmware
restart or screen reopen.

## macOS protocol harness

`tools/insta360_lab/remote.py` hosts the minimal `0xCE80` service through
CoreBluetooth and can privately log every `0xCE81` write while sending
state-gated Start, Stop, and power-off notifications. The accompanying pure
protocol tests lock the ORBIT vector and GPS state decoding. Raw JSONL and nRF
captures stay outside the repository.

An nRF capture of the Mac's usable normal advertisement found this exact
31-byte payload after the harness requested only the captured vendor name;
CoreBluetooth inserted its published `0xCE80` service and measured `+12 dBm`
TX-power field automatically:

```text
02 01 1A 02 0A 0C 03 03 80 CE 14 09
49 6E 73 74 61 33 36 30 20 47 50 53 20 52 65 6D 6F 74 65
```

The private normal-advertisement capture has SHA-256
`7d22ac385015671113e58b457e3e4f58be634d150836a0db943af188ecf07d0b`.
The X5 accepted this advertisement and the minimal CE80-only GATT server. It
subscribed to CE82, then sent initialization writes and the first confirmed
video-idle state 4.36 seconds later without any remote notification. The
initial writes included serial identity, state candidates, and other opaque
initialization frames.

In the same live session, the shutter toggle produced confirmed recording
state 1.47 seconds after Start and confirmed video-idle state 3.52 seconds
after Stop. The power-off command was followed by CE82 unsubscribe 3.24
seconds later. This confirms transport and camera-reported state transitions;
physical outcomes remain a separate operator observation.

CoreBluetooth is not a valid capture-exact wake backend on this Mac. A cold
start with only the captured ORBIT manufacturer data and requested `0 dBm`
returned a successful advertising callback, but an eight-second nRF capture
contained no ORBIT packet. That private negative capture has SHA-256
`0e26af864063cf45f506923309bf3aa7eb4dd3afb2b51b5e7b5e41effa9d8496`.
Wake therefore requires the panel or another raw-HCI-capable external adapter;
CoreBluetooth callback success must not be presented as emitted wake evidence.
After the confirmed shutdown above, a 128-second Mac wake request produced no
reconnection, consistent with the independently observed missing ORBIT packet.

## Scope and open gates

- No explicit status-query command was identified. Synchronization depends on
  camera display writes after connection, mode changes, or local actions.
- The captured GPS Remote exposes two additional vendor-specific service
  regions alongside `0xCE80`: an `0xF002`-related service at handles
  `0x003F`-`0x0049` and an `0xD0FF`-related service at handles
  `0x0052`-`0x0064`. Their semantics remain opaque and Ble(e)p does not emulate
  them. The CE80-only Mac test received the complete initialization and state
  stream, disproving their absence as the cause of missing initial state on
  this X5 path. Their purpose remains a separate research question.
- Mode-change commands ending in `01 01 00` were observed but are not exposed.
- Pairing, bond deletion, and reconnect timing are not fully characterized.
- The capture proves X5 behavior only. X3, X4, RS, ONE, GO 3, and GO Ultra are
  not covered by this evidence.
- The Mac test confirms idle state without a query, state-gated Start/Stop,
  and shutdown. The panel confirms initial state and physical ORBIT wake. A
  returning X5 could present an address that was routed away from the saved
  session; the wake owner now claims that connection and maps it to the single
  `PoweringOn` session, pending one final hardware check.
