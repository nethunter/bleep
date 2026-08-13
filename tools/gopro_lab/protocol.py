from __future__ import annotations

from dataclasses import dataclass


SERVICE_UUID = "0000fea6-0000-1000-8000-00805f9b34fb"
COMMAND_UUID = "b5f90072-aa8d-11e3-9046-0002a5d5c51b"
COMMAND_RESPONSE_UUID = "b5f90073-aa8d-11e3-9046-0002a5d5c51b"
QUERY_UUID = "b5f90076-aa8d-11e3-9046-0002a5d5c51b"
QUERY_RESPONSE_UUID = "b5f90077-aa8d-11e3-9046-0002a5d5c51b"

SET_SHUTTER = 0x01
SET_PAIRING_STATE = 0x17
GET_HARDWARE_INFO = 0x3C
GET_STATUS_VALUES = 0x13
REGISTER_STATUS_UPDATES = 0x53
NOTIFY_STATUS_UPDATE = 0x93
GET_STATUS_VALUES_2BYTE = 0x16
REGISTER_STATUS_UPDATES_2BYTE = 0x56
NOTIFY_STATUS_UPDATE_2BYTE = 0x96
ENCODING_STATUS = 0x0A
SUCCESS = 0x00


def packet(payload: bytes) -> bytes:
    """Wrap one small Open GoPro payload in a General packet header."""
    if not payload or len(payload) > 20:
        raise ValueError("the lab only emits non-empty payloads up to 20 bytes")
    return bytes((len(payload),)) + payload


def set_shutter(enabled: bool) -> bytes:
    return packet(bytes((SET_SHUTTER, 0x01, 0x01 if enabled else 0x00)))


def set_pairing_state() -> bytes:
    return packet(bytes((SET_PAIRING_STATE, 0x01, 0x01)))


def get_hardware_info() -> bytes:
    return packet(bytes((GET_HARDWARE_INFO,)))


def get_encoding(two_byte_ids: bool = False) -> bytes:
    if two_byte_ids:
        return packet(bytes((GET_STATUS_VALUES_2BYTE, 0x00, ENCODING_STATUS)))
    return packet(bytes((GET_STATUS_VALUES, ENCODING_STATUS)))


def register_encoding(two_byte_ids: bool = False) -> bytes:
    if two_byte_ids:
        return packet(
            bytes((REGISTER_STATUS_UPDATES_2BYTE, 0x00, ENCODING_STATUS))
        )
    return packet(bytes((REGISTER_STATUS_UPDATES, ENCODING_STATUS)))


@dataclass(frozen=True)
class CommandResponse:
    command: int
    status: int
    data: bytes


@dataclass(frozen=True)
class StatusResponse:
    operation: int
    status: int
    encoding: bool | None


class PacketAccumulator:
    """Reassemble General, Extended, and Continuation Open GoPro packets."""

    def __init__(self) -> None:
        self._expected = 0
        self._payload = bytearray()
        self._counter = 0

    def reset(self) -> None:
        self._expected = 0
        self._payload.clear()
        self._counter = 0

    def feed(self, data: bytes) -> bytes | None:
        if not data:
            return None
        first = data[0]
        if first & 0x80:
            if self._expected == 0 or (first & 0x0F) != self._counter:
                self.reset()
                return None
            self._counter = (self._counter + 1) & 0x0F
            self._payload.extend(data[1:])
        else:
            self.reset()
            header_type = (first >> 5) & 0x03
            if header_type == 0:
                self._expected = first & 0x1F
                header_length = 1
            elif header_type == 1:
                if len(data) < 2:
                    return None
                self._expected = ((first & 0x1F) << 8) | data[1]
                header_length = 2
            elif header_type == 2:
                if len(data) < 3:
                    return None
                self._expected = (data[1] << 8) | data[2]
                header_length = 3
            else:
                return None
            if self._expected == 0:
                self.reset()
                return None
            self._payload.extend(data[header_length:])

        if len(self._payload) > self._expected:
            self.reset()
            return None
        if len(self._payload) != self._expected:
            return None
        payload = bytes(self._payload)
        self.reset()
        return payload


def decode_command_response(payload: bytes) -> CommandResponse | None:
    if len(payload) < 2:
        return None
    return CommandResponse(payload[0], payload[1], payload[2:])


def decode_status_response(payload: bytes) -> StatusResponse | None:
    if len(payload) < 2 or payload[0] not in (
        GET_STATUS_VALUES,
        REGISTER_STATUS_UPDATES,
        NOTIFY_STATUS_UPDATE,
        GET_STATUS_VALUES_2BYTE,
        REGISTER_STATUS_UPDATES_2BYTE,
        NOTIFY_STATUS_UPDATE_2BYTE,
    ):
        return None
    operation = payload[0]
    status = payload[1]
    position = 2
    encoding = None
    two_byte_ids = operation in (
        GET_STATUS_VALUES_2BYTE,
        REGISTER_STATUS_UPDATES_2BYTE,
        NOTIFY_STATUS_UPDATE_2BYTE,
    )
    while position < len(payload):
        header_length = 3 if two_byte_ids else 2
        if position + header_length > len(payload):
            return None
        if two_byte_ids:
            element_id = (payload[position] << 8) | payload[position + 1]
            element_length = payload[position + 2]
        else:
            element_id = payload[position]
            element_length = payload[position + 1]
        position += header_length
        if position + element_length > len(payload):
            return None
        value = payload[position : position + element_length]
        position += element_length
        if element_id == ENCODING_STATUS:
            if element_length != 1 or value[0] not in (0, 1):
                return None
            encoding = value[0] == 1
    return StatusResponse(operation, status, encoding)
