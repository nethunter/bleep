#!/usr/bin/env python3
"""Create a deterministic, signed Ble(e)p update release asset set."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


MAX_IMAGE_SIZE = 0x2C0000
MAX_RECOVERY_IMAGE_SIZE = 0xF0000
REPOSITORY = "nethunter/bleep"


def canonical_bytes(manifest: dict[str, object]) -> bytes:
  return (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--firmware", type=Path, required=True)
  parser.add_argument("--recovery", type=Path, required=True)
  parser.add_argument("--output-dir", type=Path, required=True)
  parser.add_argument("--channel", choices=("stable", "development"), required=True)
  parser.add_argument("--version", required=True)
  parser.add_argument("--commit", required=True)
  parser.add_argument("--release-sequence", type=int, required=True)
  parser.add_argument("--release-ref", required=True)
  parser.add_argument("--key-id", required=True)
  parser.add_argument("--private-key-env", default="BLEEP_OTA_PRIVATE_KEY")
  args = parser.parse_args()

  image = args.firmware.read_bytes()
  recovery = args.recovery.read_bytes()
  if not image or len(image) > MAX_IMAGE_SIZE:
    raise SystemExit(f"firmware size {len(image)} exceeds {MAX_IMAGE_SIZE}")
  if image[0] != 0xE9:
    raise SystemExit("firmware does not have an ESP application image header")
  if not recovery or len(recovery) > MAX_RECOVERY_IMAGE_SIZE:
    raise SystemExit(
        f"recovery size {len(recovery)} exceeds {MAX_RECOVERY_IMAGE_SIZE}")
  if recovery[0] != 0xE9:
    raise SystemExit("recovery does not have an ESP application image header")
  private_pem = os.environ.get(args.private_key_env, "").encode()
  if not private_pem:
    raise SystemExit(f"{args.private_key_env} is not configured")
  private_key = serialization.load_pem_private_key(private_pem, password=None)
  if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(
      private_key.curve, ec.SECP256R1):
    raise SystemExit("signing key must be ECDSA P-256")

  args.output_dir.mkdir(parents=True, exist_ok=True)
  payload_name = "bleep-update.bin"
  recovery_payload_name = "bleep-recovery.bin"
  manifest = {
      "channel": args.channel,
      "commit": args.commit,
      "hardware": "crowpanel-1.28",
      "image_size": len(image),
      "key_id": args.key_id,
      "partition_schema": 2,
      "payload_url": (
          f"https://github.com/{REPOSITORY}/releases/download/"
          f"{args.release_ref}/{payload_name}"
      ),
      "profile": "bleep",
      "release_sequence": args.release_sequence,
      "recovery_schema": 1,
      "recovery_image_size": len(recovery),
      "recovery_payload_url": (
          f"https://github.com/{REPOSITORY}/releases/download/"
          f"{args.release_ref}/{recovery_payload_name}"
      ),
      "recovery_sequence": args.release_sequence,
      "recovery_sha256": hashlib.sha256(recovery).hexdigest(),
      "schema": 1,
      "sha256": hashlib.sha256(image).hexdigest(),
      "version": args.version,
  }
  encoded = canonical_bytes(manifest)
  signature = private_key.sign(encoded, ec.ECDSA(hashes.SHA256()))
  (args.output_dir / "bleep-update.json").write_bytes(encoded)
  (args.output_dir / "bleep-update.sig").write_bytes(signature)
  shutil.copyfile(args.firmware, args.output_dir / payload_name)
  shutil.copyfile(args.recovery, args.output_dir / recovery_payload_name)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
