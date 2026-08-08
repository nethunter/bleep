# Protocol research

The protocol notes here were derived from controlled captures and public
research. The public repository keeps extracted command vectors, annotated
behavior, hashes of the original research material, and confidence labels. It
does not ship raw packet captures.

- [Tascam Portacapture X8](tascam-x8.md)
- [Canon smartphone-mode BLE](canon-smartphone-ble.md)
- [Zhiyun MOLUS X100](zhiyun-x100.md)
- [Zhiyun MOLUS X60RGB](zhiyun-x60rgb.md)
- [Android screen-recording + HCI capture workflow](capture-workflow.md)

Raw captures were removed before publication because radio traces can contain
stable addresses, nearby device names, phone/camera identifiers, pairing
material, host paths, and unrelated traffic. A small filtered capture can still
leak identifiers in metadata or advertisements.

For contributions:

1. follow the [capture workflow](capture-workflow.md) so app actions, ATT
   traffic, reported state, and physical behavior remain distinguishable;
2. extract the smallest golden request/response or notification vectors needed
   to reproduce the conclusion;
3. replace device-specific identifiers and omit credential-bearing values;
4. document capture conditions, packet order, and the original capture's
   SHA-256 while keeping the raw material private;
5. keep confirmed behavior separate from `Research`, `Hypothesis`, and
   `Blocked` conclusions;
6. do not attach raw captures or mobile bugreports to public issues or pull
   requests.
