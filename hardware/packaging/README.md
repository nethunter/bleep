# Ble(e)p prototype packaging

This directory contains the editable generator for a five-unit packaging
prototype. The design holds:

- one assembled Ble(e)p controller, nominally 118 x 48 x 21 mm;
- one folded USB-C cable beneath the controller;
- one 140 x 75 mm quick-start card above the controller.

The package is a two-piece telescoping paperboard box with a folded bridge
insert. Its nominal internal size is 155 x 90 x 45 mm. The lid has 2.5 mm of
clearance per side before accounting for material thickness. The visual design
uses warm-white faces, a centered Ble(e)p identity, a restrained `Studio
Controller` label, and cropped cyan, blue, purple, pink, and orange accents at
the lid edges. The structure remains telescoping; it is not a hinged or magnetic
book box.

## Generate the prototype

From the repository root:

```sh
/Users/nethunter/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  hardware/packaging/generate_prototype.py
```

This writes `hardware/packaging/bleep-packaging-prototype.pdf`. The PDF contains:

1. the package concept and dimensions;
2. a full-scale lid dieline;
3. a full-scale base dieline;
4. a full-scale bridge-insert template;
5. quick-start cards;
6. a fit and handling test protocol.

Generate the two-up pocket guide separately:

```sh
/Users/nethunter/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  hardware/packaging/generate_pocket_guide.py
```

This writes `hardware/packaging/bleep-pocket-guide-2up.pdf`, a two-page Tabloid PDF.
Each sheet produces two identical 69 x 130 mm, 12-panel accordion guides. Print
three sheets to make five guides plus one fit/registration spare.

Print the pocket guide double-sided in color on 11 x 17 inch, 32 lb matte text
paper. Use **Actual Size / 100%**, landscape orientation, and **flip on short
edge**. Disable automatic scaling and verify the 50 mm calibration bar before
cutting. The front reads panels 1-6 from left to right. Unfold the strip, turn
it over, and continue panels 7-12 from right to left.

Cut the magenta outer borders, then score at each pair of cyan fold ticks. With
the front side up, fold panel 2 behind cover panel 1 and alternate fold
direction at every remaining crease. The closed cover should face outward. No
glue or staples are required. Keep the guide in the concealed cable bay; do not
place it between the controller and the lid.

The first instruction panel deliberately places a stable firmware update before
device pairing. The recipient should connect USB power, configure trusted Wi-Fi
from the Home cog, choose **Settings > Firmware update > Check now**, install the
available stable release, keep USB power connected throughout preparation and
recovery, and press **Restart** when **Update successful** appears. Hidden Wi-Fi
networks use Portal. Manual Recovery mode is not part of the routine first-use
upgrade path.

Print dieline pages at **100% / Actual Size** with all automatic scaling
disabled. Check the 50 mm calibration bar before cutting. Solid magenta lines
are cuts and dashed cyan lines are scores/folds.

Start with ordinary paper to confirm scale and folding order. Make the next
article from 300-400 gsm cover stock or thin E-flute board. The insert is best
made from 1-1.5 mm E-flute or laminated cover stock. Apply adhesive only to the
orange glue tabs.

The dielines are prototype geometry, not production tooling. A packaging
vendor must compensate for its exact board caliper, crease allowance, print
bleed, grain direction, and cutting equipment before manufacturing samples.

For the five presentation samples, request a bright warm-white SBS board or a
white paper-wrapped rigid-board equivalent, digital CMYK printing, and a matte
anti-scuff finish. Ask for one physical color proof before the complete run;
soft-touch films can shift or yellow an otherwise clean white surface. A vendor
can replace the folded paper insert with white CNC-cut EVA/EPE foam while
retaining the same controller opening and concealed cable-bay arrangement.

## Fit assumptions

The controller pocket starts at 116 x 44 mm, leaving a nominal 1 mm supporting
lip at the enclosure ends and 2 mm along its sides for a 118 x 48 mm enclosure.
Print and cut the insert before ordering foam or production board. Enlarge the
opening in 0.5 mm increments only where the physical enclosure rubs.

The cable bay is 17 mm high. Use a compact cable folded flat rather than a
tightly wound coil. The quick-start card sits above the controller and should
be removed before lifting the device.
