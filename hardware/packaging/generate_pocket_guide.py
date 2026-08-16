#!/usr/bin/env python3
"""Generate the two-up, duplex Ble(e)p accordion pocket guide."""

from pathlib import Path

from reportlab.graphics.barcode import qr
from reportlab.graphics.shapes import Drawing
from reportlab.lib.colors import Color, HexColor
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import TABLOID, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch, mm
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).with_name("bleep-pocket-guide-2up.pdf")
WIDE_LOGO = ROOT / "website" / "assets" / "logo-wide.png"

PAGE_SIZE = landscape(TABLOID)
PANEL_W = 69 * mm
GUIDE_H = 130 * mm
GUIDE_W = 6 * PANEL_W
GUIDE_GAP = 6 * mm
PANEL_PAD = 5.5 * mm

INK = HexColor("#050708")
PANEL = HexColor("#101618")
PAPER = HexColor("#EDF6F5")
WARM_WHITE = HexColor("#FAF9F6")
MUTED = HexColor("#637476")
LIGHT_LINE = HexColor("#CBD8D9")
CYAN = HexColor("#24D9FF")
ORANGE = HexColor("#FF7A32")
PURPLE = HexColor("#7957FF")
BLUE = HexColor("#2775FF")
PINK = HexColor("#B84DFF")
CUT = HexColor("#E6007E")
FOLD = HexColor("#00A9C7")

MANUAL_URL = "https://bleep.hml.tech/downloads/bleep-instruction-manual.pdf"


PANELS = {
    1: {
        "kind": "cover",
        "kicker": "POCKET GUIDE",
        "title": "Studio control, without the scramble.",
        "body": (
            "A compact guide to setup, control, scenes, troubleshooting, and "
            "safe operation.<br/><br/><b>Development hardware</b><br/>Verify "
            "your exact equipment before critical or unrepeatable work."
        ),
    },
    2: {
        "kicker": "01 / START HERE",
        "title": "Update before setup.",
        "body": (
            "On first use, install the latest stable firmware before pairing "
            "equipment.<br/><br/>"
            "<b>1.</b> Connect USB power and switch Ble(e)p on.<br/>"
            "<b>2.</b> Open the Home cog, then <b>Wi-Fi</b>. Scan, choose a "
            "trusted network, and enter its password. Use Portal for a hidden "
            "network.<br/>"
            "<b>3.</b> Return to <b>Settings &gt; Firmware update</b> and choose "
            "<b>Check now</b>.<br/>"
            "<b>4.</b> Choose <b>Install now</b> and confirm.<br/>"
            "<b>5.</b> Keep USB power connected through Preparing update and "
            "Recovery. Do not switch off.<br/>"
            "<b>6.</b> At <b>Update successful</b>, press <b>Restart</b>. Wait "
            "for Home, then continue with Devices."
        ),
    },
    3: {
        "kicker": "02 / DEVICES",
        "title": "Pair and manage gear.",
        "body": (
            "Choose <b>Devices &gt; Add device</b>, then select the equipment "
            "category and family. Follow the pairing instructions for that exact "
            "device.<br/><br/>"
            "A saved device can be renamed, enabled, disabled, disconnected, "
            "forgotten/re-paired, or deleted from its management menu.<br/><br/>"
            "Opening a saved device begins its connection. Ble(e)p can keep up "
            "to four physical equipment links ready at once. Compatible Aputure "
            "and Zhiyun lights share one link."
        ),
    },
    4: {
        "kicker": "03 / CAMERAS",
        "title": "Check what the camera confirms.",
        "body": (
            "<b>Canon Trigger</b> sends one movie toggle; recording state is "
            "unknown. <b>Canon Smart</b>, <b>GoPro</b>, and <b>DJI Osmo</b> "
            "offer explicit Start/Stop with device feedback on supported paths.<br/><br/>"
            "<b>Insta360</b> offers Start or Stop only when its reported mode and "
            "state allow it. <b>Phone Camera</b> sends Volume Up as a shutter and "
            "cannot confirm capture.<br/><br/>"
            "When Ble(e)p says <b>Sent</b>, <b>Optimistic</b>, or <b>Unknown</b>, "
            "look at the camera before sending another toggle."
        ),
    },
    5: {
        "kicker": "04 / LIGHTS + AUDIO + MOTION",
        "title": "Control the rest of the studio.",
        "body": (
            "<b>Aputure Light</b> controls supported Aputure/amaran fixtures. "
            "<b>Zhiyun Light</b> controls MOLUS X100 and X60RGB paths. Available "
            "controls can include power, brightness, CCT, tint, and RGB looks.<br/><br/>"
            "<b>Tascam X8</b> requires the AK-BT1 adapter and provides confirmed "
            "Record Start/Stop.<br/><br/>"
            "<b>Shark Nano II</b> supports keypoints, manual positioning, speed, "
            "timing, looping, Standby, Start, and Stop. Clear the rail and watch "
            "every move."
        ),
    },
    6: {
        "kicker": "05 / SCENES",
        "title": "Build repeatable workflows.",
        "body": (
            "Open <b>Scenes &gt; Add sequence</b>. Add Start actions and Wait "
            "steps in order. New waits begin at 200 ms; tune them for your gear.<br/><br/>"
            "Review the generated Stop list, or choose <b>Customize Stop</b> for "
            "an independent list. Save, open the scene, and wait until every "
            "target is <b>Ready</b>.<br/><br/>"
            "Press Start, watch step progress, then use Stop for cleanup. If Start "
            "fails partway through, Stop remains available.<br/><br/>"
            "<font color='#FF7A32'><b>UNFOLD FULLY - THEN TURN OVER</b></font>"
        ),
    },
    7: {
        "kicker": "06 / PORTAL + HOME ASSISTANT",
        "title": "Configure in a browser.",
        "body": (
            "Open <b>Portal</b>. Join the matching <b>Bleep-Setup-XXXXX</b> "
            "network or scan the panel QR, then use the address shown.<br/><br/>"
            "Portal pauses normal device control and exists only while its screen "
            "is open. Use <b>Finish &amp; Exit</b> when done.<br/><br/>"
            "For Home Assistant, add trusted studio Wi-Fi, enter the local server "
            "URL and a long-lived token, then select up to four supported entities. "
            "The setup AP is open and Portal traffic is not encrypted."
        ),
    },
    8: {
        "kicker": "07 / READ THE STATUS",
        "title": "Ready is not the same as done.",
        "body": (
            "<b>Connecting / Preparing</b><br/>The link is being established.<br/><br/>"
            "<b>Ready</b><br/>A command can be sent; physical success is not promised.<br/><br/>"
            "<b>Pending</b><br/>Ble(e)p is waiting for an answer.<br/><br/>"
            "<b>Confirmed</b><br/>The device reported the displayed state.<br/><br/>"
            "<b>Sent / Optimistic / Unknown</b><br/>Check the real equipment.<br/><br/>"
            "<b>Unavailable / Failed</b><br/>Resolve the device and choose Retry."
        ),
    },
    9: {
        "kicker": "08 / TROUBLESHOOT",
        "title": "Recover methodically.",
        "body": (
            "<b>Device missing:</b> use the exact pairing menu, remove an old "
            "phone/remote registration if needed, and retry.<br/><br/>"
            "<b>Will not reconnect:</b> wake the device, choose Manage &gt; "
            "Disconnect, then reopen it. Free a physical link if four are active.<br/><br/>"
            "<b>Scene not Ready:</b> open the failed target, confirm it is enabled, "
            "and resolve its connection first.<br/><br/>"
            "<b>Wi-Fi scan fails:</b> return Home and retry near the access point. "
            "Use Portal to type a hidden network name.<br/><br/>"
            "<b>Portal missing:</b> stay on the Portal screen and use the numeric "
            "address shown if bleep.local does not work."
        ),
    },
    10: {
        "kicker": "09 / WORK SAFELY",
        "title": "Observe the real studio.",
        "body": (
            "Secure cameras, sliders, lights, cables, and recorders before sending "
            "commands. Keep people and loose objects clear of moving equipment.<br/><br/>"
            "Watch for movement, recording, power, and light-output confirmation "
            "whenever state is optimistic or unknown.<br/><br/>"
            "Do not charge, use, ship, or enclose a hot, damaged, or swollen battery. "
            "Power Ble(e)p off before packing it.<br/><br/>"
            "Treat Wi-Fi details, tokens, pairing data, and device identities as private."
        ),
    },
    11: {
        "kicker": "10 / SETTINGS + RESET",
        "title": "Know what Factory Reset removes.",
        "body": (
            "Open the Home cog for version information, Wi-Fi, signed firmware "
            "updates, haptics, diagnostics, and Factory Reset.<br/><br/>"
            "Factory Reset requires a three-second hold. It removes saved devices "
            "and pairings, scenes, Wi-Fi, Home Assistant links, light setup, and "
            "preferences, then restarts Ble(e)p. It does not remove the installed software.<br/><br/>"
            "Use the full owner's guide for exact-model compatibility, pairing "
            "steps, limitations, assembly, and repair information."
        ),
    },
    12: {
        "kind": "back",
        "kicker": "FULL OWNER'S GUIDE",
        "title": "Details change. Scan before the shoot.",
        "body": (
            "Exact-model compatibility matters. A similar camera, light, recorder, "
            "phone, or motion device is not automatically supported.<br/><br/>"
            "The complete guide includes current setup procedures, supported models, "
            "limitations, troubleshooting, safety, and developer information."
        ),
    },
}


def draw_qr(c, value, x, y, size):
  code = qr.QrCodeWidget(value)
  bounds = code.getBounds()
  drawing = Drawing(size, size, transform=[
      size / (bounds[2] - bounds[0]), 0, 0,
      size / (bounds[3] - bounds[1]), 0, 0])
  drawing.add(code)
  drawing.drawOn(c, x, y)


def draw_corner_accents(c, x, y, width, height):
  c.saveState()
  c.setLineCap(1)
  accents = (
      (CYAN, x - 8 * mm, y + height - 18 * mm, 25 * mm, 25 * mm, 200, 325),
      (PURPLE, x + width - 17 * mm, y + height - 10 * mm,
       27 * mm, 27 * mm, 170, 285),
      (ORANGE, x - 9 * mm, y - 8 * mm, 23 * mm, 23 * mm, 20, 135),
      (BLUE, x + width - 20 * mm, y - 13 * mm,
       26 * mm, 26 * mm, 25, 150),
      (PINK, x + width - 7 * mm, y + 7 * mm,
       16 * mm, 16 * mm, 100, 245),
  )
  for color, ax, ay, aw, ah, start, extent in accents:
    c.setStrokeColor(color)
    c.setLineWidth(3.2)
    c.arc(ax, ay, ax + aw, ay + ah, start, extent)
  c.restoreState()


def draw_paragraph(c, value, x, y_top, width, height, style):
  paragraph = Paragraph(value, style)
  _, used = paragraph.wrap(width, height)
  if used > height:
    raise RuntimeError(f"Panel text overflow: needed {used:.1f}, have {height:.1f}")
  paragraph.drawOn(c, x, y_top - used)


def draw_panel(c, panel_number, x, y, copy_label):
  data = PANELS[panel_number]
  is_cover = data.get("kind") == "cover"
  is_back = data.get("kind") == "back"
  background = PANEL if is_cover else WARM_WHITE if panel_number % 2 else PAPER
  title_color = WARM_WHITE if is_cover else INK
  body_color = HexColor("#D6E3E2") if is_cover else INK

  c.setFillColor(background)
  c.rect(x, y, PANEL_W, GUIDE_H, fill=1, stroke=0)
  if is_cover:
    draw_corner_accents(c, x, y, PANEL_W, GUIDE_H)

  top = y + GUIDE_H - PANEL_PAD
  if is_cover:
    c.drawImage(str(WIDE_LOGO), x + PANEL_PAD, top - 20 * mm,
                PANEL_W - 2 * PANEL_PAD, 17 * mm,
                preserveAspectRatio=True, mask="auto")
    top -= 28 * mm

  c.setFont("Helvetica-Bold", 6.3)
  c.setFillColor(CYAN if is_cover else ORANGE)
  c.drawString(x + PANEL_PAD, top, data["kicker"])
  top -= 7 * mm

  title_style = ParagraphStyle(
      f"title-{panel_number}", fontName="Helvetica-Bold", fontSize=13,
      leading=14.5, textColor=title_color, alignment=TA_LEFT, spaceAfter=0)
  title = Paragraph(data["title"], title_style)
  _, title_h = title.wrap(PANEL_W - 2 * PANEL_PAD, 35 * mm)
  title.drawOn(c, x + PANEL_PAD, top - title_h)
  top -= title_h + 5 * mm

  body_bottom = y + (18 * mm if is_back else 11 * mm)
  body_style = ParagraphStyle(
      f"body-{panel_number}", fontName="Helvetica", fontSize=7.7,
      leading=9.5, textColor=body_color, alignment=TA_LEFT, spaceAfter=0)
  draw_paragraph(c, data["body"], x + PANEL_PAD, top,
                 PANEL_W - 2 * PANEL_PAD, top - body_bottom, body_style)

  if is_back:
    qr_size = 25 * mm
    draw_qr(c, MANUAL_URL, x + (PANEL_W - qr_size) / 2,
            y + 12 * mm, qr_size)
    c.setFont("Helvetica-Bold", 5.5)
    c.setFillColor(MUTED)
    c.drawCentredString(x + PANEL_W / 2, y + 7 * mm,
                        "BLEEP.HML.TECH / OWNER'S GUIDE")

  c.setFont("Helvetica-Bold", 5.1)
  c.setFillColor(MUTED if not is_cover else LIGHT_LINE)
  c.drawString(x + PANEL_PAD, y + 4 * mm, f"{panel_number:02d}")
  c.drawRightString(x + PANEL_W - PANEL_PAD, y + 4 * mm,
                    f"{copy_label} / POCKET GUIDE")


def draw_guide(c, y, back, copy_label):
  page_w, _ = PAGE_SIZE
  x0 = (page_w - GUIDE_W) / 2
  order = (12, 11, 10, 9, 8, 7) if back else (1, 2, 3, 4, 5, 6)
  for column, panel_number in enumerate(order):
    draw_panel(c, panel_number, x0 + column * PANEL_W, y, copy_label)

  c.setStrokeColor(CUT)
  c.setLineWidth(0.45)
  c.rect(x0, y, GUIDE_W, GUIDE_H, fill=0, stroke=1)
  c.setStrokeColor(FOLD)
  c.setLineWidth(0.45)
  for column in range(1, 6):
    x = x0 + column * PANEL_W
    c.line(x, y, x, y + 2.2 * mm)
    c.line(x, y + GUIDE_H - 2.2 * mm, x, y + GUIDE_H)


def draw_calibration(c):
  page_w, page_h = PAGE_SIZE
  center_y = page_h / 2
  rule_y = center_y - 1.1 * mm
  x = (page_w - 50 * mm) / 2
  c.setStrokeColor(INK)
  c.setLineWidth(0.45)
  c.line(x, rule_y, x + 50 * mm, rule_y)
  for offset in (0, 25, 50):
    c.line(x + offset * mm, rule_y - 0.8 * mm,
           x + offset * mm, rule_y + 0.8 * mm)
  c.setFillColor(INK)
  c.setFont("Helvetica-Bold", 4.2)
  c.drawCentredString(page_w / 2, center_y + 1.0 * mm,
                      "50 mm - PRINT AT ACTUAL SIZE")


def draw_page(c, back):
  _, page_h = PAGE_SIZE
  total_h = 2 * GUIDE_H + GUIDE_GAP
  bottom = (page_h - total_h) / 2
  draw_guide(c, bottom + GUIDE_H + GUIDE_GAP, back, "A")
  draw_guide(c, bottom, back, "B")
  draw_calibration(c)
  c.showPage()


def generate():
  page_w, page_h = PAGE_SIZE
  assert GUIDE_W < page_w
  assert 2 * GUIDE_H + GUIDE_GAP < page_h
  assert len(PANELS) == 12

  OUTPUT.parent.mkdir(parents=True, exist_ok=True)
  c = canvas.Canvas(
      str(OUTPUT), pagesize=PAGE_SIZE, pageCompression=1,
      title="Ble(e)p Pocket Guide - Two-Up Accordion",
      author="Ble(e)p project",
      subject="Two-up duplex accordion guide for the packaging prototype")
  c.setTitle("Ble(e)p Pocket Guide - Two-Up Accordion")
  c.setAuthor("Ble(e)p project")
  c.setSubject("Two-up duplex accordion guide for the packaging prototype")
  draw_page(c, back=False)
  draw_page(c, back=True)
  c.save()
  print(OUTPUT)


if __name__ == "__main__":
  generate()
