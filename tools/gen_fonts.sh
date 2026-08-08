#!/usr/bin/env bash
# Regenerate the embedded Roboto LVGL fonts in src/fonts/.
#
# These are an optional alternate UI font family, selected at compile time with
# -D UI_FONT_ROBOTO (see the crowpanel_128_roboto env in platformio.ini). The
# generated .c files are committed; only re-run this when changing sizes/range.
#
# Requires Node (for npx). Downloads the Roboto TTF if it is not already present.
#
# Usage: tools/gen_fonts.sh
set -euo pipefail

cd "$(dirname "$0")/.."

TTF="tools/fontsrc/Roboto-Regular.ttf"
TTF_URL="https://raw.githubusercontent.com/googlefonts/roboto-2/main/src/hinted/Roboto-Regular.ttf"
OUT="src/fonts"
SIZES=(12 14 16 20)
RANGE="0x20-0x7F"   # printable ASCII; the UI uses no LVGL symbols

mkdir -p "$OUT" "$(dirname "$TTF")"

if [ ! -f "$TTF" ]; then
  echo "Downloading Roboto-Regular.ttf..."
  curl -sSL --max-time 60 -o "$TTF" "$TTF_URL"
fi

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
