#!/usr/bin/env python3
"""Host tests for deterministic OTA packaging and independent verification."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


ROOT = Path(__file__).resolve().parents[1]


class FirmwareUpdateToolsTest(unittest.TestCase):
  def setUp(self) -> None:
    self.temporary = tempfile.TemporaryDirectory()
    self.root = Path(self.temporary.name)
    self.firmware = self.root / "firmware.bin"
    self.firmware.write_bytes(b"\xE9" + bytes(range(1, 128)))
    self.private_key = ec.generate_private_key(ec.SECP256R1())
    self.private_pem = self.private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption(),
    ).decode()
    self.public_key = self.root / "public.pem"
    self.public_key.write_bytes(self.private_key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    ))
    self.output = self.root / "release"
    self.recovery = self.root / "recovery.bin"
    self.recovery.write_bytes(b"\xE9recovery")
    self.bootloader = self.root / "bootloader.bin"
    self.bootloader.write_bytes(b"bootloader")
    self.partitions = self.root / "partitions.bin"
    self.partitions.write_bytes(b"partitions")

  def tearDown(self) -> None:
    self.temporary.cleanup()

  def package(self) -> None:
    environment = os.environ.copy()
    environment["TEST_OTA_KEY"] = self.private_pem
    subprocess.run([
        sys.executable, str(ROOT / "scripts/package_firmware_update.py"),
        "--firmware", str(self.firmware),
        "--recovery", str(self.recovery),
        "--output-dir", str(self.output),
        "--channel", "development",
        "--version", "0.3.0-dev",
        "--commit", "0123456789abcdef",
        "--release-sequence", "1234",
        "--release-ref", "latest",
        "--key-id", "test-key",
        "--private-key-env", "TEST_OTA_KEY",
    ], check=True, env=environment)

  def verify(self, expect_success: bool) -> None:
    result = subprocess.run([
        sys.executable, str(ROOT / "scripts/verify_firmware_update.py"),
        "--manifest", str(self.output / "bleep-update.json"),
        "--signature", str(self.output / "bleep-update.sig"),
        "--firmware", str(self.output / "bleep-update.bin"),
        "--recovery", str(self.output / "bleep-recovery.bin"),
        "--public-key", str(self.public_key),
    ], check=False, capture_output=True)
    self.assertEqual(expect_success, result.returncode == 0, result.stderr.decode())

  def test_package_is_canonical_and_verifies(self) -> None:
    self.package()
    encoded = (self.output / "bleep-update.json").read_bytes()
    self.assertTrue(encoded.endswith(b"\n"))
    document = json.loads(encoded)
    self.assertEqual("0.3.0-dev", document["version"])
    self.assertEqual(1234, document["release_sequence"])
    self.assertEqual(2, document["partition_schema"])
    self.assertEqual(1, document["recovery_schema"])
    self.assertEqual(1234, document["recovery_sequence"])
    self.assertEqual(len(self.recovery.read_bytes()),
                     document["recovery_image_size"])
    self.assertLessEqual(len(encoded), 1536)
    self.assertEqual(len(self.firmware.read_bytes()), document["image_size"])
    first = encoded
    self.package()
    self.assertEqual(first, (self.output / "bleep-update.json").read_bytes())
    self.verify(True)

  def test_tampered_firmware_is_rejected(self) -> None:
    self.package()
    with (self.output / "bleep-update.bin").open("ab") as target:
      target.write(b"tamper")
    self.verify(False)

  def test_tampered_manifest_is_rejected(self) -> None:
    self.package()
    manifest = self.output / "bleep-update.json"
    manifest.write_bytes(manifest.read_bytes().replace(b"development", b"stable"))
    self.verify(False)

  def test_tampered_recovery_is_rejected(self) -> None:
    self.package()
    with (self.output / "bleep-recovery.bin").open("ab") as target:
      target.write(b"tamper")
    self.verify(False)

  def test_wrong_public_key_is_rejected(self) -> None:
    self.package()
    wrong = ec.generate_private_key(ec.SECP256R1()).public_key()
    self.public_key.write_bytes(wrong.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    ))
    self.verify(False)

  def test_partition_geometry_and_both_size_ceilings(self) -> None:
    subprocess.run([
        sys.executable, str(ROOT / "scripts/check_ota_layout.py"),
        "--partitions", str(ROOT / "partitions/bleep_recovery.csv"),
        "--firmware", str(self.firmware),
        "--recovery", str(self.recovery),
    ], check=True)
    oversized = self.root / "oversized-recovery.bin"
    with oversized.open("wb") as target:
      target.truncate(0xF0001)
    result = subprocess.run([
        sys.executable, str(ROOT / "scripts/check_ota_layout.py"),
        "--partitions", str(ROOT / "partitions/bleep_recovery.csv"),
        "--recovery", str(oversized),
    ], check=False)
    self.assertNotEqual(0, result.returncode)

  def test_usb_bundle_is_deterministic_and_excludes_nvs(self) -> None:
    bundle = self.root / "migration.zip"
    command = [
        sys.executable, str(ROOT / "scripts/package_usb_migration.py"),
        "--firmware", str(self.firmware),
        "--recovery", str(self.recovery),
        "--bootloader", str(self.bootloader),
        "--partitions", str(self.partitions),
        "--output", str(bundle),
    ]
    subprocess.run(command, check=True)
    first = bundle.read_bytes()
    subprocess.run(command, check=True)
    self.assertEqual(first, bundle.read_bytes())
    with zipfile.ZipFile(bundle) as archive:
      manifest = json.loads(archive.read("bleep-usb-migration/manifest.json"))
      self.assertNotIn("0x9000", manifest["writes"])
      self.assertEqual("recovery.bin", manifest["writes"]["0x10000"])
      self.assertEqual("firmware.bin", manifest["writes"]["0x120000"])
      journal = archive.read("bleep-usb-migration/recovery-journal.bin")
      self.assertEqual(0x10000, len(journal))
      self.assertEqual({0xFF}, set(journal))


if __name__ == "__main__":
  unittest.main()
