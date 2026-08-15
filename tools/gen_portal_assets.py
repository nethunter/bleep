#!/usr/bin/env python3
"""Generate the deterministic Brotli-compressed Portal page."""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "portal_page.h"
OUTPUT = ROOT / "src" / "portal_assets.h"


def raw_string(source: str, name: str) -> str:
    match = re.search(
        rf'const char {name}\[\] PROGMEM = R"HTML\((.*?)\)HTML";',
        source,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit(f"missing {name} in {SOURCE}")
    return match.group(1)


source = SOURCE.read_text()
page = "".join(raw_string(source, name) for name in ("kHead", "kStyle", "kBody"))
try:
    import brotli
    compressed = brotli.compress(page.encode(), mode=brotli.MODE_TEXT, quality=11)
except ImportError:
    executable = shutil.which("brotli")
    if executable is None:
        raise SystemExit("install the Python Brotli package or brotli CLI")
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "portal.html"
        output = Path(directory) / "portal.br"
        source.write_text(page)
        subprocess.run([executable, "-f", "-q", "11", str(source), "-o", str(output)],
                       check=True)
        compressed = output.read_bytes()
rows = []
for offset in range(0, len(compressed), 16):
    rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in compressed[offset:offset + 16]) + ",")

OUTPUT.write_text(
    "#pragma once\n\n"
    "#include <Arduino.h>\n\n"
    "namespace portal::assets {\n\n"
    "const uint8_t kPageBrotli[] PROGMEM = {\n"
    + "\n".join(rows)
    + "\n};\n"
    + f"constexpr size_t kPageBrotliSize = {len(compressed)};\n\n"
    "}  // namespace portal::assets\n"
)
print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(compressed)} compressed bytes)")
