#!/usr/bin/env python3
"""Build the NVS-preserving recovery/main USB migration bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import stat
import zipfile
from pathlib import Path


def digest(path: Path) -> str:
  return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--firmware", type=Path, required=True)
  parser.add_argument("--recovery", type=Path, required=True)
  parser.add_argument("--bootloader", type=Path, required=True)
  parser.add_argument("--partitions", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  args = parser.parse_args()

  staging = args.output.parent / f".{args.output.stem}-staging"
  if staging.exists():
    shutil.rmtree(staging)
  staging.mkdir(parents=True)
  files = {
      "bootloader.bin": args.bootloader,
      "partitions.bin": args.partitions,
      "firmware.bin": args.firmware,
      "recovery.bin": args.recovery,
  }
  for name, source in files.items():
    shutil.copyfile(source, staging / name)
  (staging / "otadata-initial.bin").write_bytes(b"\xff" * 0x2000)
  (staging / "recovery-journal.bin").write_bytes(b"\xff" * 0x10000)
  flash = staging / "migrate.sh"
  flash.write_text("""#!/usr/bin/env sh
set -eu
if [ "$#" -ne 1 ]; then
  echo "usage: $0 /dev/cu.usbserial-..." >&2
  exit 2
fi
cd "$(dirname "$0")"
python3 -m esptool --chip esp32c3 --port "$1" write_flash \\
  0x0000 bootloader.bin \\
  0x8000 partitions.bin \\
  0xE000 otadata-initial.bin \\
  0x10000 recovery.bin \\
  0x110000 recovery-journal.bin \\
  0x120000 firmware.bin
""")
  flash.chmod(flash.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
  manifest = {
      "format": 1,
      "preserves": {"nvs_offset": "0x9000", "nvs_size": "0x5000"},
      "writes": {
          "0x0000": "bootloader.bin",
          "0x8000": "partitions.bin",
          "0xE000": "otadata-initial.bin",
          "0x10000": "recovery.bin",
          "0x110000": "recovery-journal.bin",
          "0x120000": "firmware.bin",
      },
      "sha256": {path.name: digest(path) for path in sorted(staging.iterdir())
                  if path.is_file() and path.name != "manifest.json"},
  }
  (staging / "manifest.json").write_text(
      json.dumps(manifest, indent=2, sort_keys=True) + "\n")
  args.output.parent.mkdir(parents=True, exist_ok=True)
  with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(staging.iterdir()):
      info = zipfile.ZipInfo(f"bleep-usb-migration/{path.name}")
      info.date_time = (2020, 1, 1, 0, 0, 0)
      info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
      archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
  shutil.rmtree(staging)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
