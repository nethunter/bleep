#!/usr/bin/env python3
"""Timeline a ZY Vega Android HCI snoop log for Zhiyun mesh research.

Requires tshark on PATH. Prints one line per interesting event with the
absolute capture time, the seconds since the first event, the ACL
connection handle, and a decoded summary:

- LE connection complete / disconnect events with the peer address;
- ATT writes and notifications on the Zhiyun `0xFEE9` characteristics,
  with the `24 3c` envelope decoded into sequence, command, and payload;
- Mesh Provisioning PDUs (PB-GATT) and Mesh Proxy PDUs with their proxy
  message type and network PDU length, which is enough to classify
  configuration traffic even while it stays encrypted;
- every other ATT write/notify, so unknown setup channels are not missed.

Optionally pass `--video-start "HH:MM:SS"` (phone local time of the screen
recording's first frame) to print the matching recording offset next to every
event. Raw addresses are printed; keep the output outside the repository.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys

FIELDS = [
    "frame.number",
    "frame.time_epoch",
    "hci_h4.direction",
    "bthci_evt.code",
    "bthci_evt.le_meta_subevent",
    "bthci_evt.connection_handle",
    "bthci_evt.bd_addr",
    "bthci_evt.status",
    "bthci_evt.reason",
    "bthci_acl.chandle",
    "btatt.opcode",
    "btatt.handle",
    "btatt.value",
    "btatt.uuid16",
    "btatt.uuid128",
    "btle.advertising_address",
    "btcommon.eir_ad.entry.device_name",
]

ZHIYUN_MAGIC = "243c"


def crc16_xmodem(data: bytes) -> int:
  crc = 0
  for value in data:
    crc ^= value << 8
    for _ in range(8):
      crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
  return crc


def decode_zhiyun(value: bytes) -> str:
  out = []
  offset = 0
  while offset + 4 <= len(value):
    if value[offset:offset + 2] != b"\x24\x3c":
      offset += 1
      continue
    body_length = int.from_bytes(value[offset + 2:offset + 4], "little")
    total = body_length + 6
    frame = value[offset:offset + total]
    if len(frame) < total or total < 12:
      out.append(f"partial:{value[offset:].hex()}")
      break
    body = frame[4:-2]
    ok = crc16_xmodem(body) == int.from_bytes(frame[-2:], "little")
    marker = body[:2].hex()
    direction = {"0001": "req", "0100": "rsp"}.get(marker, marker)
    seq = int.from_bytes(body[2:4], "little")
    cmd = int.from_bytes(body[4:6], "little")
    out.append(
        f"{direction} seq={seq} cmd=0x{cmd:04x} payload={body[6:].hex()}"
        f"{'' if ok else ' CRC_BAD'}")
    offset += total
  return "; ".join(out) if out else f"raw:{value.hex()}"


def run_tshark(path: str) -> list[dict[str, str]]:
  # `-T fields` is used rather than `-T json` because the JSON path drops
  # btatt.value for frames the ATT dissector hands to a sub-dissector, while
  # the tab output keeps the raw value. Multi-valued fields are collapsed to
  # their first occurrence, which is all these single-PDU frames carry.
  cmd = ["tshark", "-r", path, "-T", "fields",
         "-E", "separator=\t", "-E", "occurrence=f"]
  for field in FIELDS:
    cmd += ["-e", field]
  result = subprocess.run(cmd, capture_output=True, text=True, check=True)
  rows = []
  for line in result.stdout.splitlines():
    values = line.split("\t")
    row = {}
    for field, value in zip(FIELDS, values):
      if value != "":
        row[field] = value
    rows.append(row)
  return rows


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("log")
  parser.add_argument("--video-start", help="phone local time HH:MM:SS of recording start")
  parser.add_argument("--all-att", action="store_true", help="also print non-Zhiyun ATT traffic")
  args = parser.parse_args()

  rows = run_tshark(args.log)
  if not rows:
    print("no packets", file=sys.stderr)
    return
  first = next(float(r["frame.time_epoch"]) for r in rows if "frame.time_epoch" in r)
  video_start = None
  if args.video_start:
    day = dt.datetime.fromtimestamp(first).date()
    video_start = dt.datetime.combine(
        day, dt.time.fromisoformat(args.video_start)).timestamp()

  def stamp(epoch: float) -> str:
    local = dt.datetime.fromtimestamp(epoch).strftime("%H:%M:%S.%f")[:-3]
    text = f"{local} +{epoch - first:8.3f}s"
    if video_start is not None:
      text += f" video={epoch - video_start:7.2f}s"
    return text

  handles: dict[str, str] = {}
  for row in rows:
    if "frame.time_epoch" not in row:
      continue
    epoch = float(row["frame.time_epoch"])
    direction = {"0x00": "tx", "0x01": "rx"}.get(row.get("hci_h4.direction", ""), "?")
    evt = row.get("bthci_evt.code")
    sub = row.get("bthci_evt.le_meta_subevent")
    if evt == "0x3e" and sub in ("0x01", "0x0a"):
      handle = row.get("bthci_evt.connection_handle", "?")
      addr = row.get("bthci_evt.bd_addr", "?")
      handles[handle] = addr
      print(f"{stamp(epoch)} CONNECT handle={handle} peer={addr} status={row.get('bthci_evt.status')}")
      continue
    if evt == "0x05":
      handle = row.get("bthci_evt.connection_handle", "?")
      print(f"{stamp(epoch)} DISCONNECT handle={handle} peer={handles.get(handle, '?')} reason={row.get('bthci_evt.reason')}")
      continue
    opcode = row.get("btatt.opcode")
    if opcode is None:
      continue
    handle = row.get("bthci_acl.chandle", "?")
    peer = handles.get(handle, "?")
    att_handle = row.get("btatt.handle", "?")
    value_hex = (row.get("btatt.value") or "").replace(":", "")
    kind = {"0x12": "WRITE_REQ", "0x52": "WRITE_CMD", "0x1b": "NOTIFY",
            "0x0b": "READ_RSP", "0x0a": "READ_REQ", "0x13": "WRITE_RSP"}.get(opcode, opcode)
    if not value_hex and kind not in ("READ_REQ",):
      continue
    value = bytes.fromhex(value_hex) if value_hex else b""
    if value and value[0] == 0x03 and kind in ("WRITE_CMD", "NOTIFY") and len(value) >= 2:
      print(f"{stamp(epoch)} {direction} {kind} handle={handle} peer={peer} att={att_handle} PB-GATT type=0x{value[1]:02x} len={len(value) - 1} pdu={value[1:].hex()}")
      continue
    if (value and value[0] in (0x00, 0x01, 0x02, 0x40, 0x41, 0x42, 0x80, 0x81, 0x82, 0xc0, 0xc1, 0xc2) and kind in ("WRITE_CMD", "NOTIFY") and ZHIYUN_MAGIC not in value_hex[:4] and len(value) >= 2):
      sar = value[0] >> 6
      ptype = value[0] & 0x3f
      label = {0: "NETWORK", 1: "BEACON", 2: "PROXY_CONFIG", 3: "PROVISIONING"}.get(ptype, f"type{ptype}")
      print(f"{stamp(epoch)} {direction} {kind} handle={handle} peer={peer} att={att_handle} MESH {label} sar={sar} len={len(value) - 1} pdu={value[1:].hex()}")
      continue
    if ZHIYUN_MAGIC in value_hex[:4]:
      print(f"{stamp(epoch)} {direction} {kind} handle={handle} peer={peer} att={att_handle} FEE9 {decode_zhiyun(value)}")
      continue
    if args.all_att:
      print(f"{stamp(epoch)} {direction} {kind} handle={handle} peer={peer} att={att_handle} value={value_hex}")


if __name__ == "__main__":
  main()
