#!/usr/bin/env python3
"""Run a timed Sidus-group plus Zhiyun power cycle through one gateway."""

from __future__ import annotations

import argparse
import asyncio
import stat
import time
from pathlib import Path

from studio_lighter.mesh_crypto import encode_unsegmented_access_message
from studio_lighter.mesh_proxy import make_network_proxy_pdu
from studio_lighter.mesh_state import next_sequence
from studio_lighter.storage import StateStore

from decode_notifications import decode_network, decrypt_upper, k4, split_opcode, decode_status
from zhiyun_probe import FrameScanner, request, state_read_payload


PROXY_IN = "00002add-0000-1000-8000-00805f9b34fb"
PROXY_OUT = "00002ade-0000-1000-8000-00805f9b34fb"
ZH_WRITE = "d44bc439-abfd-45a2-b575-925416129600"
ZH_NOTIFY = "d44bc439-abfd-45a2-b575-925416129601"


def private_state(path: Path) -> None:
  mode = stat.S_IMODE(path.stat().st_mode)
  if mode & 0o077:
    raise RuntimeError(f"state file must be private (mode is {mode:04o})")


def camera_frame(camera: object, path: str) -> None:
  import cv2
  ok, frame = camera.read()
  if not ok or not cv2.imwrite(path, frame):
    raise RuntimeError(f"could not capture {path}")


async def run(args: argparse.Namespace) -> None:
  from bleak import BleakClient

  private_state(args.state)
  store = StateStore(args.state)
  state = store.load()
  network = state.mesh_network
  if network is None:
    raise RuntimeError("mesh network is missing")
  network_key = bytes.fromhex(network.network_key)
  app_key = bytes.fromhex(network.application_key)
  sequences = [next_sequence(state) for _ in range(4)]
  store.save(state)

  def mesh_packet(access_hex: str, sequence: int) -> bytes:
    network_pdu = encode_unsegmented_access_message(
      network_key=network_key,
      application_key=app_key,
      access_payload=bytes.fromhex(access_hex),
      sequence=sequence,
      source=network.provisioner_address,
      destination=network.group_address,
      iv_index=network.iv_index,
    )
    return make_network_proxy_pdu(network_pdu)

  group_on = mesh_packet("268d00000000000000018c", sequences[0])
  group_get_on = mesh_packet("260e00000000000000000e", sequences[1])
  group_off = mesh_packet("268c00000000000000008c", sequences[2])
  group_get_off = mesh_packet("260e00000000000000000e", sequences[3])

  proxy_notifications: list[bytes] = []
  zh_queue: asyncio.Queue[tuple[int, int, bytes]] = asyncio.Queue()
  scanner = FrameScanner()

  def proxy_notify(_: object, data: bytearray) -> None:
    proxy_notifications.append(bytes(data))

  def zh_notify(_: object, data: bytearray) -> None:
    for frame in scanner.feed(bytes(data)):
      zh_queue.put_nowait(frame)

  async def zh_command(client: BleakClient, sequence: int, command: int,
                       payload: bytes) -> bytes:
    await client.write_gatt_char(
      ZH_WRITE, request(sequence, command, payload), response=False
    )
    while True:
      reply_sequence, reply_command, reply_payload = await asyncio.wait_for(
        zh_queue.get(), timeout=args.timeout
      )
      if reply_sequence == sequence and reply_command == command:
        return reply_payload

  camera = None
  if args.camera_prefix:
    import cv2
    camera = cv2.VideoCapture(args.camera, cv2.CAP_AVFOUNDATION)
    time.sleep(1)

  try:
    async with BleakClient(args.address, timeout=args.timeout) as client:
      await client.start_notify(PROXY_OUT, proxy_notify)
      await client.start_notify(ZH_NOTIFY, zh_notify)
      initialization = (
        (2, 0x2003, b""),
        (3, 0x8001, b""),
        (4, 0x2001, b""),
        (5, 0x0006, bytes((args.selector, 0x80, 0x00, 0x00))),
        (6, 0x1002, state_read_payload(args.selector, 2)),
        (7, 0x1008, state_read_payload(args.selector, 1)),
        (8, 0x1001, state_read_payload(args.selector, 4)),
      )
      for sequence, command, payload in initialization:
        await zh_command(client, sequence, command, payload)

      started = asyncio.get_running_loop().time()
      await client.write_gatt_char(PROXY_IN, group_on, response=False)
      await zh_command(
        client, 9, 0x1008,
        bytes((args.selector, 0x80, 0x01, 0x01)),
      )
      await client.write_gatt_char(PROXY_IN, group_get_on, response=False)
      if camera is not None:
        await asyncio.sleep(max(0, started + 1 - asyncio.get_running_loop().time()))
        camera_frame(camera, f"{args.camera_prefix}-on.jpg")
      await asyncio.sleep(max(0, started + args.hold - asyncio.get_running_loop().time()))

      await client.write_gatt_char(PROXY_IN, group_off, response=False)
      await zh_command(
        client, 10, 0x1008,
        bytes((args.selector, 0x80, 0x01, 0x00)),
      )
      await client.write_gatt_char(PROXY_IN, group_get_off, response=False)
      power = await zh_command(
        client, 11, 0x1008, state_read_payload(args.selector, 1)
      )
      if power != bytes((args.selector, 0x80, 0x00, 0x00)):
        raise RuntimeError("X60RGB Off readback did not match")
      await asyncio.sleep(1)
      if camera is not None:
        camera_frame(camera, f"{args.camera_prefix}-off.jpg")
      await asyncio.sleep(1)
      await client.stop_notify(ZH_NOTIFY)
      await client.stop_notify(PROXY_OUT)
  finally:
    if camera is not None:
      camera.release()

  seen: set[tuple[int, int]] = set()
  statuses: list[str] = []
  for proxy in proxy_notifications:
    if not proxy or proxy[0] != 0:
      continue
    message = decode_network(proxy[1:], network_key, network.iv_index)
    if message.ctl or not message.lower or message.lower[0] & 0x80:
      continue
    lower_header = message.lower[0]
    if not lower_header & 0x40 or lower_header & 0x3f != k4(app_key):
      continue
    key = (message.source, message.sequence)
    if key in seen:
      continue
    seen.add(key)
    _, access = decrypt_upper(
      message.lower[1:], akf=1, aid=lower_header & 0x3f,
      sequence=message.sequence, source=message.source,
      destination=message.destination, iv_index=network.iv_index,
      application_key=app_key, device_keys={},
    )
    opcode, params = split_opcode(access)
    decoded = decode_status(opcode, params)
    if decoded and decoded.startswith("vendor_power="):
      statuses.append(f"src=0x{message.source:04x} {decoded}")
  print(f"on_window={args.hold:g}s x60_final=off")
  for status in statuses:
    print(status)


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--state", type=Path, required=True)
  parser.add_argument("--address", required=True)
  parser.add_argument("--selector", type=lambda value: int(value, 0), default=1)
  parser.add_argument("--hold", type=float, default=3.0)
  parser.add_argument("--timeout", type=float, default=3.0)
  parser.add_argument("--camera", type=int, default=0)
  parser.add_argument("--camera-prefix")
  args = parser.parse_args()
  if args.hold <= 0:
    raise ValueError("hold must be positive")
  asyncio.run(run(args))


if __name__ == "__main__":
  main()
