#!/usr/bin/env python3
"""Build the maintainable Ble(e)p Markdown manual as a styled PDF."""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path

import yaml
from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch, mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    Flowable,
    Frame,
    Image,
    KeepTogether,
    ListFlowable,
    ListItem,
    NextPageTemplate,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.platypus.tableofcontents import TableOfContents


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("manual.md")
DEFAULT_OUTPUT = ROOT / "output" / "pdf" / "bleep-instruction-manual.pdf"

INK = colors.HexColor("#15202B")
MUTED = colors.HexColor("#5F6B76")
CYAN = colors.HexColor("#14C8E8")
BLUE = colors.HexColor("#087FA2")
ORANGE = colors.HexColor("#FF8A2A")
PALE_BLUE = colors.HexColor("#EAF9FC")
PALE_ORANGE = colors.HexColor("#FFF2E7")
PALE_GRAY = colors.HexColor("#F3F6F8")
LINE = colors.HexColor("#D8E1E6")


def register_fonts() -> tuple[str, str, str]:
  candidates = [
      (
          Path("/Library/Fonts/Montserrat-Medium.ttf"),
          Path("/Library/Fonts/Montserrat-SemiBold.ttf"),
          Path("/Library/Fonts/Montserrat-Medium.ttf"),
      ),
  ]
  for regular, bold, italic in candidates:
    if all(path.exists() for path in (regular, bold, italic)):
      pdfmetrics.registerFont(TTFont("BleepSans", str(regular)))
      pdfmetrics.registerFont(TTFont("BleepSans-Bold", str(bold)))
      pdfmetrics.registerFont(TTFont("BleepSans-Italic", str(italic)))
      pdfmetrics.registerFontFamily(
          "BleepSans",
          normal="BleepSans",
          bold="BleepSans-Bold",
          italic="BleepSans-Italic",
          boldItalic="BleepSans-Bold",
      )
      return "BleepSans", "BleepSans-Bold", "BleepSans-Italic"
  return "Helvetica", "Helvetica-Bold", "Helvetica-Oblique"


FONT, FONT_BOLD, FONT_ITALIC = register_fonts()


def inline_markup(text: str) -> str:
  placeholders: list[str] = []

  def save(value: str) -> str:
    placeholders.append(value)
    return f"@@{len(placeholders) - 1}@@"

  text = re.sub(r"`([^`]+)`", lambda m: save(f"<font name='Courier'>{html.escape(m.group(1))}</font>"), text)
  text = re.sub(
      r"\[([^\]]+)\]\((https?://[^)]+)\)",
      lambda m: save(f"<link href='{html.escape(m.group(2), quote=True)}' color='#087FA2'>{html.escape(m.group(1))}</link>"),
      text,
  )
  text = html.escape(text)
  text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
  text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<i>\1</i>", text)
  for index, value in enumerate(placeholders):
    text = text.replace(f"@@{index}@@", value)
  return text


class RotatedFigure(Flowable):
  def __init__(self, path: Path, width: float, rotation: int, caption: Paragraph):
    super().__init__()
    self.path = path
    self.width = width
    self.rotation = rotation % 360
    with PILImage.open(path) as image:
      px_w, px_h = image.size
    if self.rotation in (90, 270):
      px_w, px_h = px_h, px_w
    self.image_height = width * px_h / px_w
    self.caption = caption
    _, caption_height = caption.wrap(width, 100 * mm)
    self.caption_height = caption_height
    self.height = self.image_height + caption_height + 4 * mm

  def wrap(self, avail_width, avail_height):
    return min(self.width, avail_width), self.height

  def draw(self):
    canvas = self.canv
    width = self.width
    height = self.image_height
    canvas.saveState()
    if self.rotation == 90:
      canvas.translate(width, 0)
      canvas.rotate(90)
      canvas.drawImage(str(self.path), 0, 0, width=height, height=width, preserveAspectRatio=True)
    elif self.rotation == 270:
      canvas.translate(0, height)
      canvas.rotate(-90)
      canvas.drawImage(str(self.path), 0, 0, width=height, height=width, preserveAspectRatio=True)
    elif self.rotation == 180:
      canvas.translate(width, height)
      canvas.rotate(180)
      canvas.drawImage(str(self.path), 0, 0, width=width, height=height, preserveAspectRatio=True)
    else:
      canvas.drawImage(str(self.path), 0, 0, width=width, height=height, preserveAspectRatio=True)
    canvas.restoreState()
    self.caption.drawOn(canvas, 0, -self.caption_height - 2 * mm)


class ManualDocTemplate(BaseDocTemplate):
  def __init__(self, filename: str, metadata: dict[str, str]):
    super().__init__(
        filename,
        pagesize=A4,
        leftMargin=18 * mm,
        rightMargin=18 * mm,
        topMargin=21 * mm,
        bottomMargin=18 * mm,
        title=metadata.get("title", "Ble(e)p Instruction Manual"),
        author=metadata.get("author", "Ble(e)p project"),
        subject=metadata.get("subtitle", "User and compatibility guide"),
    )
    self.metadata = metadata
    self.current_section = "Instruction manual"
    cover_frame = Frame(0, 0, A4[0], A4[1], id="cover", leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
    body_frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="body")
    self.addPageTemplates([
        PageTemplate(id="Cover", frames=[cover_frame], onPage=self.draw_cover_background),
        PageTemplate(id="Body", frames=[body_frame], onPage=self.draw_body_chrome),
    ])

  def draw_cover_background(self, canvas, doc):
    canvas.saveState()
    canvas.setFillColor(INK)
    canvas.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
    canvas.setFillColor(CYAN)
    canvas.circle(A4[0] - 20 * mm, A4[1] - 18 * mm, 38 * mm, fill=1, stroke=0)
    canvas.setFillColor(ORANGE)
    canvas.circle(4 * mm, 12 * mm, 25 * mm, fill=1, stroke=0)
    canvas.restoreState()

  def draw_body_chrome(self, canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(LINE)
    canvas.setLineWidth(0.5)
    canvas.line(self.leftMargin, A4[1] - 13 * mm, A4[0] - self.rightMargin, A4[1] - 13 * mm)
    canvas.setFont(FONT_BOLD, 7.5)
    canvas.setFillColor(BLUE)
    canvas.drawString(self.leftMargin, A4[1] - 10 * mm, "BLE(E)P")
    canvas.setFont(FONT, 7.5)
    canvas.setFillColor(MUTED)
    header = "Instruction manual"
    canvas.drawRightString(A4[0] - self.rightMargin, A4[1] - 10 * mm, header)
    canvas.line(self.leftMargin, 12 * mm, A4[0] - self.rightMargin, 12 * mm)
    canvas.setFont(FONT, 7.5)
    canvas.drawString(self.leftMargin, 8 * mm, self.metadata.get("edition", "Development edition"))
    canvas.drawRightString(A4[0] - self.rightMargin, 8 * mm, str(doc.page - 1))
    canvas.restoreState()

  def afterFlowable(self, flowable):
    if isinstance(flowable, Paragraph):
      level = getattr(flowable, "toc_level", None)
      if level is not None:
        text = flowable.getPlainText()
        key = f"heading-{self.seq.nextf('heading')}"
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(text, key, level=level, closed=False)
        self.notify("TOCEntry", (level, text, self.page - 1, key))
        if level == 0:
          self.current_section = text


def make_styles():
  styles = getSampleStyleSheet()
  styles.add(ParagraphStyle(
      "ManualBody", fontName=FONT, fontSize=9.2, leading=13.3,
      textColor=INK, spaceAfter=6, allowWidows=0, allowOrphans=0,
  ))
  styles.add(ParagraphStyle(
      "ManualH1", fontName=FONT_BOLD, fontSize=22, leading=25,
      textColor=INK, spaceBefore=14, spaceAfter=9, keepWithNext=True,
  ))
  styles.add(ParagraphStyle(
      "ManualH2", fontName=FONT_BOLD, fontSize=14.5, leading=18,
      textColor=BLUE, spaceBefore=11, spaceAfter=6, keepWithNext=True,
  ))
  styles.add(ParagraphStyle(
      "ManualH3", fontName=FONT_BOLD, fontSize=10.5, leading=14,
      textColor=INK, spaceBefore=8, spaceAfter=4, keepWithNext=True,
  ))
  styles.add(ParagraphStyle(
      "ManualCaption", fontName=FONT, fontSize=7.5, leading=10,
      textColor=MUTED, alignment=TA_CENTER, spaceBefore=3, spaceAfter=7,
  ))
  styles.add(ParagraphStyle(
      "ManualCallout", fontName=FONT, fontSize=8.6, leading=12.5,
      textColor=INK, leftIndent=2 * mm, rightIndent=2 * mm,
  ))
  styles.add(ParagraphStyle(
      "ManualTable", fontName=FONT, fontSize=7.1, leading=9.4, textColor=INK,
  ))
  styles.add(ParagraphStyle(
      "ManualTableHead", fontName=FONT_BOLD, fontSize=7.2, leading=9.4,
      textColor=colors.white,
  ))
  styles.add(ParagraphStyle(
      "ManualCode", fontName="Courier", fontSize=7.5, leading=10,
      textColor=INK, backColor=PALE_GRAY, borderPadding=6, spaceAfter=7,
  ))
  return styles


STYLES = make_styles()


def load_source(path: Path) -> tuple[dict, list[str]]:
  text = path.read_text(encoding="utf-8")
  if not text.startswith("---\n"):
    return {}, text.splitlines()
  _, frontmatter, body = text.split("---\n", 2)
  return yaml.safe_load(frontmatter) or {}, body.splitlines()


def paragraph(text: str, style: str = "ManualBody") -> Paragraph:
  return Paragraph(inline_markup(text.strip()), STYLES[style])


def figure(path: Path, caption: str, width_in: float, rotation: int = 0):
  if not path.exists():
    raise FileNotFoundError(f"Manual image not found: {path}")
  width = min(width_in * inch, A4[0] - 36 * mm)
  caption_flowable = Paragraph(inline_markup(caption), STYLES["ManualCaption"])
  if rotation:
    return RotatedFigure(path, width, rotation, caption_flowable)
  with PILImage.open(path) as source:
    px_w, px_h = source.size
  image = Image(str(path), width=width, height=width * px_h / px_w)
  image.hAlign = "CENTER"
  return KeepTogether([image, caption_flowable])


def parse_table(lines: list[str]) -> Table:
  rows = [[cell.strip() for cell in line.strip().strip("|").split("|")] for line in lines]
  if len(rows) > 1 and all(re.fullmatch(r":?-{3,}:?", cell) for cell in rows[1]):
    del rows[1]
  columns = max(len(row) for row in rows)
  usable = A4[0] - 36 * mm
  weights = []
  for col in range(columns):
    max_len = max((len(row[col]) if col < len(row) else 0) for row in rows)
    weights.append(max(8, min(max_len, 38)))
  total_weight = sum(weights)
  widths = [usable * weight / total_weight for weight in weights]
  data = []
  for row_index, row in enumerate(rows):
    style = "ManualTableHead" if row_index == 0 else "ManualTable"
    data.append([Paragraph(inline_markup(row[col] if col < len(row) else ""), STYLES[style]) for col in range(columns)])
  table = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
  table.setStyle(TableStyle([
      ("BACKGROUND", (0, 0), (-1, 0), BLUE),
      ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
      ("VALIGN", (0, 0), (-1, -1), "TOP"),
      ("GRID", (0, 0), (-1, -1), 0.35, LINE),
      ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE_GRAY]),
      ("LEFTPADDING", (0, 0), (-1, -1), 5),
      ("RIGHTPADDING", (0, 0), (-1, -1), 5),
      ("TOPPADDING", (0, 0), (-1, -1), 4),
      ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
  ]))
  return table


def parse_markdown(lines: list[str], source_dir: Path) -> list[Flowable]:
  story: list[Flowable] = []
  pending: list[str] = []
  list_items: list[tuple[int, str]] = []
  table_lines: list[str] = []
  code_lines: list[str] = []
  in_code = False

  def flush_paragraph():
    if pending:
      story.append(paragraph(" ".join(part.strip() for part in pending)))
      pending.clear()

  def flush_list():
    if not list_items:
      return
    ordered = list_items[0][0] == 1
    items = [ListItem(paragraph(text), leftIndent=5 * mm) for _, text in list_items]
    list_options = {
        "bulletType": "1" if ordered else "bullet",
        "leftIndent": 5 * mm,
        "bulletFontName": FONT_BOLD,
        "bulletFontSize": 8,
        "bulletColor": BLUE,
        "spaceAfter": 5,
    }
    if ordered:
      list_options["start"] = "1"
    else:
      list_options["bulletChar"] = "•"
    story.append(ListFlowable(items, **list_options))
    list_items.clear()

  def flush_table():
    if table_lines:
      story.append(parse_table(table_lines.copy()))
      story.append(Spacer(1, 6))
      table_lines.clear()

  def flush_all():
    flush_paragraph()
    flush_list()
    flush_table()

  for raw in lines + [""]:
    line = raw.rstrip()
    if line.startswith("```"):
      if in_code:
        story.append(Paragraph("<br/>".join(html.escape(item) for item in code_lines), STYLES["ManualCode"]))
        code_lines.clear()
        in_code = False
      else:
        flush_all()
        in_code = True
      continue
    if in_code:
      code_lines.append(line)
      continue
    if line == "<!-- pagebreak -->":
      flush_all()
      story.append(PageBreak())
      continue
    heading = re.match(r"^(#{1,3})\s+(.+)$", line)
    if heading:
      flush_all()
      level = len(heading.group(1)) - 1
      flowable = Paragraph(inline_markup(heading.group(2)), STYLES[f"ManualH{level + 1}"])
      flowable.toc_level = level
      story.append(flowable)
      continue
    image_match = re.match(r"^!\[([^\]]*)\]\(([^)]+)\)(?:\{([^}]*)\})?$", line)
    if image_match:
      flush_all()
      options = dict(re.findall(r"(width|rotate)=([\d.]+)", image_match.group(3) or ""))
      story.append(figure(
          source_dir / image_match.group(2), image_match.group(1),
          float(options.get("width", "3.1")), int(float(options.get("rotate", "0"))),
      ))
      continue
    if line.startswith("|") and line.endswith("|"):
      flush_paragraph()
      flush_list()
      table_lines.append(line)
      continue
    bullet = re.match(r"^[-*]\s+(.+)$", line)
    number = re.match(r"^\d+[.]\s+(.+)$", line)
    if bullet or number:
      flush_paragraph()
      flush_table()
      list_items.append((1 if number else 0, (number or bullet).group(1)))
      continue
    if line.startswith("> "):
      flush_all()
      callout = Table([[Paragraph(inline_markup(line[2:]), STYLES["ManualCallout"]) ]], colWidths=[A4[0] - 42 * mm])
      callout.setStyle(TableStyle([
          ("BACKGROUND", (0, 0), (-1, -1), PALE_ORANGE),
          ("BOX", (0, 0), (-1, -1), 0.8, ORANGE),
          ("LEFTPADDING", (0, 0), (-1, -1), 8),
          ("RIGHTPADDING", (0, 0), (-1, -1), 8),
          ("TOPPADDING", (0, 0), (-1, -1), 7),
          ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
      ]))
      story.extend([callout, Spacer(1, 7)])
      continue
    if not line.strip():
      flush_all()
      continue
    if table_lines:
      flush_table()
    if list_items and line.startswith("  "):
      marker, previous = list_items[-1]
      list_items[-1] = (marker, previous + " " + line.strip())
      continue
    if list_items:
      flush_list()
    pending.append(line)
  return story


def cover_story(metadata: dict) -> list[Flowable]:
  logo = Image(str(Path(__file__).with_name("assets") / "logo.png"), width=26 * mm, height=26 * mm)
  logo.hAlign = "LEFT"
  cover_photo = Image(str(Path(__file__).with_name("assets") / "controller-front-line.png"), width=72 * mm, height=128 * mm)
  cover_photo.hAlign = "CENTER"
  title_style = ParagraphStyle("CoverTitle", fontName=FONT_BOLD, fontSize=31, leading=34, textColor=colors.white)
  subtitle_style = ParagraphStyle("CoverSub", fontName=FONT, fontSize=12, leading=17, textColor=colors.HexColor("#CDEEF4"))
  meta_style = ParagraphStyle("CoverMeta", fontName=FONT_BOLD, fontSize=8.5, leading=12, textColor=INK, alignment=TA_CENTER)
  badge = Table([[Paragraph(inline_markup(metadata.get("status", "Development manual")), meta_style)]], colWidths=[48 * mm])
  badge.setStyle(TableStyle([
      ("BACKGROUND", (0, 0), (-1, -1), ORANGE),
      ("BOX", (0, 0), (-1, -1), 0, ORANGE),
      ("LEFTPADDING", (0, 0), (-1, -1), 7),
      ("RIGHTPADDING", (0, 0), (-1, -1), 7),
      ("TOPPADDING", (0, 0), (-1, -1), 6),
      ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
  ]))
  return [
      Spacer(1, 22 * mm),
      Table([[logo, cover_photo]], colWidths=[84 * mm, 90 * mm], style=TableStyle([
          ("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LEFTPADDING", (0, 0), (-1, -1), 0),
          ("RIGHTPADDING", (0, 0), (-1, -1), 0),
      ])),
      Spacer(1, -104 * mm),
      Table([[
          Paragraph(inline_markup(metadata.get("title", "Ble(e)p Instruction Manual")), title_style),
          "",
      ]], colWidths=[91 * mm, 83 * mm], style=TableStyle([
          ("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LEFTPADDING", (0, 0), (-1, -1), 0),
          ("RIGHTPADDING", (0, 0), (-1, -1), 0),
      ])),
      Spacer(1, 6 * mm),
      Table([[
          Paragraph(inline_markup(metadata.get("subtitle", "User guide")), subtitle_style),
          "",
      ]], colWidths=[82 * mm, 92 * mm], style=TableStyle([
          ("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LEFTPADDING", (0, 0), (-1, -1), 0),
          ("RIGHTPADDING", (0, 0), (-1, -1), 0),
      ])),
      Spacer(1, 25 * mm),
      badge,
      Spacer(1, 4 * mm),
      Paragraph(
          inline_markup(f"{metadata.get('edition', '')}  |  {metadata.get('date', '')}"),
          ParagraphStyle("CoverEdition", fontName=FONT, fontSize=8, leading=11, textColor=colors.white, alignment=TA_CENTER),
      ),
      NextPageTemplate("Body"),
      PageBreak(),
  ]


def build(source: Path, output: Path):
  metadata, lines = load_source(source)
  output.parent.mkdir(parents=True, exist_ok=True)
  doc = ManualDocTemplate(str(output), metadata)
  toc = TableOfContents()
  toc.levelStyles = [
      ParagraphStyle("TOC1", fontName=FONT_BOLD, fontSize=10, leading=15, textColor=INK, leftIndent=0, firstLineIndent=0),
      ParagraphStyle("TOC2", fontName=FONT, fontSize=8.5, leading=12, textColor=MUTED, leftIndent=8 * mm, firstLineIndent=0),
      ParagraphStyle("TOC3", fontName=FONT, fontSize=7.5, leading=10, textColor=MUTED, leftIndent=14 * mm, firstLineIndent=0),
  ]
  contents_title = Paragraph("Contents", STYLES["ManualH1"])
  story = cover_story(metadata) + [contents_title, Spacer(1, 4 * mm), toc, PageBreak()]
  story.extend(parse_markdown(lines, source.parent))
  doc.multiBuild(story)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, default=SOURCE)
  parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
  args = parser.parse_args()
  build(args.source.resolve(), args.output.resolve())
  print(args.output.resolve())


if __name__ == "__main__":
  main()
