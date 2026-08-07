#!/usr/bin/env python3
"""Send focused configuration/state probes to a provisioned mesh node.

The state file contains secrets and must remain outside the repository. This
tool reserves and persists sequence numbers before attempting a radio write so
a failed connection cannot cause nonce reuse.
"""

from __future__ import annotations

import argparse
import asyncio
import stat
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESCCM

try:
  from studio_lighter.amaran_replay import ReplayPacket
  from studio_lighter.mesh_config import (
    build_appkey_add,
    build_composition_data_get,
    build_model_app_bind,
    build_model_subscription_add,
  )
  from studio_lighter.mesh_crypto import (
    aes_ecb_encrypt,
    encode_unsegmented_access_message,
    encode_unsegmented_device_message,
    k2,
  )
  from studio_lighter.mesh_proxy import send_packets_via_proxy
  from studio_lighter.mesh_state import next_sequence
  from studio_lighter.storage import StateStore
except ImportError as error:
  raise SystemExit(
    "studio_lighter is required; add its src directory to PYTHONPATH as shown "
    "in tools/mesh_lab/README.md"
  ) from error


def encode_segmented_device_messages(
  *,
  network_key: bytes,
  device_key: bytes,
  access_payload: bytes,
  first_sequence: int,
  source: int,
  destination: int,
  iv_index: int,
) -> list[bytes]:
  """Encode a device-key access message using 12-byte transport segments."""
  sequence_bytes = first_sequence.to_bytes(3, "big")
  source_bytes = source.to_bytes(2, "big")
  destination_bytes = destination.to_bytes(2, "big")
  iv_index_bytes = iv_index.to_bytes(4, "big")
  nonce = b"\x02\x00" + sequence_bytes + source_bytes + destination_bytes + iv_index_bytes
  encrypted = AESCCM(device_key, tag_length=4).encrypt(nonce, access_payload, None)
  segments = [encrypted[offset:offset + 12] for offset in range(0, len(encrypted), 12)]
  seq_zero = first_sequence & 0x1FFF
  seg_n = len(segments) - 1
  return [
    encode_network_pdu(
      network_key=network_key,
      lower_transport=(
        bytes(
          [
            0x80,
            (seq_zero >> 6) & 0x7F,
            ((seq_zero & 0x3F) << 2) | ((seg_o >> 3) & 0x03),
            ((seg_o & 0x07) << 5) | seg_n,
          ]
        )
        + segment
      ),
      sequence=first_sequence + seg_o,
      source=source,
      destination=destination,
      iv_index=iv_index,
    )
    for seg_o, segment in enumerate(segments)
  ]


def encode_network_pdu(
  *,
  network_key: bytes,
  lower_transport: bytes,
  sequence: int,
  source: int,
  destination: int,
  iv_index: int,
  ttl: int = 6,
) -> bytes:
  keys = k2(network_key)
  sequence_bytes = sequence.to_bytes(3, "big")
  source_bytes = source.to_bytes(2, "big")
  destination_bytes = destination.to_bytes(2, "big")
  iv_index_bytes = iv_index.to_bytes(4, "big")
  clear_header = bytes([ttl]) + sequence_bytes + source_bytes
  network_nonce = b"\x00" + clear_header + b"\x00\x00" + iv_index_bytes
  encrypted = AESCCM(keys.encryption_key, tag_length=4).encrypt(
    network_nonce,
    destination_bytes + lower_transport,
    None,
  )
  pecb = aes_ecb_encrypt(keys.privacy_key, bytes(5) + iv_index_bytes + encrypted[:7])
  obfuscated = bytes(left ^ right for left, right in zip(clear_header, pecb[:6]))
  return bytes([((iv_index >> 31) << 7) | keys.nid]) + obfuscated + encrypted


def check_private_state_file(path: Path) -> None:
  mode = stat.S_IMODE(path.stat().st_mode)
  if mode & 0o077:
    raise RuntimeError(f"state file must not be group/world accessible (mode is {mode:04o})")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--state", type=Path, required=True)
  parser.add_argument("--address", required=True)
  parser.add_argument(
    "--proxy-address",
    help="BLE identifier of the proxy gateway; defaults to the target light",
  )
  parser.add_argument("--other-address", help="second logical node for soak")
  parser.add_argument(
    "--group-address",
    type=lambda value: int(value, 0),
    help="override the mesh group destination/subscription address",
  )
  parser.add_argument("--listen", type=float, default=4.0)
  parser.add_argument("--count", type=int, default=10, help="packet count for soak")
  parser.add_argument("--interval", type=float, default=1.0, help="seconds between soak packets")
  parser.add_argument(
    "--lightness",
    type=lambda value: int(value, 0),
    help="Light Lightness Actual value for lightness-set (0..65535)",
  )
  parser.add_argument("--access", help="raw access payload hex for raw-unicast/raw-group")
  parser.add_argument("--company-id", type=lambda value: int(value, 0), default=0x03F6)
  parser.add_argument("--model-id", type=lambda value: int(value, 0), default=0x1000)
  parser.add_argument(
    "operation",
    choices=(
      "composition-get",
      "appkey-add",
      "vendor-bind",
      "sig-bind",
      "sig-subscribe",
      "vendor-subscribe",
      "onoff-get",
      "group-onoff-get",
      "group-onoff-on",
      "group-onoff-off",
      "vendor-power-get",
      "vendor-power-on",
      "vendor-power-off",
      "vendor-power-soak",
      "listen",
      "raw-unicast",
      "raw-group",
      "onoff-on",
      "onoff-off",
      "level-get",
      "onpowerup-get",
      "transition-get",
      "lightness-get",
      "lightness-set",
      "soak",
    ),
  )
  return parser.parse_args()


def main() -> None:
  args = parse_args()
  check_private_state_file(args.state)
  store = StateStore(args.state)
  state = store.load()
  network = state.mesh_network
  light = state.lights[args.address]
  node = state.mesh_nodes[args.address]
  if network is None or light.mesh_address is None:
    raise RuntimeError("missing mesh state")

  network_key = bytes.fromhex(network.network_key)
  application_key = bytes.fromhex(network.application_key)
  device_key = bytes.fromhex(node.device_key)
  source = network.provisioner_address
  destination = light.mesh_address
  group_address = args.group_address or network.group_address

  if args.operation == "listen":
    packets = []
  elif args.operation in ("soak", "vendor-power-soak"):
    if args.operation == "soak" and not args.other_address:
      raise RuntimeError("soak requires --other-address")
    if args.count < 1:
      raise RuntimeError(f"{args.operation} --count must be positive")
    target_addresses = (args.address, args.other_address)
    packets = []
    for index in range(args.count):
      sequence = next_sequence(state)
      if args.operation == "vendor-power-soak":
        access = bytes.fromhex("260e00000000000000000e")
        target_mesh_address = group_address
        packet_name = "vendor-power-get-group"
      else:
        target_address = target_addresses[index % len(target_addresses)]
        target = state.lights[target_address]
        if target.mesh_address is None:
          raise RuntimeError(f"missing mesh address for {target_address}")
        access = bytes.fromhex("8201")
        target_mesh_address = target.mesh_address
        packet_name = f"onoff-get-node-0x{target.mesh_address:04x}"
      pdu = encode_unsegmented_access_message(
        network_key=network_key,
        application_key=application_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=target_mesh_address,
        iv_index=network.iv_index,
      )
      packets.append(
        ReplayPacket(
          packet_name,
          sequence,
          access,
          pdu,
        )
      )
  elif args.operation == "appkey-add":
    access = build_appkey_add(netkey_index=0, appkey_index=0, application_key=application_key)
    first_sequence = next_sequence(state)
    pdus = encode_segmented_device_messages(
      network_key=network_key,
      device_key=device_key,
      access_payload=access,
      first_sequence=first_sequence,
      source=source,
      destination=destination,
      iv_index=network.iv_index,
    )
    for _ in pdus[1:]:
      next_sequence(state)
    packets = [
      ReplayPacket(f"appkey-add-segment-{index}", first_sequence + index, access, pdu)
      for index, pdu in enumerate(pdus)
    ]
  else:
    sequence = next_sequence(state)
    if args.operation == "composition-get":
      access = build_composition_data_get()
      pdu = encode_unsegmented_device_message(
        network_key=network_key,
        device_key=device_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=destination,
        iv_index=network.iv_index,
      )
    elif args.operation == "vendor-bind":
      access = build_model_app_bind(
        element_address=destination,
        appkey_index=0,
        company_id=args.company_id,
        model_id=args.model_id,
      )
      pdu = encode_unsegmented_device_message(
        network_key=network_key,
        device_key=device_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=destination,
        iv_index=network.iv_index,
      )
    elif args.operation == "sig-bind":
      access = build_model_app_bind(
        element_address=destination,
        appkey_index=0,
        model_id=0x1000,
      )
      pdu = encode_unsegmented_device_message(
        network_key=network_key,
        device_key=device_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=destination,
        iv_index=network.iv_index,
      )
    elif args.operation == "sig-subscribe":
      access = build_model_subscription_add(
        element_address=destination,
        subscription_address=group_address,
        model_id=0x1000,
      )
      pdu = encode_unsegmented_device_message(
        network_key=network_key,
        device_key=device_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=destination,
        iv_index=network.iv_index,
      )
    elif args.operation == "vendor-subscribe":
      access = build_model_subscription_add(
        element_address=destination,
        subscription_address=group_address,
        company_id=args.company_id,
        model_id=args.model_id,
      )
      pdu = encode_unsegmented_device_message(
        network_key=network_key,
        device_key=device_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=destination,
        iv_index=network.iv_index,
      )
    else:
      if args.operation in ("raw-unicast", "raw-group"):
        if not args.access:
          raise RuntimeError(f"{args.operation} requires --access")
        access = bytes.fromhex(args.access)
        if not access or len(access) > 15:
          raise RuntimeError("raw access payload must contain 1..15 bytes")
      elif args.operation == "lightness-set":
        if args.lightness is None or not 0 <= args.lightness <= 0xFFFF:
          raise RuntimeError("lightness-set requires --lightness from 0 to 65535")
        access = bytes.fromhex("824c") + args.lightness.to_bytes(2, "little") + bytes([sequence & 0xFF])
      else:
        application_messages = {
          "onoff-get": bytes.fromhex("8201"),
          "group-onoff-get": bytes.fromhex("8201"),
          "onoff-on": bytes.fromhex("820201") + bytes([sequence & 0xFF]),
          "onoff-off": bytes.fromhex("820200") + bytes([sequence & 0xFF]),
          "group-onoff-on": bytes.fromhex("820201") + bytes([sequence & 0xFF]),
          "group-onoff-off": bytes.fromhex("820200") + bytes([sequence & 0xFF]),
          "vendor-power-get": bytes.fromhex("260e00000000000000000e"),
          "vendor-power-on": bytes.fromhex("268d00000000000000018c"),
          "vendor-power-off": bytes.fromhex("268c00000000000000008c"),
          "level-get": bytes.fromhex("8205"),
          "transition-get": bytes.fromhex("820d"),
          "onpowerup-get": bytes.fromhex("8211"),
          "lightness-get": bytes.fromhex("824b"),
        }
        access = application_messages[args.operation]
      access_destination = group_address if (
        args.operation.startswith("group-")
        or args.operation.startswith("vendor-power-")
        or args.operation == "raw-group"
      ) else destination
      pdu = encode_unsegmented_access_message(
        network_key=network_key,
        application_key=application_key,
        access_payload=access,
        sequence=sequence,
        source=source,
        destination=access_destination,
        iv_index=network.iv_index,
      )
    packets = [ReplayPacket(args.operation, sequence, access, pdu)]

  # Persist first: sequence reuse is unsafe even if the BLE operation fails.
  store.save(state)
  sent, notifications = asyncio.run(
    send_packets_via_proxy(
      packets,
      address=args.proxy_address or args.address,
      delay_seconds=(
        args.interval
        if args.operation in ("soak", "vendor-power-soak")
        else 0.35
      ),
      listen_seconds=args.listen,
    )
  )
  for packet in sent:
    print(f"sent {packet.name} seq={packet.sequence} proxy={packet.proxy_pdu.hex()}")
  for index, notification in enumerate(notifications, 1):
    print(f"notification {index}: {notification.hex()}")


if __name__ == "__main__":
  main()
