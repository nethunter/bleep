#!/usr/bin/env python3
"""Independently verify a Ble(e)p signed update asset set."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--manifest", type=Path, required=True)
  parser.add_argument("--signature", type=Path, required=True)
  parser.add_argument("--firmware", type=Path, required=True)
  parser.add_argument("--recovery", type=Path, required=True)
  parser.add_argument("--public-key", type=Path, required=True)
  args = parser.parse_args()

  encoded = args.manifest.read_bytes()
  signature = args.signature.read_bytes()
  firmware = args.firmware.read_bytes()
  recovery = args.recovery.read_bytes()
  public_key = serialization.load_pem_public_key(args.public_key.read_bytes())
  if not isinstance(public_key, ec.EllipticCurvePublicKey) or not isinstance(
      public_key.curve, ec.SECP256R1):
    raise SystemExit("verification key must be ECDSA P-256")
  try:
    public_key.verify(signature, encoded, ec.ECDSA(hashes.SHA256()))
  except InvalidSignature as error:
    raise SystemExit("invalid manifest signature") from error
  manifest = json.loads(encoded)
  if manifest["image_size"] != len(firmware):
    raise SystemExit("firmware length does not match manifest")
  if manifest["sha256"] != hashlib.sha256(firmware).hexdigest():
    raise SystemExit("firmware hash does not match manifest")
  if manifest["recovery_image_size"] != len(recovery):
    raise SystemExit("recovery length does not match manifest")
  if manifest["recovery_sha256"] != hashlib.sha256(recovery).hexdigest():
    raise SystemExit("recovery hash does not match manifest")
  if manifest["recovery_sequence"] <= 0:
    raise SystemExit("invalid recovery sequence")
  if (manifest["schema"] != 1 or manifest["partition_schema"] != 2 or
      manifest["recovery_schema"] != 1):
    raise SystemExit("unsupported manifest schema")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
