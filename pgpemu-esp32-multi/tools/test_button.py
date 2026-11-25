"""Host tests execute the production C press-merging logic (Linux/WSL with cc installed)."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class Request(ctypes.Structure):
    _fields_ = [("pending", ctypes.c_bool), ("generation", ctypes.c_uint32),
                ("sequence", ctypes.c_uint32), ("expires_us", ctypes.c_int64),
                ("len", ctypes.c_uint8), ("data", ctypes.c_uint8 * 2)]


PRESS = (ctypes.c_uint8 * 2)(0x03, 0xFF)
RELEASE = (ctypes.c_uint8 * 2)(0x00, 0x00)


class ButtonRequestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None or os.name == "nt":
            raise unittest.SkipTest("Run in Linux/WSL with cc to execute the production C logic")
        cls.directory = tempfile.TemporaryDirectory(prefix="pgp-button-tests-")
        cls.addClassCleanup(cls.directory.cleanup)
        library = Path(cls.directory.name) / "button.so"
        source = Path(__file__).resolve().parents[1] / "main/pgp-button.c"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-fPIC", "-shared", str(source), "-o", str(library)], check=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.pgp_button_request.argtypes = [ctypes.POINTER(Request), ctypes.c_uint32,
                                                  ctypes.c_void_p, ctypes.c_uint8, ctypes.c_int64]
        cls.native.pgp_button_request.restype = ctypes.c_bool
        cls.native.pgp_button_complete.argtypes = [ctypes.POINTER(Request), ctypes.c_uint32]

    def request(self, state, generation, data, expires):
        return self.native.pgp_button_request(ctypes.byref(state), generation, data, 2, expires)

    def test_duplicate_press_merges_and_extends_deadline(self):
        state = Request()
        self.assertFalse(self.request(state, 1, PRESS, 1000))
        sequence = state.sequence
        self.assertTrue(self.request(state, 1, PRESS, 5000))
        self.assertTrue(self.request(state, 1, PRESS, 3000))
        self.assertEqual((state.sequence, state.expires_us), (sequence, 5000))
        self.native.pgp_button_complete(ctypes.byref(state), sequence)
        self.assertFalse(state.pending)

    def test_new_press_after_completion_is_sent_again(self):
        state = Request()
        self.request(state, 1, PRESS, 1000)
        self.native.pgp_button_complete(ctypes.byref(state), state.sequence)
        self.assertFalse(self.request(state, 1, PRESS, 2000))
        self.assertTrue(state.pending)

    def test_replacement_survives_completion_of_older_send(self):
        state = Request()
        self.request(state, 1, PRESS, 1000)
        in_flight = state.sequence
        self.assertFalse(self.request(state, 1, RELEASE, 1000))
        self.native.pgp_button_complete(ctypes.byref(state), in_flight)
        self.assertTrue(state.pending)
        self.assertEqual(bytes(state.data), b"\0\0")

    def test_new_connection_generation_never_merges(self):
        state = Request()
        self.request(state, 1, PRESS, 9000)
        self.assertFalse(self.request(state, 2, PRESS, 1000))
        self.assertEqual((state.generation, state.expires_us), (2, 1000))


if __name__ == "__main__":
    unittest.main()
