# Android BLE capture workflow

This guide describes the preferred Ble(e)p workflow for reverse-engineering a
new BLE device from two synchronized sources:

- a phone screen recording showing the exact vendor-app actions; and
- an Android Bluetooth HCI snoop log collected in an `adb bugreport`.

The goal is not merely to find bytes that appear near a button press. The goal
is to preserve enough context to distinguish discovery, connection, protocol
initialization, commands, acknowledgements, reported state, and observed
physical behavior. Raw videos, bugreports, and snoop logs are private research
material and must not be committed.

## Evidence levels

Keep these observations separate throughout the investigation:

1. **BLE link:** the phone connected to a peripheral or mesh proxy.
2. **ATT result:** a GATT read/write completed or a notification arrived.
3. **Protocol result:** a decoded response correlates with the request.
4. **Reported state:** a later independent read or status notification reports
   the requested value.
5. **Physical result:** the device visibly or audibly did the requested thing.

A successful ATT write or protocol ACK does not prove reported or physical
state. For a mesh, a live proxy link proves only that the proxy is connected;
it does not prove that every logical member is powered, reachable, or responding.
The phone screen recording normally proves only what the app displayed and
which control the operator touched. Record physical output separately with a
camera or an explicit contemporaneous observation unless the fixture itself is
clearly visible in the same recording.

Use the documentation labels consistently:

- `Confirmed`: repeated capture-backed behavior with correlated state and, for
  physical commands, an independent physical observation;
- `Research`: captured behavior whose limits or semantics are incomplete;
- `Hypothesis`: a proposed interpretation that still needs a discriminating
  capture; and
- `Blocked`: the missing evidence is named and implementation should not guess.

## Before the capture

Record a small private manifest before touching the vendor app:

```text
Capture ID:
Date and timezone:
Phone model / Android version:
Vendor app name and version:
Fixture model / firmware version / battery or power source:
Fixture order in the camera view:
Initial power, mode, brightness, and color:
Factory-reset, bonded, provisioned, or already connected:
Other fixtures in the network and their add order:
Experiment goal:
```

Use a fresh capture ID for each experiment. Put private work in a directory
outside the repository with mode `0700`; use opaque fixture labels such as
`light-a` in notes. Do not copy mesh keys, pairing keys, Wi-Fi credentials,
serial numbers, stable addresses, or account data into the manifest.

Reduce noise before recording:

- enable Android Developer options and **Enable Bluetooth HCI snoop log**;
- enable Do Not Disturb and hide notification contents from the recording;
- close unrelated BLE apps and disconnect unnecessary watches, headphones, and
  other active Bluetooth devices without destroying bonds you still need;
- charge battery-powered fixtures and record their starting state;
- place multiple fixtures in a fixed, documented left-to-right order; and
- decide the exact values to test before moving a slider.

Toggling HCI snoop logging off and on, or restarting Bluetooth/the phone, may
start a cleaner log, but behavior varies by Android build. Never assume it
cleared older data; identify the actual session later from timestamps and packet
content.

Provisioning is often destructive or non-repeatable. Capture discovery,
pairing, provisioning, disconnect, and first reconnect in one uninterrupted
session when possible. If the device is already provisioned, do not factory
reset it merely to make a cleaner capture unless that reset is explicitly part
of the test plan.

### Short operator request

When another person is doing the capture, give them a concrete script instead
of asking for “some Bluetooth logs.” Adapt this template to the capability:

> Enable Bluetooth HCI snoop logging, then start screen recording before you
> open the vendor app. Show the device list, connect or add the named fixture,
> and wait three seconds between every action. Use exact values: Off, On,
> brightness 10/50/90, low/middle/high CCT, and red/green/blue if supported.
> Do not sweep sliders continuously. Show the app's state after each action and
> tell me the fixture order and what physically changed. Keep recording through
> disconnect and one reconnect. Stop recording, immediately run `adb bugreport`,
> and provide the original video and bugreport privately.

For a provisioning-only fixture, explicitly say whether factory reset is safe
and capture the first add only once. For a mesh, also ask for the member add
order and which fixture the app appears connected to.

## Record a discriminating action sequence

Start the phone's built-in screen recorder before opening the vendor app.
Showing touch indicators is useful. Microphone narration is optional and may
leak private conversation, so a written action sheet is usually safer.

Use this cadence for every action:

1. hold the known initial state for about three seconds;
2. perform exactly one tap or enter one exact value;
3. do not scrub a slider or press another control for about three seconds;
4. show any app-reported result; and
5. record the independently observed physical result.

The pauses create recognizable packet clusters and make the video/HCI
correlation much faster. Prefer deliberately distinct values over tiny changes.
For a light, a compact first pass is:

```text
connect -> read current state -> off -> on -> brightness 10 -> 50 -> 90
-> CCT low -> middle -> high -> red -> green -> blue -> restore initial state
```

Only test capabilities the device actually exposes. Enter exact numeric values
when the app permits it. If it only provides a slider, stop at clearly visible
values instead of sweeping continuously. Include boundary values and one
non-round value to expose clamping or quantization.

For state and reachability research, add separate experiments rather than
mixing them into the command-identification pass:

- change a control on the physical fixture and wait for an unsolicited app
  update;
- disconnect and reconnect without changing state;
- power-cycle one fixture while leaving the proxy or other devices running;
- issue a read-only refresh after each transition; and
- wait beyond the suspected timeout to identify stale/offline policy.

For motorized, recording, delete, reset, standby, or high-output commands,
clear the physical area and test the least hazardous read-only/state path first.
Restore every fixture to its recorded starting state after the experiment.

### Mesh-specific capture notes

Write down the logical network membership, provisioning order, and which
physical fixture currently supplies the phone's BLE Mesh Proxy connection.
These are different identities. A useful mesh capture deliberately shows:

- one retained proxy connection controlling at least two logical members;
- a command to each member with several seconds between them;
- source-addressed or otherwise member-specific status responses;
- one member powered off or out of range while the proxy stays connected; and
- if supported, reconnect through a different proxy without reprovisioning.

Capture group commands and per-member commands separately. A group ACK or one
member's response must not be generalized to every member. Preserve private
unicast/group allocation and key material only in the private manifest; public
notes should use sanitized labels and relative ordering.

## Finish and collect the bugreport

Stop the screen recording only after the final idle interval. Immediately run
`adb bugreport`; do not spend time searching the phone filesystem for the HCI
log first. A bugreport preserves the Bluetooth logs and the supporting system
context together.

```sh
CAPTURE_DIR="/private/tmp/bleep-capture-YYYYMMDD-HHMMSS"
mkdir -m 700 "$CAPTURE_DIR"
adb devices
adb bugreport "$CAPTURE_DIR"
```

Copy the screen recording into the private capture directory without renaming
or transcoding the original. Record SHA-256 hashes before analysis:

```sh
find "$CAPTURE_DIR" -type f -exec shasum -a 256 {} \;
```

If `adb bugreport` fails, keep the original video and action sheet and retry the
bugreport promptly. Do not repeat provisioning or state-changing actions until
you know whether the first HCI session was retained.

## Extract and identify the correct HCI log

Android paths and rotation names differ by vendor and release. Inventory the
archive rather than assuming one fixed path. Set `BUGREPORT_ZIP` to the exact
archive path printed by `adb bugreport`:

```sh
BUGREPORT_ZIP="/private/tmp/bleep-capture-YYYYMMDD-HHMMSS/bugreport-DEVICE-DATE.zip"
unzip -l "$BUGREPORT_ZIP" \
  | rg -i 'btsnoop|btsnooz|bluetooth.*/log|bluetooth.*snoop'
mkdir -m 700 "$CAPTURE_DIR/extracted"
unzip -q "$BUGREPORT_ZIP" -d "$CAPTURE_DIR/extracted"
find "$CAPTURE_DIR/extracted" -type f \
  | rg -i 'btsnoop|btsnooz|bluetooth.*/log|bluetooth.*snoop'
```

Common archives contain `btsnoop_hci.log` plus rotated siblings somewhere
below `FS/data/misc/bluetooth/logs/`, but this is not universal. Some builds
provide only a `btsnooz` representation embedded in the bugreport. Decode that
with the matching AOSP `btsnooz.py` tooling; renaming it to `.log` does not turn
it into a packet capture.

Hash every candidate and inspect its packet-time range. The newest filename is
not necessarily the file containing the experiment:

```sh
capinfos -a -e -c /private/path/to/btsnoop_hci.log
shasum -a 256 /private/path/to/btsnoop_hci.log
```

The protocol note should retain only the hash of the relevant original log,
the capture conditions, and sanitized conclusions. It should not retain the
private path.

## Correlate video actions with ATT traffic

First inspect the video metadata, but treat its creation timestamp as a hint:
phone exports and messaging apps sometimes rewrite it.

```sh
ffprobe -v error \
  -show_entries format=start_time,duration:format_tags=creation_time \
  -of default=noprint_wrappers=1 /private/path/to/screen-recording.mp4
```

Open the HCI log in Wireshark and begin with the display filter `btatt`. Find
the scan/connect/discovery burst visible just after the corresponding app action
in the video. That recognizable event is a stronger synchronization anchor than
file metadata. Record one offset, then verify it against a second event near the
end of the session; clock drift or a wrong rotated log will otherwise produce a
plausible but false match.

For a compact chronological ATT table, `tshark` can export the useful fields:

```sh
tshark -r /private/path/to/btsnoop_hci.log -Y 'btatt' -T fields \
  -E header=y -E separator=/t -E occurrence=a \
  -e frame.number -e frame.time_epoch -e bthci_acl.chandle \
  -e bluetooth.src -e bluetooth.dst -e btatt.opcode -e btatt.handle \
  -e btatt.service_uuid16 -e btatt.service_uuid128 \
  -e btatt.characteristic_uuid16 -e btatt.characteristic_uuid128 \
  -e btatt.value
```

Field availability varies with Wireshark version; use `tshark -G fields` to
check names instead of silently dropping a column. Useful ATT methods include
reads and responses, writes and write commands, notifications, and indications.
Do not discard discovery traffic until handles have been mapped to service and
characteristic UUIDs.

Build a private correlation table before interpreting payloads:

| Video time | HCI frame/time | Link/handle | Direction | App action | ATT value | App result | Physical result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `00:18.2` | `frame 1234` | `0x0040 / 0x0012` | phone to device | Power Off | private raw value | Off | emitter dark |

Look for controlled differences across repeated actions:

- bytes that change with the entered value;
- counters, transaction IDs, checksums, lengths, or routing selectors;
- a response carrying the same sequence/command;
- independent readback after a setter;
- unsolicited state after a fixture-local action; and
- initialization writes that repeat on every reconnect and are not commands.

Use multiple contrasting values to separate parameters. Two neighboring slider
writes rarely prove field width, units, byte order, scaling, or checksum. For
mesh traffic, decrypt only with privately held keys, authenticate before trusting
source/state, reject replayed sequence numbers, and document the sanitized
access payload rather than network/device keys.

## Active probing after passive analysis

Do not start by replaying every captured write. First implement offline frame
parsing/building and golden-vector tests. Then use a bounded host probe that:

- selects a product-qualified advertisement, not merely the strongest device;
- performs the captured initialization in order;
- defaults to read-only commands;
- requires an explicit option for state-changing operations;
- prints decoded values without secrets or stable identifiers;
- follows setters with independent readback where possible; and
- restores the original state before disconnecting.

Change one field at a time. Start with known-safe values seen in the vendor app,
then test boundaries. Unknown opcodes, reset/delete commands, malformed lengths,
and fuzzing are outside ordinary device onboarding and require a separate safety
plan.

When the target scans for and connects to a remote, the host probe must emulate
a BLE peripheral rather than act as a client. Follow the
[desktop BLE protocol harness guide](desktop-protocol-harness.md) for the
CoreBluetooth/PyObjC structure, state gating, private JSONL transcript, and
independent over-the-air advertisement checks.

## Durable handoff for the next agent

The final public/repository handoff should contain:

1. exact model, app/firmware versions when known, and capture scenario;
2. SHA-256 of each private source capture used;
3. advertising, services, characteristics, and initialization order;
4. the smallest sanitized request/response/notification golden vectors;
5. a command/state table with evidence labels and known ranges/quantization;
6. explicit separation of ATT ACK, protocol response, readback, and physical
   observation;
7. reconnection, timeout, routing, and multi-member findings;
8. open hypotheses plus the exact next discriminating capture; and
9. host tests and a conservative research probe when one was needed.

Put device-specific findings in `docs/protocols/<device>.md`, link them from
`docs/protocols/README.md`, and update `docs/device-support.md` and
`docs/progress.md`. Keep raw captures, extracted bugreports, private manifests,
keys, and unsanitized exports outside the repository.

Before committing, inspect both tracked and untracked files:

```sh
git status --short
git diff --check
```

If a conclusion cannot be reproduced from the sanitized vectors and written
conditions, leave it as `Research`, `Hypothesis`, or `Blocked` and specify what
future capture would resolve it.
