# Canon smartphone-mode BLE

This document records the smartphone BLE protocol research and the bounded
BLE-only experiment authorized by ADR-017. The handoff capture informs the
Canon Smart transport defined by ADR-015, but the experimental branch does not
implement Wi-Fi or CCAPI.

## Evidence and confidence

### Research: public Camera Connect protocol

The command and notification values below are documented by
[`3bl3gamer/canon-bluetooth-control`](https://github.com/3bl3gamer/canon-bluetooth-control),
which was tested on an EOS M6:

- pairing service: `00010000-0000-1000-0000-d8492fffa821`;
- pairing command: `00010006-0000-1000-0000-d8492fffa821`;
- pairing data: `0001000a-0000-1000-0000-d8492fffa821`;
- shooting service: `00030000-0000-1000-0000-d8492fffa821`;
- mode command/result: `00030010-...` / `00030011-...`;
- shooting command/state: `00030030-...` / `00030031-...`.

Two public handshake orderings exist. The original EOS M6 demo waits for camera
confirmation before sending controller identity, while some newer public
implementations send identity first. The Pixel 9 Pro XL Camera Connect capture
confirms that the EOS R6 Mark III uses the confirmation-first ordering:

1. complete BLE bonding;
2. write `01` followed by the controller name to the pairing-command
   characteristic;
3. subscribe to indications on the pairing-command characteristic;
4. wait for `02` confirmation (`03` means rejected);
5. subscribe to the required service notifications;
6. write `03` plus a stable 16-byte controller ID to pairing data;
7. write `04` plus the controller name;
8. write `05 02` to identify as Android;
9. write `01` to finish.

The Pixel requests Secure Connections and MITM, but the camera's pairing
response accepts bonding only. The resulting exchange is legacy Just Works
pairing with 16-byte keys, not Secure Connections with MITM.

The corresponding wire sequence is:

1. write `01` followed by the controller name to `00010006-...`;
2. enable indications on `00010006-...`;
3. receive `02` from `00010006-...`;
4. write controller ID, name, and Android type to `0001000a-...`;
5. write `01` to `0001000a-...` to finish.

Camera Connect then writes `06`, `07`, `08`, and `0c` to `0001000a-...` and
receives structured indications on `0001000c-...`. Their exact semantics remain
`Research`. The previously hypothesized identity-first sequence is not used by
Camera Connect on this camera.

Movie commands on `00030030-...` are:

- `00 10`: start recording;
- `00 11`: stop recording.

Shooting-state notifications on `00030031-...` include:

- `01 01 02`: recording started;
- `01 01 01`: recording stopped;
- `10 10 10`: focus or movie button pressed, not proof of recording state.

On the EOS R6 Mark III, Camera Connect writes `03` to `00030010-...` and
receives `05` from `00030011-...` when waking the camera from Bluetooth standby
and opening the BLE shooting session. It writes `04` to leave shooting while
keeping the camera available, receiving `01`. It writes `05` to power the
camera down after shooting, receives `01`, and the camera disconnects. Values
`02` and `06` are not used in these fixtures.

### Capture: EOS R6 Mark III

Fixture: [`dumps/canon-capture.pcapng`](dumps/canon-capture.pcapng)

SHA-256:
`e61fbf83a0e57551fa64b086b06cf85772531d818aba41d1085afc842e0d0d62`

The capture identifies camera `7c:b8:da:2a:c8:75` advertising as
`EOSR6m3_2AC874`. Unencrypted discovery confirms the smartphone pairing service
and characteristics `00010005`, `00010006`, `0001000a`, and `0001000b`.
Secure Connections with MITM protection and bonding begins before the command
exchange. The capture does not contain the keys required to decrypt the
remaining ATT traffic.

Therefore the R6-specific pairing sequence, movie writes, state notifications,
and reconnect behavior remained `Hypothesis` until a host-side HCI capture was
obtained.

### Capture: Camera Connect pairing and BLE movie control

Fixture:
[`dumps/canon-camera-connect-pairing.pcapng`](dumps/canon-camera-connect-pairing.pcapng)

SHA-256:
`fac58a7277072f25b45c91f5051dae9c335d71ca9323e9388b69f6e3399cd08c`

This sanitized host-side HCI fixture contains 297 ATT packets from the Pixel 9
Pro XL Camera Connect session. SMP key exchange, camera serial number,
controller ID, and the SSID-like and credential-like characteristic values are
excluded. It confirms:

- confirmation-first pairing and Android type `05 02`;
- pairing-data finish and follow-up query writes `01`, `06`, `07`, `08`, `0c`;
- shooting-session request `03` and result `05`;
- explicit movie writes `00 10` and `00 11`;
- camera state notifications `01 01 02` recording and `01 01 01` stopped.

The first `00 10` in the session receives `01 01 01` plus a separate `03 12`
notification and does not start recording. A later identical write receives
`01 01 02`. This is direct evidence that ATT write success is not physical
success and state must remain notification-driven.

### Capture: Camera Connect Wi-Fi handoff

Fixture:
[`dumps/canon-camera-connect-wifi-handoff.pcapng`](dumps/canon-camera-connect-wifi-handoff.pcapng)

SHA-256:
`25e59aca42f47a9ca554fd85273f8bfe5b9f5577d96c9d59051838e407bf17ad`

This sanitized fixture contains 90 ATT packets plus the final disconnect event
from a bonded Camera Connect handoff. Camera serial number, SSID-like value,
and credential-like value are excluded. The ordered handoff is:

1. perform the normal bonded reconnect setup;
2. read an ASCII SSID-like value from `00020004-...`;
3. read an eight-byte credential-like value from `00020006-...`;
4. open BLE shooting with `03` on `00030010-...`, receiving `05`;
5. close that mode with `04`, receiving `01`;
6. write `01` to `00020002-...`;
7. receive `01 03`, then `02 03` on `00020003-...` approximately 69 ms and
   791 ms after the write.

The Android bug report records Camera Connect acquiring
`camera_connect:CCBleHandOverWakeLock` at the same time, and the operator
captured the successful Wi-Fi offload. This identifies the missing BLE handoff
request and response sequence. The HCI fixture does not contain Wi-Fi packets,
DHCP details, the camera HTTP endpoint, or the first CCAPI request, so those
remain `Blocked` for the production `Canon (Smart)` workflow.

### Capture: camera power lifecycle

The same host-HCI log includes operator-observed camera wake and power-off
after shooting:

- `03` on `00030010-...` wakes the camera from Bluetooth standby and receives
  `05` on `00030011-...`;
- `04` receives `01` and leaves shooting without powering off, as shown by the
  subsequent Wi-Fi handoff on the same BLE connection;
- `05` receives `01`, powers the camera down, and is followed by a camera-side
  BLE disconnect.

Two captured `05` sequences disconnect approximately 147 ms and 154 ms after
the result notification. A later bonded connection followed by `03` wakes the
camera again. These are camera power/session controls, not recording-state
notifications, and are not implemented by the ADR-017 panel experiment.

## State and safety rules

- A successful GATT write is not proof that recording started or stopped.
- Recording state becomes confirmed only after `00030031-...` reports one of
  the documented steady values.
- Unknown and transitional notifications do not overwrite the last confirmed
  state.
- When reconnect state is unknown, the UI must offer both Start and Stop.
- The panel's ADR-017 BLE-only experiment never writes the Wi-Fi AP-start
  command and never joins the camera network.

## Hardware gate

The experiment remains incomplete until the EOS R6 Mark III verifies:

1. first pairing and camera confirmation;
2. bonded reconnect;
3. explicit start and stop with matching physical behavior;
4. state changes initiated from the camera;
5. reconnect while recording and while stopped;
6. Back during pairing and explicit forget/re-pair.
