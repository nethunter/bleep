---
title: "Ble(e)p Instruction Manual"
subtitle: "Setup, operation, device support, and recovery for the open studio controller"
edition: "0.2.0-dev / source e97d0b6"
date: "10 August 2026"
status: "Development hardware - verify before critical work"
author: "Ble(e)p project"
---

# Welcome to Ble(e)p

Ble(e)p is a compact, touch-first controller for studio equipment. Its playful name means **Bluetooth Links Everything, Eventually, Probably**; the practical expansion is **Bluetooth Low Energy Equipment Panel**. It runs locally on an ESP32-C3 CrowPanel with a 1.28-inch round touchscreen and brings motion, recording, lighting, Home Assistant, and multi-device sequences into one interface.

> This manual describes development firmware, not a finished consumer product. Read the support status and limitations for each device before relying on it for paid or unrepeatable work. A protocol acknowledgement is not always proof that a camera recorded, a light changed, or a slider moved.

## What this manual covers

- identifying and operating the controller;
- adding, managing, reconnecting, and removing devices;
- using every currently exposed user function;
- building and running Start/Stop sequences;
- configuring the temporary Portal and local Home Assistant control;
- supported, experimental, candidate, research-only, and later devices;
- troubleshooting, Factory Reset, safety, and maintenance of this manual.

## Status words used here

| Label | Meaning |
| --- | --- |
| Current | Implemented and hardware-verified for the stated bounded feature set. Remaining endurance or coexistence checks may still be open. |
| Experimental; verified path | Implemented and proven on the exact named model and path, but broader coverage remains open. |
| Experimental | Implemented, but some important real-device, recovery, coexistence, or endurance checks remain open. |
| Candidate | An implemented protocol path may apply; there is no model-specific hardware result yet. |
| Research | Visible or documented, but not ready to save and control as a normal device. |
| Later | Not implemented. Architecture or protocol research still has to begin. |

<!-- pagebreak -->

# Hardware tour

The current target is the **ESP32-C3 CrowPanel 1.28-inch round display** with a 240 x 240 GC9A01 LCD, CST816D touch controller, PI4IOE5V6408 I/O expander, and vibration motor. Enclosures and external controls are community-built and may vary.

![Line illustration of five Ble(e)p prototypes. The firmware and circular UI are shared across the enclosure colors.](assets/controller-family-line.png){width=6.4}

## Controls

| Control | Short action | Long action |
| --- | --- | --- |
| Touchscreen | Select cards, buttons, tabs, fields, devices, steps, and controls. Drag scrollable lists. | Where a screen offers a hold-to-confirm control, keep touching it for the displayed duration. |
| Optional GPIO 1 button | Runs the screen's primary action. On a sequence run screen it starts from Ready and stops once armed or while Start is running. On supported device screens it activates the primary Shark, Canon, GoPro, Phone Camera, or Tascam action. | At 700 ms: Back, cancel, or close the current overlay. Continue the same hold to 2 seconds: unwind navigation and return to Home. |
| Hardware power switch | Turns the controller hardware on or off. | No firmware action. The GPIO 1 action button does not control power. |

Accepted touches produce a short haptic tap when haptics are enabled. Ready uses two quick ticks, Back uses two uneven taps, and a newly surfaced error uses two strong pulses. Scrolling and canceled touches do not vibrate.

![Line illustration of the enclosure side profile. The orange hardware control belongs to this enclosure build; placement can differ.](assets/controller-side-line.png){width=2.0}

## Important hardware limits

- The screen and backlight remain on continuously in the current firmware.
- The CrowPanel does not measure its own battery voltage. Any battery value on the Shark screen belongs to the slider, not the controller.
- Up to 24 device records and 16 BLE bonds can be saved. Up to four physical BLE transport groups may remain connected. All Aputure Light and Zhiyun Light members on the panel-owned mesh share one physical group.
- Normal Home and Settings use does not start Wi-Fi. Wi-Fi runs only for Portal or an active Home Assistant owner, then returns to `WIFI_OFF`.

# First start and navigation

Ble(e)p always boots to a neutral Home screen. It does not scan, pair, reconnect, or open the last device automatically. This keeps startup predictable and avoids waking studio equipment unexpectedly.

![Home: Devices, Groups, Scenes, and Portal. The small cog opens Settings.](assets/ui-home.png){width=2.7}

## Home destinations

| Destination | Purpose | Current status |
| --- | --- | --- |
| Devices | Add physical equipment and Home Assistant entities; open, rename, enable, disable, disconnect, forget/re-pair, or delete saved records. | Available |
| Groups | Intended for named, capability-safe user groups. | Not implemented yet; do not confuse this with the internal shared light-mesh transport. |
| Scenes | Create and run ordered Start sequences with generated or custom Stop lists. | Available |
| Portal | Temporarily start setup or studio Wi-Fi administration. | Available; local plaintext HTTP on a trusted network only. |
| Settings cog | About/build information, saved Wi-Fi status, haptics, diagnostics, and Factory Reset. | Available |

## Everyday operating pattern

1. Power on and wait for Home.
2. Open **Devices** and select a saved device, or open **Scenes** and select a saved sequence.
3. Wait for the screen to report protocol-ready status. A Bluetooth link alone may not be enough.
4. Use touch or the optional action button.
5. Use Back or Done when finished. Healthy sessions can stay retained for quick reuse.
6. Use **Manage > Disconnect** when you need to release a link immediately.

<!-- pagebreak -->

# Add and manage devices

## Add a physical device

1. Open **Devices**.
2. Scroll to and select **Add device**.
3. Choose the equipment category, then the device family.
4. Put the real device in the exact pairing mode listed in this manual.
5. Wait for Ble(e)p to finish both connection and protocol setup.
6. Confirm the Ready state. The record is saved only after a first-time attempt reaches protocol ready; Back or a failed attempt leaves no half-paired device record.

![The Add device category picker. Device choices are grouped by capability rather than by one long brand list.](assets/ui-add-device.png){width=2.7}

## Manage a saved device

Open **Devices**, select the device's management control, then choose an available operation:

- **Open/control:** connect on demand and open the device UI.
- **Rename:** edit the panel-visible name.
- **Enable/disable:** hide or restore the device in operational pickers without deleting its configuration.
- **Disconnect:** release the active session explicitly.
- **Forget/re-pair:** remove the saved bond or pairing identity and repeat onboarding.
- **Delete/remove:** permanently remove the record after a named confirmation. Removal is blocked while a sequence still references the device.

The Devices list shows up to six records without paging, then pages six at a time. Long names scroll horizontally. Driver records omitted by a custom firmware build remain preserved but unavailable.

## Connection and state labels

| Label or behavior | Meaning |
| --- | --- |
| Connecting / Preparing | Ble(e)p is acquiring the transport and completing required protocol setup. |
| Ready | Required transport and protocol initialization succeeded. It does not prove a later physical action. |
| Pending | A command is awaiting device-originated confirmation or a bounded result. |
| Confirmed | State came from a decoded device or service response appropriate to that integration. |
| Optimistic / Sent | The command or response succeeded, but physical or recording state is not readable. |
| Unknown | No trustworthy state readback has arrived. Use the real device display or physical observation. |
| Unavailable / Failed | The device, protocol, or service did not become ready. Use Retry when shown, or return and reopen after checking the target. |

# Motion control: iFootage Shark Nano II

## Pair

1. Make the Shark Nano II discoverable.
2. On Ble(e)p choose **Devices > Add device > Motion > Shark Nano II**.
3. Ble(e)p matches service `0xFFF0` or an advertised name containing `Nano` or `Shark`.
4. Wait for the specialized Shark screen to become ready.

## Functions

- read slider battery, keypoints, run state, and progress;
- save and select keypoints A-H;
- move manually with positioning controls and joystick;
- set speed and hold timing per keypoint;
- choose run direction and looping;
- enter Standby, Start, and Stop the programmed move;
- reconnect on demand and retain a healthy session across navigation.

![Shark Run shows progress, direction, looping, and the primary run-state action.](assets/ui-shark-run.png){width=2.7}

> Slider movement can damage equipment or injure people. Clear the rail, secure the payload, and test at low speed. A GATT write or ACK is not proof that motion completed.

<!-- pagebreak -->

# Camera control

## Canon (Trigger): BR-E1-compatible mode

Use this for the fast, stateless Canon Bluetooth-remote workflow.

1. On the camera open its **Bluetooth remote / BR-E1** pairing menu.
2. On Ble(e)p choose **Canon (Trigger)**.
3. Complete pairing and wait for Ready.
4. Use **Trigger** on screen or the optional short-press hardware action.

The same movie trigger changes recording in either direction. Ble(e)p cannot read the camera's recording state in this mode and does not show separate Start and Stop commands.

Verified exact models: **Canon EOS R6 Mark II** and **Canon EOS R6 Mark III** for pairing, bonded reconnect, and the movie trigger. EOS R6 is not yet claimed.

## Canon (Smart): smartphone BLE experiment

1. On the camera choose **Connect to smartphone > Add a device to connect to**.
2. On Ble(e)p choose **Canon (Smart)**. A phone registration and BR-E1 pairing are different bonds.
3. Complete pairing, wait for setup, and use explicit **Record Start** or **Record Stop**.
4. Treat recording as confirmed only after a camera-originated notification.

The EOS R6 Mark III path supports smartphone-mode pairing, explicit record control, camera-reported recording state, captured automatic wake when reopening a camera Ble(e)p powered down, and explicit on-screen power-down. Power-down is blocked while recording is confirmed or a record command is pending. Back releases panel ownership but does not power off the camera.

Full automatic Wi-Fi/CCAPI control is not implemented. It remains blocked on network-side endpoint evidence. EOS R6 Mark II Smart needs a fresh camera-side Add a device test; an old phone registration can produce **Connection target not found**.

## GoPro

Ble(e)p implements bonded multi-instance discovery and Open GoPro Set Shutter Start/Stop. Choose **GoPro**, place a candidate camera in its supported wireless pairing mode, and wait for Ready. Successful protocol responses produce optimistic state; camera-confirmed recording status is not implemented.

No GoPro camera has been physically verified in this project snapshot. Treat only models in GoPro's current official Open GoPro support table as candidates, and do not infer support for legacy HERO8, MAX, or MINI models from retailer claims.

## Phone Camera

Ble(e)p behaves as a bonded BLE HID volume-up remote.

1. Add **Phone Camera** on Ble(e)p.
2. Open Bluetooth settings on the phone and pair with **Ble(e)p Shutter**.
3. Open a camera app that maps Volume Up to shutter.
4. Use the on-screen Shutter action or the short-press hardware action.

Ble(e)p can save up to four phone identities and sends to the authenticated peer. The phone OS controls reconnect behavior. The panel can confirm only that the HID report was sent, not that the app captured a photo or video.

Verified exact model: **Google Pixel 9** for bonded reconnect and mixed-sequence shutter operation. Other phone models, camera apps, iOS/Android/HarmonyOS coverage, and multi-phone operation remain unverified.

## Insta360

Ble(e)p emulates a GPS remote and sends a mode-dependent shutter toggle.

1. Open the camera's **GPS Remote** pairing menu.
2. Add **Insta360** on Ble(e)p and wait for the camera to connect to the panel.
3. Use **Shutter Toggle**. Generated Stop repeats the same toggle because no state or inverse command is known.

Verified exact model: **Insta360 X5** for GPS-remote connection and mixed-sequence shutter operation. **Insta360 GO 3** is an unverified candidate. **GO Ultra** is a separate experimental probe with no established legacy GPS-remote compatibility.

## DJI Osmo

1. Put the camera in its compatible remote-controller pairing flow.
2. Add **DJI Osmo** on Ble(e)p.
3. During first pairing, compare the four-digit code shown on the panel with the camera and approve it.
4. Wait for Ready, then use explicit Record Start/Stop.

Pairing and explicit recording start/stop are operator-confirmed on **DJI Osmo Action 5 Pro** and **DJI Osmo 360**. Osmo 360 camera-confirmed status has also passed a retest. Saved reconnect, Action 5 Pro status, forget/re-pair, two-camera concurrency, and broader coexistence remain open.

## Sony Camera

The Sony entry stops at a recoverable research screen. No device record is committed and no control is available. RMT-P1BT-compatible peripheral-role behavior still needs implementation and camera validation.

<!-- pagebreak -->

# Audio recording: Tascam Portacapture X8

The bounded Tascam integration requires the **AK-BT1** Bluetooth adapter.

## Pair and operate

1. Install the AK-BT1 in the Portacapture X8.
2. Make the recorder available to its remote-app connection.
3. Choose **Tascam X8** on Ble(e)p and wait for Ready.
4. Use explicit **Record Start** and **Record Stop**. The hardware button invokes the primary action for the current confirmed state.
5. Trust Recording or Stopped only after the recorder's own protocol event or restored state field is decoded.

Verified: start/stop, media-file creation, persisted reconnect, state restoration after remote restart, stopping an existing recording, and idempotent Stop when already stopped. Battery, media status, and broader recorder features remain research work.

# Light control

## Aputure Light

The generic **Aputure Light** entry provisions compatible factory-reset Aputure and amaran fixtures into one panel-owned mesh. All members share one retained proxy link, but user-facing device records and supported per-member color routes stay separate.

1. Factory-reset the target fixture and place only the intended nearby light in provisioning mode.
2. Choose **Devices > Add device > Lights > Aputure Light**.
3. Wait for PB-GATT provisioning, mesh configuration, proxy discovery, and Ready.
4. Use explicit On/Off, CCT/tint/brightness, or RGB/saturation/brightness controls as exposed.
5. Observe the fixture. Power has authenticated per-source readback on the tested path; color-property values remain optimistic.

![Aputure Light RGB controls. Color values are responsive, but property readback remains optimistic.](assets/ui-aputure-rgb.png){width=2.7}

Physical evidence currently covers **amaran Ace 25c** and **Aputure MC Pro** provisioning, common-group physical power, separate vendor-group RGB output, and authenticated per-member power/reachability. CCT/property readback, composition-driven enforcement, interrupted provisioning recovery, safe reset, and amaran Pano 60c/120c validation remain open.

> Do not describe common mesh power as independent fixture power. The tested Ace 25c/MC Pro pair uses mesh-wide common-group On/Off; separate per-member vendor groups are verified for RGB/color routing.

## Zhiyun Light: MOLUS X100 and X60RGB

1. Choose **Add device > Lights > Zhiyun Light** once for each fixture.
2. Keep one intended X100 or X60RGB nearby. Factory-reset and previously provisioned fixtures are accepted.
3. Wait while Ble(e)p detects the model, provisions if needed, rediscovers the proxy, opens proprietary control, and reads initial state.
4. Use power and CCT/brightness. X60RGB also exposes hue/saturation RGB control.

![X60RGB control after correlated device replies. X100 uses the same driver but omits RGB.](assets/ui-zhiyun-rgb.png){width=2.7}

**MOLUS X100:** implemented with power and 2700-6500 K CCT/brightness. Current progress records a panel-live verified path; boundary, power-cycle, multi-fixture, recovery, and coexistence gates remain open before production status.

**MOLUS X60RGB:** adds captured hue/saturation control. Host-originated optical evidence exists, while the shared embedded panel path, reconnect, reset recovery, simultaneous X100/X60RGB use, and full coexistence gates remain open.

Existing Sidus/amaran mesh import is not supported. Aputure and Zhiyun share internal mesh transport only; Zhiyun remains a separate logical driver.

<!-- pagebreak -->

# Scenes and sequences

Scenes coordinate supported actions across saved device instances and Home Assistant entities. The panel prepares targets concurrently, then executes action and wait steps in order. There is no configured scene-count ceiling; creation continues until safe allocation or persistence fails.

## Create a scene on the panel

1. Open **Scenes** and select **Add sequence**.
2. Build the **Start** list with **Add step**. Choose a target, action, and any parameters.
3. Add Wait steps in milliseconds where equipment needs time between actions.
4. Select the header arrow to review the generated **Stop** list. It reverses Start order and uses safe inverses where known.
5. Optional: choose **Customize Stop** to copy the generated list into an independently editable Stop list.
6. Use the checkmark, enter a name, and save.

Editing Start regenerates the read-only generated Stop preview. **Use generated Stop** discards a custom override only after confirmation. Existing steps can be edited and reordered. An orphaned custom row remains deletable if its target device was removed.

## Run a scene

1. Open the scene. Ble(e)p begins preparing every target.
2. Wait for **Ready**. Each target must have a physical transport and completed protocol initialization.
3. Select Start or short-press the hardware action button.
4. Watch per-step progress. Circular target chips show readiness and open full device controls without releasing the other scene links.
5. Once armed, use Stop or the hardware action button. A partial Start failure can still run Stop for cleanup, then allow Start to be retried.
6. Select Done or Back when finished.

![A prepared multi-target sequence. Target chips provide readiness and access to retained device controls.](assets/ui-scene-ready.png){width=2.7}

Opening scene Settings cancels pending preparation and releases ownership so editing is safe. It does not interrupt an active Start, armed recording, Stop, or a partial failure that still permits cleanup. Parallel steps, user Groups, import/export, and success-journal rollback are not implemented.

## Action behavior to understand

| Integration | Start/Stop behavior in scenes |
| --- | --- |
| Canon Trigger | Stateless Record Trigger; state remains unknown. |
| Canon Smart, DJI, Tascam, GoPro | Separate start/stop actions; confidence depends on each driver's readback boundary. |
| Insta360 | Explicit Shutter Toggle in both authored lists; generated Stop repeats it. |
| Phone Camera | Sends a volume-up HID shutter report; app response is not observable. |
| Lights and HA switch-like entities | Explicit On/Off. Aputure Set color carries CCT or RGB parameters. |
| Home Assistant button | Press. |
| Home Assistant scene/script | Activate. |
| Wait | Non-blocking millisecond delay. |

# Portal and Home Assistant

Portal is a temporary administration mode. It suspends normal physical-device control while active and tears down the server and Wi-Fi when you leave.

## First-time Portal setup

1. From Home open **Portal**.
2. Scan the on-panel QR code or manually join the temporary `Bleep-Setup-...` WPA2 network with password `12345678`.
3. Open the phone's sign-on page, or browse to the numeric setup address on the panel.
4. Scan for or manually enter the trusted studio Wi-Fi and password.
5. After Ble(e)p joins, note its numeric LAN address. Rejoin the normal studio Wi-Fi on your phone or computer.
6. Open the numeric address while the panel remains on Portal. `http://bleep.local` is a best-effort convenience and may not resolve on every network.

![Portal after LAN handoff. The address exists only while the Portal screen is active.](assets/ui-portal-lan.png){width=2.7}

The browser Portal provides **Overview**, **Devices**, **Sequences**, and **Home Assistant**. It can rename, enable, disable, or remove committed devices and create, duplicate, reorder, and edit sequences. Physical pairing stays on the panel. Choose **Finish & Exit** in the browser or Exit on the panel when finished.

## Link Home Assistant entities

1. In Portal open **Home Assistant**.
2. Enter a local `http://` Home Assistant URL and long-lived access token.
3. Select at most four canonical entity IDs in the supported domains.
4. Save and exit Portal.
5. Open the saved entity under Devices or add it to a sequence.

Supported: `light`, `switch`, and `input_boolean` power; `button` Press; `scene` and `script` Activate. Unsupported: brightness, color, Toggle, arbitrary values, sensors, covers, climate, media, automations, HA devices/areas, cloud/OAuth, and exposing Ble(e)p-controlled equipment back to HA.

An authenticated WebSocket subscription can be Ready while initial state remains `UNKNOWN`. A successful service result proves action delivery, including an idempotent action with no state-change event; it does not by itself prove a changed external state.

> Portal and Home Assistant credentials use local plaintext HTTP in this development version. Use only a trusted studio network. Portal is temporary, but saved Wi-Fi credentials and the HA token persist until unlinked or Factory Reset.

<!-- pagebreak -->

# Settings, diagnostics, and reset

Open the cog on Home for local settings that do not start the radio.

![Settings includes About, Wi-Fi status, haptics, system information, and Factory Reset.](assets/ui-settings.png){width=2.7}

| Function | What it does |
| --- | --- |
| About | Shows the Ble(e)p identity, firmware/build information, and commit date. |
| Wi-Fi | Shows saved SSID and the normal radio-off state. Changing Wi-Fi transfers to Portal. |
| Haptics | Persistently enables or disables semantic vibration patterns. |
| System Info | Shows sanitized heap, largest allocation, minimum heap, physical BLE groups, and Wi-Fi mode without exposing secrets or stable device identifiers. |
| Factory Reset | After a separate warning and three-second hold, cancels work, deactivates transports, erases the complete NVS configuration partition, and reboots. |

## Factory Reset consequences

Factory Reset removes saved devices, BLE bonds, scenes, Wi-Fi credentials, Home Assistant tokens/entities, mesh identity/keys, and preferences. It does **not** erase the installed firmware application.

The `0.2.0-dev` Aputure Light naming/storage baseline intentionally does not migrate former Amaran driver IDs or the old `amaran_mesh` key. Before flashing this build over earlier development firmware, perform Factory Reset from the currently installed firmware. Do not erase flash regions manually unless you have confirmed the board partition table and intend to destroy configuration.

# Complete user-function reference

| Area | Function | Availability and boundary |
| --- | --- | --- |
| Startup | Neutral Home boot | Available; no automatic scan, pairing, reconnect, or Wi-Fi. |
| Navigation | Touch cards, lists, tabs, overlays, Back/Done | Available; round-safe UI with scrolling titles. |
| Feedback | Tap, Ready, Back, Error haptic patterns | Available and globally toggleable. |
| Hardware action | Short primary action; 700 ms Back; 2 s Home | Available when optional GPIO 1 button is fitted. No power action. |
| Registry | Save up to 24 devices; retain dormant records | Available. Up to 16 BLE bonds. |
| Device setup | Add, protocol-ready commit, cancel/retry | Available per implemented driver. Physical pairing stays on panel. |
| Device management | Open, rename, enable, disable, disconnect, forget/re-pair, delete | Available; referenced devices cannot be deleted. |
| Connection pool | Retain and reuse up to four physical BLE groups | Available. Eight logical active instances; one shared light mesh consumes one group. |
| Shark | Battery, A-H keypoints, manual/joystick move, speed, hold, direction, loop, run/progress | Current bounded hardware path. Movement needs physical observation. |
| Cameras | Trigger, explicit Start/Stop, toggle, power, status, or HID shutter | Varies by driver; see compatibility matrix. |
| Tascam | Record Start/Stop and restored confirmed recording state | Current bounded X8 + AK-BT1 path. |
| Aputure Light | On/Off, CCT/tint/brightness, RGB/saturation/brightness, Set color scene action | Experimental; power/status evidence is stronger than color-property state. |
| Zhiyun X100 | On/Off and CCT/brightness | Experimental, correlated readback path. |
| Zhiyun X60RGB | On/Off, CCT/brightness, hue/saturation | Experimental, correlated capture-backed replies. |
| Scenes | Create, rename, enable, disable, duplicate, delete | Available on panel/Portal as applicable. |
| Scene editing | Ordered Action/Wait, in-place edit, reorder, generated Stop, Custom Stop | Available. Parallel, groups, import/export, and rollback journal are not. |
| Scene running | Concurrent preparation, Ready gate, Start/Stop, cleanup after partial failure, target chips | Available within transport and driver limits. |
| Portal | Temporary setup AP, LAN handoff, device/sequence/HA administration | Experimental; local plaintext HTTP, active only on Portal screen. |
| Home Assistant | Four entities; light/switch/input_boolean On/Off, button Press, scene/script Activate | Experimental local integration. No cloud/OAuth or inbound HA exposure. |
| Groups | Capability-intersection user groups | Not implemented. Internal shared mesh transport is not a user Group. |
| Settings | About, saved Wi-Fi status, haptics, diagnostics | Available without starting radio. |
| Recovery | Named delete confirmations, Retry paths, three-second Factory Reset | Available. Factory Reset destroys all NVS configuration, not firmware. |
| Power/battery | Hardware switch; Shark battery readout | No firmware power command for the controller and no controller battery gauge. |

<!-- pagebreak -->

# Device compatibility matrix

Compatibility is intentionally exact. “Implemented” is not the same as “verified on this model.”

| Device or service | Status | Available functions | Key limitation or open gate |
| --- | --- | --- | --- |
| iFootage Shark Nano II | Current | Pair/reconnect, battery, A-H keypoints, manual movement, timing, loop/direction, run/progress | Preserve physical movement safety; broad endurance remains development work. |
| Canon EOS R6 Mark II via BR-E1 | Current bounded path | Stateless movie trigger, bonded reconnect | No recording-state readback. |
| Canon EOS R6 Mark III via BR-E1 | Current bounded path | Stateless movie trigger, bonded reconnect | No recording-state readback. |
| Canon EOS R6 | Unverified candidate | Driver architecture includes R6 family intent | Exact model is not claimed. |
| Canon EOS R6 Mark III smartphone mode | Experimental; verified path | Pair/reconnect, explicit record start/stop, camera notifications, wake/power-down | Full Wi-Fi/CCAPI is blocked; extended coexistence remains open. |
| Canon EOS R6 Mark II smartphone mode | Experimental candidate | Same implemented BLE path | Needs fresh camera-side pairing and hardware verification. |
| GoPro models in current official Open GoPro table | Experimental candidates | Bonded pairing, response-gated shutter start/stop | No camera has been tested; state is optimistic. |
| Google Pixel 9 | Experimental; verified path | Bonded BLE HID volume-up shutter and reconnect | App result is not observable; other phones remain unverified. |
| Other iOS/Android/HarmonyOS phones | Implemented candidates | BLE HID volume-up shutter | Model/app and multi-phone tests remain open. |
| Insta360 X5 | Experimental; verified path | GPS-remote pairing and shutter toggle in mixed sequence | No camera-state readback. |
| Insta360 GO 3 | Candidate | Implemented GPS-remote path | No model-specific result. |
| Insta360 GO Ultra | Experimental probe | Separate visible target | Legacy GPS-remote compatibility is not established. |
| DJI Osmo Action 5 Pro | Experimental; verified path | Four-digit pairing, explicit record start/stop | Reconnect, camera status, forget/re-pair, coexistence open. |
| DJI Osmo 360 | Experimental; verified path | Four-digit pairing, explicit record start/stop, confirmed-status retest | Reconnect, forget/re-pair, multi-camera coexistence open. |
| Sony RMT-P1BT-compatible cameras | Research | Capture-required screen only | No savable driver or camera test. |
| Tascam Portacapture X8 + AK-BT1 | Current bounded path | Record start/stop, confirmed/restored recording state | Battery, media status, extended behavior remain research. |
| Home Assistant local entities | Experimental; mixed path verified | Four `light`, `switch`, `input_boolean`, `button`, `scene`, or `script` entities | Full domains, failure recovery, and lifecycle/heap gate open. |
| amaran Ace 25c | Experimental; verified light path | Provisioning, common-group power, separate RGB route, authenticated power status | Independent fixture power is not verified; CCT/property readback and recovery open. |
| Aputure MC Pro | Experimental; verified light path | Provisioning, common-group power, separate RGB route, authenticated power status | Same common-power boundary; recovery and wider model coverage open. |
| amaran Pano 60c / Pano 120c | Validation targets | Generic Aputure Light implementation may be applicable | No model-specific validation yet. |
| Zhiyun MOLUS X100 | Experimental; verified path | Power and CCT/brightness with correlated readback | Boundary, power-cycle, multi-fixture, recovery, and coexistence gates remain open. |
| Zhiyun MOLUS X60RGB | Experimental | Power, CCT/brightness, hue/saturation | Shared embedded panel path and broader gates remain open. |
| Deity PR4 | Later | None | Protocol and transport research have not started. |

# Troubleshooting

## A device does not appear

- Confirm the target is in the correct pairing menu, not merely powered on.
- Keep only the intended first-time light or camera nearby when discovery could match multiple devices.
- Return with Back, re-enter the target's pairing mode, and retry. Failed first-time attempts do not create a saved record.
- If the device was paired to another phone or remote, remove the old registration or use Forget/re-pair as appropriate.
- For Canon, do not interchange BR-E1 and smartphone bonds.

## A saved device will not reconnect

- Open the saved device so it acquires an owner; an ownerless dropped session does not reconnect indefinitely.
- Confirm the target is awake and still trusts Ble(e)p.
- Use **Manage > Disconnect**, then reopen.
- Use **Forget/re-pair** only when you intend to remove the existing bond.
- If all four physical groups are occupied, close or disconnect an unneeded session so normal retained-session eviction can proceed.

## A command says Sent, Optimistic, or Unknown

This is expected when the integration cannot read physical state. Check the camera display, recorder, light output, phone app, or slider directly. Do not repeat a toggle blindly when the real state is uncertain.

## A scene cannot reach Ready

- Open the failed target chip or device and resolve its pairing/protocol issue.
- Confirm every referenced device is enabled and available in the installed build.
- Check the four-link budget. The shared Aputure/Zhiyun mesh counts once; Home Assistant uses Wi-Fi and no BLE group.
- Use the in-place Retry flow when shown. If preparation is still pending, opening scene Settings cancels it safely.

## Portal does not open

- Keep the panel on the Portal screen. Leaving it destroys the listener and Wi-Fi session.
- During first setup, join `Bleep-Setup-...` and try the numeric setup address if the phone sign-on page does not appear.
- After LAN handoff, rejoin studio Wi-Fi and use the numeric address shown on the panel.
- Treat `bleep.local` as optional; multicast DNS may be blocked by the network.
- Portal has no TLS. Do not expose it to an untrusted or routed network.

## The controller behaves like an older development build

Check **Settings > About** for build identity. If moving from firmware that used the former Amaran identity/storage key to `0.2.0-dev`, perform the documented Factory Reset before flashing. Remember that reset removes all configuration and bonds.

# Safety, privacy, and known limits

- Secure cameras, sliders, lights, cables, and recorders before sending commands.
- Observe real hardware for movement, recording, power, and light-output confirmation whenever state is optimistic or unknown.
- Use Portal and Home Assistant only on a trusted local network; credentials cross local plaintext HTTP in this version.
- Treat mesh keys, Wi-Fi passwords, HA tokens, BLE bonds, and device identities as secrets. Factory Reset removes them from the controller's NVS.
- Do not rely on Groups, Canon Wi-Fi/CCAPI, existing Sidus mesh import, scene Parallel, import/export, success-journal rollback, Sony control, Deity control, or controller battery measurement; they are not implemented.
- Four retained BLE transport groups, 16 bonds, and constrained ESP32-C3 memory are product limits of this profile, not suggestions to bypass without measurement.
- This project is independent and is not endorsed by iFootage, Canon, GoPro, Insta360, DJI, Sony, Tascam, Aputure, amaran, Zhiyun, Deity, Espressif, Elecrow, or Home Assistant.

# Maintaining this manual

The editable source is `docs/manual/manual.md`; assets are in `docs/manual/assets/`; the reproducible PDF builder is `docs/manual/build_manual.py`. The stable output is `output/pdf/bleep-instruction-manual.pdf`.

## Update checklist

1. Confirm current behavior in `README.md`, `docs/device-support.md`, `docs/progress.md`, decisions, and the relevant roadmap phase.
2. Update exact model claims. Never widen compatibility from a shared protocol alone.
3. Label ACK-only or send-only behavior as optimistic/unknown and record physical evidence separately.
4. Replace simulator figures when the illustrated UI changes and update the source snapshot in the front matter.
5. Build with the pinned requirements and inspect every rendered page for clipping, overflow, broken tables, or unreadable images.
6. Keep the manual explicitly development/WIP until the repository's hardware and release gates pass.

Build commands:

```sh
cd docs/manual
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
make PYTHON=.venv/bin/python pdf
```

The detailed architecture, protocol evidence, and implementation roadmap remain in the repository documentation. This manual summarizes user-visible behavior; it does not replace those engineering sources of truth.
