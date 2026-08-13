#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import signal
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

import protocol


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{value:02X}" for value in data)


class Transcript:
    def __init__(self, path: str | None):
        self.started_at = time.monotonic()
        self.file = None
        if path is not None:
            log_path = Path(path).expanduser().resolve()
            log_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            self.file = log_path.open("a", encoding="utf-8")

    def emit(self, event: str, **fields: object) -> None:
        record = {
            "elapsed_s": round(time.monotonic() - self.started_at, 6),
            "event": event,
            **fields,
        }
        line = json.dumps(record, sort_keys=True)
        print(line, flush=True)
        if self.file is not None:
            self.file.write(line + "\n")
            self.file.flush()

    def close(self) -> None:
        if self.file is not None:
            self.file.close()
            self.file = None


async def discover(arguments: argparse.Namespace, log: Transcript):
    devices = await BleakScanner.discover(
        timeout=arguments.scan_seconds,
        return_adv=True,
    )
    candidates = []
    for device, advertisement in devices.values():
        service_uuids = [value.lower() for value in advertisement.service_uuids]
        is_gopro = protocol.SERVICE_UUID in service_uuids or any(
            value.endswith("fea6-0000-1000-8000-00805f9b34fb")
            for value in service_uuids
        )
        if arguments.device:
            is_gopro = arguments.device.lower() in (
                device.address.lower(),
                (device.name or "").lower(),
                (advertisement.local_name or "").lower(),
            )
        if not is_gopro:
            continue
        fields: dict[str, object] = {
            "name": advertisement.local_name or device.name or "",
            "address": device.address,
            "rssi": advertisement.rssi,
            "service_uuids": service_uuids,
        }
        if arguments.raw_all:
            fields["manufacturer_data"] = {
                str(company): hex_bytes(bytes(value))
                for company, value in advertisement.manufacturer_data.items()
            }
            fields["service_data"] = {
                key: hex_bytes(bytes(value))
                for key, value in advertisement.service_data.items()
            }
        log.emit("candidate", **fields)
        candidates.append(device)
    if arguments.scan_only:
        return None
    if not candidates:
        raise RuntimeError("no matching GoPro advertising 0xFEA6 was found")
    if len(candidates) > 1 and not arguments.device:
        raise RuntimeError("multiple GoPros found; rerun with --device NAME_OR_ID")
    return candidates[0]


class GoProLab:
    def __init__(self, arguments: argparse.Namespace, log: Transcript):
        self.arguments = arguments
        self.log = log
        self.client: BleakClient | None = None
        self.accumulators: dict[str, protocol.PacketAccumulator] = {}
        self.responses: asyncio.Queue[tuple[str, bytes]] = asyncio.Queue()
        self.encoding: bool | None = None
        self.hardware_ready = False
        self.two_byte_ids = False
        self.disconnected = asyncio.Event()

    def on_disconnect(self, _client: BleakClient) -> None:
        self.log.emit("disconnected")
        self.disconnected.set()

    def on_notification(self, characteristic, value: bytearray) -> None:
        uuid = characteristic.uuid.lower()
        data = bytes(value)
        fields: dict[str, object] = {"uuid": uuid, "length": len(data)}
        if self.arguments.raw_all:
            fields["bytes"] = hex_bytes(data)
        accumulator = self.accumulators.setdefault(
            uuid, protocol.PacketAccumulator()
        )
        payload = accumulator.feed(data)
        if payload is None:
            self.log.emit("notification_packet", **fields)
            return
        fields["payload_length"] = len(payload)
        if self.arguments.raw_all:
            fields["payload"] = hex_bytes(payload)
        if uuid == protocol.COMMAND_RESPONSE_UUID:
            response = protocol.decode_command_response(payload)
            if response is not None:
                fields.update(command=response.command, status=response.status)
                if response.command == protocol.GET_HARDWARE_INFO:
                    self.hardware_ready = response.status == protocol.SUCCESS
        elif uuid == protocol.QUERY_RESPONSE_UUID:
            response = protocol.decode_status_response(payload)
            if response is not None:
                fields.update(operation=response.operation, status=response.status)
                if response.encoding is not None:
                    self.encoding = response.encoding
                    fields["encoding"] = response.encoding
        self.log.emit("notification_message", **fields)
        self.responses.put_nowait((uuid, payload))

    async def connect(self, device) -> None:
        self.client = BleakClient(
            device,
            disconnected_callback=self.on_disconnect,
            timeout=self.arguments.connect_timeout,
        )
        self.log.emit("connect_requested", name=device.name or "", address=device.address)
        await self.client.connect()
        self.log.emit("connected")
        services = self.client.services
        for service in services.services.values():
            for characteristic in service.characteristics:
                self.log.emit(
                    "gatt_characteristic",
                    service_uuid=service.uuid.lower(),
                    uuid=characteristic.uuid.lower(),
                    properties=sorted(characteristic.properties),
                )
        for required in (
            protocol.COMMAND_UUID,
            protocol.COMMAND_RESPONSE_UUID,
            protocol.QUERY_UUID,
            protocol.QUERY_RESPONSE_UUID,
        ):
            if services.get_characteristic(required) is None:
                raise RuntimeError(f"required characteristic is absent: {required}")
        for characteristic in services.characteristics.values():
            if "notify" in characteristic.properties or "indicate" in characteristic.properties:
                await self.client.start_notify(characteristic, self.on_notification)
                self.log.emit("subscribed", uuid=characteristic.uuid.lower())

    async def close(self) -> None:
        if self.client is not None and self.client.is_connected:
            await self.client.disconnect()

    async def wait_response(self, uuid: str, operation: int, timeout: float):
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for operation 0x{operation:02X}")
            response_uuid, payload = await asyncio.wait_for(
                self.responses.get(), timeout=remaining
            )
            if response_uuid != uuid:
                continue
            if payload and payload[0] == operation:
                return payload

    async def write(self, uuid: str, data: bytes, operation: str) -> None:
        if self.client is None:
            raise RuntimeError("not connected")
        self.log.emit(
            "write_requested",
            uuid=uuid,
            operation=operation,
            bytes=hex_bytes(data),
        )
        await self.client.write_gatt_char(uuid, data, response=True)
        self.log.emit("write_accepted", uuid=uuid, operation=operation)

    async def command(self, data: bytes, command_id: int, operation: str):
        await self.write(protocol.COMMAND_UUID, data, operation)
        payload = await self.wait_response(
            protocol.COMMAND_RESPONSE_UUID,
            command_id,
            self.arguments.response_timeout,
        )
        response = protocol.decode_command_response(payload)
        if response is None or response.status != protocol.SUCCESS:
            status = None if response is None else response.status
            raise RuntimeError(f"{operation} failed with status {status}")
        return response

    async def query(self, data: bytes, query_id: int, operation: str):
        await self.write(protocol.QUERY_UUID, data, operation)
        payload = await self.wait_response(
            protocol.QUERY_RESPONSE_UUID,
            query_id,
            self.arguments.response_timeout,
        )
        response = protocol.decode_status_response(payload)
        if response is None or response.status != protocol.SUCCESS:
            status = None if response is None else response.status
            raise RuntimeError(f"{operation} failed with status {status}")
        return response

    async def finish_pairing(self) -> None:
        await self.command(
            protocol.set_pairing_state(),
            protocol.SET_PAIRING_STATE,
            "finish_pairing",
        )

    async def wait_hardware_ready(self) -> None:
        deadline = asyncio.get_running_loop().time() + self.arguments.ready_timeout
        while asyncio.get_running_loop().time() < deadline:
            try:
                await self.command(
                    protocol.get_hardware_info(),
                    protocol.GET_HARDWARE_INFO,
                    "get_hardware_info",
                )
                self.log.emit("protocol_ready")
                return
            except (RuntimeError, TimeoutError) as error:
                self.log.emit("readiness_retry", error=str(error))
                await asyncio.sleep(1)
        raise TimeoutError("camera did not return successful hardware info")

    async def query_encoding(self) -> bool:
        query_id = (
            protocol.GET_STATUS_VALUES_2BYTE
            if self.two_byte_ids
            else protocol.GET_STATUS_VALUES
        )
        response = await self.query(
            protocol.get_encoding(self.two_byte_ids),
            query_id,
            "get_encoding",
        )
        if response.encoding is None:
            raise RuntimeError("encoding query omitted status 10")
        return response.encoding

    async def register_encoding(self) -> bool:
        try:
            response = await self.query(
                protocol.register_encoding(False),
                protocol.REGISTER_STATUS_UPDATES,
                "register_encoding_1byte",
            )
            self.two_byte_ids = False
        except RuntimeError as error:
            self.log.emit("status_id_fallback", from_format="1byte", error=str(error))
            response = await self.query(
                protocol.register_encoding(True),
                protocol.REGISTER_STATUS_UPDATES_2BYTE,
                "register_encoding_2byte",
            )
            self.two_byte_ids = True
        if response.encoding is None:
            raise RuntimeError("encoding registration omitted status 10")
        self.log.emit(
            "status_id_format_selected",
            format="2byte" if self.two_byte_ids else "1byte",
        )
        return response.encoding

    async def wait_encoding(self, expected: bool) -> None:
        deadline = asyncio.get_running_loop().time() + self.arguments.state_timeout
        while asyncio.get_running_loop().time() < deadline:
            if self.encoding is expected:
                return
            await asyncio.sleep(0.05)
        raise TimeoutError(f"camera did not report encoding={expected}")

    async def set_shutter(self, enabled: bool) -> None:
        if self.encoding is None:
            raise RuntimeError("recording state is unknown; query or register first")
        if self.encoding is enabled:
            raise RuntimeError(f"camera already reports encoding={enabled}")
        await self.command(
            protocol.set_shutter(enabled),
            protocol.SET_SHUTTER,
            "shutter_on" if enabled else "shutter_off",
        )
        await self.wait_encoding(enabled)
        self.log.emit("reported_state_reached", encoding=enabled)

    async def run_cycle(self) -> None:
        if self.encoding is not False:
            raise RuntimeError("cycle requires confirmed idle video state")
        await self.set_shutter(True)
        await asyncio.sleep(self.arguments.record_seconds)
        await self.set_shutter(False)
        self.log.emit("cycle_complete", reported_encoding=self.encoding)

    async def interactive(self) -> None:
        self.log.emit(
            "interactive_ready",
            commands="status, query, register, start, stop, pairing, quit",
        )
        while self.client is not None and self.client.is_connected:
            try:
                command = (await asyncio.to_thread(input, "gopro> ")).strip().lower()
            except EOFError:
                return
            try:
                if command == "status":
                    self.log.emit(
                        "status",
                        hardware_ready=self.hardware_ready,
                        encoding=self.encoding,
                        status_id_format="2byte" if self.two_byte_ids else "1byte",
                    )
                elif command == "query":
                    await self.query_encoding()
                elif command == "register":
                    await self.register_encoding()
                elif command == "start":
                    await self.set_shutter(True)
                elif command == "stop":
                    await self.set_shutter(False)
                elif command == "pairing":
                    await self.finish_pairing()
                elif command in ("quit", "exit"):
                    return
                elif command:
                    print("commands: status, query, register, start, stop, pairing, quit")
            except (RuntimeError, TimeoutError) as error:
                self.log.emit("command_failed", command=command, error=str(error))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GoPro Open GoPro BLE protocol lab")
    parser.add_argument("--device", help="exact camera name or macOS BLE identifier")
    parser.add_argument("--scan-only", action="store_true")
    parser.add_argument(
        "--status-only",
        action="store_true",
        help="connect, establish protocol readiness, read Encoding, and exit",
    )
    parser.add_argument("--scan-seconds", type=float, default=8)
    parser.add_argument("--connect-timeout", type=float, default=15)
    parser.add_argument("--response-timeout", type=float, default=5)
    parser.add_argument("--ready-timeout", type=float, default=20)
    parser.add_argument("--state-timeout", type=float, default=10)
    parser.add_argument("--finish-pairing", action="store_true")
    parser.add_argument("--run-cycle", action="store_true")
    parser.add_argument("--record-seconds", type=float, default=5)
    parser.add_argument("--raw-all", action="store_true")
    parser.add_argument("--log", help="private JSONL transcript path")
    return parser.parse_args()


async def async_main(arguments: argparse.Namespace, log: Transcript) -> None:
    device = await discover(arguments, log)
    if device is None:
        return
    lab = GoProLab(arguments, log)
    try:
        await lab.connect(device)
        if arguments.finish_pairing:
            await lab.finish_pairing()
        await lab.wait_hardware_ready()
        await lab.register_encoding()
        if arguments.status_only:
            log.emit(
                "status_only_complete",
                hardware_ready=lab.hardware_ready,
                encoding=lab.encoding,
                status_id_format="2byte" if lab.two_byte_ids else "1byte",
            )
        elif arguments.run_cycle:
            await lab.run_cycle()
        else:
            await lab.interactive()
    finally:
        await lab.close()


def main() -> int:
    arguments = parse_arguments()
    log = Transcript(arguments.log)
    try:
        asyncio.run(async_main(arguments, log))
        return 0
    except KeyboardInterrupt:
        log.emit("interrupted")
        return 130
    except Exception as error:
        log.emit("fatal", error=str(error), error_type=type(error).__name__)
        return 2
    finally:
        log.close()


if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal.default_int_handler)
    sys.exit(main())
