# Multiple Ble(e)p Manufacturing and Coexistence Plan

This plan makes independently manufactured Ble(e)p panels safe to provision and
operate in the same room. Each panel owns its identity, setup AP, configuration,
and Bluetooth Mesh. Panels may share a studio LAN, but they do not share device
records, mesh keys, scenes, or Home Assistant state.

## Product rules

- The immutable unit identity is derived from the ESP32-C3 factory eFuse MAC;
  it is not a counter compiled into firmware and is not stored in erasable NVS.
- The canonical display form is `BLP-XXXXXXXXXXXX`, using all 48 factory-MAC
  bits. Shortened suffixes are presentation aids only and never database keys.
- The setup SSID is `Bleep-Setup-XXXXX`, using the final five hexadecimal
  characters of the canonical ID. The 20-bit suffix is a presentation aid;
  the full canonical identity remains authoritative.
- The temporary setup AP is open. Its Wi-Fi QR payload uses `T:nopass` and the
  panel does not show a password.
- Every panel generates its own random Bluetooth Mesh Network Key and AppKey
  when its mesh store is missing. Provisioner address `0x0001`, group addresses,
  and node addresses may repeat across panels because the keys define separate
  mesh security domains.
- Every panel continues to request `bleep.local` on the LAN. The numeric DHCP
  address shown on that panel is authoritative when mDNS conflicts or resolves
  to a different Ble(e)p.
- Factory Reset erases runtime configuration and mesh secrets, but cannot
  change the eFuse-derived unit identity or setup SSID. The next mesh use creates
  a new independent network.

## Tranche 1: Stable panel identity

Add one small identity module with host-testable formatting and a target adapter
for `ESP.getEfuseMac()`.

The adapter reverses the six bytes placed into Arduino's little-endian integer
before formatting. Formatters receive the canonical 48-bit MAC value; otherwise
the short suffix would select the shared vendor prefix instead of the unique
device tail.

It provides:

- the full 48-bit hardware identity;
- canonical `BLP-XXXXXXXXXXXX` text;
- the full setup SSID;
- an optional short suffix for round-screen layouts.

Use the canonical identity in About, Portal overview/handoff, diagnostic serial
startup, and manufacturing records. Do not use a new random NVS UUID: cloning or
erasing NVS must never duplicate or change the panel identity. Do not expose
Wi-Fi credentials, BLE bonds, mesh keys, or other secrets beside it.

Acceptance:

- formatting vectors cover leading zeroes and the full 48-bit range;
- two injected hardware IDs produce distinct canonical IDs and setup SSIDs;
- Factory Reset simulation leaves the identity unchanged;
- every firmware profile compiles with one identity implementation.

## Tranche 2: Open, uniquely named setup AP

Replace the fixed password path with an open SoftAP and use the requested short
`Bleep-Setup-XXXXX` suffix. Keep the existing physical Portal entry,
ten-minute teardown, AP-scoped HTTP server, captive DNS, per-session mutation
nonce, no CORS, no-store responses, and frame denial. Limit the AP to one client
when the Arduino API permits it, and show `OPEN NETWORK` on the panel so the
security boundary is honest.

Update both embedded and simulator behavior:

- call the open-network `WiFi.softAP` form;
- return an empty password;
- emit `WIFI:T:nopass;S:Bleep-Setup-XXXXX;;`;
- remove password instructions from the UI and documentation;
- retain the numeric `192.168.4.1` setup URL and LAN handoff flow.

An open AP provides no radio authentication. A nearby person can join while the
Portal is active and can obtain the current page nonce. Physical Portal entry,
the bounded lifetime, visible on-panel status, and one-client capacity reduce
exposure but do not make it equivalent to WPA2. Revisit an on-panel approval for
configuration writes if hostile shared venues become a supported environment.

Acceptance:

- a phone joins without a password or misleading saved credentials;
- QR scanning joins the correct unit when two setup APs are visible;
- captive discovery, manual `192.168.4.1`, Wi-Fi scan/join, LAN handoff, Exit,
  timeout, and heap recovery still work;
- two panels advertise distinct SSIDs simultaneously.

## Tranche 3: Independent mesh creation and recovery

The repository now assembles a missing mesh in temporary state through an
injectable random-fill seam. Both Network Key and AppKey must be filled and the
complete existing-schema blob must save before the live repository publishes
it. RNG and save failures leave the caller unchanged. Production uses the ESP
hardware RNG; keys are never derived from the public unit ID or MAC.

Add validation for:

- independent first boots producing different Network Keys and AppKeys;
- reboot preserving one panel's keys and sequence high-water mark;
- Factory Reset producing a new mesh rather than restoring old keys;
- a save failure preventing provisioning from starting with unsaved keys;
- proxy candidates from another panel's mesh failing authentication and being
  skipped without changing node reachability or state.

Never print raw mesh keys. A manufacturing diagnostic may print a short
cryptographic fingerprint only in an explicitly enabled factory build; normal
firmware should report merely `mesh initialized` and its node count.

## Tranche 4: Same-room device selection

Fresh Aputure Light and Zhiyun adds now use a bounded four-entry picker with
advertised model/name, radio-address suffix, and RSSI. Duplicate observations
retain a stable address-plus-type selection token, and a full list replaces
only its weakest member with a stronger candidate. The operator must choose the
intended fixture before PB-GATT provisioning begins; saved-target reconnect is
still automatic.

Software gates cover stable refresh, weakest replacement, selection rollback,
cancel, and no normal registry commit before protocol-ready. The two-panel,
two-fixture acceptance below remains entirely unverified on hardware.

For ordinary GATT devices, continue persisting the full discovered BLE identity
and address type. For mesh nodes, persist the device UUID, device key, assigned
unicast address, and proxy candidates only in the owning panel's mesh store.
Concurrent panels may scan, but only one may provision a particular
factory-reset fixture; losing the provisioning race must return to the picker
without creating a device record or consuming an address permanently.

Acceptance uses two panels and at least two factory-reset fixtures in one room:

- each panel provisions only the selected fixture;
- cancellation, timeout, and a competing provisioning attempt leave no phantom
  registry entry;
- each panel controls its own mesh member and rejects authenticated state from
  the other mesh;
- reboot and fallback-proxy selection preserve that ownership.

## Tranche 5: LAN and service coexistence

Keep `bleep.local` as requested. Do not silently introduce numbered hostnames.
Because multiple responders requesting the same hostname are inherently
ambiguous, every LAN Portal screen and handoff page must pair the canonical unit
ID with its numeric DHCP URL. The URL is the recovery path and source of truth.

Add the canonical unit ID to the Portal overview JSON and page chrome so an
operator can verify which panel answered. If supported by ESPmDNS, give the
advertised HTTP service instance a unique display name while leaving the host
request as `bleep.local`; this improves browser discovery without changing the
requested alias.

Acceptance:

- two panels join the same WLAN and display different numeric addresses;
- direct IP access reaches the expected unit ID on each panel;
- an mDNS collision never blocks Portal startup or numeric-IP access;
- Portal teardown on either panel does not affect the other.

## Manufacturing flow

1. Flash the same signed firmware image to every panel. Do not compile serials,
   AP names, mesh keys, or credentials into per-unit binaries.
2. Do not clone a configured full-flash image. Erase NVS on every new or
   refurbished panel before first boot; the current `huge_app.csv` NVS region is
   `0x9000` with size `0x5000`.
3. Boot the panel and record the canonical unit ID against the enclosure label,
   PCB revision, and test record. A printed QR may encode only that public unit
   ID, not setup credentials or mesh material.
4. Open Portal and verify the visible setup SSID matches the enclosure record,
   joins without a password, and serves the same unit ID at `192.168.4.1`.
5. Verify display, touch, haptics, battery/charging, Wi-Fi scan/join/teardown,
   BLE scan, and clean reboot. Do not provision production lights during the
   generic factory test.
6. For a refurbished panel, Factory Reset it and separately reset any lights
   that were provisioned to its old mesh. Confirm the panel identity is stable
   and the mesh is newly generated before release.

## Release gate

Do not call multi-panel manufacturing ready until a two-panel soak passes with
both open APs, both LAN Portals, two independent meshes, overlapping BLE scans,
reboots, Portal teardown, and factory reset. Record free heap, largest free
block, scan failures, provisioning failures, and the exact firmware profile.
Build/simulator evidence does not substitute for observing the two physical
panels and their selected fixtures.
