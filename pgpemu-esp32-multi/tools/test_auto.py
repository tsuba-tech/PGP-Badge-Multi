"""Host tests execute the production C automation policy (Linux/WSL with cc installed)."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

(INVALID, UNKNOWN, OFF, POKEMON, NEW_POKEMON, POKESTOP, CAUGHT, FLED, SPUN,
 BAG_FULL, BOX_FULL, BALLS_OR_RANGE) = range(12)
BOX, BAG, NO_BALLS = 1, 2, 4


class Settings(ctypes.Structure):
    _fields_ = [("catch_enabled", ctypes.c_bool), ("spin_enabled", ctypes.c_bool),
                ("press_mode", ctypes.c_uint8), ("restock_spins", ctypes.c_uint8)]


class Client(ctypes.Structure):
    _fields_ = [("catch_enabled", ctypes.c_bool), ("spin_enabled", ctypes.c_bool),
                ("last_request", ctypes.c_int), ("restock_remaining", ctypes.c_uint8),
                ("alerts", ctypes.c_uint8)]


class AutomationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None or os.name == "nt":
            raise unittest.SkipTest("Run in Linux/WSL with cc to execute the production C policy")
        cls.directory = tempfile.TemporaryDirectory(prefix="pgp-auto-tests-")
        cls.addClassCleanup(cls.directory.cleanup)
        library = Path(cls.directory.name) / "auto.so"
        main = Path(__file__).resolve().parents[1] / "main"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-fPIC", "-shared",
                        str(main / "pgp-auto.c"), str(main / "pgp-led.c"),
                        "-o", str(library)], check=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.pgp_auto_reset.argtypes = [ctypes.POINTER(Client), ctypes.POINTER(Settings)]
        cls.native.pgp_auto_on_led.argtypes = [ctypes.POINTER(Client), ctypes.POINTER(Settings),
                                               ctypes.c_int]
        cls.native.pgp_auto_on_led.restype = ctypes.c_bool

    def setUp(self):
        self.settings = Settings(True, True, 0, 10)
        self.client = Client()
        self.native.pgp_auto_reset(ctypes.byref(self.client), ctypes.byref(self.settings))

    def led(self, *events):
        return [self.native.pgp_auto_on_led(ctypes.byref(self.client),
                                            ctypes.byref(self.settings), e) for e in events]

    def test_defaults_press_for_pokemon_and_stops(self):
        self.assertEqual(self.led(POKEMON, NEW_POKEMON, POKESTOP, UNKNOWN),
                         [True, True, True, True])
        self.assertEqual(self.led(CAUGHT, FLED, SPUN, OFF, INVALID), [False] * 5)

    def test_out_of_balls_pauses_catching_for_ten_spins(self):
        self.assertEqual(self.led(POKEMON, BALLS_OR_RANGE), [True, False])
        self.assertEqual((self.client.restock_remaining, self.client.alerts), (10, NO_BALLS))
        for spun in range(9):
            self.assertEqual(self.led(POKEMON, POKESTOP, SPUN), [False, True, False])
        self.assertEqual(self.client.restock_remaining, 1)
        self.led(POKESTOP, SPUN)
        self.assertEqual((self.client.restock_remaining, self.client.alerts), (0, 0))
        self.assertEqual(self.led(POKEMON), [True])

    def test_red_after_pokestop_is_out_of_range_not_out_of_balls(self):
        self.led(POKESTOP, BALLS_OR_RANGE)
        self.assertEqual((self.client.restock_remaining, self.client.alerts), (0, 0))

    def test_failed_spins_do_not_count(self):
        self.led(POKEMON, BALLS_OR_RANGE, POKESTOP, BALLS_OR_RANGE)
        self.assertEqual(self.client.restock_remaining, 10)

    def test_bag_full_ends_restocking_instead_of_waiting_forever(self):
        self.led(POKEMON, BALLS_OR_RANGE, POKESTOP, BAG_FULL)
        self.assertEqual((self.client.restock_remaining, self.client.alerts), (0, BAG))
        self.assertEqual(self.led(POKEMON), [True])

    def test_no_restock_without_spinning_or_when_disabled(self):
        self.client.spin_enabled = False
        self.led(POKEMON, BALLS_OR_RANGE)
        self.assertEqual((self.client.restock_remaining, self.client.alerts), (0, NO_BALLS))
        self.setUp()
        self.settings.restock_spins = 0
        self.led(POKEMON, BALLS_OR_RANGE)
        self.assertEqual(self.client.restock_remaining, 0)
        self.assertEqual(self.led(POKEMON), [True])

    def test_turning_spin_off_during_restock_resumes_catching(self):
        self.led(POKEMON, BALLS_OR_RANGE)
        self.client.spin_enabled = False
        self.assertEqual(self.led(POKEMON), [True])

    def test_alerts_set_and_clear(self):
        self.led(BOX_FULL, BAG_FULL)
        self.assertEqual(self.client.alerts, BOX | BAG)
        self.led(CAUGHT)
        self.assertEqual(self.client.alerts, BAG)
        self.led(SPUN)
        self.assertEqual(self.client.alerts, 0)

    def test_switches(self):
        self.client.catch_enabled = False
        self.assertEqual(self.led(POKEMON, POKESTOP, UNKNOWN), [False, True, True])
        self.client.spin_enabled = False
        self.assertEqual(self.led(POKEMON, POKESTOP, UNKNOWN), [False, False, False])


if __name__ == "__main__":
    unittest.main()
