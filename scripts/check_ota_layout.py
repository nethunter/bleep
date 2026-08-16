#!/usr/bin/env python3
"""Fail CI if the OTA partition geometry or firmware ceiling drifts."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


EXPECTED = {
    "nvs": ("data", "nvs", 0x9000, 0x5000),
    "otadata": ("data", "ota", 0xE000, 0x2000),
    "recovery": ("app", "factory", 0x10000, 0x100000),
    "rec_state": ("data", "0x40", 0x110000, 0x10000),
    "ota_0": ("app", "ota_0", 0x120000, 0x2D0000),
    "coredump": ("data", "coredump", 0x3F0000, 0x10000),
}


def number(value: str) -> int:
  return int(value.strip(), 0)


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--partitions", type=Path, required=True)
  parser.add_argument("--firmware", type=Path)
  parser.add_argument("--recovery", type=Path)
  args = parser.parse_args()
  rows: dict[str, tuple[str, str, int, int]] = {}
  with args.partitions.open(newline="") as source:
    for row in csv.reader(line for line in source if not line.lstrip().startswith("#")):
      if not row:
        continue
      rows[row[0].strip()] = (
          row[1].strip(), row[2].strip(), number(row[3]), number(row[4]))
  if rows != EXPECTED:
    raise SystemExit(f"unexpected OTA partition layout: {rows!r}")
  details = ["Recovery layout valid"]
  if args.firmware is not None:
    size = args.firmware.stat().st_size
    if size > 0x2C0000:
      raise SystemExit(f"firmware size {size} exceeds reserved ceiling 0x2C0000")
    details.append(f"firmware {size} bytes (ceiling 0x2C0000)")
  if args.recovery is not None:
    recovery_size = args.recovery.stat().st_size
    if recovery_size > 0xF0000:
      raise SystemExit(
          f"recovery size {recovery_size} exceeds reserved ceiling 0xF0000")
    details.append(f"recovery {recovery_size} bytes (ceiling 0xF0000)")
  print("; ".join(details))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
