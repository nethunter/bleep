# GoPro Open GoPro BLE

This note records the published Open GoPro BLE flow and the controlled desktop
validation used to repair Ble(e)p's GoPro driver. It does not broaden support to
untested GoPro models.

## Confidence labels

- **Published**: GoPro's Open GoPro BLE documentation defines the behavior.
- **Observed**: the 2026-08-12 desktop harness received the bytes from a GoPro
  MAX2 and the operator confirmed the corresponding physical result.
- **Pending panel gate**: implemented in firmware but not yet flashed and
  repeated from Ble(e)p hardware.

## Roles and GATT

The camera advertises `0xFEA6` as a BLE peripheral and Ble(e)p is the central.
The validated control service exposed these characteristic pairs:

| Purpose | Write | Notify |
| --- | --- | --- |
| Command | `B5F90072-AA8D-11E3-9046-0002A5D5C51B` | `B5F90073-AA8D-11E3-9046-0002A5D5C51B` |
| Query | `B5F90076-AA8D-11E3-9046-0002A5D5C51B` | `B5F90077-AA8D-11E3-9046-0002A5D5C51B` |

GoPro does not cache subscriptions, so both response characteristics must be
subscribed after every connection. NimBLE callbacks only copy raw packets into
a bounded queue; packet reassembly, parsing, state changes, and writes remain
in the main loop.

## Confirmed initialization

The successful bonded MAX2 session did not send Set Pairing State. After
subscribing, the harness followed GoPro's documented readiness gate:

```text
Command write:  01 3C
Response:       fragmented message with payload beginning 3C 00
```

`0x3C 0x00` is a successful Get Hardware Info response. Its full payload
contains stable camera identifiers and remains private. Ble(e)p now reassembles
General, 13-bit Extended, 16-bit Extended, and Continuation packets and polls
Hardware Info for up to 20 seconds before declaring protocol readiness.

Once ready, the client registers Encoding status 10:

```text
Query write:    02 53 0A
Initial reply:  05 53 00 0A 01 00
```

The first byte in each line is the short-packet payload length. The reply means
Register Status Updates succeeded and Encoding is false. The original one-byte
status-ID operation worked on MAX2. Firmware also has a two-byte-ID fallback,
which remains unverified on hardware.

## Confirmed recording cycle

With the camera in Video mode and idle, the desktop harness performed one
bounded five-second cycle:

```text
Shutter On:       03 01 01 01
Command reply:    02 01 00
Status notify:    05 93 00 0A 01 01

Shutter Off:      03 01 01 00
Command reply:    02 01 00
Status notify:    05 93 00 0A 01 00
```

The command replies mean the camera accepted each request. Only the independent
`0x93` Encoding notification confirms recording or stopped state. Shutter On
reached confirmed encoding about 510 ms after the write. Shutter Off returned a
command response in about 38 ms and confirmed stopped about 1.06 seconds after
the write. The operator separately observed that the GoPro connected, started
recording, and stopped recording correctly.

Ble(e)p therefore keeps Start/Stop pending after a successful command response
and completes them only when status 10 reports the requested state. A rejected
command or ten-second status timeout returns recording to unknown. When a
confirmed target already has the requested state, the command is an idempotent
success without another shutter write.

The first panel test exposed a transition race not seen in the desktop cycle:
an old Encoding=false value could arrive while Start was pending and complete
the transition before Encoding=true. The camera started, but Ble(e)p then
allowed a duplicate Start that MAX2 rejected. Pending Start/Stop now ignores a
status value that does not match the requested target and polls Get Status
Values (`02 13 0A`) until the target is observed or the ten-second deadline
expires. The repaired panel then reported confirmed idle on connection and
completed the operator-observed Start/recording/Stop cycle correctly.

## Published sleep and wake path

Open GoPro assigns command ID `0x05` to Sleep. Its unparameterized command is:

```text
Sleep:          01 05
Success reply:  02 05 00
```

The command is intentionally Sleep rather than the legacy hard power-down
operation because GoPro documents the sleeping camera as BLE-connectable. A
BLE connection wakes and boots it while it continues advertising; the
documented remote-wake window is the first eight hours after sleep.

The firmware does not equate the `0x05 0x00` response with physical sleep. It
closes its BLE link immediately after successful acceptance because a connected
central wakes the camera again, then waits until that disconnect is observed
before publishing **Camera asleep**. The retained session's Power action and
the generic retained-session resume hook reconnect to wake, then repeat
Hardware Info readiness and initial Encoding registration. The documented
manufacturer-data processor-state bit distinguishes the low-power wake phase
from the post-boot control phase: the former gets a short wake-only connection,
then the client waits for an awake advertisement and starts protocol setup on a
fresh link. The complete wake attempt is bounded to 30 seconds; expiry cancels
any pending shared-coordinator retry and returns the retained session to its
retryable asleep state. The packet, state machine, routing, and simulator UI
are implemented. A flashed MAX2 visibly completed Sleep, physical wake, the
fresh post-boot connection, and return to Ready.

## Evidence and privacy

The reference implementation is under `tools/gopro_lab/`. It records a private
JSONL transcript, logs host write acceptance separately from command response
and camera state, and refuses unsafe Start/Stop transitions while state is
unknown.

Private evidence hashes:

- status-only transcript SHA-256:
  `e8acca3510a4fac42e2ad37653184b7b309de0cf2ddfb88799af344084a0011f`
- bounded cycle transcript SHA-256:
  `adeacdd3904a85df92909b7b9f839803fc92271a98996c030c72a53e203bac5c`

The raw files remain outside the repository because advertisements and Hardware
Info include stable identifiers. The vectors above omit those values.

## Controller label

The MAX2 displayed the macOS desktop controller as `...`, even though the
computer name was `Everlost`. CoreBluetooth does not expose a way for this
Bleak central harness to publish a custom local GATT/GAP name. Open GoPro's
protobuf pairing-finish message includes a required non-empty `phoneName`, but
the published schema explicitly says that field does not affect anything. The
desktop harness therefore does not expose a misleading controller-name option.

The ESP32 NimBLE runtime initializes its local identity as `Ble(e)p`. After the
repaired image was flashed and connected successfully, MAX2 still displayed
that controller as `...`. The label is therefore observed camera behavior for
this third-party BLE control path, not evidence that the harness or panel failed
to provide its configured local name.

## Remaining gates

- verify local camera-button state changes from Ble(e)p;
- verify bonded reconnect and wake from sleep on the panel;
- verify the on-screen Sleep response, controller disconnect, visible low-power
  state, reconnect wake, and return to Ready on MAX2;
- verify forget/re-pair, cancellation, multiple GoPros, heap return, and
  coexistence with the other retained links; and
- do not infer support for another GoPro model from the MAX2 result.

Published references:

- <https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/>
- <https://gopro.github.io/OpenGoPro/docs/ble/protocol/data_protocol/>
- <https://gopro.github.io/OpenGoPro/docs/ble/query/>
- <https://gopro.github.io/OpenGoPro/docs/ble/control/>
