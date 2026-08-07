"""Generate the Ble(e)p CrowPanel watch crown as STEP and STL solids.

The drive geometry follows the Elecrow/F-Switch drawing for
E5A5-2.35S10-12B15-F200: a nominal 0.8 mm square socket.  The default
0.76 mm male drive is deliberately conservative for a removable resin-print
prototype.  Adjust SHAFT_SIZE_MM after measuring a printed fit coupon.
"""

from pathlib import Path

import cadquery as cq
from cadquery import exporters, importers


OUTPUT_DIR = Path(__file__).resolve().parent
STEP_PATH = OUTPUT_DIR / "bleep_watch_crown_0p76mm.step"
STL_PATH = OUTPUT_DIR / "bleep_watch_crown_0p76mm.stl"

# Crown envelope.
CROWN_DIAMETER_MM = 11.5
CROWN_THICKNESS_MM = 3.0
EDGE_CHAMFER_MM = 0.35

# Straight rim flutes for a watch-crown grip.
FLUTE_COUNT = 40
FLUTE_DEPTH_MM = 0.32
FLUTE_WIDTH_MM = 0.34

# Encoder interface.  Z=0 is the underside of the crown body.  The hub and
# square drive point toward the PCB/encoder in negative Z.
HUB_DIAMETER_MM = 1.30
HUB_LENGTH_MM = 0.75
SHAFT_SIZE_MM = 0.76
SHAFT_LENGTH_MM = 1.60
LEAD_LENGTH_MM = 0.22
LEAD_SIZE_MM = 0.64
CORNER_RELIEF_MM = 0.06


def relieved_square_points(size: float, relief: float) -> list[tuple[float, float]]:
  """Return an octagonal square profile with relieved outside corners."""
  half = size / 2.0
  return [
      (half - relief, half),
      (-half + relief, half),
      (-half, half - relief),
      (-half, -half + relief),
      (-half + relief, -half),
      (half - relief, -half),
      (half, -half + relief),
      (half, half - relief),
  ]


def make_profile(size: float, relief: float, z: float) -> cq.Workplane:
  return (
      cq.Workplane("XY")
      .workplane(offset=z)
      .polyline(relieved_square_points(size, relief))
      .close()
  )


def make_crown() -> cq.Workplane:
  radius = CROWN_DIAMETER_MM / 2.0

  body = cq.Workplane("XY").circle(radius).extrude(CROWN_THICKNESS_MM)
  body = body.edges("%CIRCLE").chamfer(EDGE_CHAMFER_MM)

  # Cut shallow axial flutes into the cylindrical rim.  The teeth remain
  # broad and printable while the chamfer softens both face edges.
  flute = (
      cq.Workplane("XY")
      .center(radius - FLUTE_DEPTH_MM / 2.0 + 0.08, 0)
      .box(
          FLUTE_DEPTH_MM + 0.30,
          FLUTE_WIDTH_MM,
          CROWN_THICKNESS_MM + 0.20,
          centered=(True, True, False),
      )
      .translate((0, 0, -0.10))
  )
  for index in range(FLUTE_COUNT):
    cutter = flute.rotate((0, 0, 0), (0, 0, 1), index * 360.0 / FLUTE_COUNT)
    body = body.cut(cutter)

  hub = (
      cq.Workplane("XY")
      .workplane(offset=-HUB_LENGTH_MM)
      .circle(HUB_DIAMETER_MM / 2.0)
      .extrude(HUB_LENGTH_MM)
  )

  shaft_main_bottom_z = -(HUB_LENGTH_MM + SHAFT_LENGTH_MM - LEAD_LENGTH_MM)
  shaft_main = make_profile(
      SHAFT_SIZE_MM, CORNER_RELIEF_MM, shaft_main_bottom_z
  ).extrude(SHAFT_LENGTH_MM - LEAD_LENGTH_MM)

  lead_bottom_z = -(HUB_LENGTH_MM + SHAFT_LENGTH_MM)
  lead = (
      cq.Workplane("XY")
      .workplane(offset=lead_bottom_z)
      .polyline(
          relieved_square_points(
              LEAD_SIZE_MM,
              min(CORNER_RELIEF_MM, LEAD_SIZE_MM * 0.08),
          )
      )
      .close()
      .workplane(offset=LEAD_LENGTH_MM)
      .polyline(relieved_square_points(SHAFT_SIZE_MM, CORNER_RELIEF_MM))
      .close()
      .loft(combine=True)
  )

  crown = body.union(hub).union(shaft_main).union(lead).clean()
  return crown


def validate(shape: cq.Workplane) -> None:
  solids = shape.solids().vals()
  if len(solids) != 1:
    raise RuntimeError(f"Expected one solid, found {len(solids)}")
  solid = solids[0]
  if not solid.isValid():
    raise RuntimeError("Generated crown is not a valid solid")
  if solid.Volume() <= 0:
    raise RuntimeError("Generated crown has no volume")


def main() -> None:
  crown = make_crown()
  validate(crown)
  exporters.export(crown, str(STEP_PATH), opt={"write_pcurves": True})
  exporters.export(crown, str(STL_PATH), tolerance=0.015, angularTolerance=0.08)

  imported = importers.importStep(str(STEP_PATH))
  validate(imported)
  box = imported.val().BoundingBox()
  print(f"Wrote {STEP_PATH}")
  print(f"Wrote {STL_PATH}")
  print(
      "Validated 1 solid; "
      f"bounds={box.xlen:.3f} x {box.ylen:.3f} x {box.zlen:.3f} mm; "
      f"volume={imported.val().Volume():.3f} mm^3"
  )


if __name__ == "__main__":
  main()
