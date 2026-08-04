# Ble(e)p Hardware

This directory contains the printable enclosure and editable mechanical model
for the Ble(e)p handheld controller. The enclosure is designed around the
Elecrow CrowPanel ESP32 1.28-inch round display and an SS12D00G slide switch.

## Parts

| Part | Specification | Link |
| --- | --- | --- |
| Display and controller | Elecrow CrowPanel ESP32 1.28-inch round display, model DIS12824D; ESP32-C3, 240x240 IPS display, capacitive touch | [Elecrow product page](https://www.elecrow.com/crowpanel-esp32-display-1-28-r-inch-240-240-round-ips-display-capacitive-touch-spi-screen.html) |
| Slide switch | SS12D00G SPDT slide switch with a 5 mm actuator | [Amazon](https://amzn.to/3RRJRAK) |
| Battery | Battery used by this enclosure; confirm its voltage, connector, and polarity before assembly | [Amazon](https://amzn.to/4wzTu6d) |
| Battery connector | JST SH 1.0 mm 2-pin connector with pigtails; replaces the battery's incompatible plug | [Amazon](https://amzn.to/4hNGGEG) |
| Heat-set inserts | 3x M3 x 6 x 5 mm brass heat-set inserts | [Amazon](https://amzn.to/4yYaTH8) |
| Machine screws | 3x M3 x 8 mm socket-head machine screws | [Amazon](https://amzn.to/3S8nI18) |

The Amazon links in this parts list are affiliate links. Purchases made through
them may earn a commission at no additional cost to you and help support the
[Hacking Modern Life YouTube channel](https://www.youtube.com/@hml).

## Enclosure files

| File | Purpose |
| --- | --- |
| [Bleep Remote Top.3mf](<Bleep Remote Top.3mf>) / [STL](<Bleep Remote Top.stl>) | Printable top shell |
| [Bleep Remote Bottom.3mf](<Bleep Remote Bottom.3mf>) / [STL](<Bleep Remote Bottom.stl>) | Printable bottom shell |
| [Bleep Remote Button.3mf](<Bleep Remote Button.3mf>) / [STL](<Bleep Remote Button.stl>) | Printable actuator for the CrowPanel's custom button |
| [Bleep Remote.step](<Bleep Remote.step>) | Editable assembly model containing the enclosure, CrowPanel, button, and slide switch |

Use the 3MF files when your slicer supports them. The STL files are provided
for broader compatibility, and the STEP file is the best starting point for
changing the enclosure or adapting it to another component.

## Printing

The original print used a Bambu Lab X1 Carbon with a 0.4 mm nozzle, a SuperTack
plate, and the stock `0.12mm High Quality @BBL X1C` process preset. The top and
bottom shells used Bambu PLA Matte in Ivory White; the button used Bambu PLA
Matte in Black. Each part was placed on its own plate.

Change only these process settings from the preset defaults:

- enable supports;
- select **Normal (Auto)** supports instead of the default **Tree (Auto)**.

All other process and filament settings remain at the defaults supplied by the
selected Bambu presets. Print the button first as a quick fit test before
committing to both shells.

## Assembly notes

Dry-fit every component before soldering. The CrowPanel fits directly into the
top shell; no adhesive is used. The CrowPanel's custom button is GPIO 1 and
active low; the printed button transfers the case press to that control. In the
current firmware, a short press triggers the current screen's primary action,
including Start/Stop on a sequence run screen. A 700 ms hold navigates Back,
cancels, or closes the current overlay; releasing after a handled hold does not
also trigger the short-press action. The button has no power behavior—the
hardware SPDT switch is the remote's sole power control.

Install the three heat-set inserts into the modeled bosses with a temperature-
controlled soldering iron and a suitable insertion tip. Keep each insert
square to the boss and stop when it is flush; excessive heat or pressure can
deform the shell. Let the plastic cool completely before joining the enclosure
with the three M3 x 8 mm screws. Tighten the screws only until the shells are
secure.

The battery's original plug is not compatible with the CrowPanel battery
socket. Replace it with the linked JST SH 1.0 mm 2-pin pigtail and put the
slide switch in series with one battery lead:

```text
Battery lead A ── switch COM ── selected throw ── connector lead A
Battery lead B ────────────────────────────────── connector lead B
```

Only the switch's common terminal and one throw are used; insulate the unused
throw. Identify the terminals with a multimeter rather than relying on the
switch's physical orientation. Verify continuity in both switch positions, and
verify the finished connector's voltage and polarity against the CrowPanel
markings and documentation before plugging it into the board.

Battery leads remain live while replacing the connector. Cut and splice only
one conductor at a time, insulate each joint before exposing the other lead,
and cover the finished solder joints with heat-shrink tubing. Do not apply the
soldering iron directly to the battery cell or tabs.

Do not charge, use, or enclose a damaged or swollen lithium battery. Prevent
bare switch terminals and battery leads from contacting the PCB or each other.

## Firmware and pin details

Build, flashing, firmware behavior, and the complete CrowPanel pin assumptions
are documented in the [project README](../README.md#hardware).
