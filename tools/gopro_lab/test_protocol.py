import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import protocol


class ProtocolTest(unittest.TestCase):
    def test_official_short_command_vectors(self):
        self.assertEqual(protocol.set_shutter(True), bytes.fromhex("03 01 01 01"))
        self.assertEqual(protocol.set_shutter(False), bytes.fromhex("03 01 01 00"))
        self.assertEqual(protocol.set_pairing_state(), bytes.fromhex("03 17 01 01"))
        self.assertEqual(protocol.get_hardware_info(), bytes.fromhex("01 3C"))
        self.assertEqual(protocol.get_encoding(), bytes.fromhex("02 13 0A"))
        self.assertEqual(protocol.register_encoding(), bytes.fromhex("02 53 0A"))
        self.assertEqual(
            protocol.get_encoding(two_byte_ids=True), bytes.fromhex("03 16 00 0A")
        )
        self.assertEqual(
            protocol.register_encoding(two_byte_ids=True),
            bytes.fromhex("03 56 00 0A"),
        )

    def test_general_packet_and_command_response(self):
        packets = protocol.PacketAccumulator()
        payload = packets.feed(bytes.fromhex("02 01 00"))
        self.assertEqual(
            protocol.decode_command_response(payload),
            protocol.CommandResponse(protocol.SET_SHUTTER, protocol.SUCCESS, b""),
        )

    def test_extended_and_continuation_packets(self):
        packets = protocol.PacketAccumulator()
        self.assertIsNone(packets.feed(bytes.fromhex("20 16 3C 00 01 3E")))
        payload = packets.feed(bytes.fromhex("80 0C 48 45 52 4F 31 33"))
        self.assertIsNone(payload)
        payload = packets.feed(bytes.fromhex("81 20 42 6C 61 63 6B 00 00 00 00 00"))
        self.assertEqual(len(payload), 22)
        self.assertEqual(payload[:4], bytes.fromhex("3C 00 01 3E"))

    def test_encoding_query_and_notification(self):
        response = protocol.decode_status_response(bytes.fromhex("53 00 0A 01 00"))
        self.assertEqual(
            response,
            protocol.StatusResponse(protocol.REGISTER_STATUS_UPDATES, 0, False),
        )
        notification = protocol.decode_status_response(bytes.fromhex("93 00 0A 01 01"))
        self.assertEqual(
            notification,
            protocol.StatusResponse(protocol.NOTIFY_STATUS_UPDATE, 0, True),
        )
        extended = protocol.decode_status_response(
            bytes.fromhex("96 00 00 0A 01 01")
        )
        self.assertEqual(
            extended,
            protocol.StatusResponse(protocol.NOTIFY_STATUS_UPDATE_2BYTE, 0, True),
        )

    def test_malformed_encoding_values_are_rejected(self):
        self.assertIsNone(protocol.decode_status_response(bytes.fromhex("53 00 0A 02 00")))
        self.assertIsNone(protocol.decode_status_response(bytes.fromhex("93 00 0A 01 02")))


if __name__ == "__main__":
    unittest.main()
