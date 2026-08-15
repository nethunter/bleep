#!/usr/bin/env python3
"""Run every host-only protocol lab through one stable entry point."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
LABS = ("gopro_lab", "insta360_lab")


def main() -> int:
    for lab in LABS:
        print(f"== {lab} ==", flush=True)
        result = subprocess.run(
            [sys.executable, "-m", "unittest", "-v", "test_protocol.py"],
            cwd=ROOT / lab,
        )
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
