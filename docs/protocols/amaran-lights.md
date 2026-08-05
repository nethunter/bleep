# Amaran Pano/Ace Bluetooth Mesh evidence

Status: `Research` with host-tested encoders; target fixtures not yet verified.

## Reference boundary

The implementation was ported from the working
`/Users/nethunter/dev/hml/research/studio-lighter` tree, including its
uncommitted Home Assistant additions. Ble(e)p does not import Python or Home
Assistant code at runtime. Captures and successful writes in that reference are
evidence for packet construction, not confirmed fixture state.

## Transport

- Provisioning service `0x1827`, Data In `0x2ADB`, Data Out `0x2ADC`.
- Mesh Proxy service `0x1828`, Data In `0x2ADD`, Data Out `0x2ADE`.
- Provisioning is P-256, no-OOB PB-GATT with secure random values.
- The userspace mesh implementation includes AES-CMAC `s1`/`k1`/`k2`/`k4`,
  AES-CCM upper/network encryption, privacy obfuscation, proxy framing, and
  segmented device-key configuration messages.
- Configuration sends Composition Data Get, AppKey Add, Generic OnOff Server
  bind, and group subscription. Status-response decoding remains required
  before the configuration path is considered hardware-proven.

## Captured vendor access payloads

All supported light controls use Telink vendor opcode `0x26` followed by a
one-byte additive checksum and nine command bytes:

- power on/off;
- CCT from 2300 K through 10000 K, tint from -1000 through +1000 permille, and
  brightness from 0 through 100;
- packed `0xRRGGBB` color and brightness from 0 through 100;
- captured node-reset payload.

Native golden tests lock AES, CMAC, a known encrypted network packet, power,
CCT, RGB, validation bounds, segmented configuration shape, checksummed store,
and durable sequence reservation. Successful control writes are represented as
`Optimistic`; no fixture state is `Confirmed` until readback is decoded.

## Hardware gate

For each Pano 60c, Pano 120c, and Ace 25c, verify provisioning, configuration
statuses, several CCT/tint/RGB/brightness combinations, reboot recovery, proxy
fallback, interrupted configuration, sequence-number continuity, mixed-device
sequences, and reset followed by the return of provisioning advertisements.
Record latency, dropped events, heap, and reconnect stability in
`docs/progress.md`.
