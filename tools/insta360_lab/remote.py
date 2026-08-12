#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import queue
import signal
import sys
import threading
import time
from pathlib import Path

import CoreBluetooth as CB
import Foundation
import objc

import protocol


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{value:02X}" for value in data)


class Insta360Remote(Foundation.NSObject):
    def initWithArguments_(self, arguments):
        self = objc.super(Insta360Remote, self).init()
        if self is None:
            return None
        self.arguments = arguments
        self.started_at = time.monotonic()
        self.commands = queue.SimpleQueue()
        self.manager = None
        self.write_characteristic = None
        self.notify_characteristic = None
        self.info_characteristic = None
        self.subscribed_centrals = []
        self.capture_state = None
        self.advertising_mode = "none"
        self.ready = False
        self.done = False
        self.exit_code = 0
        self.scenario_phase = "wait_initial" if arguments.run_cycle else "interactive"
        self.phase_deadline = None
        self.stop_after = None
        self.log_file = None
        if arguments.log is not None:
            log_path = Path(arguments.log).expanduser().resolve()
            log_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            self.log_file = log_path.open("a", encoding="utf-8")
        self.manager = CB.CBPeripheralManager.alloc().initWithDelegate_queue_options_(
            self, None, None
        )
        return self

    @objc.python_method
    def close(self):
        if self.manager is not None:
            self.manager.stopAdvertising()
            self.manager.removeAllServices()
        if self.log_file is not None:
            self.log_file.close()
            self.log_file = None

    @objc.python_method
    def emit(self, event: str, **fields):
        record = {
            "elapsed_s": round(time.monotonic() - self.started_at, 6),
            "event": event,
            **fields,
        }
        print(json.dumps(record, sort_keys=True), flush=True)
        if self.log_file is not None:
            self.log_file.write(json.dumps(record, sort_keys=True) + "\n")
            self.log_file.flush()

    def peripheralManagerDidUpdateState_(self, manager):
        state = manager.state()
        self.emit("bluetooth_state", state=int(state))
        if state == CB.CBManagerStatePoweredOn:
            self.install_service()
        elif state in (CB.CBManagerStateUnsupported, CB.CBManagerStateUnauthorized):
            self.emit("fatal", reason="Bluetooth peripheral mode unavailable")
            self.exit_code = 2
            self.done = True

    @objc.python_method
    def install_service(self):
        write_properties = (
            CB.CBCharacteristicPropertyWrite
            | CB.CBCharacteristicPropertyWriteWithoutResponse
        )
        self.write_characteristic = CB.CBMutableCharacteristic.alloc().initWithType_properties_value_permissions_(
            CB.CBUUID.UUIDWithString_(protocol.WRITE_UUID),
            write_properties,
            None,
            CB.CBAttributePermissionsWriteable,
        )
        self.notify_characteristic = CB.CBMutableCharacteristic.alloc().initWithType_properties_value_permissions_(
            CB.CBUUID.UUIDWithString_(protocol.NOTIFY_UUID),
            CB.CBCharacteristicPropertyNotify,
            None,
            0,
        )
        self.info_characteristic = CB.CBMutableCharacteristic.alloc().initWithType_properties_value_permissions_(
            CB.CBUUID.UUIDWithString_(protocol.INFO_UUID),
            CB.CBCharacteristicPropertyRead,
            Foundation.NSData.data(),
            CB.CBAttributePermissionsReadable,
        )
        service = CB.CBMutableService.alloc().initWithType_primary_(
            CB.CBUUID.UUIDWithString_(protocol.SERVICE_UUID), True
        )
        service.setCharacteristics_(
            [self.notify_characteristic, self.write_characteristic, self.info_characteristic]
        )
        self.manager.addService_(service)

    def peripheralManager_didAddService_error_(self, manager, service, error):
        if error is not None:
            self.emit("fatal", reason="add_service_failed", error=str(error))
            self.exit_code = 2
            self.done = True
            return
        self.emit("service_ready", uuid=str(service.UUID()))
        self.ready = True
        if self.arguments.advertise == "wake":
            self.advertise_wake()
        else:
            self.advertise_normal()

    @objc.python_method
    def advertise_normal(self):
        self.manager.stopAdvertising()
        self.advertising_mode = "normal"
        advertisement = {
            CB.CBAdvertisementDataLocalNameKey: self.arguments.name,
        }
        if self.arguments.advertise_service:
            advertisement[CB.CBAdvertisementDataServiceUUIDsKey] = [
                CB.CBUUID.UUIDWithString_(protocol.SERVICE_UUID)
            ]
        self.manager.startAdvertising_(advertisement)
        self.emit("advertising_requested", mode="normal", name=self.arguments.name)

    @objc.python_method
    def advertise_wake(self):
        try:
            manufacturer = protocol.orbit_manufacturer_data(self.arguments.camera_name)
        except ValueError as error:
            self.emit("fatal", reason="invalid_camera_name", error=str(error))
            self.exit_code = 2
            self.done = True
            return
        self.manager.stopAdvertising()
        self.advertising_mode = "wake"
        advertisement = {
            CB.CBAdvertisementDataManufacturerDataKey: Foundation.NSData.dataWithBytes_length_(
                manufacturer, len(manufacturer)
            ),
            CB.CBAdvertisementDataTxPowerLevelKey: 0,
        }
        self.manager.startAdvertising_(advertisement)
        self.emit(
            "advertising_requested",
            mode="wake",
            manufacturer_data=hex_bytes(manufacturer),
            tx_power_dbm=0,
        )

    def peripheralManagerDidStartAdvertising_error_(self, manager, error):
        if error is not None:
            self.emit(
                "advertising_failed",
                mode=self.advertising_mode,
                error=str(error),
            )
            if self.advertising_mode == "wake":
                self.emit(
                    "platform_limit",
                    reason="macOS CoreBluetooth rejected capture-exact ORBIT advertising",
                )
            self.exit_code = 3
            self.done = True
            return
        self.emit("advertising_started", mode=self.advertising_mode)
        if self.advertising_mode == "wake":
            self.emit(
                "platform_limit",
                reason=(
                    "CoreBluetooth callback does not prove ORBIT emission; "
                    "the independent nRF probe on this Mac observed none"
                ),
            )

    def peripheralManager_central_didSubscribeToCharacteristic_(
        self, manager, central, characteristic
    ):
        self.subscribed_centrals = [central]
        self.emit(
            "ce82_subscribed",
            maximum_update_length=int(central.maximumUpdateValueLength()),
        )
        if self.advertising_mode == "wake":
            self.scenario_phase = "wait_wake_state"
            self.phase_deadline = time.monotonic() + 20

    def peripheralManager_central_didUnsubscribeFromCharacteristic_(
        self, manager, central, characteristic
    ):
        self.subscribed_centrals = []
        self.capture_state = None
        self.emit("ce82_unsubscribed")
        if self.scenario_phase == "wait_power_disconnect":
            self.advertise_wake()
            self.scenario_phase = "wait_wake_connect"
            self.phase_deadline = time.monotonic() + 60

    def peripheralManager_didReceiveWriteRequests_(self, manager, requests):
        for request in requests:
            data = bytes(request.value() or b"")
            decoded = protocol.decode_capture_state(data)
            fields = {"length": len(data)}
            if self.arguments.raw_all or protocol.is_state_candidate(data):
                fields["bytes"] = hex_bytes(data)
            if decoded is not None:
                fields["mode"] = decoded.mode
                fields["phase"] = decoded.phase
                self.capture_state = decoded
            self.emit("ce81_write", **fields)
            manager.respondToRequest_withResult_(request, CB.CBATTErrorSuccess)

    @objc.python_method
    def send(self, payload: bytes, label: str) -> bool:
        if not self.subscribed_centrals:
            self.emit("command_rejected", command=label, reason="ce82_not_subscribed")
            return False
        sent = bool(
            self.manager.updateValue_forCharacteristic_onSubscribedCentrals_(
                Foundation.NSData.dataWithBytes_length_(payload, len(payload)),
                self.notify_characteristic,
                self.subscribed_centrals,
            )
        )
        self.emit("ce82_notify", command=label, accepted=sent, bytes=hex_bytes(payload))
        return sent

    @objc.python_method
    def request_start(self) -> bool:
        if self.capture_state != protocol.CaptureState("video", "idle"):
            self.emit("command_rejected", command="start", reason="video_idle_not_confirmed")
            return False
        return self.send(protocol.SHUTTER_COMMAND, "start")

    @objc.python_method
    def request_stop(self) -> bool:
        if self.capture_state != protocol.CaptureState("video", "recording"):
            self.emit("command_rejected", command="stop", reason="recording_not_confirmed")
            return False
        return self.send(protocol.SHUTTER_COMMAND, "stop")

    @objc.python_method
    def request_off(self) -> bool:
        if self.capture_state == protocol.CaptureState("video", "recording"):
            self.emit("command_rejected", command="off", reason="camera_recording")
            return False
        return self.send(protocol.POWER_OFF_COMMAND, "off")

    @objc.python_method
    def process_command(self, command: str):
        command = command.strip().lower()
        if command == "start":
            self.request_start()
        elif command == "stop":
            self.request_stop()
        elif command == "off":
            self.request_off()
        elif command == "wake":
            self.advertise_wake()
        elif command == "normal":
            self.advertise_normal()
        elif command == "status":
            self.emit(
                "status",
                advertising=self.advertising_mode,
                subscribed=bool(self.subscribed_centrals),
                state=None if self.capture_state is None else self.capture_state.__dict__,
            )
        elif command in ("quit", "exit"):
            self.done = True
        elif command:
            self.emit("unknown_command", command=command)

    @objc.python_method
    def tick(self):
        while True:
            try:
                self.process_command(self.commands.get_nowait())
            except queue.Empty:
                break
        if not self.arguments.run_cycle or self.done:
            return
        now = time.monotonic()
        if self.phase_deadline is not None and now >= self.phase_deadline:
            self.emit("scenario_failed", phase=self.scenario_phase, reason="timeout")
            self.exit_code = 4
            self.done = True
            return
        state = self.capture_state
        if self.scenario_phase == "wait_initial" and state == protocol.CaptureState("video", "idle"):
            if self.request_start():
                self.scenario_phase = "wait_recording"
                self.phase_deadline = now + 20
        elif self.scenario_phase == "wait_recording" and state == protocol.CaptureState("video", "recording"):
            self.scenario_phase = "recording_hold"
            self.stop_after = now + self.arguments.record_seconds
            self.phase_deadline = self.stop_after + 20
        elif self.scenario_phase == "recording_hold" and now >= self.stop_after:
            if self.request_stop():
                self.scenario_phase = "wait_stopped"
                self.phase_deadline = now + 20
        elif self.scenario_phase == "wait_stopped" and state == protocol.CaptureState("video", "idle"):
            if self.request_off():
                self.scenario_phase = "wait_power_disconnect"
                self.phase_deadline = now + 20
        elif self.scenario_phase == "wait_wake_state" and state is not None:
            self.emit("scenario_complete", final_state=state.__dict__)
            self.done = True


def stdin_worker(target: Insta360Remote):
    for line in sys.stdin:
        target.commands.put(line)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Mac CoreBluetooth Insta360 GPS Remote protocol harness"
    )
    parser.add_argument("--camera-name", default="X5 1HDKAB")
    parser.add_argument(
        "--name",
        default="Insta360 GPS Remote",
        help="normal advertised name; the captured vendor name fits macOS's generated packet",
    )
    parser.add_argument(
        "--advertise-service",
        action="store_true",
        help="also request CE80 in advertising; the captured GPS Remote did not",
    )
    parser.add_argument("--advertise", choices=("normal", "wake"), default="normal")
    parser.add_argument("--run-cycle", action="store_true")
    parser.add_argument("--record-seconds", type=float, default=5.0)
    parser.add_argument("--raw-all", action="store_true")
    parser.add_argument("--log", help="private JSONL output path outside the repository")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.advertise == "wake":
        protocol.camera_serial(arguments.camera_name)
    remote = Insta360Remote.alloc().initWithArguments_(arguments)
    signal.signal(signal.SIGINT, lambda *_: setattr(remote, "done", True))
    if not arguments.run_cycle:
        threading.Thread(target=stdin_worker, args=(remote,), daemon=True).start()
        print("commands: status, start, stop, off, wake, normal, quit", flush=True)
    run_loop = Foundation.NSRunLoop.currentRunLoop()
    while not remote.done:
        run_loop.runUntilDate_(Foundation.NSDate.dateWithTimeIntervalSinceNow_(0.1))
        remote.tick()
    remote.close()
    return remote.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
