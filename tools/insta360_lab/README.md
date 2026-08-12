# Insta360 Mac protocol lab

This is a private-capture harness for testing the X5 GPS Remote protocol before
moving a behavior into firmware. The Mac acts as the BLE peripheral, hosts
`0xCE80`, logs every camera write to `0xCE81`, and sends state-gated Start,
Stop, and power-off notifications on `0xCE82`.

The reusable design, CoreBluetooth callback lifecycle, validation sequence,
privacy rules, and limits of macOS advertising are documented in
[the desktop protocol harness guide](../../docs/protocols/desktop-protocol-harness.md).
This directory is the concrete X5 reference, not a generic multi-device runner.

macOS CoreBluetooth constructs the actual advertisement. Normal mode defaults
to the captured name `Insta360 GPS Remote` and, like the captured physical
remote, does not request CE80 in the advertisement; CE80 is still present in
GATT. `--advertise-service` enables a comparison probe. The longer firmware
name does not fit when macOS also inserts TX power and the service UUID, so
CoreBluetooth can silently omit it. Wake mode requests the captured Apple
manufacturer `ORBIT` data and `0 dBm` TX power; success from CoreBluetooth is
not proof that the controller emitted the capture-exact primary/scan packets.
On the development Mac, a cold-start nRF capture found no ORBIT frame even
though CoreBluetooth reported success. Use a raw-HCI-capable external adapter
when exact wake advertising matters.

A live X5 test confirmed that the minimal CE80-only GATT profile is sufficient:
the camera subscribed to CE82, sent its initialization and current video-idle
state without a query, accepted Start and Stop, reported recording/idle after
each transition, and disconnected after power-off. The two opaque services on
the captured physical remote are therefore not required for this X5 path.
Mac-native wake remains the one unsupported leg: no reconnect occurred during
a 128-second ORBIT request after shutdown because the requested ORBIT packet
was not present over the air.

Run from this directory with the repository virtual environment:

```sh
/Users/nethunter/dev/bleep/.venv/bin/python -m unittest -v test_protocol.py
/Users/nethunter/dev/bleep/.venv/bin/python remote.py \
  --camera-name "X5 SERIAL" \
  --raw-all \
  --log /private/tmp/insta360-mac-lab.jsonl
```

On first use, macOS may request Bluetooth permission for the terminal or Python
process. Select the advertised remote from the X5, wait for a decoded state,
then enter `start`, `stop`, `off`, or `wake`. Start and Stop are rejected unless
the preceding camera write confirms the safe video state. `status`, `normal`,
and `quit` are also available.

For an automated destructive cycle, begin with the X5 on, in video mode, idle,
with media available:

```sh
/Users/nethunter/dev/bleep/.venv/bin/python remote.py \
  --camera-name "X5 SERIAL" \
  --run-cycle \
  --record-seconds 5 \
  --raw-all \
  --log /private/tmp/insta360-mac-cycle.jsonl
```

The cycle waits for confirmed idle, starts recording, waits for confirmed
recording, records for five seconds, stops, waits for confirmed idle, powers
off, waits for CE82 unsubscribe, requests ORBIT wake advertising for up to 60
seconds, and finishes only after reconnect plus a new camera state. Transport
acceptance is logged separately from reported and physical state.

Raw logs may contain the camera serial and other stable identifiers. Keep them
outside the repository and sanitize any vectors before adding them to protocol
documentation.
