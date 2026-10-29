"""Real C relay/recorder tests over loopback UDP, with no tuner or VLC.

These exercise byte copying, not RF reception or decoding a television service.
"""
import ctypes
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load_library(path):
    library = ctypes.CDLL(str(path))
    library.tsrelay_start.argtypes = [ctypes.c_uint16, ctypes.c_uint16]
    library.tsrelay_start.restype = ctypes.c_int
    library.tsrelay_start_recording.argtypes = [ctypes.c_char_p]
    library.tsrelay_start_recording.restype = ctypes.c_int
    library.tsrelay_stop_recording.restype = ctypes.c_int
    library.tsrelay_is_recording.restype = ctypes.c_int
    library.tsrelay_recorded_bytes.restype = ctypes.c_uint64
    library.tsrelay_forward_errors.restype = ctypes.c_uint64
    library.tsrelay_copy_error.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    return library


class TSRecorderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("clang") or shutil.which("cc")
        if not cls.compiler:
            raise unittest.SkipTest("C compiler unavailable")
        cls.build = tempfile.TemporaryDirectory()
        cls.library_path = Path(cls.build.name) / (
            "tsrecorder.dylib" if sys.platform == "darwin" else "tsrecorder.so")
        subprocess.run([
            cls.compiler, "-shared", "-fPIC", "-std=c11", "-D_DEFAULT_SOURCE",
            "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I", str(ROOT / "Sources/TSRecorder/include"),
            str(ROOT / "Sources/TSRecorder/tsrecorder.c"),
            "-o", str(cls.library_path),
        ], check=True)
        cls.library = load_library(cls.library_path)

    @classmethod
    def tearDownClass(cls):
        cls.library.tsrelay_shutdown()
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="PT682C grabación ")
        self.target = Path(self.temp.name) / "canal de prueba.ts"
        self.destination = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.destination.bind(("127.0.0.1", 0))
        self.destination.settimeout(2)
        self.output = self.destination.getsockname()[1]
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(("127.0.0.1", 0))
        self.input = reserve.getsockname()[1]
        reserve.close()
        self.sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.assertEqual(self.library.tsrelay_start(self.input, self.output), 0)

    def tearDown(self):
        self.library.tsrelay_shutdown()
        self.sender.close()
        self.destination.close()
        self.temp.cleanup()

    def packet(self, seed):
        # Seven syntactically packetized null TS packets per receiver datagram.
        return b"".join(
            b"\x47\x1f\xff" + bytes([0x10 | (i & 15)]) + bytes([seed]) * 184
            for i in range(7))

    def exchange(self, data):
        self.sender.sendto(data, ("127.0.0.1", self.input))
        self.assertEqual(self.destination.recv(65536), data)

    def wait_for(self, condition):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if condition():
                return
            time.sleep(0.005)
        self.fail("Worker did not reach the expected state within 2 seconds")

    def test_live_relay_does_not_create_recording(self):
        self.exchange(self.packet(1))
        self.assertFalse(self.library.tsrelay_is_recording())
        self.assertFalse(self.target.exists())
        self.assertEqual(self.library.tsrelay_forward_errors(), 0)

    def test_ts_is_byte_exact_and_playback_continues_after_stop(self):
        self.assertEqual(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        expected = b""
        for seed in range(20):
            data = self.packet(seed)
            expected += data
            self.exchange(data)
        self.wait_for(lambda: self.library.tsrelay_recorded_bytes() == len(expected))
        self.assertEqual(self.library.tsrelay_stop_recording(), 0)
        self.assertEqual(self.target.read_bytes(), expected)
        self.exchange(self.packet(99))
        self.assertEqual(self.target.read_bytes(), expected)
        self.assertFalse(self.library.tsrelay_is_recording())

    def test_refuses_overwrite_and_symlink(self):
        self.target.write_bytes(b"keep this existing file")
        self.assertLess(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        self.assertEqual(self.target.read_bytes(), b"keep this existing file")
        link = self.target.with_name("link.ts")
        link.symlink_to(self.target)
        self.assertLess(self.library.tsrelay_start_recording(os.fsencode(link)), 0)
        self.assertEqual(self.target.read_bytes(), b"keep this existing file")
        self.exchange(self.packet(2))

    def test_multiple_recordings_do_not_mix(self):
        second = self.target.with_name("segundo.ts")
        for path, seed in ((self.target, 3), (second, 4)):
            data = self.packet(seed)
            self.assertEqual(self.library.tsrelay_start_recording(os.fsencode(path)), 0)
            self.exchange(data)
            self.wait_for(lambda: self.library.tsrelay_recorded_bytes() == len(data))
            self.assertEqual(self.library.tsrelay_stop_recording(), 0)
            self.assertEqual(path.read_bytes(), data)
        self.assertNotEqual(self.target.read_bytes(), second.read_bytes())

    def test_second_rec_fails_without_replacing_first(self):
        self.assertEqual(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        other = self.target.with_name("otra.ts")
        self.assertLess(self.library.tsrelay_start_recording(os.fsencode(other)), 0)
        data = self.packet(5)
        self.exchange(data)
        self.wait_for(lambda: self.library.tsrelay_recorded_bytes() == len(data))
        self.assertEqual(self.library.tsrelay_stop_recording(), 0)
        self.assertEqual(self.target.read_bytes(), data)
        self.assertFalse(other.exists())

    def test_close_while_recording_finishes_file_and_reopens_relay(self):
        self.assertEqual(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        data = self.packet(6)
        self.exchange(data)
        self.wait_for(lambda: self.library.tsrelay_recorded_bytes() == len(data))
        self.library.tsrelay_shutdown()
        self.assertFalse(self.library.tsrelay_is_recording())
        self.assertEqual(self.target.read_bytes(), data)
        self.assertEqual(self.library.tsrelay_start(self.input, self.output), 0)
        self.exchange(self.packet(7))

    def test_port_conflict_fails_explicitly(self):
        self.library.tsrelay_shutdown()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as owner:
            owner.bind(("127.0.0.1", self.input))
            self.assertLess(self.library.tsrelay_start(self.input, self.output), 0)
            message = ctypes.create_string_buffer(1024)
            self.library.tsrelay_copy_error(message, len(message))
            self.assertIn(b"UDP", message.value)

    def test_same_relay_start_is_idempotent_but_cannot_change_ports(self):
        self.assertEqual(self.library.tsrelay_start(self.input, self.output), 0)
        self.assertLess(self.library.tsrelay_start(self.input, self.output - 1), 0)
        self.exchange(self.packet(8))

    def test_start_without_relay_and_invalid_paths(self):
        self.assertLess(self.library.tsrelay_start_recording(b""), 0)
        self.assertLess(self.library.tsrelay_start_recording(
            os.fsencode(self.target / "nonexistent.ts")), 0)
        self.library.tsrelay_shutdown()
        self.assertLess(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        self.assertFalse(self.target.exists())

    def test_empty_recording_and_permissions(self):
        self.assertEqual(self.library.tsrelay_start_recording(os.fsencode(self.target)), 0)
        self.assertEqual(self.library.tsrelay_stop_recording(), 0)
        self.assertEqual(self.target.stat().st_size, 0)
        self.assertEqual(self.target.stat().st_mode & 0o777, 0o600)

    @unittest.skipUnless(sys.platform.startswith("linux"), "RLIMIT_FSIZE failure fixture on Linux")
    def test_write_failure_closes_file_but_preserves_live_relay(self):
        script = r"""
import ctypes, resource, signal, socket, sys, time
lib = ctypes.CDLL(sys.argv[1])
lib.tsrelay_start_recording.argtypes = [ctypes.c_char_p]
lib.tsrelay_recorded_bytes.restype = ctypes.c_uint64
lib.tsrelay_copy_error.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
resource.setrlimit(resource.RLIMIT_FSIZE, (512, 512))
out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
out.bind(("127.0.0.1", 0))
out.settimeout(2)
reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
reserve.bind(("127.0.0.1", 0))
port = reserve.getsockname()[1]
reserve.close()
assert lib.tsrelay_start(port, out.getsockname()[1]) == 0
assert lib.tsrelay_start_recording(sys.argv[2].encode()) == 0
sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
data = b"\x47" * 1316
sender.sendto(data, ("127.0.0.1", port))
assert out.recv(65536) == data
deadline = time.monotonic() + 2
while lib.tsrelay_is_recording() and time.monotonic() < deadline:
    time.sleep(.005)
assert not lib.tsrelay_is_recording()
assert lib.tsrelay_recorded_bytes() == 512
message = ctypes.create_string_buffer(1024)
lib.tsrelay_copy_error(message, len(message))
assert b"Error escribiendo TS" in message.value, message.value
sender.sendto(b"still live", ("127.0.0.1", port))
assert out.recv(65536) == b"still live"
assert lib.tsrelay_stop_recording() < 0
lib.tsrelay_shutdown()
"""
        subprocess.run([sys.executable, "-c", script, str(self.library_path),
                        str(self.target)], check=True, timeout=10)
        self.assertEqual(self.target.stat().st_size, 512)


class GUIRecordingWiringTests(unittest.TestCase):
    def test_existing_receiver_and_vlc_shim_remain_unchanged(self):
        source = (ROOT / "Sources/PT682CTV/AppState.swift").read_text()
        self.assertIn("tsrelay_start(1234, 1235)", source)
        self.assertNotIn('vlcshim_play("udp://@127.0.0.1:1234"', source)
        self.assertEqual(source.count('vlcshim_play("udp://@127.0.0.1:1235"'), 2)
        self.assertIn("func applicationWillTerminate", (
            ROOT / "Sources/PT682CTV/AppDelegate.swift").read_text())

    def test_rec_and_stop_are_distinct_from_tv_stop(self):
        source = (ROOT / "Sources/PT682CTV/ContentView.swift").read_text()
        self.assertIn('Label("REC"', source)
        self.assertIn('Label("STOP"', source)
        self.assertIn('Button("Detener TV")', source)
        self.assertIn(".disabled(!state.canRecord)", source)
        self.assertIn(".disabled(!state.isRecording)", source)


if __name__ == "__main__":
    unittest.main()
