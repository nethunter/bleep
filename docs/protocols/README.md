# Protocol research

The protocol notes here were derived from controlled captures and public
research. The public repository keeps extracted command vectors, annotated
behavior, hashes of the original research material, and confidence labels. It
does not ship raw packet captures.

- [Tascam Portacapture X8](tascam-x8.md)
- [Canon smartphone-mode BLE](canon-smartphone-ble.md)
- [Insta360 GPS Remote](insta360-gps-remote.md) — implemented X3/X4/X4 Air/X5 path
- [Insta360 Mini Remote](insta360-mini-remote.md) — retained capture research
- [Zhiyun MOLUS X100](zhiyun-x100.md)
- [Zhiyun MOLUS X60RGB](zhiyun-x60rgb.md)
- [Android screen-recording + HCI capture workflow](capture-workflow.md)
- [Desktop BLE protocol harnesses](desktop-protocol-harness.md) — active
  central/client and macOS peripheral testing

Raw captures were removed before publication because radio traces can contain
stable addresses, nearby device names, phone/camera identifiers, pairing
material, host paths, and unrelated traffic. A small filtered capture can still
leak identifiers in metadata or advertisements.

For contributions:

1. follow the [capture workflow](capture-workflow.md) so app actions, ATT
   traffic, reported state, and physical behavior remain distinguishable;
2. use the [desktop harness workflow](desktop-protocol-harness.md) when active
   protocol validation can safely isolate the protocol from firmware;
3. extract the smallest golden request/response or notification vectors needed
   to reproduce the conclusion;
4. replace device-specific identifiers and omit credential-bearing values;
5. document capture conditions, packet order, and the original capture's
   SHA-256 while keeping the raw material private;
6. keep confirmed behavior separate from `Research`, `Hypothesis`, and
   `Blocked` conclusions;
7. do not attach raw captures or mobile bugreports to public issues or pull
   requests.

Every protocol implementation or research tranche must add or update its note
in this directory at the same time as the code. Record known frame directions,
state transitions, confidence, capture provenance, and unresolved fields; do
not leave the implementation as the only protocol specification.
