# Contributing to Ble(e)p

Ble(e)p grows one carefully verified integration at a time. Contributions can
be device drivers, protocol research, board support, UI improvements, tests, or
documentation. You do not need to arrive with a complete driver; a well-scoped
research issue is useful too.

## Before you start

For a substantial change, open an issue first so the protocol evidence, safety
boundary, and roadmap tranche can be agreed before code grows around an
assumption. Read these documents before implementation:

1. [`docs/README.md`](docs/README.md)
2. [`docs/decisions.md`](docs/decisions.md)
3. [`docs/progress.md`](docs/progress.md)
4. the relevant section of [`docs/implementation-roadmap.md`](docs/implementation-roadmap.md)
5. the relevant section of [`docs/device-support.md`](docs/device-support.md)

Work within one roadmap phase or explicitly recorded tranche. If a contribution
changes an accepted architecture, persistence, safety, or user-workflow
decision, propose a new ADR instead of rewriting the old decision silently.

## Development setup

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements.txt
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio test -e native
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e crowpanel_128
```

Use two-space indentation and C++17. Match the surrounding file. Keep comments
for hardware caveats, protocol uncertainty, and non-obvious trade-offs.

## Adding a device driver

Put a device integration under `src/devices/<device>/`. Keep protocol framing,
state reduction, transport behavior, driver metadata, and specialized UI
separate where the device needs them.

A driver pull request should explain:

- the exact tested model and required accessory;
- transport, discovery, pairing, and reconnect behavior;
- capabilities and the meaning/quality of reported state;
- the evidence behind each command and notification;
- what was verified on hardware and what remains research;
- flash, RAM, connection latency, and teardown impact when relevant.

Device callbacks must not parse frames, mutate application state, perform GATT
writes, or call LVGL. They may only enqueue bounded raw data or flip atomic
flags. The Arduino main loop owns the rest.

Do not treat a successful write or ACK as physical success unless the device
protocol actually defines it that way and the behavior has been verified.

## Protocol research and privacy

Wireless captures can expose more than the target protocol: addresses, device
names, phone models, serial numbers, Wi-Fi details, pairing keys, credentials,
hostnames, local paths, timestamps, and unrelated nearby devices.

- Do not commit raw `.pcap`, `.pcapng`, HCI snoop logs, mobile bugreports, or
  vendor-app exports.
- Extract the smallest reproducible golden vectors into tests or protocol
  documentation.
- Remove or replace stable identifiers and credential-bearing values.
- Label conclusions `Confirmed`, `Research`, `Hypothesis`, or `Blocked`.
- Describe how a fixture was minimized and sanitized if a future text fixture
  is genuinely needed.
- When in doubt, open an issue describing the evidence without attaching it.

Never submit secrets in an issue. If the material relates to a vulnerability,
follow [`SECURITY.md`](SECURITY.md).

## Tests and verification

Run native tests whenever protocol, state, registry, persistence, catalog,
command-routing, BLE scheduling, or scene logic changes:

```sh
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio test -e native
```

Build every affected firmware profile. Build `crowpanel_128` at minimum. UI
changes should also build and run `ui_sim`; inspect its screenshots for clipped
text and unsafe placement near the round edge.

If a board is available, flash it and report the exact hardware checks. If it
is not, say so plainly. A clean build does not close a physical verification
gate.

## Pull requests

Keep a pull request focused on one phase or recorded tranche. Include:

- the problem and intended behavior;
- the evidence or issue that owns the work;
- tests/builds run and their results;
- hardware verification and remaining blockers;
- screenshots for UI changes;
- documentation updates, including `docs/progress.md`.

By participating, you agree to follow [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
Unless you explicitly state otherwise, contributions submitted for inclusion in
Ble(e)p are provided under the project's [Apache License 2.0](LICENSE).
