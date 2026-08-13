# GoPro desktop protocol lab

This is the central/client form of the reusable desktop BLE protocol harness.
The Mac scans for a GoPro advertising `0xFEA6`, connects with Bleak, records the
discovered GATT shape and notifications, and tests the smallest Open GoPro
recording flow before any behavior is copied into firmware.

The lab deliberately separates four facts:

1. the host BLE API accepted a write;
2. the camera returned a successful command response;
3. status 10 (`Encoding`) independently reported the requested state; and
4. the operator observed the physical camera start or stop.

A successful command response is not recording-state confirmation. The current
firmware stops at step 2. This lab subscribes to Query Response (`GP-0077`) and
registers status 10 so that a firmware repair can be based on camera-originated
state.

## Environment

The repository's documented PlatformIO virtual environment already includes
Bleak. If a worktree has no `.venv`, use the main checkout's interpreter:

```sh
LAB_PYTHON="/Users/nethunter/dev/bleep/.venv/bin/python"
"$LAB_PYTHON" -m unittest -v tools/gopro_lab/test_protocol.py
```

On first use, macOS may request Bluetooth access for Terminal or Python. Put the
camera in its Open GoPro wireless pairing flow before scanning.

First list only matching advertisements. This is read-only:

```sh
"$LAB_PYTHON" tools/gopro_lab/client.py \
  --scan-only \
  --raw-all \
  --log /private/tmp/gopro-lab-scan.jsonl
```

Use the exact name or macOS CoreBluetooth identifier if multiple cameras are
nearby. A fresh pairing normally needs `--finish-pairing`; omit it when testing
whether an existing bond and protocol initialization work without that command:

```sh
"$LAB_PYTHON" tools/gopro_lab/client.py \
  --device "GoPro 1234" \
  --finish-pairing \
  --raw-all \
  --log /private/tmp/gopro-lab.jsonl
```

For a non-recording diagnostic, add `--status-only`. It connects, subscribes,
polls Hardware Info until ready, registers Encoding status, records the reported
idle/recording value, and exits without sending Set Pairing State or Shutter.

The harness subscribes to every discovered notifiable characteristic, polls
`Get Hardware Info` until the camera reports BLE readiness, and registers for
Encoding status. It tries the original one-byte status operation first and
records a fallback to the dedicated two-byte-ID operation if the camera rejects
it. Interactive commands are `status`, `query`, `register`,
`start`, `stop`, `pairing`, and `quit`. Start and Stop are rejected when the
last camera-originated Encoding status does not make the transition safe.

For a bounded five-second recording test:

```sh
"$LAB_PYTHON" tools/gopro_lab/client.py \
  --device "GoPro 1234" \
  --run-cycle \
  --record-seconds 5 \
  --raw-all \
  --log /private/tmp/gopro-lab-cycle.jsonl
```

Begin the cycle with the camera in video mode and physically idle. The runner
requires reported idle, sends Shutter On, waits for reported encoding, holds for
the bounded duration, sends Shutter Off, and waits for reported idle. It does
not claim physical success; record that observation separately.

Raw advertisements, GATT traffic, Hardware Info, and logs can contain a serial
number, stable BLE identifier, Wi-Fi identity, and model information. Keep logs
under a private directory such as `/private/tmp`, never in the repository. Add
only sanitized golden vectors and conclusions to public tests or protocol docs.

## Firmware comparison points

Compare the live transcript with these current firmware boundaries:

- whether a bonded reconnect needs Set Pairing State again;
- which notifiable characteristics must be subscribed on every connection;
- how many readiness retries precede a successful Hardware Info response;
- the exact short packets for Shutter and Encoding registration;
- whether Query Response reports `0x53` initially and `0x93` on transitions;
- whether camera-side or local-button changes also produce Encoding updates;
- command-response and reported-state timing; and
- disconnect/reconnect behavior after the camera sleeps.

Do not port a conclusion into firmware until the transcript and physical camera
agree. Desktop success does not validate ESP32 memory, NimBLE callback ownership,
multi-link coexistence, or panel UI behavior.
