#!/usr/bin/env bash
# Regenerate the embedded Roboto LVGL fonts in src/fonts/.
#
# Requires Node (for npx) and the Roboto TTF in tools/fontsrc/. The generated
# .c files are committed; only re-run this when changing sizes/glyph range.
#
# Usage: tools/gen_fonts.sh
set -euo pipefail

cd "$(dirname "$0")/.."

TTF="tools/fontsrc/Roboto-Regular.ttf"
OUT="src/fonts"
SIZES=(14 16 20)
RANGE="0x20-0x7F"   # printable ASCII; the UI uses no LVGL symbols

mkdir -p "$OUT"

for size in "${SIZES[@]}"; do
  echo "Generating lv_font_roboto_${size}..."
  npx -y lv_font_conv@^1.5.3 \
    --font "$TTF" \
    --size "$size" \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    --force-fast-kern-format \
    -r "$RANGE" \
    -o "$OUT/lv_font_roboto_${size}.c"
done

echo "Done. Generated: ${SIZES[*]}"
