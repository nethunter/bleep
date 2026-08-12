import unittest

import protocol


class ProtocolTest(unittest.TestCase):
    def test_captured_orbit_vector(self):
        self.assertEqual(
            protocol.orbit_primary_advertisement("X5 1HDKAB"),
            bytes.fromhex(
                "02 01 06 1B FF 4C 00 02 15 09 4F 52 42 49 54 09 "
                "FF 0F 00 31 48 44 4B 41 42 00 00 00 00 E4 01"
            ),
        )

    def test_invalid_serials_are_rejected(self):
        for name in ("X5", "X5 ABC", "X5 ABCDEFG", "X5 AB-CD1", "Camera ABC123"):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    protocol.orbit_manufacturer_data(name)

    def test_gps_states(self):
        self.assertEqual(
            protocol.decode_capture_state(bytes.fromhex("FE EF FE 10 80 07 01 2C 46 01 33 35 6D")),
            protocol.CaptureState("video", "idle"),
        )
        self.assertEqual(
            protocol.decode_capture_state(bytes.fromhex("FE EF FE 10 80 0D 01 0E 46 01 2E 30 30 3A 30 30 3A 30 30")),
            protocol.CaptureState("video", "recording"),
        )
        self.assertEqual(
            protocol.decode_capture_state(bytes.fromhex("FE EF FE 10 80 09 01 1E 46 01 20 39 39 39 2B")),
            protocol.CaptureState("photo", "idle"),
        )


if __name__ == "__main__":
    unittest.main()
