#!/usr/bin/env python3
"""Read identity and light state from a provisioned Zhiyun gateway."""

from __future__ import annotations

import argparse
import asyncio
import struct


CONTROL_SERVICE = "0000fee9-0000-1000-8000-00805f9b34fb"
WRITE_CHARACTERISTIC = "d44bc439-abfd-45a2-b575-925416129600"
NOTIFY_CHARACTERISTIC = "d44bc439-abfd-45a2-b575-925416129601"


def crc16_xmodem(data: bytes) -> int:
  crc = 0
  for value in data:
    crc ^= value << 8
    for _ in range(8):
      crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
  return crc


def request(sequence: int, command: int, payload: bytes = b"") -> bytes:
  body = b"\x00\x01" + struct.pack("<HH", sequence, command) + payload
  checksum = struct.pack("<H", crc16_xmodem(body))
  return b"\x24\x3c" + struct.pack("<H", len(body)) + body + checksum


class FrameScanner:
  def __init__(self) -> None:
    self.buffer = bytearray()

  def feed(self, data: bytes) -> list[tuple[int, int, bytes]]:
    self.buffer.extend(data)
    frames: list[tuple[int, int, bytes]] = []
    while True:
      marker = self.buffer.find(b"\x24\x3c")
      if marker < 0:
        self.buffer.clear()
        return frames
      del self.buffer[:marker]
      if len(self.buffer) < 4:
        return frames
      body_length = int.from_bytes(self.buffer[2:4], "little")
      total = body_length + 6
      if total < 12 or total > 80:
        del self.buffer[0]
        continue
      if len(self.buffer) < total:
        return frames
      frame = bytes(self.buffer[:total])
      del self.buffer[:total]
      body = frame[4:-2]
      if crc16_xmodem(body) != int.from_bytes(frame[-2:], "little"):
        continue
      if body[:2] != b"\x01\x00":
        continue
      frames.append((int.from_bytes(body[2:4], "little"),
                     int.from_bytes(body[4:6], "little"), body[6:]))


def state_read_payload(selector: int, value_length: int) -> bytes:
  return bytes((selector, 0x80, 0x00)) + bytes(value_length)


def describe(command: int, payload: bytes, selector: int) -> str:
  if command == 0x2003:
    lowered = payload.lower()
    if b"plx104" in lowered:
      return "identity=x60rgb"
    if b"pl105" in lowered:
      return "identity=x100"
    return "identity=unknown"
  if len(payload) < 3 or payload[:2] != bytes((selector, 0x80)) or payload[2] > 1:
    return f"payload=unexpected length={len(payload)}"
  value = payload[3:]
  if command == 0x1001 and len(value) == 4:
    return f"brightness={struct.unpack('<f', value)[0]:g}%"
  if command == 0x1004 and len(value) == 4:
    return f"hue={struct.unpack('<f', value)[0]:g}deg"
  if command == 0x1005 and len(value) == 4:
    return f"saturation={struct.unpack('<f', value)[0]:g}%"
  if command == 0x1002 and len(value) == 2:
    return f"cct={int.from_bytes(value, 'little')}K"
  if command == 0x1008 and len(value) == 1 and value[0] <= 1:
    return f"power={'on' if value[0] else 'off'}"
  return f"reply=ok length={len(payload)}"


async def run(address: str, selector: int, timeout: float,
              power: str | None, hue: float | None,
              saturation: float | None, brightness: float | None) -> None:
  try:
    from bleak import BleakClient
  except ImportError as error:
    raise RuntimeError("Bleak is required; install it in the workspace venv") from error

  queue: asyncio.Queue[tuple[int, int, bytes]] = asyncio.Queue()
  scanner = FrameScanner()

  def notification(_: object, data: bytearray) -> None:
    for frame in scanner.feed(bytes(data)):
      queue.put_nowait(frame)

  commands = [
    (2, 0x2003, b""),
    (3, 0x8001, b""),
    (4, 0x2001, b""),
    (5, 0x0006, bytes((selector, 0x80, 0x00, 0x00))),
    (6, 0x1002, state_read_payload(selector, 2)),
    (7, 0x1008, state_read_payload(selector, 1)),
    (8, 0x1001, state_read_payload(selector, 4)),
  ]
  sequence = 9
  for command, value in (
    (0x1004, hue),
    (0x1005, saturation),
    (0x1001, brightness),
  ):
    if value is None:
      continue
    commands.append(
      (sequence, command, bytes((selector, 0x80, 0x01)) + struct.pack("<f", value))
    )
    sequence += 1
    commands.append((sequence, command, state_read_payload(selector, 4)))
    sequence += 1
  if power is not None:
    commands.append(
      (sequence, 0x1008, bytes((selector, 0x80, 0x01, power == "on")))
    )
    sequence += 1
    commands.append((sequence, 0x1008, state_read_payload(selector, 1)))
  async with BleakClient(address, timeout=timeout) as client:
    if client.services.get_service(CONTROL_SERVICE) is None:
      raise RuntimeError("Zhiyun 0xFEE9 control service is unavailable")
    await client.start_notify(NOTIFY_CHARACTERISTIC, notification)
    try:
      for sequence, command, payload in commands:
        await client.write_gatt_char(
          WRITE_CHARACTERISTIC, request(sequence, command, payload),
          response=False,
        )
        while True:
          reply_sequence, reply_command, reply_payload = await asyncio.wait_for(
            queue.get(), timeout=timeout
          )
          if reply_sequence == sequence and reply_command == command:
            print(
              f"seq={sequence} command=0x{command:04x} "
              f"{describe(command, reply_payload, selector)}"
            )
            break
    finally:
      await client.stop_notify(NOTIFY_CHARACTERISTIC)


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--address", required=True)
  parser.add_argument("--selector", type=lambda value: int(value, 0), default=1)
  parser.add_argument("--timeout", type=float, default=3.0)
  parser.add_argument("--power", choices=("on", "off"))
  parser.add_argument("--hue", type=float)
  parser.add_argument("--saturation", type=float)
  parser.add_argument("--brightness", type=float)
  args = parser.parse_args()
  if not 0 <= args.selector <= 255:
    raise ValueError("selector must fit in one byte")
  if args.hue is not None and not 0 <= args.hue <= 360:
    raise ValueError("hue must be between 0 and 360")
  for name in ("saturation", "brightness"):
    value = getattr(args, name)
    if value is not None and not 0 <= value <= 100:
      raise ValueError(f"{name} must be between 0 and 100")
  asyncio.run(run(
    args.address, args.selector, args.timeout, args.power,
    args.hue, args.saturation, args.brightness,
  ))


if __name__ == "__main__":
  main()
