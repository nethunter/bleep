#!/usr/bin/env python3
"""Reject generated caches and broken local Markdown links in tracked files."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ALLOWED_PDFS = {
    "output/pdf/bleep-instruction-manual.pdf",
    "website/downloads/bleep-instruction-manual.pdf",
}
MANUAL_PDFS = tuple(sorted(ALLOWED_PDFS))
LINK = re.compile(r"(?<!!)\[[^]]*]\(([^)]+)\)")


def tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True
    )
    return result.stdout.splitlines()


def main() -> int:
    tracked = tracked_files()
    errors: list[str] = []
    for name in tracked:
        path = Path(name)
        if "__pycache__" in path.parts or path.suffix == ".pyc":
            errors.append(f"tracked Python cache: {name}")
        if path.parts and path.parts[0] == "tmp":
            errors.append(f"tracked temporary artifact: {name}")
        if path.suffix.lower() == ".pdf" and name not in ALLOWED_PDFS:
            errors.append(f"unexpected tracked PDF: {name}")

    missing_manuals = [
        name
        for name in MANUAL_PDFS
        if name not in tracked or not (ROOT / name).is_file()
    ]
    for name in missing_manuals:
        errors.append(f"missing tracked manual PDF: {name}")
    if not missing_manuals:
        source_manual, website_manual = (ROOT / name for name in MANUAL_PDFS)
        if source_manual.read_bytes() != website_manual.read_bytes():
            errors.append("manual PDF copies differ")

    for name in tracked:
        if not name.endswith(".md"):
            continue
        source = ROOT / name
        for raw_target in LINK.findall(source.read_text(encoding="utf-8")):
            target = raw_target.strip().split("#", 1)[0]
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            target = target.removeprefix("<").removesuffix(">")
            resolved = (source.parent / target).resolve()
            if not resolved.exists():
                errors.append(f"broken Markdown link: {name} -> {raw_target}")

    if errors:
        print("\n".join(errors))
        return 1
    print(f"repository artifact check passed ({len(tracked)} tracked files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
