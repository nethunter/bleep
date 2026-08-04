# Home Mode Icon Generation

The PNGs in this directory are the source artwork for the 48x48 Home mode
icons embedded in the firmware. Keep this prompt recipe when adding or
regenerating an icon so the Home grid remains visually consistent.

## Successful generation recipe

The current colorful icons were generated on 2026-08-03 with Nano Banana Pro,
using the model ID `gemini-3-pro-image-preview`, a square image response, and
temperature `0.8`. Model IDs may change; use the current Nano Banana Pro image
model when the recorded preview ID is unavailable.

Start every prompt with this shared style block:

> Fun colorful app icon for a round OLED studio remote home screen. Playful
> glossy sticker / toy-like 3D illustration, saturated cheerful colors, soft
> rounded shapes, delightful and quirky, not corporate. Subject centered on a
> pure black background with NO outer square frame, NO border plate, NO text,
> NO watermark. Square 1:1, generous padding so the glyph reads clearly at 48
> pixels.

Append exactly one concise subject description:

- **Devices:** `Subject: a cute cartoon motorized camera slider with a tiny
  smiling camera riding the rail, cyan and electric blue accents, warm orange
  wheels.`
- **Groups:** `Subject: three cute glowing LED softbox lights clustered as a
  friendly group, lime green, magenta, and gold colors.`
- **Scenes:** `Subject: a playful film clapperboard character mid-clap, sunny
  yellow and hot pink stripes, big friendly energy.`
- **Portal:** `Subject: a whimsical Wi-Fi portal doorway / gateway arch
  shooting playful signal waves, violet and turquoise neon candy colors.`

Device-category icons (Add device / Add scene step grids):

- **Motion:** `Subject: a cute cartoon motorized camera slider with a tiny
  smiling camera riding the rail, cyan and electric blue accents, warm orange
  wheels.`
- **Lights:** `Subject: a cute glowing LED softbox light character with a warm
  friendly face, lime green and gold colors, soft glow.`
- **Cameras:** `Subject: a cute cartoon mirrorless camera with a big friendly
  lens eye and a tiny smiling face, coral red and cyan accents, glossy body.`
- **Recorders:** `Subject: a cute cartoon portable audio recorder with chunky
  knobs and a smiling VU meter face, mint green and gold accents.`

For a new mode or category, preserve the shared block and append a similarly
concrete subject sentence. Specify recognizable objects, a small palette, and
any important count or arrangement. Generate each icon independently as a 1:1
PNG. Save category icons as `icon_cat_<name>.png`.

## Review checklist

Before accepting generated artwork:

- the subject is recognizable when reduced to 48x48;
- the outer area is pure or near black for clean transparency keying;
- there is no enclosing app-icon frame, border plate, text, or watermark;
- the subject has generous padding and does not touch the image edges;
- colors and glossy, rounded forms match the existing four icons;
- the image is saved as `assets/icons/icon_<mode>.png`.

If a model adds a baked-in frame, repeat `NO outer square frame, NO border
plate` and describe the subject as a standalone glyph. If a result is too
abstract, state the exact object count and arrangement, such as “three
identical lights, one above two.”

## Embed into LVGL

The checked-in sources are 1024x1024 RGB PNGs. Regenerate the 48x48 true-color
LVGL arrays after changing any source:

```sh
python3 tools/gen_icons.py --size 48
```

`tools/gen_icons.py` keys the near-black background to transparency, crops and
scales the artwork, and writes `src/assets/ui_icons.c` and
`src/assets/ui_icons.h`. Review `sim/screenshots/01_home.png` after running the
UI simulator because fine details that work at 1024x1024 may disappear on the
panel.
