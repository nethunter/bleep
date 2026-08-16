#!/usr/bin/env python3
"""Generate the full-scale Ble(e)p packaging prototype PDF."""

from pathlib import Path

from reportlab.graphics.barcode import qr
from reportlab.graphics.shapes import Drawing
from reportlab.lib.colors import Color, HexColor, white
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import landscape, letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch, mm
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).with_name("bleep-packaging-prototype.pdf")
LOGO = ROOT / "assets" / "logo.png"
WIDE_LOGO = ROOT / "website" / "assets" / "logo-wide.png"

INK = HexColor("#050708")
PANEL = HexColor("#101618")
PAPER = HexColor("#EDF6F5")
MUTED = HexColor("#8EA0A2")
CYAN = HexColor("#24D9FF")
ORANGE = HexColor("#FF7A32")
GREEN = HexColor("#2BE59D")
CUT = HexColor("#E6007E")
FOLD = HexColor("#00A9C7")
LIGHT_LINE = HexColor("#CBD8D9")
WARM_WHITE = HexColor("#FAF9F6")
BOX_EDGE = HexColor("#DDD9D1")
PURPLE = HexColor("#7957FF")
PINK = HexColor("#B84DFF")
BLUE = HexColor("#2775FF")

BOX_L = 155.0
BOX_W = 90.0
BOX_H = 45.0
LID_L = 160.0
LID_W = 95.0
LID_H = 30.0
INSERT_L = 151.0
INSERT_W = 86.0
INSERT_LEG = 17.0
POCKET_L = 116.0
POCKET_W = 44.0
CARD_L = 140.0
CARD_W = 75.0


def text(c, value, x, y, size, color=INK, font="Helvetica", align="left"):
  c.setFillColor(color)
  c.setFont(font, size)
  if align == "center":
    c.drawCentredString(x, y, value)
  elif align == "right":
    c.drawRightString(x, y, value)
  else:
    c.drawString(x, y, value)


def paragraph(c, value, x, y_top, width, height, size=9, leading=13,
              color=INK, font="Helvetica", align=TA_LEFT):
  style = ParagraphStyle(
      "body", fontName=font, fontSize=size, leading=leading,
      textColor=color, alignment=align, spaceAfter=0)
  story = Paragraph(value, style)
  story.wrapOn(c, width, height)
  story.drawOn(c, x, y_top - height)


def page_header(c, index, title, subtitle=None):
  width, height = c._pagesize
  c.setFillColor(INK)
  c.rect(0, height - 23 * mm, width, 23 * mm, fill=1, stroke=0)
  text(c, f"0{index} / PACKAGING PROTOTYPE", 14 * mm, height - 10 * mm,
       7.5, CYAN, "Helvetica-Bold")
  text(c, title, 14 * mm, height - 18 * mm, 15, PAPER, "Helvetica-Bold")
  if subtitle:
    text(c, subtitle, width - 14 * mm, height - 17 * mm, 7.5, MUTED,
         "Helvetica", "right")
  c.setFillColor(ORANGE)
  c.rect(0, height - 24 * mm, width, 1 * mm, fill=1, stroke=0)


def footer(c, label):
  width, _ = c._pagesize
  text(c, label, 12 * mm, 7 * mm, 6.5, MUTED)
  text(c, "BLEEP.HML.TECH / PROTOTYPE - NOT FOR RESALE",
       width - 12 * mm, 7 * mm, 6.5, MUTED, align="right")


def calibration(c, x, y):
  c.setStrokeColor(INK)
  c.setLineWidth(0.7)
  c.line(x, y, x + 50 * mm, y)
  for offset in (0, 25, 50):
    c.line(x + offset * mm, y - 1.5 * mm, x + offset * mm, y + 1.5 * mm)
  text(c, "50 mm - measure before cutting", x + 25 * mm, y + 2.5 * mm,
       6.5, INK, "Helvetica", "center")


def legend(c, x, y):
  c.setStrokeColor(CUT)
  c.setLineWidth(1.2)
  c.setDash()
  c.line(x, y, x + 13 * mm, y)
  text(c, "CUT", x + 16 * mm, y - 1.2 * mm, 6.5, CUT, "Helvetica-Bold")
  c.setStrokeColor(FOLD)
  c.setDash(4, 3)
  c.line(x + 32 * mm, y, x + 45 * mm, y)
  c.setDash()
  text(c, "SCORE / FOLD", x + 48 * mm, y - 1.2 * mm, 6.5, FOLD,
       "Helvetica-Bold")


def glue_label(c, x, y, width, height):
  c.saveState()
  c.setFillColor(Color(1, 0.478, 0.196, alpha=0.22))
  c.rect(x, y, width, height, fill=1, stroke=0)
  c.translate(x + width / 2, y + height / 2)
  c.rotate(90 if height > width else 0)
  text(c, "GLUE", 0, -2, 6, ORANGE, "Helvetica-Bold", "center")
  c.restoreState()


def corner_accents(c, x, y, width, height, weight=4.2):
  """Draw cropped color loops inspired by the enclosure accent palette."""
  c.saveState()
  c.setLineCap(1)
  accents = [
      (CYAN, x - 10 * mm, y + height - 17 * mm, 31 * mm, 31 * mm, 205, 330),
      (PURPLE, x + width - 19 * mm, y + height - 12 * mm,
       34 * mm, 34 * mm, 175, 292),
      (BLUE, x + width - 25 * mm, y - 15 * mm,
       31 * mm, 31 * mm, 25, 155),
      (ORANGE, x - 11 * mm, y - 10 * mm, 27 * mm, 27 * mm, 15, 130),
      (PINK, x + width - 8 * mm, y + 7 * mm,
       19 * mm, 19 * mm, 105, 245),
  ]
  for color, ax, ay, aw, ah, start, extent in accents:
    c.setStrokeColor(color)
    c.setLineWidth(weight)
    c.arc(ax, ay, ax + aw, ay + ah, start, extent)
  c.restoreState()


def draw_tray_dieline(c, center_l, center_w, wall_h, tab_w, x0, y0,
                      lid=False):
  """Draw a glue-tab tray with cut and fold lines in millimetres."""
  l = center_l * mm
  w = center_w * mm
  h = wall_h * mm
  t = tab_w * mm
  x = x0
  y = y0

  # Warm-white artwork. Printed side becomes the exterior of the tray.
  c.setFillColor(WARM_WHITE)
  c.rect(x + h, y + h, l, w, fill=1, stroke=0)
  c.setFillColor(HexColor("#F1EFEA"))
  c.rect(x, y + h, h, w, fill=1, stroke=0)
  c.rect(x + h + l, y + h, h, w, fill=1, stroke=0)
  c.rect(x + h, y, l, h, fill=1, stroke=0)
  c.rect(x + h, y + h + w, l, h, fill=1, stroke=0)

  # Glue tabs on the short ends of the top and bottom walls.
  glue_label(c, x + h - t, y + 1.5 * mm, t, h - 3 * mm)
  glue_label(c, x + h + l, y + 1.5 * mm, t, h - 3 * mm)
  glue_label(c, x + h - t, y + h + w + 1.5 * mm, t, h - 3 * mm)
  glue_label(c, x + h + l, y + h + w + 1.5 * mm, t, h - 3 * mm)

  if lid:
    # Centered product artwork with color peeking in from the edges.
    corner_accents(c, x + h, y + h, l, w)
    logo_w = 88 * mm
    logo_h = 27 * mm
    c.drawImage(str(WIDE_LOGO), x + h + (l - logo_w) / 2,
                y + h + w / 2 - 4 * mm,
                logo_w, logo_h, preserveAspectRatio=True, mask="auto")
    text(c, "STUDIO CONTROLLER", x + h + l / 2,
         y + h + w / 2 - 15 * mm,
         7.2, HexColor("#595959"), "Helvetica-Bold", "center")
    text(c, "PROTOTYPE SERIES / P-___", x + h + l - 8 * mm,
         y + h + 6 * mm, 6.2, HexColor("#777777"), "Helvetica-Bold", "right")
    # Long wall artwork.
    text(c, "Ble(e)p  /  Studio Controller", x + h + l / 2,
         y + h - 7 * mm, 7, HexColor("#4A4A4A"), "Helvetica-Bold", "center")
    text(c, "OPEN FIRMWARE / ACTIVE DEVELOPMENT", x + h + l / 2,
         y + h + w + h - 8 * mm, 6.5, PURPLE, "Helvetica-Bold", "center")
  else:
    text(c, "BLE(E)P / PROTOTYPE SERIES", x + h + 8 * mm,
         y + h + 8 * mm, 7, HexColor("#777777"), "Helvetica-Bold")
    text(c, "P-___", x + h + l - 8 * mm, y + h + 8 * mm,
         8, ORANGE, "Helvetica-Bold", "right")
    text(c, "bleep.hml.tech", x + h + l / 2, y + h + w / 2,
         9, HexColor("#555555"), "Helvetica-Bold", "center")

  # Fold lines around the center panel and each glue tab.
  c.setStrokeColor(FOLD)
  c.setLineWidth(0.75)
  c.setDash(4, 3)
  c.line(x + h, y + h, x + h + l, y + h)
  c.line(x + h, y + h + w, x + h + l, y + h + w)
  c.line(x + h, y + h, x + h, y + h + w)
  c.line(x + h + l, y + h, x + h + l, y + h + w)
  for yy in (y, y + h + w):
    c.line(x + h, yy, x + h, yy + h)
    c.line(x + h + l, yy, x + h + l, yy + h)
  c.setDash()

  # Cut outline and the four separations between side walls and glue tabs.
  c.setStrokeColor(CUT)
  c.setLineWidth(0.9)
  c.setDash()
  # Left wall outer edge and exposed horizontal edges.
  c.line(x, y + h, x, y + h + w)
  c.line(x, y + h, x + h - t, y + h)
  c.line(x, y + h + w, x + h - t, y + h + w)
  # Right wall.
  c.line(x + 2 * h + l, y + h, x + 2 * h + l, y + h + w)
  c.line(x + h + l + t, y + h, x + 2 * h + l, y + h)
  c.line(x + h + l + t, y + h + w, x + 2 * h + l, y + h + w)
  # Top and bottom walls plus chamfered tabs.
  for yy, direction in ((y, 1), (y + 2 * h + w, -1)):
    edge_y = yy
    wall_y = y if direction == 1 else y + h + w
    c.line(x + h, edge_y, x + h + l, edge_y)
    # Left tab has a 3 mm clipped corner at its free edge.
    c.line(x + h - t + 3 * mm, edge_y, x + h, edge_y)
    c.line(x + h - t, wall_y + (3 * mm if direction == 1 else h - 3 * mm),
           x + h - t + 3 * mm, edge_y)
    c.line(x + h - t, wall_y + (3 * mm if direction == 1 else h - 3 * mm),
           x + h - t, wall_y + (h if direction == 1 else 0))
    # Right tab.
    c.line(x + h + l, edge_y, x + h + l + t - 3 * mm, edge_y)
    c.line(x + h + l + t - 3 * mm, edge_y,
           x + h + l + t, wall_y + (3 * mm if direction == 1 else h - 3 * mm))
    c.line(x + h + l + t, wall_y + (3 * mm if direction == 1 else h - 3 * mm),
           x + h + l + t, wall_y + (h if direction == 1 else 0))
  # Cuts separating tabs from the adjacent side walls.
  c.line(x + h - t, y + h, x + h, y + h)
  c.line(x + h + l, y + h, x + h + l + t, y + h)
  c.line(x + h - t, y + h + w, x + h, y + h + w)
  c.line(x + h + l, y + h + w, x + h + l + t, y + h + w)


def page_concept(c):
  page_header(c, 1, "Device + card + cable", "155 x 90 x 45 mm internal")
  width, height = c._pagesize

  text(c, "White outside. Color at the edges.", 16 * mm,
       height - 39 * mm, 22, INK, "Helvetica-Bold")
  paragraph(c,
      "The telescoping box keeps the clean white presentation of the reference: "
      "centered Ble(e)p identity, restrained product naming, and cropped color "
      "loops at the lid edges. Inside, a white bridge insert presents the "
      "controller while hiding the USB-C cable below; the quick-start card sits "
      "on top.",
      16 * mm, height - 45 * mm, 92 * mm, 34 * mm, 9.2, 13, MUTED)

  # Simple isometric box concept.
  ox, oy = 126 * mm, height - 105 * mm
  c.setFillColor(HexColor("#E8E5DE"))
  c.setStrokeColor(BOX_EDGE)
  c.setLineWidth(1)
  c.rect(ox, oy, 67 * mm, 39 * mm, fill=1, stroke=1)
  c.setFillColor(WARM_WHITE)
  p = c.beginPath()
  p.moveTo(ox, oy + 39 * mm)
  p.lineTo(ox + 14 * mm, oy + 49 * mm)
  p.lineTo(ox + 81 * mm, oy + 49 * mm)
  p.lineTo(ox + 67 * mm, oy + 39 * mm)
  p.close()
  c.drawPath(p, fill=1, stroke=1)
  c.setFillColor(HexColor("#DEDAD2"))
  p = c.beginPath()
  p.moveTo(ox + 67 * mm, oy)
  p.lineTo(ox + 81 * mm, oy + 10 * mm)
  p.lineTo(ox + 81 * mm, oy + 49 * mm)
  p.lineTo(ox + 67 * mm, oy + 39 * mm)
  p.close()
  c.drawPath(p, fill=1, stroke=1)
  c.drawImage(str(WIDE_LOGO), ox + 9 * mm, oy + 13 * mm, 49 * mm, 15 * mm,
              preserveAspectRatio=True, mask="auto")
  text(c, "STUDIO CONTROLLER", ox + 33.5 * mm, oy + 9 * mm,
       5.5, HexColor("#666666"), "Helvetica-Bold", "center")
  c.setStrokeColor(CYAN)
  c.setLineWidth(3.2)
  c.arc(ox - 7 * mm, oy + 25 * mm, ox + 13 * mm, oy + 45 * mm, 205, 310)
  c.setStrokeColor(PURPLE)
  c.arc(ox + 56 * mm, oy + 32 * mm, ox + 78 * mm, oy + 54 * mm, 165, 275)
  c.setStrokeColor(ORANGE)
  c.arc(ox + 57 * mm, oy - 8 * mm, ox + 76 * mm, oy + 11 * mm, 35, 145)

  y = height - 134 * mm
  items = [
      ("01", "LID", "160 x 95 mm panel; 30 mm walls"),
      ("02", "CARD", "140 x 75 mm; QR and prototype ID"),
      ("03", "DEVICE", "118 x 48 x about 21 mm"),
      ("04", "INSERT", "17 mm cable bay; 116 x 44 mm pocket"),
      ("05", "BASE", "155 x 90 mm panel; 45 mm walls"),
  ]
  for index, label, detail in items:
    c.setStrokeColor(LIGHT_LINE)
    c.line(16 * mm, y - 3 * mm, width - 16 * mm, y - 3 * mm)
    text(c, index, 16 * mm, y, 7, ORANGE, "Helvetica-Bold")
    text(c, label, 30 * mm, y, 9, INK, "Helvetica-Bold")
    text(c, detail, 64 * mm, y, 8, MUTED)
    y -= 14 * mm

  c.setFillColor(HexColor("#E9F9FC"))
  c.roundRect(16 * mm, 20 * mm, width - 32 * mm, 31 * mm,
              4 * mm, fill=1, stroke=0)
  text(c, "START WITH PAPER", 22 * mm, 42 * mm, 7, FOLD, "Helvetica-Bold")
  paragraph(c,
      "Print pages 2-4 at Actual Size. Verify the calibration bar, cut the "
      "magenta lines, score the dashed cyan lines, and assemble with removable "
      "tape. The first goal is fit and opening behavior, not final materials.",
      22 * mm, 39 * mm, width - 44 * mm, 17 * mm, 8.3, 11, INK)
  footer(c, "WHITE TELESCOPING CONCEPT / REV B")
  c.showPage()


def page_dieline(c, index, title, dims, kind):
  c.setPageSize(landscape(letter))
  page_header(c, index, title, dims)
  width, height = c._pagesize
  if kind == "lid":
    blank_w = (LID_L + 2 * LID_H) * mm
    blank_h = (LID_W + 2 * LID_H) * mm
    x = (width - blank_w) / 2
    y = (height - 24 * mm - blank_h) / 2 + 4 * mm
    draw_tray_dieline(c, LID_L, LID_W, LID_H, 14, x, y, lid=True)
  else:
    blank_w = (BOX_L + 2 * BOX_H) * mm
    blank_h = (BOX_W + 2 * BOX_H) * mm
    x = (width - blank_w) / 2
    y = (height - 24 * mm - blank_h) / 2 + 4 * mm
    draw_tray_dieline(c, BOX_L, BOX_W, BOX_H, 15, x, y, lid=False)
  legend(c, 12 * mm, 12 * mm)
  calibration(c, width - 72 * mm, 12 * mm)
  c.showPage()


def page_insert(c):
  c.setPageSize(landscape(letter))
  page_header(c, 4, "Bridge insert", "151 x 86 mm platform / 17 mm cable bay")
  width, height = c._pagesize
  blank_w = INSERT_L * mm
  blank_h = (INSERT_W + 2 * INSERT_LEG) * mm
  x = (width - blank_w) / 2
  y = (height - 24 * mm - blank_h) / 2 + 3 * mm

  c.setFillColor(PAPER)
  c.rect(x, y, blank_w, blank_h, fill=1, stroke=0)
  # Cable bay labels on the folded legs.
  c.setFillColor(HexColor("#DFF8FD"))
  c.rect(x, y, blank_w, INSERT_LEG * mm, fill=1, stroke=0)
  c.rect(x, y + (INSERT_LEG + INSERT_W) * mm,
         blank_w, INSERT_LEG * mm, fill=1, stroke=0)
  text(c, "FOLD DOWN / CABLE BAY", x + blank_w / 2,
       y + 6 * mm, 6.5, FOLD, "Helvetica-Bold", "center")
  text(c, "FOLD DOWN / CABLE BAY", x + blank_w / 2,
       y + (INSERT_LEG + INSERT_W + 6) * mm,
       6.5, FOLD, "Helvetica-Bold", "center")

  # Outer cut line.
  c.setStrokeColor(CUT)
  c.setLineWidth(0.9)
  c.rect(x, y, blank_w, blank_h, fill=0, stroke=1)
  # Device pocket and finger holes.
  pocket_x = x + (INSERT_L - POCKET_L) * mm / 2
  pocket_y = y + (INSERT_LEG + (INSERT_W - POCKET_W) / 2) * mm
  c.roundRect(pocket_x, pocket_y, POCKET_L * mm, POCKET_W * mm,
              POCKET_W * mm / 2, fill=0, stroke=1)
  for cx in (pocket_x + 4 * mm, pocket_x + POCKET_L * mm - 4 * mm):
    c.circle(cx, pocket_y + POCKET_W * mm / 2, 7 * mm, fill=0, stroke=1)
  text(c, "DEVICE POCKET / START 116 x 44 mm",
       x + blank_w / 2, pocket_y + POCKET_W * mm / 2 - 2,
       6.5, MUTED, "Helvetica-Bold", "center")

  # Fold lines.
  c.setStrokeColor(FOLD)
  c.setLineWidth(0.75)
  c.setDash(4, 3)
  c.line(x, y + INSERT_LEG * mm,
         x + blank_w, y + INSERT_LEG * mm)
  c.line(x, y + (INSERT_LEG + INSERT_W) * mm,
         x + blank_w, y + (INSERT_LEG + INSERT_W) * mm)
  c.setDash()
  legend(c, 12 * mm, 12 * mm)
  calibration(c, width - 72 * mm, 12 * mm)
  c.showPage()


def draw_qr(c, value, x, y, size):
  code = qr.QrCodeWidget(value)
  bounds = code.getBounds()
  drawing = Drawing(size, size, transform=[
      size / (bounds[2] - bounds[0]), 0, 0,
      size / (bounds[3] - bounds[1]), 0, 0])
  drawing.add(code)
  drawing.drawOn(c, x, y)


def draw_card_front(c, x, y):
  w, h = CARD_L * mm, CARD_W * mm
  c.setFillColor(WARM_WHITE)
  c.roundRect(x, y, w, h, 3 * mm, fill=1, stroke=0)
  corner_accents(c, x, y, w, h, 3.2)
  c.drawImage(str(WIDE_LOGO), x + 31 * mm, y + 34 * mm, 78 * mm, 24 * mm,
              preserveAspectRatio=True, mask="auto")
  text(c, "STUDIO CONTROLLER", x + w / 2, y + 25 * mm,
       7, HexColor("#595959"), "Helvetica-Bold", "center")
  text(c, "PROTOTYPE SERIES  /  UNIT P-___", x + w / 2, y + 13 * mm,
       6.5, PURPLE, "Helvetica-Bold", "center")
  c.setStrokeColor(CUT)
  c.setLineWidth(0.8)
  c.roundRect(x, y, w, h, 3 * mm, fill=0, stroke=1)


def draw_card_back(c, x, y):
  w, h = CARD_L * mm, CARD_W * mm
  c.setFillColor(PAPER)
  c.roundRect(x, y, w, h, 3 * mm, fill=1, stroke=0)
  text(c, "START HERE", x + 7 * mm, y + h - 10 * mm,
       8, ORANGE, "Helvetica-Bold")
  text(c, "1", x + 7 * mm, y + h - 23 * mm, 10, CYAN, "Helvetica-Bold")
  text(c, "Turn on with the side power switch.", x + 14 * mm,
       y + h - 22.5 * mm, 7.2, INK, "Helvetica-Bold")
  text(c, "2", x + 7 * mm, y + h - 34 * mm, 10, CYAN, "Helvetica-Bold")
  text(c, "Choose Devices, Scenes, or Portal from Home.", x + 14 * mm,
       y + h - 33.5 * mm, 7.2, INK, "Helvetica-Bold")
  text(c, "3", x + 7 * mm, y + h - 45 * mm, 10, CYAN, "Helvetica-Bold")
  text(c, "Hold the Action Button 700 ms to go Back.", x + 14 * mm,
       y + h - 44.5 * mm, 7.2, INK, "Helvetica-Bold")
  text(c, "Hold it 2 seconds to return Home.", x + 14 * mm,
       y + h - 51 * mm, 6.7, MUTED)
  text(c, "PROTOTYPE - NOT FOR RESALE", x + 7 * mm, y + 8 * mm,
       6.3, ORANGE, "Helvetica-Bold")
  draw_qr(c, "https://bleep.hml.tech", x + w - 31 * mm, y + 12 * mm, 23 * mm)
  text(c, "GUIDE + FEEDBACK", x + w - 19.5 * mm, y + 7 * mm,
       5.5, MUTED, "Helvetica-Bold", "center")
  c.setStrokeColor(CUT)
  c.setLineWidth(0.8)
  c.roundRect(x, y, w, h, 3 * mm, fill=0, stroke=1)


def page_cards(c):
  c.setPageSize(letter)
  page_header(c, 5, "Quick-start card", "140 x 75 mm / print duplex or glue back-to-back")
  width, height = c._pagesize
  x = (width - CARD_L * mm) / 2
  draw_card_front(c, x, height - 115 * mm)
  text(c, "FRONT", x - 4 * mm, height - 79 * mm,
       6, MUTED, "Helvetica-Bold", "right")
  draw_card_back(c, x, height - 202 * mm)
  text(c, "BACK", x - 4 * mm, height - 166 * mm,
       6, MUTED, "Helvetica-Bold", "right")
  paragraph(c,
      "For a duplex card, use the printer's short-edge setting and run one "
      "plain-paper alignment test. Otherwise print both faces, trim, and mount "
      "them back-to-back on 300-350 gsm stock.",
      35 * mm, 68 * mm, width - 70 * mm, 25 * mm, 7.7, 10.5, MUTED,
      align=TA_CENTER)
  footer(c, "CARD / REV A")
  c.showPage()


def page_test(c):
  c.setPageSize(letter)
  page_header(c, 6, "Fit and handling test", "Do this before ordering five boxes")
  width, height = c._pagesize
  sections = [
      ("01 / SCALE", [
          "Print at Actual Size; the 50 mm bar must measure within 0.5 mm.",
          "Use plain paper first, then the intended prototype board.",
      ]),
      ("02 / FIT", [
          "The lid seats without bowing and lifts in roughly 2-3 seconds.",
          "The controller does not rub, rattle, or press its Action Button.",
          "The folded cable stays below the insert and never touches the case.",
          "The card lies flat and can be removed without lifting the device.",
      ]),
      ("03 / OPENING EXPERIENCE", [
          "A tester sees the quick-start card first and the controller second.",
          "Finger openings make the controller removable without pulling wires.",
          "No raw adhesive, sharp board edge, or loose print debris is exposed.",
      ]),
      ("04 / HANDLING", [
          "Shake the closed box ten times in each axis; contents must not migrate.",
          "Use an inert device-shaped weight for initial 75 cm face, edge, and "
          "corner drop tests inside the intended shipping carton.",
          "After geometry passes, repeat only with a powered-off, inspected unit. "
          "Never test with a swollen, damaged, hot, or suspect LiPo.",
          "Place 2 kg evenly on the closed package for 24 hours; reject crushing "
          "that changes lid fit or touches the device.",
      ]),
      ("05 / RECORD", [
          "Mark the prototype P-001 through P-005 and record board material.",
          "Photograph wear, note every interference point, and revise dimensions "
          "in 0.5 mm increments before requesting a vendor sample.",
      ]),
  ]
  y = height - 38 * mm
  for title, bullets in sections:
    text(c, title, 16 * mm, y, 8, ORANGE, "Helvetica-Bold")
    y -= 7 * mm
    for bullet in bullets:
      text(c, "+", 18 * mm, y, 8, CYAN, "Helvetica-Bold")
      paragraph(c, bullet, 25 * mm, y + 4 * mm, width - 42 * mm,
                10 * mm, 8.2, 10.5, INK)
      y -= 10.5 * mm
    y -= 3 * mm
  c.setFillColor(INK)
  c.roundRect(16 * mm, 18 * mm, width - 32 * mm, 24 * mm,
              3 * mm, fill=1, stroke=0)
  text(c, "PASS GATE", 22 * mm, 33 * mm, 7, CYAN, "Helvetica-Bold")
  paragraph(c,
      "Order the five presentation boxes only after the same physical device, "
      "card, and cable pass the paper and board prototypes without movement, "
      "button activation, abrasion, or lid interference.",
      48 * mm, 39 * mm, width - 70 * mm, 17 * mm, 8, 10.5, PAPER)
  footer(c, "TEST PROTOCOL / REV A")
  c.showPage()


def generate():
  OUTPUT.parent.mkdir(parents=True, exist_ok=True)
  c = canvas.Canvas(str(OUTPUT), pagesize=letter,
                    pageCompression=1,
                    title="Ble(e)p Packaging Prototype - Device, Card, and Cable",
                    author="Ble(e)p")
  page_concept(c)
  page_dieline(c, 2, "Lid dieline - print at 100%", "160 x 95 x 30 mm", "lid")
  page_dieline(c, 3, "Base dieline - print at 100%", "155 x 90 x 45 mm", "base")
  page_insert(c)
  page_cards(c)
  page_test(c)
  c.setTitle("Ble(e)p Packaging Prototype - Device, Card, and Cable")
  c.setAuthor("Ble(e)p project")
  c.setSubject("Full-scale prototype dielines, quick-start card, and fit test")
  c.save()
  print(OUTPUT)


if __name__ == "__main__":
  generate()
