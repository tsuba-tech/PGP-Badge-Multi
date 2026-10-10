import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location("diagnostics", Path(__file__).with_name("download-diagnostics.py"))
diagnostics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostics)


def record(sequence, uptime, message):
    payload = struct.pack("<IIQ236s", diagnostics.MAGIC, sequence, uptime, message.encode())
    return payload + struct.pack("<I", zlib.crc32(payload))


class DiagnosticDecoderTests(unittest.TestCase):
    def test_empty_partition(self):
        self.assertEqual(diagnostics.decode_records(b"\xff" * diagnostics.FLASH_SIZE), ([], 0))

    def test_ring_order_and_reboot(self):
        data = bytearray(b"\xff" * diagnostics.FLASH_SIZE)
        data[:256] = record(1025, 100, "BOOT")
        data[-256:] = record(1024, 900000, "NOTIFY_FAILED conn=2")
        decoded, invalid = diagnostics.decode_records(data)
        self.assertEqual([item[0] for item in decoded], [1024, 1025])
        self.assertEqual(decoded[0][2], "NOTIFY_FAILED conn=2")
        self.assertEqual(invalid, 0)

    def test_interrupted_or_corrupt_write(self):
        data = bytearray(b"\xff" * diagnostics.FLASH_SIZE)
        data[:256] = record(1, 10, "BOOT")
        data[256:512] = record(2, 20, "LED conn=1")
        data[280] ^= 1
        data[512:612] = record(3, 30, "INTERRUPTED")[:100]
        decoded, invalid = diagnostics.decode_records(data)
        self.assertEqual(len(decoded), 1)
        self.assertEqual(invalid, 2)

    def test_wrong_partition_length(self):
        with self.assertRaises(ValueError):
            diagnostics.decode_records(b"short")

    def test_foreign_data_is_not_a_crc_failure(self):
        data = bytearray(b"\xff" * diagnostics.FLASH_SIZE)
        data[:256] = b"\x3d\xf0" * 128
        data[256:512] = record(1, 10, "BOOT")
        data[280] ^= 1
        decoded, counts = diagnostics.decode_details(data)
        self.assertEqual(decoded, [])
        self.assertEqual(counts, {"non_record": 1, "crc_bad": 1, "erased": 2046})


if __name__ == "__main__":
    unittest.main()
