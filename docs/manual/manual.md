---
title: "Ble(e)p Owner's Guide"
subtitle: "Set up your gear, control a shoot, and build repeatable studio workflows"
edition: "0.3.6"
date: "16 August 2026"
status: "Development hardware - verify before critical work"
author: "Ble(e)p project"
---

# Welcome

Ble(e)p brings your cameras, lights, audio gear, slider, phone, and Home
Assistant controls together in one small touchscreen remote. You can also save
common actions as sequences and run them in order. Its playful name means
**Bluetooth Links Everything, Eventually, Probably**; the practical version is
**Bluetooth Low Energy Equipment Panel**.

![Five Ble(e)p enclosure finishes. All five use the same circular interface.](assets/controller-family-line.png){width=6.4}

> Ble(e)p is still in development - the "Eventually, Probably" part is there for a reason. Before an important shoot, check that your exact model is supported. When Ble(e)p says **Sent**, **Optimistic**, or **Unknown**, look at the equipment itself to make sure the action happened.

## Status words used here

| Label | Meaning |
| --- | --- |
| Supported | Verified on the exact model for the listed functions. |
| Experimental | You can use it, but some situations still need testing. |
| Candidate | It may work, but this exact model has not been tested. |
| Research | It is being investigated and cannot be controlled yet. |
| Later | Not implemented. |

# Quick start

Ble(e)p starts at Home and leaves your equipment disconnected until you open a
device or run a sequence. If you have saved Wi-Fi, it briefly connects about
five seconds after Home appears to check for a safe, signed update. It turns
Wi-Fi off again when the check is finished.

1. Open **Devices**.
2. Open a saved device, or choose **Add device** and follow its pairing steps.
3. Wait for **Ready**.
4. Use the on-screen control or optional Action Button.
5. Check the equipment when the result is **Sent**, **Optimistic**, or **Unknown**.

## Action Button

A short press runs the main action on the current screen. Hold it for about
700 ms to go Back or cancel, or for two seconds to return Home. It is not a
power button.

## Action Button by screen

| Screen | Short press |
| --- | --- |
| Canon - Trigger Mode | Sends the movie trigger, which starts or stops recording without reading the camera's state. |
| Canon Smart, GoPro, DJI Osmo, or Tascam | Starts or stops recording according to the confirmed state. |
| Phone Camera | Sends the shutter command. |
| Insta360 | Runs Record Start while video is idle or Record Stop while recording. It does nothing while the camera reports photo mode or while recording state is unavailable. |
| Aputure, amaran, or Zhiyun light | Toggles the selected light On or Off. |
| Shark Nano II Keypoints | Opens the Run screen. |
| Shark Nano II Run | Runs the displayed Standby, Start, or Stop action. |
| Home Assistant entity | Light or switch: On; input boolean: On/Off toggle; button: Press; scene or script: Activate. Use the on-screen Off control for a Home Assistant light or switch. |
| Scene Run | Starts when Ready; once started or armed, stops the scene. Scene lists, editors, settings, and pickers ignore short presses. |

On supported device screens, a short press also selects **Retry** when it is shown.

# Find your way around

![Home: Devices, Groups, Scenes, and Portal. The small cog opens Settings.](assets/ui-home.png){width=2.7}

From Home, choose **Devices** to control equipment, **Scenes** to run saved
workflows, **Portal** to set up Ble(e)p in a browser, or the cog to open
Settings. **Groups** is not available yet.

<!-- pagebreak -->

# Set up your equipment

## Add a physical device

1. Open **Devices**.
2. Scroll to and select **Add device**.
3. Choose the equipment category, then the device family.
4. Put the real device in the exact pairing mode listed in this manual.
5. Wait for Ble(e)p to finish connecting and show **Ready**.

![The Add device screen groups equipment into simple categories.](assets/ui-add-device.png){width=2.7}

## Manage a saved device

Open a device's management menu to rename, enable, disable, disconnect, forget/re-pair, or delete it. Disabled devices are hidden from control and sequence lists without being deleted. Remove a device from every sequence before deleting it.

If your installed firmware does not include a saved device type, the entry
stays saved but cannot be opened.

## Connection and state labels

| Label or behavior | Meaning |
| --- | --- |
| Connecting / Preparing | Ble(e)p is connecting and getting the device ready. |
| Ready | You can send a command. Watch the equipment to make sure the action happens. |
| Pending | Ble(e)p is waiting for the device to answer. |
| Confirmed | The device reported the displayed state. |
| Optimistic / Sent | Ble(e)p sent the command but cannot check the physical result. |
| Unknown | Ble(e)p does not have a reliable current state. Check the equipment itself. |
| Unavailable / Failed | The device did not become ready. Check it and choose Retry, or go Back and reopen it. |

# Control cameras

## Canon cameras

Canon Trigger Mode and Smart Phone Mode use separate camera pairings. **Retry** reconnects the camera saved for that entry; choose **Forget** before replacing it with another body.

| Mode | Best for | Controls and feedback |
| --- | --- | --- |
| Trigger Mode | The quickest remote-control setup | One movie trigger starts or stops recording. Ble(e)p cannot read the recording state. |
| Smart Phone Mode | Separate recording controls and camera feedback | Record Start, Record Stop, confirmed recording state, automatic wake, and on-screen power-down on the supported EOS R6 Mark II and Mark III paths. |

### Trigger Mode

1. On the camera open its **Bluetooth remote / BR-E1** pairing menu.
2. On Ble(e)p choose **Canon (Trigger)**.
3. Complete pairing and wait for Ready.
4. Use **Trigger** on screen or press the Action Button.

The same trigger starts and stops recording, so check the camera before pressing it again. Trigger Mode is supported on the **Canon EOS R6 Mark II** and **R6 Mark III**. The original R6 has not been tested.

### Smart Phone Mode

1. On the camera choose **Connect to smartphone > Add a device to connect to**.
2. On Ble(e)p choose **Canon (Smart)**.
3. Complete pairing, then use **Record Start** or **Record Stop**.

Smart Phone Mode supports confirmed recording state and automatic wake on the **EOS R6 Mark II** and **R6 Mark III**. The R6 Mark III also supports on-screen power-down; it is disabled during recording or a pending command. If the camera shows **Connection target not found**, remove its old phone registration and pair again.

## GoPro

Choose **GoPro**, put the camera in wireless pairing mode, and wait for Ready. Recording state and Start/Stop results are camera-confirmed.

Use the top-right power icon to sleep or wake an idle camera. Opening a sleeping GoPro or preparing it for a sequence also wakes it. GoPro documents remote BLE wake for eight hours after sleep; do not rely on it indefinitely.

The **GoPro MAX2** is supported for pairing, confirmed Start/Stop, Sleep, wake, reconnection, and return to Ready. Other GoPro models have not been verified.

## Phone Camera

Ble(e)p appears to the phone as a wireless volume-up remote, which many camera apps can use as a shutter button.

1. Add **Phone Camera** on Ble(e)p.
2. Open Bluetooth settings on the phone and pair with **Ble(e)p Shutter**.
3. Open a camera app that maps Volume Up to shutter.
4. Use the on-screen Shutter action or press the Action Button.

Ble(e)p can remember four phones but cannot confirm that the camera app captured a photo or video. Automatic reconnection and mixed-sequence shutter operation are verified on the **Google Pixel 9**; other combinations remain unverified.

## Insta360

Ble(e)p appears as **Insta360 Remote (Bleep)** and uses the camera's reported state to offer Start or Stop.

1. Open the camera's **GPS Remote** pairing flow.
2. Add **Insta360** on Ble(e)p and wait for the camera to connect to the panel.
3. Wait for the panel to show idle or recording state.
4. Use **Start** while video is idle or **Stop** while recording. No recording action is shown in photo mode or when state is unavailable.
5. On X5, use the on-screen power control to shut down or wake the camera. Wake
   can take up to 60 seconds while Ble(e)p waits for it to reconnect.

The **Insta360 X3**, **X4**, **X4 Air**, and **X5** are supported. On X5,
recording state, Start/Stop, photo-mode feedback, shutdown, and physical wake
have been tested. Waking the camera after leaving and returning to its control
screen still needs more testing. **GO 3** and **GO Ultra** are unverified.

## DJI Osmo

1. Put the camera in its compatible remote-controller pairing flow.
2. Add **DJI Osmo** on Ble(e)p.
3. During first pairing, compare the four-digit code shown on the panel with the camera and approve it.
4. Wait for Ready, then use explicit Record Start/Stop.

Pairing, Start/Stop, and confirmed recording state are verified on the **DJI Osmo Action 5 Pro** and **DJI Osmo 360**. Reconnection, forget/re-pair, and two-camera use need more testing.

## Sony Camera

The Sony entry is for research only. It does not save a camera or provide controls yet.

<!-- pagebreak -->

# Control lights

## Aputure Light

**Aputure Light** adds compatible, factory-reset Aputure and amaran fixtures.
Several compatible lights can share one Bluetooth connection while you still
control each light separately.

1. Factory-reset the light and place it nearby in pairing mode.
2. Choose **Devices > Add device > Lights > Aputure Light**.
3. If several compatible fixtures appear, choose the intended name and address suffix.
4. Leave the light powered while Ble(e)p sets it up. The light may restart;
   wait for **Ready**.
5. Use On/Off, color temperature (CCT), tint, brightness, or RGB color controls as shown.
6. Check the fixture when a change is shown only as sent.

![Aputure Light RGB controls. Color changes are sent quickly, but the displayed values are not read back from the light.](assets/ui-aputure-rgb.png){width=2.7}

The controls adapt to each light. Ble(e)p remembers its CCT and RGB looks, brightness, mode, and power state. Changing tabs applies the stored look only while the light is On.

The **amaran Ray 60c**, **amaran Ace 25c**, **Aputure MC Pro**, and **Aputure MT Pro** are supported. The MT Pro currently appears as MC Pro because the tested fixtures report the same identity. Pano 60c and Pano 120c remain candidates.

## Zhiyun Light: MOLUS X100 and X60RGB

1. Choose **Add device > Lights > Zhiyun Light** once for each fixture.
2. If several compatible fixtures appear, choose the intended name and address suffix.
3. Wait while Ble(e)p identifies the model, connects, and reads its settings.
4. Use power, color temperature, and brightness. The X60RGB also offers hue and saturation controls.

![X60RGB control. The X100 uses the same layout without the RGB tab.](assets/ui-zhiyun-rgb.png){width=2.7}

**MOLUS X100:** power, 2700-6500 K color temperature, and brightness. Everyday
control is tested. Reconnecting after a power cycle and using several devices
together need more testing.

**MOLUS X60RGB:** adds hue and saturation. Its color commands have been tested,
but everyday panel use, reconnecting, recovery after a reset, and use with
other lights need more testing.

Sidus Link network import is not supported. Aputure and Zhiyun lights share one connection but remain separate controls. Keep a compatible saved Zhiyun fixture powered whenever a Zhiyun target is active; it acts as the shared gateway.

# Control audio

## Tascam Portacapture X8

Tascam control requires the **AK-BT1** Bluetooth adapter.

1. Install the AK-BT1 in the Portacapture X8.
2. Make the recorder available to its remote-app connection.
3. Choose **Tascam X8** on Ble(e)p and wait for Ready.
4. Use **Record Start** and **Record Stop**. The Action Button performs the appropriate action for the state shown.

Start/Stop, reconnection, and restored recording state are verified. Battery, media status, and other recorder features are not available.

<!-- pagebreak -->

# Control motion

## iFootage Shark Nano II

### Pair

1. Make the Shark Nano II discoverable.
2. On Ble(e)p choose **Devices > Add device > Motion > Shark Nano II**.
3. Wait for the Shark control screen to show **Ready**.

### Functions

- read slider battery, keypoints, run state, and progress;
- save and select keypoints A-H;
- move manually with positioning controls and joystick;
- set speed and hold timing per keypoint;
- choose run direction and looping;
- enter Standby, Start, and Stop the programmed move;
- reconnect when opened.

![Shark Run shows progress, direction, looping, and the primary run-state action.](assets/ui-shark-run.png){width=2.15}

> Slider movement can damage equipment or injure people. Clear the rail, secure the payload, and test at low speed. Watch the slider until the move is complete.

# Connect Wi-Fi and Home Assistant

Portal lets you configure Ble(e)p from a phone or computer. Device control is
paused while Portal is open.

## First-time Portal setup

1. From Home open **Portal**.
2. Scan the QR code or join the open `Bleep-Setup-XXXXX` network matching the panel suffix.
3. Open the phone's sign-on page, or browse to the numeric setup address on the panel.
4. Manage devices and sequences directly; studio Wi-Fi is not required.
5. To use Home Assistant or configure a hidden network, open **Wi-Fi**, choose a network found during Portal entry or enter its SSID, and supply its password.
6. After Ble(e)p joins, note the web address shown on its screen. Rejoin the normal studio Wi-Fi on your phone or computer.
7. Open the displayed address while Ble(e)p remains on the Portal screen. You can also try `http://bleep.local`.

![Portal after Ble(e)p joins your normal Wi-Fi. The address works only while the Portal screen is open.](assets/ui-portal-lan.png){width=2.7}

Pair new physical equipment on Ble(e)p itself. Choose **Finish & Exit** in the browser or Exit on the panel when finished.

## Link Home Assistant entities

1. In Portal open **Home Assistant**.
2. Enter your server's local URL and a long-lived access token. Choose **Change stored token** only when replacing one.
3. Select up to four supported Home Assistant entities.
4. Save and exit Portal.
5. Open the saved entity under Devices or add it to a sequence.

Supported entities: lights, switches, input booleans, buttons, scenes, and scripts. Light and switch control is power-only.

Home Assistant can be Ready while an entity still shows **Unknown**. A successful command means Home Assistant accepted it; check the target if you need to confirm that something physically changed.

> The `Bleep-Setup` network is open, and Portal traffic is not encrypted. Use it only in a controlled place. Connect Portal to your normal Wi-Fi only when you trust that network.

# Build repeatable workflows

A **scene**, also called a **sequence**, runs a saved list of actions and waits
across one or more devices.

## Create a scene on the panel

1. Open **Scenes** and select **Add sequence**.
2. Build the **Start** list with **Add step**. Choose a target, action, and any parameters. For a light, **Set look + On** combines its color, brightness, and power-on in one step.
3. Add Wait steps where needed. New waits start at 200 ms; the included Press Record example uses 500 ms.
4. Select the arrow in the header to review the automatic **Stop** list. Ble(e)p
   reverses the Start order and adds a matching Stop action where it can.
5. Optional: choose **Customize Stop** if you want to edit that Stop list
   yourself.
6. Use the checkmark, enter a name, and save.

When you change Start, Ble(e)p updates the automatic Stop preview. New light
looks begin at 5600 K, 50% brightness, neutral tint, and full RGB saturation.
You can preview changes on the selected light. **Set look + On** adds one
**Turn Off** action to Stop.

**Use generated Stop** replaces a custom Stop list after confirmation. You can edit and reorder existing steps; unavailable-device steps can still be deleted from a custom Stop list.

## Run a scene

1. Open the scene. Ble(e)p begins preparing every target.
2. Wait for **Ready**. Every target must be connected and prepared.
3. Select Start or press the Action Button.
4. Watch each step progress. Circular equipment shortcuts open individual controls.
5. Once armed, use Stop or press the Action Button. If Start fails partway through, you can still run Stop for cleanup and then try Start again.

![A prepared multi-device sequence. The circular equipment shortcuts show readiness and open individual controls.](assets/ui-scene-ready.png){width=2.7}

Opening scene Settings cancels preparation. It does not interrupt a Start or
Stop already in progress, an armed recording, or a failed Start that may still
need cleanup.

## Action behavior to understand

| Integration | Start/Stop behavior in scenes |
| --- | --- |
| Canon Trigger | Record Trigger; Ble(e)p cannot tell whether recording started or stopped. |
| Canon Smart, DJI, Tascam, GoPro | Separate Start and Stop actions. Confirmation varies by device. |
| Insta360 | Separate Start and Stop actions. Start is available after a fresh video-mode connection; Stop appears after the camera confirms it is recording. |
| Phone Camera | Sends a volume-up command; Ble(e)p cannot see what the camera app did. |
| Lights and Home Assistant switches | Separate On/Off. A light's Set look + On applies its stored CCT or RGB look and turns on that fixture; generated Stop adds one Turn Off. |
| Home Assistant button | Press. |
| Home Assistant scene/script | Activate. |
| Wait | Pause for the chosen number of milliseconds. |

<!-- pagebreak -->

# Personalize, diagnose, and reset

Open the Home cog for version information, Wi-Fi, firmware updates, haptics,
diagnostics, and Factory Reset.

![Settings includes About, Wi-Fi, Firmware update, haptics, system information, and Factory Reset.](assets/ui-settings.png){width=2.7}

## Connect Wi-Fi from the panel

Opening **Settings > Wi-Fi** does not connect by itself. To join or replace a
visible network:

1. Choose **Scan networks** or **Replace network**.
2. If Ble(e)p is keeping equipment connected, confirm **Disconnect & scan**.
   An active command must finish first.
3. Select the network. Enter its password on the masked round keyboard when
   required.
4. Wait for Ble(e)p to connect. It saves the network only after the connection
   succeeds, then turns Wi-Fi off again.

![Visible networks are listed by signal strength after an explicit scan.](assets/ui-wifi-networks.png){width=2.7}

Use Portal for a hidden SSID, easier phone-based text entry, or Home Assistant
setup. **Forget network** removes only the saved SSID and password; it preserves
Home Assistant settings.

## Install a firmware update

Stable releases are selected by default. You can opt into Development builds,
but they may not have been tested as thoroughly on real hardware.

With saved Wi-Fi, Ble(e)p checks your selected Stable or Development channel
about five seconds after Home appears. If you are already using a device,
scene, command, or Portal, it waits until Ble(e)p is idle. You can also choose
**Settings > Firmware update > Check now**. A check downloads only the update
details, not the firmware itself, and turns Wi-Fi off when finished.

![A newer signed release uses a round amber prompt with equal Install now and Later actions.](assets/ui-firmware-update.png){width=2.7}

To install:

1. Keep Ble(e)p connected to reliable USB power.
2. Choose **Install now**, review the reboot warning, and confirm.
3. Leave power connected while the amber **Preparing update** screen appears
   and Ble(e)p installs the update. It will restart more than once.
4. Wait for **Update successful**, then choose **Restart**. Ble(e)p returns to
   Home after validating the new firmware.

**Later** dismisses that release's popup but leaves it available on the
Firmware Update page. A newer release may prompt again. If preparation fails,
return to the Firmware Update page and choose **Retry update**. Do not remove
power during recovery or installation.

## Enter Recovery mode

On **Settings > Firmware update**, hold **Recovery mode** continuously for two
seconds. Recovery can retry the requested update, install the latest stable
release, start the installed firmware when it is still usable, or run Factory
Reset. Use it after an interrupted update or when Ble(e)p cannot start normally.

## Factory Reset

Factory Reset requires a continuous three-second hold and working saved Wi-Fi.
Ble(e)p first downloads and checks the latest stable firmware. It erases your
saved devices and pairings, scenes, Wi-Fi, Home Assistant links, light setup,
and preferences only after that download is ready. If the network or download
fails, your saved setup is left untouched.

# Device compatibility matrix

Compatibility is intentionally exact. A similar model is not automatically supported.

| Device or service | Status | Available functions | Key limitation or open gate |
| --- | --- | --- | --- |
| Canon EOS R6 Mark II / III - Trigger | Supported | Movie trigger and reconnect | Recording state is unavailable. |
| Canon EOS R6 - Trigger | Candidate | Intended movie trigger | Exact model untested. |
| Canon EOS R6 Mark III - Smart | Supported | Start/Stop, confirmation, wake, power-down | No additional camera controls. |
| Canon EOS R6 Mark II - Smart | Supported | Start/Stop and confirmation | No additional camera controls. |
| GoPro MAX2 | Supported | Start/Stop, confirmation, Sleep/wake | Use alongside several other devices needs more testing. |
| Other Open GoPro models | Candidate | Intended Start/Stop and state | No model-specific result. |
| Google Pixel 9 | Experimental | Volume-up shutter and reconnect | Capture cannot be confirmed. |
| Other phones | Candidate | Volume-up shutter | Phone and camera-app combinations untested. |
| Insta360 X3 / X4 / X4 Air | Supported | GPS Remote camera control | Reconnect, power, and use alongside other devices need more testing. |
| Insta360 X5 | Supported | Start/Stop, shutdown, wake | Wake after leaving and reopening its screen needs more testing. |
| Insta360 GO 3 | Candidate | Intended GPS Remote control | Exact model untested. |
| Insta360 GO Ultra | Research | None | GPS Remote compatibility unknown. |
| DJI Osmo Action 5 Pro / Osmo 360 | Supported | Four-digit pairing, Start/Stop, confirmation | Reconnect and two-camera use need testing. |
| Sony RMT-P1BT cameras | Research | None | Pairing and control unavailable. |
| Tascam Portacapture X8 + AK-BT1 | Supported | Start/Stop and confirmed state | No battery or media status. |
| Home Assistant local entities | Experimental | Up to four supported entities | Power-only lights and switches; no cloud sign-in. |
| amaran Ray 60c | Supported | Aputure Light power and looks | Long sessions and recovery after failures need more testing. |
| amaran Ace 25c / Aputure MC Pro | Supported | Setup, identity, RGB power and looks | Confirmation, recovery, and long sessions need more testing. |
| Aputure MT Pro | Supported | Setup, power, and looks | Currently labelled MC Pro. |
| amaran Pano 60c / Pano 120c | Candidate | Intended Aputure Light controls | Exact models untested. |
| Zhiyun MOLUS X100 | Experimental | Power, CCT, brightness, saved state | Cold reconnect, multi-light use, and recovery need testing. |
| Zhiyun MOLUS X60RGB | Experimental | Power, CCT, RGB, saved state | Panel, reconnect, recovery, and mixed-light use need testing. |
| Deity PR4 | Later | None | Not implemented. |
| iFootage Shark Nano II | Supported | Pair/reconnect, battery, keypoints, movement, run | Secure the payload and watch every move. |

# Troubleshooting

## A device does not appear

- Confirm the target is in the correct pairing menu, not merely powered on.
- For a first-time light, select the intended name and address suffix when several appear.
- Re-enter the target's pairing mode and retry.
- If the device was paired to another phone or remote, remove the old registration or use Forget/re-pair as appropriate.
- For Canon, do not interchange BR-E1 and smartphone pairings.

## A saved device will not reconnect

- Open the saved device to begin reconnecting.
- Confirm the target is awake and still trusts Ble(e)p.
- Use **Manage > Disconnect**, then reopen.
- Ble(e)p can keep four equipment connections ready at once. Disconnect something you are not using, then try again.
- For a light, keep a compatible fixture powered and advertising, then use Retry.

## A command says Sent, Optimistic, or Unknown

Some equipment cannot tell Ble(e)p what happened. Check the camera display,
recorder, light output, phone app, or slider directly. If you do not know the
real state, do not press a toggle again without checking first.

## A scene cannot reach Ready

- Open the failed target and resolve its pairing or connection problem.
- Confirm every device used by the scene is enabled and available in the installed version.
- Check the four-connection limit. Compatible Aputure and Zhiyun lights share one connection; Home Assistant does not use one of the four.
- If the scene contains a Zhiyun light, keep a compatible saved Zhiyun fixture powered so it can provide the shared Aputure/Zhiyun gateway.
- If a multi-light scene fails, open the failed fixture, retry it, and confirm its physical output.
- Use Retry when shown. Opening scene Settings cancels preparation.

## Portal does not open

- Keep Ble(e)p on the Portal screen. Leaving it closes Portal.
- During first setup, join the `Bleep-Setup-XXXXX` network matching the panel suffix. If the sign-on page does not appear, use the numeric setup address.
- On the setup network, `http://192.168.4.1` opens Portal directly. If the
  phone says the network has no internet, stay connected and open that address.
- After Ble(e)p joins the studio network, reconnect your phone or computer to that network and use the address shown on the panel.
- If `bleep.local` does not work, use the numeric address shown on Ble(e)p.

## Wi-Fi scan fails or finds no networks

- Return to Home, move closer to the access point, and scan again from
  **Settings > Wi-Fi**.
- For a hidden network, open Portal and enter the SSID and password yourself.

## A firmware update does not appear or complete

- Confirm **Settings > Wi-Fi** shows the intended saved network, and confirm the
  Firmware Update page is using the intended Stable or Development channel.
- Finish active device commands, leave Portal, and return Home before checking
  again. Ble(e)p never disconnects active equipment silently for an update.
- Keep USB power connected. If **Preparing update** returns to the Firmware
  Update page, choose **Retry update**. After an interrupted installation,
  enter Recovery mode and retry the requested update.

# Work safely

- Secure cameras, sliders, lights, cables, and recorders before sending commands.
- Watch the equipment itself for movement, recording, power, and light changes
  whenever Ble(e)p says **Optimistic** or **Unknown**.
- Use Portal only in a controlled place, and connect it only to Wi-Fi you trust;
  Portal traffic is not encrypted.
- Treat saved Wi-Fi details, Home Assistant tokens, pairing information, and device identities as private. Factory Reset removes them from Ble(e)p.
- Keep reliable USB power connected throughout firmware preparation, recovery,
  installation, and the final success confirmation.
- This project is independent and is not endorsed by iFootage, Canon, GoPro, Insta360, DJI, Sony, Tascam, Aputure, amaran, Zhiyun, Deity, Espressif, Elecrow, or Home Assistant.

<!-- pagebreak -->

# Advanced: developers and builders

This section is for building, repairing, or developing Ble(e)p. Opening the enclosure or modifying the battery lead requires electronics experience.

## Platform and operating limits

The reference build uses the **Elecrow CrowPanel ESP32 1.28-inch round display, model DIS12824D**: an ESP32-C3, 240 x 240 GC9A01 LCD, CST816D capacitive touch controller, PI4IOE5V6408 I/O expander, and onboard vibration motor. The case adds an optional Action Button and an SS12D00G slide switch for power.

![Side profile of the reference enclosure. The orange actuator belongs to this build; placement can differ on community enclosures.](assets/controller-side-line.png){width=2.0}

- The screen and backlight remain on continuously in the current firmware.
- The board does not sense its own battery voltage. A battery value on the Shark screen belongs to the slider.
- The registry holds 24 device records and up to 16 BLE bonds.
- Four physical BLE transport groups can remain connected. Compatible Aputure and Zhiyun fixtures share one group.

| Function | GPIO or address |
| --- | --- |
| LCD DC / CS / SCK / MOSI | GPIO 2 / 10 / 6 / 7 |
| I2C SDA / SCL | GPIO 4 / 5 |
| Touch interrupt | GPIO 0 |
| Action Button | GPIO 1, active low |
| Vibration motor | I/O expander P0 |
| I/O expander | I2C `0x43` |
| CST816D touch | I2C `0x15` |
| BM8563 RTC | I2C `0x51` |

## Parts and tools

The maintained bill of materials and source links live in `hardware/README.md`. The reference assembly uses:

| Item | Specification |
| --- | --- |
| Display/controller | Elecrow CrowPanel ESP32 1.28-inch, DIS12824D |
| Power switch | SS12D00G SPDT, 5 mm actuator |
| Battery | JLJLUP 3.7 V 1100 mAh 1S LiPo with protection board, 25 x 10 x 42 mm, and JST 1.25 plug; replace the plug during assembly |
| Battery pigtail | JST SH 1.0 mm, 2 pin |
| Threaded hardware | 3 x M3 x 6 x 5 mm heat-set inserts; 3 x M3 x 8 mm socket-head screws |
| Replacement battery-path diode | 1N5819 Schottky, DO-41; replaces the original CrowPanel D1 |

You also need a multimeter, temperature-controlled soldering iron, heat-set insertion tip, soldering supplies, heat-shrink or suitable insulation, and ordinary 3D-print cleanup tools. Battery packs and community kits vary; confirm every electrical rating rather than assuming a linked or visually similar part is correct.

## Print the enclosure

The `hardware/` directory contains top, bottom, and button parts as 3MF and STL files. Use 3MF where supported. `Bleep Remote.step` is the editable assembly model for enclosure changes.

The original print used a Bambu Lab X1 Carbon, 0.4 mm nozzle, SuperTack plate, Bambu PLA Matte, and the stock `0.12mm High Quality @BBL X1C` preset. Enable supports and select **Normal (Auto)** rather than **Tree (Auto)**; otherwise keep the selected process and filament defaults. Put each part on its own plate and print the button first as a quick fit test.

## Assemble the enclosure

1. Clean the prints and dry-fit the display, slide switch, action-button actuator, battery, and both shells before soldering. The CrowPanel fits directly into the top shell without adhesive.
2. Confirm that the printed Action Button moves freely and transfers a case press to the board control. It is an action/navigation button, not a power button.
3. With a temperature-controlled iron and suitable tip, install the three heat-set inserts square to their bosses. Stop when each is flush; excessive heat or force can deform the shell.
4. Let the plastic cool completely. Do not install or wire the battery while the shell is still warm.
5. Complete and verify the switched battery lead as described below.
6. Remove the original CrowPanel D1 and install the external 1N5819 replacement as described below.
7. Route and insulate wiring so it cannot touch the PCB, antenna, USB connector, screw bosses, or moving switch parts. Fit the CrowPanel and battery without pinching a conductor.
8. Join the shells with three M3 x 8 mm screws. Tighten only until secure; overtightening can strip an insert or distort the print.
9. Before final use, check switch operation, button travel, charging behavior, and boot from battery with the enclosure attended.

## Wire the battery and power switch

The reference battery's original plug is incompatible with the CrowPanel socket. Replace it with a correctly wired JST SH 1.0 mm two-pin pigtail and put the slide switch in series with **one** battery lead:

```text
Battery lead A -- switch COM -- selected throw -- connector lead A
Battery lead B ------------------------------- connector lead B
```

Use only the common terminal and one throw; insulate the unused throw. Identify the switch terminals with a multimeter, not by physical orientation. Verify continuity in both positions, then verify the finished connector's voltage and polarity against the CrowPanel markings and documentation **before** plugging it in.

Battery leads remain live during connector replacement. Cut and splice only one conductor at a time, insulate that joint before exposing the other conductor, and cover finished joints with heat-shrink. Never solder directly to a cell or its tabs. Do not charge, use, or enclose a damaged or swollen lithium battery, and prevent bare terminals from touching the board or each other.

## Replace the CrowPanel D1 battery path

The DIS12824D V1.0 schematic identifies D1 as a `B5819WT` Schottky diode in SOD-523, in series from `VBAT` to the board load. The reference assembly removes the original D1 and replaces it with a physically larger, through-hole 1N5819. **Never leave both diodes connected in parallel, and never substitute a wire.**

Disconnect USB and the battery. Use a multimeter to identify the actual VBAT and board-load nets rather than trusting wire colors:

```text
battery-positive / VBAT -- unbanded [ 1N5819 ] banded -- board-load side
                           anode                 cathode
```

1. With all power removed, confirm the board-load side is not shorted to ground. If resistance stays near zero, stop and find the fault before continuing.
2. Confirm and mark the original D1 end connected to battery positive as the VBAT side and the opposite end as the board-load side.
3. Carefully desolder and remove the original D1. Heat each joint only as long as needed, lift the component without prying, then clean and inspect both pads. Stop if either pad or trace lifts.
4. Connect the external 1N5819's unbanded anode to VBAT. The verified battery-positive connector joint may be mechanically safer than loading the tiny former D1 pad.
5. Connect the banded cathode to the board-load side with short, flexible insulated wire. Do not let the DO-41 body pull on the former D1 pad.
6. Confirm that the removed D1 is no longer electrically connected. Insulate all exposed conductors, strain-relieve the replacement diode, and keep it clear of the antenna, USB connector, fasteners, and exposed pads.
7. Inspect for bridges, then make the first test from battery only. Disconnect immediately if the diode heats quickly, board voltage stays near zero, or operation is abnormal.

A build using this arrangement measured 4.0 V at the battery and 3.7 V on the load side while running. Before closing the case, exercise representative BLE scanning and connected operation while monitoring diode temperature and voltage drop. Disconnect immediately if the diode heats quickly or the voltage becomes unstable. The longer battery-endurance gate remains open.

## Build, test, and flash firmware

Use a compatible Python 3 installation and PlatformIO from the repository root. The full Montserrat profile is `bleep`:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -U pip platformio
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio test -e native
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e bleep
PLATFORMIO_CORE_DIR="$PWD/.platformio-core" ./.venv/bin/python -m platformio run -e bleep -t upload
```

The configured upload port is `/dev/cu.usbserial-211240`; do not guess another
port if it is absent. A normal PlatformIO upload writes main at `0x120000` and
assumes the recovery partition layout already exists.

Build success is not proof of panel behavior, peripheral compatibility, browser behavior, tactile feel, or endurance. Record those separately and retain exact-model confidence labels.
