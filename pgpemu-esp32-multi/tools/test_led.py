"""Host tests execute the production C classifier (Linux/WSL with cc installed)."""
import collections
import ctypes
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest


def packet(*colors):
    data = bytearray([0, 0, 0, len(colors)])
    for color in colors:
        r, g, b = color
        data.extend([10, (g << 4) | r, b])
    return bytes(data)


OFF, RED, GREEN, BLUE, YELLOW, WHITE = (
    (0, 0, 0), (15, 0, 0), (0, 15, 0), (0, 0, 15), (15, 15, 0), (8, 8, 8))


class LedClassifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None or os.name == "nt":
            raise unittest.SkipTest("Run in Linux/WSL with cc to execute the production C classifier")
        cls.directory = tempfile.TemporaryDirectory(prefix="pgp-led-tests-")
        cls.addClassCleanup(cls.directory.cleanup)
        library = Path(cls.directory.name) / "led.so"
        source = Path(__file__).resolve().parents[1] / "main/pgp-led.c"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-fPIC", "-shared", str(source), "-o", str(library)], check=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.pgp_led_classify.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.native.pgp_led_classify.restype = ctypes.c_int
        cls.native.pgp_led_needs_button.argtypes = [ctypes.c_int]
        cls.native.pgp_led_needs_button.restype = ctypes.c_bool
        cls.native.pgp_led_event_name.argtypes = [ctypes.c_int]
        cls.native.pgp_led_event_name.restype = ctypes.c_char_p

    def classify(self, data):
        buffer = ctypes.create_string_buffer(data)
        event = self.native.pgp_led_classify(buffer, len(data))
        return (self.native.pgp_led_event_name(event).decode(),
                self.native.pgp_led_needs_button(event))

    def test_requests(self):
        for color, expected in [(GREEN, "pokemon"), (YELLOW, "new_pokemon"), (BLUE, "pokestop")]:
            with self.subTest(expected=expected):
                self.assertEqual(self.classify(packet(color, OFF, color)), (expected, True))

    def test_results_do_not_press(self):
        for colors, expected in [((WHITE, OFF, RED, GREEN, BLUE), "caught"),
                                 ((WHITE, OFF, RED), "fled"),
                                 ((RED, GREEN, BLUE), "spun")]:
            with self.subTest(expected=expected):
                self.assertEqual(self.classify(packet(*colors)), (expected, False))

    def test_errors_and_off_do_not_press(self):
        for colors, expected in [((WHITE, OFF), "bag_full"), ((RED,), "box_full"),
                                 ((RED, OFF), "balls_or_range"), ((OFF,), "off")]:
            with self.subTest(expected=expected):
                self.assertEqual(self.classify(packet(*colors)), (expected, False))

    def test_unknown_preserves_legacy_fallback(self):
        self.assertEqual(self.classify(packet((15, 0, 15))), ("unknown", True))
        self.assertEqual(self.classify(packet()), ("unknown", True))

    def test_invalid_never_presses(self):
        self.assertEqual(self.native.pgp_led_classify(None, 100), 0)
        for data in [b"", b"\0", b"\0\0\0", b"\0\0\0\x1f", packet(GREEN)[:-1]]:
            self.assertEqual(self.classify(data), ("invalid", False))

    def test_priority_vibration_and_interpolation_bits(self):
        data = bytearray(packet(GREEN))
        data[3] |= 0xe0
        data[6] |= 0xf0
        self.assertEqual(self.classify(bytes(data)), ("pokemon", True))

    def test_maximum_packet_and_truncation(self):
        data = packet(*([GREEN] * 31))
        self.assertEqual(len(data), 97)
        self.assertEqual(self.classify(data), ("pokemon", True))
        for end in range(len(data)):
            self.assertEqual(self.classify(data[:end]), ("invalid", False))

    def test_bounded_random_inputs(self):
        generator = random.Random(12345)
        for _ in range(2000):
            data = bytes(generator.randrange(256) for _ in range(generator.randrange(110)))
            name, press = self.classify(data)
            if len(data) < 4 or (data[3] & 31) > (len(data) - 4) // 3:
                self.assertEqual((name, press), ("invalid", False))

    @unittest.skipUnless(os.environ.get("PGP_LED_REPLAY"), "Optional private diagnostic replay")
    def test_private_field_log_replay(self):
        # No log fixtures or private exports are committed to the repository.
        text = Path(os.environ["PGP_LED_REPLAY"]).read_text(encoding="utf-8")
        counts = collections.Counter()
        for data in re.findall(r"\bLED conn=\d+ len=\d+ data=([0-9a-f]+)", text):
            counts[self.classify(bytes.fromhex(data))[0]] += 1
        self.assertEqual(counts, {"off": 34, "pokemon": 21, "bag_full": 14,
                                 "pokestop": 14, "caught": 7, "fled": 7,
                                 "balls_or_range": 4, "box_full": 2})


if __name__ == "__main__":
    unittest.main()
