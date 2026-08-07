#!/usr/bin/env python3
"""Decrypt Mesh Proxy notifications using a private mesh-lab state file."""

from __future__ import annotations

import argparse
import json
import stat
from dataclasses import dataclass
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.cmac import CMAC


def cmac(key: bytes, message: bytes) -> bytes:
  value = CMAC(algorithms.AES(key))
  value.update(message)
  return value.finalize()


def s1(message: bytes) -> bytes:
  return cmac(bytes(16), message)


def k2(network_key: bytes) -> tuple[int, bytes, bytes]:
  salt = s1(b"smk2")
  temporary = cmac(salt, network_key)
  t1 = cmac(temporary, b"\x00\x01")
  t2 = cmac(temporary, t1 + b"\x00\x02")
  t3 = cmac(temporary, t2 + b"\x00\x03")
  material = (int.from_bytes(t1 + t2 + t3, "big") & ((1 << 263) - 1)).to_bytes(33, "big")
  return material[0] & 0x7F, material[1:17], material[17:33]


def k4(application_key: bytes) -> int:
  salt = s1(b"smk4")
  temporary = cmac(salt, application_key)
  return cmac(temporary, b"id6\x01")[-1] & 0x3F


def ecb(key: bytes, message: bytes) -> bytes:
  encryptor = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
  return encryptor.update(message) + encryptor.finalize()


@dataclass(frozen=True)
class NetworkMessage:
  ctl: int
  ttl: int
  sequence: int
  source: int
  destination: int
  lower: bytes


def decode_network(pdu: bytes, network_key: bytes, iv_index: int) -> NetworkMessage:
  nid, encryption_key, privacy_key = k2(network_key)
  if len(pdu) < 14 or (pdu[0] & 0x7F) != nid:
    raise ValueError("network NID or length mismatch")
  encrypted = pdu[7:]
  pecb = ecb(privacy_key, bytes(5) + iv_index.to_bytes(4, "big") + encrypted[:7])
  clear_header = bytes(left ^ right for left, right in zip(pdu[1:7], pecb[:6]))
  ctl_ttl = clear_header[0]
  ctl = ctl_ttl >> 7
  ttl = ctl_ttl & 0x7F
  sequence = int.from_bytes(clear_header[1:4], "big")
  source = int.from_bytes(clear_header[4:6], "big")
  nonce = b"\x00" + clear_header + b"\x00\x00" + iv_index.to_bytes(4, "big")
  clear = AESCCM(encryption_key, tag_length=8 if ctl else 4).decrypt(nonce, encrypted, None)
  return NetworkMessage(ctl, ttl, sequence, source, int.from_bytes(clear[:2], "big"), clear[2:])


def decrypt_upper(
  encrypted: bytes,
  *,
  akf: int,
  aid: int,
  sequence: int,
  source: int,
  destination: int,
  iv_index: int,
  application_key: bytes,
  device_keys: dict[int, bytes],
  szmic: int = 0,
) -> tuple[str, bytes]:
  if akf:
    expected_aid = k4(application_key)
    if aid != expected_aid:
      raise ValueError(f"unknown AID 0x{aid:02x}, expected 0x{expected_aid:02x}")
    key_name = "app"
    key = application_key
    nonce_type = 0x01
  else:
    node_address = source if source in device_keys else destination
    if node_address not in device_keys:
      raise ValueError(f"no device key for 0x{node_address:04x}")
    key_name = "device"
    key = device_keys[node_address]
    nonce_type = 0x02
  nonce = (
    bytes([nonce_type, szmic << 7])
    + sequence.to_bytes(3, "big")
    + source.to_bytes(2, "big")
    + destination.to_bytes(2, "big")
    + iv_index.to_bytes(4, "big")
  )
  return key_name, AESCCM(key, tag_length=8 if szmic else 4).decrypt(nonce, encrypted, None)


def split_opcode(payload: bytes) -> tuple[str, bytes]:
  if not payload:
    return "<empty>", b""
  first = payload[0]
  if first & 0x80 == 0:
    return f"0x{first:02x}", payload[1:]
  if first & 0xC0 == 0x80 and len(payload) >= 2:
    return f"0x{first:02x}{payload[1]:02x}", payload[2:]
  if len(payload) >= 3:
    company = int.from_bytes(payload[1:3], "little")
    return f"vendor(op=0x{first:02x},company=0x{company:04x})", payload[3:]
  return "<truncated opcode>", payload[1:]


def transition_seconds(encoded: int) -> str:
  steps = encoded & 0x3F
  if steps == 0x3F:
    return "unknown"
  seconds_per_step = (0.1, 1.0, 10.0, 600.0)[encoded >> 6]
  return f"{steps * seconds_per_step:g}s"


def decode_status(opcode: str, params: bytes) -> str | None:
  if (
    opcode == "0x26"
    and len(params) == 10
    and sum(params[1:]) & 0xFF == params[0]
    and params[2:5] == b"\x00\x00\x00"
    and params[8] <= 250
    and params[9] in (1, 2)
  ):
    profiles = {1: "ace-25c", 2: "mc-pro"}
    return (
      f"vendor_power={'on' if params[1] else 'off'} "
      f"stored_intensity_raw={params[8]} profile={profiles[params[9]]} "
      "checksum=ok"
    )
  if opcode == "0x8204" and len(params) in (1, 3):
    result = f"present={'on' if params[0] else 'off'}"
    if len(params) == 3:
      result += (
        f" target={'on' if params[1] else 'off'}"
        f" remaining={transition_seconds(params[2])}"
      )
    return result
  if opcode == "0x8208" and len(params) in (2, 5):
    present = int.from_bytes(params[:2], "little", signed=True)
    result = f"present_level={present}"
    if len(params) == 5:
      target = int.from_bytes(params[2:4], "little", signed=True)
      result += f" target_level={target} remaining={transition_seconds(params[4])}"
    return result
  if opcode == "0x8210" and len(params) == 1:
    return f"default_transition={transition_seconds(params[0])}"
  if opcode == "0x8212" and len(params) == 1:
    labels = {0: "off", 1: "on", 2: "restore"}
    return f"on_power_up={labels.get(params[0], f'unknown-{params[0]}')}"
  if opcode == "0x824e" and len(params) in (2, 5):
    present = int.from_bytes(params[:2], "little")
    result = f"present_lightness={present}"
    if len(params) == 5:
      target = int.from_bytes(params[2:4], "little")
      result += f" target_lightness={target} remaining={transition_seconds(params[4])}"
    return result
  if opcode in ("0x8003", "0x801f", "0x803e") and params:
    labels = {
      0x00: "success",
      0x01: "invalid-address",
      0x02: "invalid-model",
      0x03: "invalid-appkey-index",
    }
    return f"config_status={labels.get(params[0], f'0x{params[0]:02x}')}"
  return None


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--state", type=Path, required=True)
  parser.add_argument("pdus", nargs="+")
  return parser.parse_args()


def main() -> None:
  args = parse_args()
  mode = stat.S_IMODE(args.state.stat().st_mode)
  if mode & 0o077:
    raise RuntimeError(f"state file must not be group/world accessible (mode is {mode:04o})")
  state = json.loads(args.state.read_text(encoding="utf-8"))
  network = state["mesh_network"]
  network_key = bytes.fromhex(network["network_key"])
  application_key = bytes.fromhex(network["application_key"])
  iv_index = int(network.get("iv_index", 0))
  device_keys = {
    int(node["unicast_address"]): bytes.fromhex(node["device_key"])
    for node in state.get("mesh_nodes", {}).values()
  }
  segments: dict[tuple[int, int, int, int, int, int], dict[int, bytes]] = {}
  segment_meta: dict[tuple[int, int, int, int, int, int], tuple[int, int]] = {}

  for index, raw_hex in enumerate(args.pdus, 1):
    proxy = bytes.fromhex(raw_hex)
    if not proxy or proxy[0] != 0x00:
      proxy_type = proxy[0] if proxy else -1
      print(f"{index}: proxy-type={proxy_type:#04x} raw={proxy.hex()}")
      continue
    message = decode_network(proxy[1:], network_key, iv_index)
    if message.ctl:
      opcode_value = message.lower[0] & 0x7F if message.lower else -1
      print(
        f"{index}: ctl src=0x{message.source:04x} dst=0x{message.destination:04x} "
        f"seq={message.sequence} opcode=0x{opcode_value:02x} params={message.lower[1:].hex()}"
      )
      continue
    first = message.lower[0]
    segmented = first >> 7
    akf = (first >> 6) & 1
    aid = first & 0x3F
    if not segmented:
      key_name, access = decrypt_upper(
        message.lower[1:],
        akf=akf,
        aid=aid,
        sequence=message.sequence,
        source=message.source,
        destination=message.destination,
        iv_index=iv_index,
        application_key=application_key,
        device_keys=device_keys,
      )
      opcode, params = split_opcode(access)
      decoded = decode_status(opcode, params)
      suffix = f" decoded=\"{decoded}\"" if decoded else ""
      print(
        f"{index}: access key={key_name} src=0x{message.source:04x} "
        f"dst=0x{message.destination:04x} seq={message.sequence} "
        f"opcode={opcode} params={params.hex()}{suffix}"
      )
      continue

    if len(message.lower) < 4:
      raise ValueError("truncated segmented lower transport PDU")
    szmic = message.lower[1] >> 7
    seq_zero = ((message.lower[1] & 0x7F) << 6) | (message.lower[2] >> 2)
    seg_o = ((message.lower[2] & 0x03) << 3) | (message.lower[3] >> 5)
    seg_n = message.lower[3] & 0x1F
    sequence_auth = (message.sequence & 0xFFE000) | seq_zero
    if sequence_auth > message.sequence:
      sequence_auth -= 0x2000
    key = (message.source, message.destination, seq_zero, akf, aid, szmic)
    segments.setdefault(key, {})[seg_o] = message.lower[4:]
    segment_meta[key] = (seg_n, sequence_auth)
    print(
      f"{index}: segment src=0x{message.source:04x} dst=0x{message.destination:04x} "
      f"seq={message.sequence} part={seg_o}/{seg_n} bytes={message.lower[4:].hex()}"
    )
    parts = segments[key]
    if len(parts) == seg_n + 1 and all(part in parts for part in range(seg_n + 1)):
      encrypted = b"".join(parts[part] for part in range(seg_n + 1))
      key_name, access = decrypt_upper(
        encrypted,
        akf=akf,
        aid=aid,
        sequence=sequence_auth,
        source=message.source,
        destination=message.destination,
        iv_index=iv_index,
        application_key=application_key,
        device_keys=device_keys,
        szmic=szmic,
      )
      opcode, params = split_opcode(access)
      decoded = decode_status(opcode, params)
      suffix = f" decoded=\"{decoded}\"" if decoded else ""
      print(
        f"  reassembled key={key_name} src=0x{message.source:04x} "
        f"dst=0x{message.destination:04x} seqauth={sequence_auth} "
        f"opcode={opcode} params={params.hex()}{suffix}"
      )


if __name__ == "__main__":
  main()
