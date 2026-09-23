import datetime as dt
import struct
import unittest

from ellisys_hci_bridge import (
    BTSNOOP_DLT_H4,
    BTSNOOP_MAGIC,
    BTSNOOP_VERSION,
    BtsnoopStreamParser,
    ELLISYS_HCI_ACL_FROM_CONTROLLER,
    ELLISYS_HCI_ACL_FROM_HOST,
    ELLISYS_HCI_COMMAND,
    ELLISYS_HCI_EVENT,
    ELLISYS_HCI_ISO_FROM_CONTROLLER,
    ELLISYS_HCI_SCO_FROM_HOST,
    H4_ACL,
    H4_COMMAND,
    H4_EVENT,
    H4_ISO,
    H4_SCO,
    build_ellisys_hci_packet,
    ellisys_packet_type,
)


def btsnoop_header() -> bytes:
    return BTSNOOP_MAGIC + struct.pack(">II", BTSNOOP_VERSION, BTSNOOP_DLT_H4)


def btsnoop_record(packet: bytes, flags: int, timestamp_us: int, drops: int = 0) -> bytes:
    return struct.pack(">IIIIQ", len(packet), len(packet), flags, drops, timestamp_us) + packet


class BtsnoopParserTests(unittest.TestCase):
    def test_fragmented_stream_parses_h4_records(self) -> None:
        stream = btsnoop_header() + btsnoop_record(b"\x01\x03\x0c\x00", 2, 1000) + btsnoop_record(
            b"\x04\x0e\x04\x01\x03\x0c\x00", 3, 2500, 2
        )
        parser = BtsnoopStreamParser()
        records = []
        headers = 0
        for offset in range(0, len(stream), 3):
            result = parser.feed(stream[offset : offset + 3])
            headers += result.new_headers
            records.extend(result.records)

        self.assertEqual(headers, 1)
        self.assertEqual(len(records), 2)
        self.assertEqual(records[0].h4_type, H4_COMMAND)
        self.assertFalse(records[0].direction_to_host)
        self.assertEqual(records[1].h4_type, H4_EVENT)
        self.assertTrue(records[1].direction_to_host)
        self.assertEqual(records[1].cumulative_drops, 2)

    def test_reboot_header_resynchronizes_partial_record(self) -> None:
        parser = BtsnoopStreamParser()
        partial = btsnoop_header() + struct.pack(">IIIIQ", 100, 100, 0, 0, 123) + b"\x02\x01"
        self.assertEqual(parser.feed(partial).new_headers, 1)
        restarted = btsnoop_header() + btsnoop_record(b"\x04\x0e\x00", 3, 500)
        result = parser.feed(restarted)
        self.assertEqual(result.new_headers, 1)
        self.assertEqual(len(result.records), 1)


class EllisysPacketTests(unittest.TestCase):
    def test_packet_type_mapping_matches_official_api(self) -> None:
        self.assertEqual(ellisys_packet_type(H4_COMMAND, False), ELLISYS_HCI_COMMAND)
        self.assertEqual(ellisys_packet_type(H4_EVENT, True), ELLISYS_HCI_EVENT)
        self.assertEqual(ellisys_packet_type(H4_ACL, False), ELLISYS_HCI_ACL_FROM_HOST)
        self.assertEqual(ellisys_packet_type(H4_ACL, True), ELLISYS_HCI_ACL_FROM_CONTROLLER)
        self.assertEqual(ellisys_packet_type(H4_SCO, False), ELLISYS_HCI_SCO_FROM_HOST)
        self.assertEqual(ellisys_packet_type(H4_ISO, True), ELLISYS_HCI_ISO_FROM_CONTROLLER)

    def test_injection_packet_binary_layout(self) -> None:
        timestamp = dt.datetime(2026, 9, 23, 13, 42, 7, 123456, tzinfo=dt.timezone.utc)
        packet = build_ellisys_hci_packet(timestamp, 921600.0, ELLISYS_HCI_EVENT, b"\x0e\x00", 0)

        self.assertEqual(packet[:4], b"\x02\x00\x01\x02")
        self.assertEqual(struct.unpack_from("<HBB", packet, 4), (2026, 9, 23))
        expected_ns = ((13 * 60 + 42) * 60 + 7) * 1_000_000_000 + 123456000
        self.assertEqual(int.from_bytes(packet[8:14], "little"), expected_ns)
        self.assertEqual(packet[14:17], b"\x83\x00\x80")
        self.assertEqual(struct.unpack_from("<f", packet, 17)[0], 921600.0)
        self.assertEqual(packet[21:], b"\x81\x84\x82\x0e\x00")


if __name__ == "__main__":
    unittest.main()
