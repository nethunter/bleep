# Desktop BLE protocol harnesses

This guide describes how to turn a passive BLE capture into a bounded desktop
test harness before moving the protocol into Ble(e)p firmware. The Insta360 X5
experiment under `tools/insta360_lab/` is the reference implementation.

A desktop harness is an active research instrument, not merely a packet replay
script. It should reproduce the captured BLE role and GATT shape, decode device
state independently of commands, enforce safe transitions, and preserve a
timestamped private transcript. This distinguishes a bad protocol
interpretation from a firmware, UI, or embedded-BLE integration bug.

## Choose the BLE role first

Determine which side initiated the captured connection before choosing a
library:

| Target behavior | Desktop role | Suitable macOS approach |
| --- | --- | --- |
| Product advertises and the app connects | Central/client | Bleak or a CoreBluetooth central |
| Product scans for and connects to a remote | Peripheral/GATT server | PyObjC `CBPeripheralManager` |

The X5 is the second case: the camera is the central, and the GPS Remote is the
advertising peripheral. A central-only Python probe cannot test that protocol.
Do not infer roles from words such as "controller" or "camera"; confirm them
from the connection request and ATT directions in the capture.

Desktop operating-system APIs are useful for GATT experiments but do not offer
raw controller access. On macOS, CoreBluetooth decides the public address,
advertisement layout, scan-response split, interval, and sometimes which
requested fields fit. Use a raw-HCI-capable adapter or the panel when exact
over-the-air advertising is part of the protocol.

## Reference layout

Keep pure protocol logic separate from platform callbacks:

```text
tools/<device>_lab/
  README.md          operator setup, safety, and known platform limits
  protocol.py        constants, frame builders, validation, and decoders
  remote.py          CoreBluetooth transport and bounded scenario runner
  test_protocol.py   sanitized captured vectors and decoder tests
```

For a central/client harness, replace `remote.py` with a client transport but
retain the same split. Pure builders and decoders should not import PyObjC,
Bleak, UI code, or firmware code. Golden-vector tests can then run without
Bluetooth hardware and expose differences from the C++ implementation.

The X5 reference files are:

- `tools/insta360_lab/protocol.py`: UUIDs, command bytes, ORBIT builder,
  identity validation, and state decoder;
- `tools/insta360_lab/remote.py`: peripheral manager, service lifecycle,
  writes, subscriptions, notifications, JSONL logging, and scenario state;
- `tools/insta360_lab/test_protocol.py`: captured advertisement and state
  vectors; and
- `tools/insta360_lab/README.md`: device-specific operation and safety.

## macOS environment

The reference run used arm64 macOS 26.5.2, Python 3.14.4, and PyObjC 12.2.1.
Create an isolated environment and install only the required framework:

```sh
python3 -m venv .venv-protocol-lab
LAB_PYTHON="$PWD/.venv-protocol-lab/bin/python"
"$LAB_PYTHON" -m pip install 'pyobjc-framework-CoreBluetooth==12.2.1'
```

The framework package brings in `pyobjc-core`, Cocoa/Foundation, and required
dispatch bindings. A newer compatible PyObjC release may work, but record the
actual versions used in the protocol note. On first run, macOS may ask for
Bluetooth permission for the terminal or Python executable. An unauthorized
manager state is a host-permission failure, not a protocol result.

Run the reference tests and interactive harness from its directory:

```sh
cd tools/insta360_lab
LAB_PYTHON="../../.venv-protocol-lab/bin/python"
"$LAB_PYTHON" -m unittest -v test_protocol.py
"$LAB_PYTHON" remote.py \
  --camera-name "X5 ABC123" \
  --raw-all \
  --log /private/tmp/insta360-lab.jsonl
```

Use the real private identity at runtime when the protocol requires it, but do
not paste that invocation or its log into repository documentation. The
device-specific README lists interactive commands and the destructive automated
cycle.

## Building a CoreBluetooth peripheral

The peripheral harness follows this asynchronous lifecycle:

```text
manager powered on
  -> create mutable service and characteristics
  -> add service
  -> wait for didAddService
  -> request advertising
  -> wait for central subscription and writes
  -> send notifications only to subscribed centrals
  -> stop advertising and remove services on exit
```

Create `CBMutableCharacteristic` objects from the properties seen in service
discovery. Use a `None` value for dynamic characteristics. In the X5 harness:

- CE81 is Write plus Write Without Response, from camera to remote;
- CE82 is Notify, from remote to camera; and
- CE83 is a readable, currently empty value.

Install the characteristics on a primary `CBMutableService` and call
`addService_`. Do not advertise until
`peripheralManager_didAddService_error_` reports success. Keep characteristic
objects alive for the process lifetime; notifications refer to those exact
objects.

PyObjC maps Objective-C selector colons to underscores. For example:

```python
def peripheralManager_didReceiveWriteRequests_(self, manager, requests):
    ...

def peripheralManager_central_didSubscribeToCharacteristic_(
    self, manager, central, characteristic
):
    ...
```

Run a Foundation run loop so callbacks are delivered. The reference harness
advances `NSRunLoop.currentRunLoop()` in short intervals and performs scenario
state-machine work between iterations. A separate daemon thread reads terminal
commands into a queue; it does not call CoreBluetooth directly.

For each write request:

1. copy the `NSData` into Python `bytes` immediately;
2. decode and log it outside device-specific presentation code;
3. call `respondToRequest_withResult_` for write requests that require a
   response; and
4. update confirmed state only when a strict decoder accepts the frame.

Send a notification with
`updateValue_forCharacteristic_onSubscribedCentrals_`. Its Boolean result
means CoreBluetooth accepted the value for delivery, not that the device acted
on it. A production-quality high-volume harness must queue a rejected value and
retry after `peripheralManagerIsReadyToUpdateSubscribers_`; a low-rate command
harness may fail the command clearly instead. Never classify this return value
as reported or physical state.

## Advertising experiments

Start with the smallest captured identity that lets the product connect. Probe
one variable at a time: local name, service UUID list, manufacturer data, or
appearance. Restart from a clean process when comparing advertisements because
macOS may retain or reorganize fields.

Record two distinct artifacts:

- the dictionary requested from CoreBluetooth; and
- the bytes independently observed over the air.

For the reference experiment, an nRF sniffer remained on sniffer firmware and
Wireshark recorded a fresh, bounded capture for each advertisement variant.
Filter for the expected local-name or manufacturer bytes, save the private
capture outside the repository, and record its SHA-256. An empty result is
meaningful only when the same capture also observes known nearby advertising;
otherwise the sniffer or channel setup may simply be wrong.

`peripheralManagerDidStartAdvertising_error_` proves only that CoreBluetooth
accepted the request. It does not prove the controller emitted the requested
manufacturer payload. In the X5 experiment, CoreBluetooth automatically added
CE80 and measured TX power to a name-only request. It also reported success for
an ORBIT request that an independent nRF capture never observed. The normal GATT
test was valid; the capture-exact wake test was not.

Treat these as raw-HCI gates:

- exact 31-byte legacy primary payload;
- exact primary versus scan-response placement;
- fixed transmitter power field;
- stable/public address requirements;
- directed advertising;
- precise advertising interval or channel behavior; and
- confirmation that manufacturer data actually went over the air.

Do not reflash an adapter currently used as a sniffer without explicit approval.
Losing independent observation while changing the transmitter weakens the
experiment.

## State, commands, and scenarios

Model device state as a small immutable value derived only from device-originated
traffic. A command should be permitted from explicitly safe states and rejected
from unknown state. For a toggle protocol, this is what makes separate Start and
Stop controls truthful.

Keep transport acceptance, reported state, and physical observation separate:

```text
command requested
  -> notification accepted by host API
  -> target sends a decoded state update
  -> operator confirms the physical result
```

An automated scenario should use deadlines for every phase. The X5 reference
cycle is:

```text
wait for confirmed video idle
  -> send Start
  -> wait for confirmed recording
  -> hold for a bounded duration
  -> send Stop
  -> wait for confirmed idle
  -> send power off
  -> wait for unsubscribe/disconnect
  -> request wake advertising
  -> require reconnect and a fresh state
```

If a deadline expires, record the exact phase and stop. Do not infer success or
continue with a destructive command. Interactive mode should offer a read-only
`status` command and explicit exit, while state-changing commands use the same
guards as the automated runner.

## Logging and privacy

Use newline-delimited JSON with a monotonic elapsed timestamp. Event names
should cover manager state, service readiness, advertising request/callback,
subscription changes, writes, decoded state, outgoing commands, timeouts, and
shutdown. JSONL is both human-readable and easy to reduce later.

Raw traffic may contain serial numbers, stable identifiers, names, or pairing
material. Therefore:

- write raw logs and packet captures below a private directory such as
  `/private/tmp`, never inside the repository;
- create directories with mode `0700`;
- make raw-byte logging an explicit option;
- default to lengths and decoded meanings for identity-bearing frames;
- hash private evidence before it is moved or transformed; and
- copy only sanitized golden vectors into tests and protocol notes.

The repository record should include test conditions, relevant timing deltas,
private artifact hashes, decoded conclusions, and limitations—not private
filesystem paths or unsanitized dumps.

## Validation sequence

Use this order for a new device:

1. map roles, services, characteristic properties, and directions from a
   passive capture;
2. implement pure frame builders/decoders and lock captured vectors in tests;
3. start the desktop harness and independently sniff its advertisement;
4. let the target connect without sending a command and wait beyond the
   captured initialization delay;
5. confirm whether state is unsolicited or query-driven;
6. test one safe command and require an independent state update;
7. repeat a reversible transition and compare timing/bytes;
8. test shutdown, wake, delete, movement, or other disruptive behavior only
   with an explicit bounded scenario;
9. repeat local controls on the target to prove whether updates are true state
   rather than command acknowledgements; and
10. port only confirmed behavior into firmware, then compare desktop and panel
    traces at the protocol boundary.

For every run, record the initial target state, advertised identity, timeout,
command sequence, host/API acceptance, decoded responses, physical observation,
and cleanup result. A successful desktop test isolates the protocol but does
not verify embedded memory, callback ownership, coexistence, UI, or reconnect
behavior.

## Reusing the reference harness

For another central-initiated product, copy the directory to a device-specific
lab and change these layers in order:

1. replace UUIDs and characteristic properties from the capture;
2. replace builders and strict decoders in `protocol.py`;
3. replace sanitized golden vectors in `test_protocol.py`;
4. change the advertised identity and only then add optional advertisement
   fields;
5. replace command guards and the automated scenario; and
6. update the device protocol note with results and unresolved gates.

Do not turn the Insta360 script into a universal collection of device branches.
Small device-specific harnesses sharing the documented pattern keep experimental
assumptions visible and prevent one protocol's identity, timeout, or safety
rules from leaking into another.
