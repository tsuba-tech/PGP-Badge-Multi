"""Execute production handle_protocol control flow with mock crypto and queue.

This tests state transitions and connection isolation, not cryptographic correctness,
BLE delivery, scheduling, or hardware behavior. Run with Linux/WSL cc.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PREFIX = r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "pgp-cert.h"
typedef int esp_gatt_if_t;
typedef struct {
    int cert_state;
    uint8_t session_key[16], cert_buffer[378], reconnect_challenge[32];
    uint16_t cert_len;
    bool has_reconnect_key;
} pgp_client_t;
static pgp_client_t clients[2];
static bool accept_queue;
static int queue_conn, queue_calls;
static uint16_t certificate_handle_table[1] = {1};
#define IDX_CHAR_SFIDA_COMMANDS_VAL 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define esp_log_buffer_hex(...) ((void)0)
#define pgp_diag_log(...) ((void)0)
static pgp_client_t *get_client(int conn) { return conn >= 0 && conn < 2 ? &clients[conn] : 0; }
static void log_cert_transition(pgp_client_t *client, int state) { client->cert_state = state; }
static bool queue_certificate(int interface, uint16_t conn, uint16_t handle,
    uint16_t length, uint8_t *data, bool confirm) {
    queue_conn = conn; ++queue_calls; return accept_queue;
}
void generate_next_chal(const uint8_t *data, const uint8_t *key,
                       const uint8_t *nonce, struct next_challenge *output) {
    memset(output, 0, sizeof(*output));
}
int decrypt_next(const uint8_t *data, const uint8_t *key, uint8_t *output) {
    memset(output, 0, 16); return 0;
}
void generate_reconnect_response(const uint8_t *key, const uint8_t *challenge, uint8_t *output) {
    memset(output, 0, 16);
}
'''

SUFFIX = r'''
int test_protocol(int state, int accepted, int conn, int length) {
    memset(clients, 0x77, sizeof(clients));
    for (int i = 0; i < 2; ++i) {
        clients[i].cert_state = state;
        clients[i].has_reconnect_key = false;
    }
    pgp_client_t before[2]; memcpy(before, clients, sizeof(before));
    accept_queue = accepted; queue_conn = -1; queue_calls = 0;
    const uint8_t data[52] = {0};
    handle_protocol(1, data, length, conn);
    if (conn < 0 || conn >= 2) return memcmp(before, clients, sizeof(before)) ? -100 : state;
    if (memcmp(&before[1-conn], &clients[1-conn], sizeof(clients[0]))) return -101;
    if (queue_calls && queue_conn != conn) return -102;
    return clients[conn].cert_state;
}
int test_queue_calls(void) { return queue_calls; }
'''


class AuthenticationQueueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None or os.name == "nt":
            raise unittest.SkipTest("Run in Linux/WSL with cc")
        root = Path(__file__).resolve().parents[1]
        source = (root / "main/pgpemu.c").read_text(encoding="utf-8")
        start = source.index("void handle_protocol(")
        end = source.index("void handle_led_notify_from_app(", start)
        cls.directory = tempfile.TemporaryDirectory(prefix="pgp-auth-tests-")
        cls.addClassCleanup(cls.directory.cleanup)
        harness = Path(cls.directory.name) / "auth.c"
        harness.write_text(PREFIX + source[start:end] + SUFFIX, encoding="utf-8")
        library = Path(cls.directory.name) / "auth.so"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-sign-compare",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-fPIC", "-shared",
                        "-I", str(root / "main"), str(harness), "-o", str(library)], check=True)
        cls.native = ctypes.CDLL(str(library))
        cls.native.test_protocol.argtypes = [ctypes.c_int] * 4
        cls.native.test_protocol.restype = ctypes.c_int
        cls.native.test_queue_calls.restype = ctypes.c_int

    def test_accepted_queue_advances_only_originating_client(self):
        for state, next_state, length in [(0, 1, 20), (1, 2, 52), (2, 6, 52),
                                          (3, 4, 20), (4, 5, 20), (5, 6, 5)]:
            for conn in (0, 1):
                with self.subTest(state=state, conn=conn):
                    self.assertEqual(self.native.test_protocol(state, 1, conn, length), next_state)
                    self.assertEqual(self.native.test_queue_calls(), 1)

    def test_rejected_queue_never_advances_state(self):
        for state, length in [(0, 20), (1, 52), (2, 52), (3, 20), (4, 20), (5, 5)]:
            for conn in (0, 1):
                with self.subTest(state=state, conn=conn):
                    self.assertEqual(self.native.test_protocol(state, 0, conn, length), state)
                    self.assertEqual(self.native.test_queue_calls(), 1)

    def test_short_authentication_input_does_not_advance(self):
        for state, length in [(0, 19), (1, 51), (2, 51), (3, 19), (4, 19), (5, 4)]:
            self.assertEqual(self.native.test_protocol(state, 1, 0, length), state)
            self.assertEqual(self.native.test_queue_calls(), 0)

    def test_unknown_connection_does_not_touch_clients(self):
        self.assertEqual(self.native.test_protocol(1, 1, 999, 52), 1)
        self.assertEqual(self.native.test_queue_calls(), 0)


if __name__ == "__main__":
    unittest.main()
