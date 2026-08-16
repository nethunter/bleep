# Captive Portal discovery

## Scope and status

This note covers discovery of Ble(e)p's temporary setup Portal by phone captive
network assistants. It does not describe the later station-mode Portal handoff.

- **Implementation:** a bounded Portal-lifetime DNS responder accepts standard
  one-question requests even when they contain an EDNS additional record. A/IN
  queries receive the setup address; unsupported query types receive a
  successful empty response rather than timing out.
- **Implementation:** AP-bound GET requests for common Apple, Android, and
  Windows connectivity probes receive the non-empty Portal page with HTTP 200.
- **Observed:** the reporting client associated, issued DNS and captive HTTP
  probes, and opened the Portal automatically after the corrected per-panel
  SSID image was flashed. Other supported phone platforms remain unchecked.
- **Observed correction:** a later recovery-layout image associated and
  answered DNS but refused TCP connections to `192.168.4.1:80`. The Portal had
  bound its server to the SoftAP address immediately after mode creation;
  Arduino's server wrapper silently returns from a failed `bind()` without
  exposing status. Binding port 80 on all Portal-owned interfaces, explicitly
  configuring `192.168.4.1/24`, and allowing a bounded 50 ms mode-settle period
  restored captive HTTP requests and the subsequent LAN handoff.

## Discovery flow

1. The user explicitly enters Portal mode and joins the temporary open
   `Bleep-Setup-XXXXX` network.
2. The AP DHCP server supplies the setup network configuration.
3. The scoped DNS server maps requested probe hosts to the SoftAP address.
4. The HTTP server returns the Portal page for `/generate_204`, `/gen_204`,
   `/hotspot-detect.html`, `/library/test/success.html`, `/connecttest.txt`, and
   `/ncsi.txt`. Other unknown GET paths receive the same page while the AP is
   active.
5. The phone can classify the response as captive and offer its sign-on UI.

The LAN-bound Portal does not intercept connectivity probes. Unknown LAN paths
retain the normal redirect to `/`.

## Evidence and confidence

- **Research:** Android's AOSP `NetworkMonitor` uses HTTP endpoints such as
  `connectivitycheck.gstatic.com/generate_204`; a non-empty HTTP 200 response is
  treated as a captive result rather than successful Internet access.
- **Research:** Apple documents interception-based captive detection and
  recommends DHCP/RA Captive Portal Advertisement for newer networks. The
  framework bundled with this firmware does not expose DHCP option 114, and the
  standardized status API requires HTTPS, so Ble(e)p retains scoped legacy
  interception for its offline local setup AP.
- **Observed failure:** after the 2026-08-15 full-profile flash, the operator
  reported that joining the Portal Wi-Fi did not automatically open the login
  page. A second operator test after replacing relative HTTP redirects with
  direct Portal responses failed the same way. The exact phone model, OS
  version, DNS query, and requested probe path were not captured.
- **Code inspection:** the bundled Arduino `DNSServer` accepts a request only
  when its DNS Additional Record count is zero. EDNS adds an OPT record there,
  so such a query receives no response and cannot proceed to HTTP. The
  replacement codec accepts and discards the additional section after safely
  parsing the single question. Native fixtures cover plain A, EDNS-bearing A,
  EDNS-bearing AAAA, and malformed-label queries.
- **Observed:** a 2026-08-15 serial capture recorded one AP client, successful
  DNS answers, and repeated captive HTTP probes. The operator then confirmed
  that the Portal opened and worked correctly. The exact phone model and OS
  version were not recorded, so this is not cross-platform evidence.
- **Observed:** after the address-bind correction, serial recorded repeated
  captive-probe handlers, unknown-GET fallback, Wi-Fi scan API activity, and a
  successful transition to the LAN Portal at `192.168.1.15:80`. This proves the
  reproduced phone reached and mutated the Portal, but does not identify the
  phone model or establish cross-platform behavior.

## Verification still required

- Record the exact confirmed phone model and OS version.
- Forget the setup SSID before each test so cached network classification does
  not suppress a fresh probe.
- Repeat automatic sign-on, Portal asset loading, and manual
  `http://192.168.4.1` fallback on another current phone platform.
- Confirm that Finish & Exit closes the AP and sign-on UI cleanly.
- Repeat on at least one current Android phone and one current iPhone before
  describing automatic opening as hardware-verified behavior.
