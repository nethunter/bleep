# Ble(e)p CrowPanel watch crown

`bleep_watch_crown_0p76mm.step` is a Fusion 360-compatible solid for the
CrowPanel `DIS12824D` encoder. The model follows the official
`E5A5-2.35S10-12B15-F200` drawing's nominal 0.8 mm square drive socket.

## Model dimensions

- crown: 11.5 mm diameter x 3.0 mm thick;
- rim: 40 straight grip flutes with 0.35 mm face-edge chamfers;
- clearance hub: 1.30 mm diameter x 0.75 mm long;
- drive: 0.76 mm relieved square x 1.60 mm long;
- lead-in: final 0.22 mm tapers to 0.64 mm across flats;
- overall envelope: approximately 11.495 x 11.495 x 5.350 mm.

The 0.76 mm drive is a conservative first-fit prototype, not a physically
verified production tolerance. Print a fit test before bonding anything to the
encoder. If adjustment is needed, change `SHAFT_SIZE_MM` in
`bleep_watch_crown.py` and regenerate the STEP.

The integral shaft is too small for dependable ordinary FDM printing. Use a
tough engineering resin, fine SLA process, or machine the part. Orient a resin
print so the drive points upward and place no supports on the square drive or
lead-in. Confirm that the crown rotates freely and preserves the encoder's
0.15 mm push travel before final assembly.
