#!/usr/bin/env python3
"""Generate the compact WebP logo embedded by the Portal HTTP server."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--source", type=Path, default=Path("assets/bleep_logo.png"))
  parser.add_argument("--output", type=Path,
                      default=Path("src/assets/portal_logo.h"))
  parser.add_argument("--width", type=int, default=320)
  args = parser.parse_args()

  with tempfile.TemporaryDirectory() as directory:
    webp = Path(directory) / "bleep_logo.webp"
    subprocess.run([
        "magick", str(args.source), "-resize", f"{args.width}x", "-strip",
        "-quality", "82", str(webp),
    ], check=True)
    data = webp.read_bytes()

  rows = []
  for offset in range(0, len(data), 12):
    rows.append("  " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 12]) + ",")

  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(
      "#pragma once\n\n"
      "#include <Arduino.h>\n\n"
      "// Auto-generated from assets/bleep_logo.png by tools/gen_portal_logo.py.\n"
      "namespace portal::assets {\n\n"
      "static const uint8_t kPortalLogoWebp[] PROGMEM = {\n"
      + "\n".join(rows)
      + "\n};\n"
      "static const size_t kPortalLogoWebpSize = sizeof(kPortalLogoWebp);\n\n"
      "}  // namespace portal::assets\n",
      encoding="utf-8",
  )


if __name__ == "__main__":
  main()
