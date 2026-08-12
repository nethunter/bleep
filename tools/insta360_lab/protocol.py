from __future__ import annotations

import re
from dataclasses import dataclass


ADVERTISED_NAME = "Insta360 Remote (Bleep)"
SERVICE_UUID = "CE80"
WRITE_UUID = "CE81"
NOTIFY_UUID = "CE82"
INFO_UUID = "CE83"

SHUTTER_COMMAND = bytes.fromhex("FC EF FE 86 00 03 01 02 00")
POWER_OFF_COMMAND = bytes.fromhex("FC EF FE 86 00 03 01 00 03")

ORBIT_PREFIX = bytes.fromhex(
    "4C 00 02 15 09 4F 52 42 49 54 09 FF 0F 00"
)
ORBIT_SUFFIX = bytes.fromhex("00 00 00 00 E4 01")
TX_POWER_SCAN_RESPONSE = bytes.fromhex("02 0A 00")

_CAMERA_NAME = re.compile(
    r"^(?:X3|X4|X5|RS|ONE|GO 3|Insta360 GO 3|GO Ultra|Insta360 GO Ultra) "
    r"([A-Za-z0-9]{6})$"
)


@dataclass(frozen=True)
class CaptureState:
    mode: str
    phase: str


def camera_serial(camera_name: str) -> str:
    match = _CAMERA_NAME.fullmatch(camera_name)
    if match is None:
        raise ValueError(
            "camera name must end in an exact six-character alphanumeric serial"
        )
    return match.group(1)


def orbit_manufacturer_data(camera_name: str) -> bytes:
    return ORBIT_PREFIX + camera_serial(camera_name).encode("ascii") + ORBIT_SUFFIX


def orbit_primary_advertisement(camera_name: str) -> bytes:
    manufacturer = orbit_manufacturer_data(camera_name)
    return bytes.fromhex("02 01 06") + bytes((len(manufacturer) + 1, 0xFF)) + manufacturer


def is_state_candidate(data: bytes) -> bool:
    return data.startswith(bytes.fromhex("FE EF FE 10 80"))


def _duration_text(data: bytes) -> bool:
    return bool(data) and data[-1:] in (b"m", b"h") and all(
        48 <= value <= 57 or value in (ord("h"), ord("m")) for value in data
    )


def _photo_count_text(data: bytes) -> bool:
    return len(data) >= 2 and data[:1] == b" " and all(
        48 <= value <= 57 or value == ord("+") for value in data[1:]
    )


def decode_capture_state(data: bytes) -> CaptureState | None:
    if len(data) < 6 or not data.startswith(bytes.fromhex("FE EF FE")):
        return None
    if (
        data[3:6] == bytes.fromhex("10 80 0D")
        and len(data) == 19
        and data[10] == ord(".")
        and data[13] == ord(":")
        and data[16] == ord(":")
    ):
        return CaptureState("video", "recording")
    if (
        data[3:6] == bytes.fromhex("10 80 07")
        and len(data) > 10
        and data[6] == 0x01
        and data[8:10] == bytes.fromhex("46 01")
        and _duration_text(data[10:])
    ):
        return CaptureState("video", "idle")
    if (
        data[3:6] == bytes.fromhex("10 80 09")
        and len(data) > 11
        and data[6] == 0x01
        and data[8:10] == bytes.fromhex("46 01")
        and _photo_count_text(data[10:])
    ):
        return CaptureState("photo", "idle")
    if data[3:6] == bytes.fromhex("10 80 05") and len(data) == 11:
        return CaptureState("photo", "saving")
    if data[3:7] == bytes.fromhex("55 00 07 00") and len(data) == 13:
        phase = {0x00: "idle", 0x01: "starting", 0x02: "recording", 0x04: "stopping"}.get(data[7])
        return CaptureState("video", phase) if phase is not None else None
    if data[3:7] == bytes.fromhex("55 00 07 01") and len(data) == 13:
        phase = {0x00: "idle", 0x01: "starting", 0x02: "capturing", 0x05: "saving"}.get(data[7])
        return CaptureState("photo", phase) if phase is not None else None
    return None
