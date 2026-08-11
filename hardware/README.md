# Ble(e)p Hardware

This directory contains the printable enclosure and editable mechanical model
for the Ble(e)p handheld controller. The enclosure is designed around the
Elecrow CrowPanel ESP32 1.28-inch round display and an SS12D00G slide switch.

## Parts

| Part | Specification | Link |
| --- | --- | --- |
| Display and controller | Elecrow CrowPanel ESP32 1.28-inch round display, model DIS12824D; ESP32-C3, 240x240 IPS display, capacitive touch | [Elecrow product page](https://www.elecrow.com/crowpanel-esp32-display-1-28-r-inch-240-240-round-ips-display-capacitive-touch-spi-screen.html) |
| Slide switch | SS12D00G SPDT slide switch with a 5 mm actuator | [Amazon](https://amzn.to/3RRJRAK) |
| Battery | JLJLUP 3.7 V 1100 mAh 1S LiPo with protection board; 25 x 10 x 42 mm, 19 g, 1C discharge rate, JST 1.25 plug; four-pack listing, ASIN B0GR14VMW5 | [Amazon](https://www.amazon.com/dp/B0GR14VMW5) |
| Battery connector | JST SH 1.0 mm 2-pin connector with pigtails; replaces the battery's incompatible plug | [Amazon](https://amzn.to/4hNGGEG) |
| Replacement battery-path diode | 1N5819 Schottky diode, DO-41, nominal 1 A / 40 V; replaces the original CrowPanel D1 | [Amazon](https://amzn.to/45O7ehS) |
| Heat-set inserts | 3x M3 x 6 x 5 mm brass heat-set inserts | [Amazon](https://amzn.to/4yYaTH8) |
| Machine screws | 3x M3 x 8 mm socket-head machine screws | [Amazon](https://amzn.to/3S8nI18) |

The shortened `amzn.to` links in this parts list are affiliate links. Purchases
made through them may earn a commission at no additional cost to you and help
support the
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

Experimental mechanical models belong in the gitignored
`hardware/.workbench/` directory. Promote a model into this tracked parts list
only after its fit, printability, and assembly behavior feel final.

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

After verifying the switched lead, remove the original CrowPanel D1 and install
the external 1N5819 replacement using the procedure below. Route and
strain-relieve the battery, switch, and diode wiring before fitting the board
and closing the enclosure.

Do not charge, use, or enclose a damaged or swollen lithium battery. Prevent
bare switch terminals and battery leads from contacting the PCB or each other.

## Replace the CrowPanel D1 battery path

The [DIS12824D V1.0 schematic](https://www.elecrow.com/download/product/CrowPanel/ESP32-HMI/1.28-DIS12824D/CrowPanel_ESP32_1.28-inch_V1.0_240507.zip)
identifies the CrowPanel's original D1 as a `B5819WT` Schottky diode in a small
SOD-523 package, in series between `VBAT` and the board load. The reference
assembly removes the original D1 and replaces it with the physically larger,
through-hole 1N5819 listed above.

Do not leave both diodes electrically connected in parallel. Do not replace D1
with a wire.
Confirm the diode's markings and supplier datasheet; its real current
and thermal limits depend on the specific manufacturer and installation.

Disconnect both USB and the battery before soldering. Identify the two D1 nets
with a multimeter rather than relying on connector wire colors, especially
after replacing or rewiring the JST SH pigtail:

```text
battery-positive / VBAT side ── unbanded end [ 1N5819 ] banded end ── board-load side
                                 anode                       cathode
```

Installation procedure:

1. With power disconnected, check that the board-load side is not shorted to
   ground. A steady resistance near zero indicates a fault; resolve it before
   fitting the external diode.
2. Confirm which end of the original D1 is connected to actual battery
   positive. Mark that as the `VBAT` side and mark the opposite end as the
   board-load side.
3. Carefully desolder and remove the original D1. Heat each joint only as long
   as needed, lift the component without prying, then clean and inspect both
   pads. Stop if either pad or trace lifts.
4. Connect the 1N5819's **unbanded anode** to the `VBAT` side. The battery
   connector's verified positive solder joint is a stronger alternative to
   loading the tiny former D1 pad mechanically.
5. Connect the 1N5819's **banded cathode** to the board-load side. Use short,
   flexible insulated wire so the large DO-41 body cannot pull the former D1
   pad from the PCB.
6. Confirm that the removed D1 is no longer electrically connected. Insulate
   every exposed conductor with heat-shrink or Kapton tape. Secure the diode
   body with strain relief and keep the assembly clear of the antenna, USB
   connector, enclosure hardware, and other exposed pads.
7. Inspect for solder bridges, then perform the first test from battery only.
   Disconnect immediately if the diode heats rapidly, the board-side voltage
   stays near zero, or the panel behaves abnormally.

A build using this arrangement measured 4.0 V on the battery side and 3.7 V on
the board side while the panel ran from battery. Before closing the enclosure,
exercise representative BLE scanning and connection load while monitoring
diode temperature and voltage drop. Disconnect immediately if the diode heats
rapidly or the voltage becomes unstable. Retain the longer ADR-025
battery-endurance gate.

## Firmware and pin details

Build, flashing, firmware behavior, and the complete CrowPanel pin assumptions
are documented in the [project README](../README.md#hardware).
