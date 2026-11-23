"""Isolated hardware-free ABI and stream checks for GBx-0011 GBA v3 plugin.

Build this fixture with -DRETRODE3_TESTING; production has no device
or sysfs environment overrides. These tests do not validate a real cartridge.
"""
import ctypes
import json
import os
import tempfile
import unittest

MODULE = os.environ.get("RETRODE3_GBA_TEST_MODULE", "build/migration/gba-md-test.so")
HEADER = bytes.fromhex("""
2E 00 00 EA 24 FF AE 51 69 9A A2 21 3D 84 82 0A
84 E4 09 AD 11 24 8B 98 C0 81 7F 21 A3 52 BE 19
93 09 CE 20 10 46 4A 4A F8 27 31 EC 58 C7 E8 33
82 E3 CE BF 85 F4 DF 94 CE 4B 09 C1 94 56 8A C0
13 72 A7 FC 9F 84 4D 73 A3 CA 9A 61 58 97 A3 27
FC 03 98 76 23 1D C7 61 03 04 AE 56 BF 38 84 00
40 A7 0E FD FF 52 FE 03 6F 95 30 F1 97 FB C0 85
60 D6 80 25 A9 63 BE 03 01 4E 38 E2 F9 A2 34 FF
BB 3E 03 44 78 00 90 CB 88 11 3A 94 65 C0 7C 63
87 F0 3C AF D6 25 E4 8B 38 0A AC 72 21 D4 F8 07
53 48 49 4E 49 4E 47 46 4F 52 43 45 41 46 35 50
38 50 96 00 00 00 00 00 00 00 00 00 00 3E 00 00
""")
assert len(HEADER) == 192

u32 = ctypes.c_uint32
u64 = ctypes.c_uint64
size = ctypes.c_size_t
pchar = ctypes.POINTER(ctypes.c_char)

class Param(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char * 48), ("type", u32),
                ("integer_value", u32), ("string_value", ctypes.c_char * 128)]

class Selection(ctypes.Structure):
    _fields_ = [("struct_size", u32), ("protocol_version", u32),
                ("mode", u32), ("schema_id", ctypes.c_char * 64),
                ("candidate_id", ctypes.c_char * 64),
                ("layout_id", ctypes.c_char * 64), ("parameter_count", u32),
                ("parameters", Param * 32)]

class Plan(ctypes.Structure):
    _fields_ = [("struct_size", u32), ("payload_bytes", u64),
                ("prefix_bytes", u32)]

class Result(ctypes.Structure):
    _fields_ = [("struct_size", u32), ("prefix_bytes", u32),
                ("prefix", ctypes.c_ubyte * 64),
                ("payload_crc_known", ctypes.c_uint8),
                ("payload_crc32", u32),
                ("details_json", ctypes.c_char * 4096)]

WRITE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, size)
CANCEL = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)

class HostIO(ctypes.Structure):
    _fields_ = [("abi_version", u32), ("struct_size", u32),
                ("context", ctypes.c_void_p), ("write", WRITE),
                ("cancelled", CANCEL)]

class Ops(ctypes.Structure):
    _fields_ = [("abi_version", u32), ("struct_size", u32),
                ("capabilities_json", ctypes.c_void_p),
                ("fast_probe_json", ctypes.c_void_p),
                ("plan_read", ctypes.c_void_p),
                ("read_rom", ctypes.c_void_p)]

JSONFN = ctypes.CFUNCTYPE(ctypes.c_int, pchar, size, pchar, size)
PLANFN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(Selection),
                          ctypes.POINTER(Plan), pchar, size)
READFN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(Selection),
                          ctypes.POINTER(HostIO), ctypes.POINTER(Result),
                          pchar, size)

class CoreTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = self.temp.name
        self.param = os.path.join(root, "module/retrode3/parameters/gbx_md_test")
        self.vcc = os.path.join(root, "class/retrode3/slot-md/vcc")
        os.makedirs(os.path.dirname(self.param))
        os.makedirs(os.path.dirname(self.vcc))
        self.write_state(self.param, "0\n")
        self.write_state(self.vcc, "3300mV\n")
        self.device = os.path.join(root, "slot-md-test")
        # A regular sparse file replaces kernel read modes 24 and 28.
        self.data = bytearray((i * 37 + 11) & 255 for i in range(1024 * 1024))
        self.data[:192] = HEADER
        with open(self.device, "wb") as out:
            out.seek(24 << 24)
            out.write(HEADER)
            out.seek(28 << 24)
            out.write(self.data)
        os.environ["RETRODE3_GBA_TEST_STATE_ROOT"] = root
        os.environ["RETRODE3_GBA_TEST_DEVICE"] = self.device
        lib = ctypes.CDLL(MODULE)
        lib.retrode3_core_get_operations_v3.restype = ctypes.POINTER(Ops)
        ops = lib.retrode3_core_get_operations_v3().contents
        self.assertEqual(ops.abi_version, 3)
        self.cap = JSONFN(ops.capabilities_json)
        self.probe = JSONFN(ops.fast_probe_json)
        self.plan = PLANFN(ops.plan_read)
        self.read = READFN(ops.read_rom)
        self.lib = lib

    def tearDown(self):
        for key in ("RETRODE3_GBA_TEST_STATE_ROOT", "RETRODE3_GBA_TEST_DEVICE"):
            os.environ.pop(key, None)
        self.temp.cleanup()

    def write_state(self, path, text):
        with open(path, "w") as out:
            out.write(text)

    def json_call(self, callback):
        out = ctypes.create_string_buffer(4096)
        err = ctypes.create_string_buffer(256)
        code = callback(out, len(out), err, len(err))
        return code, json.loads(out.value.decode("utf-8")) if code == 0 else err.value.decode("utf-8")

    def selection(self, mode=2, name=b"gba-1m"):
        x = Selection()
        x.struct_size = ctypes.sizeof(x)
        x.protocol_version = 1
        x.mode = mode
        x.schema_id = b"gba-md-size-v1"
        x.layout_id = name
        return x

    def call_plan(self, choice):
        plan = Plan()
        plan.struct_size = ctypes.sizeof(plan)
        err = ctypes.create_string_buffer(256)
        code = self.plan(ctypes.byref(choice), ctypes.byref(plan), err, len(err))
        return code, plan, err.value.decode("utf-8")

    def test_capabilities_are_metadata_only(self):
        code, data = self.json_call(self.cap)
        self.assertEqual(code, 0)
        self.assertEqual(data["schema_id"], "gba-md-size-v1")
        self.assertEqual(data["probe_voltage_mv"], 3300)
        self.assertFalse(data["automatic_voltage_switch"])
        self.assertNotIn("hardware_access", data)
        self.assertFalse(data["write_operations"])

    def test_requires_manual_arm_and_3300_mv(self):
        code, message = self.json_call(self.probe)
        self.assertNotEqual(code, 0)
        self.assertIn("not enabled", message)
        self.write_state(self.param, "1\n")
        self.write_state(self.vcc, "5000mV\n")
        code, message = self.json_call(self.probe)
        self.assertNotEqual(code, 0)
        self.assertIn("3300", message)
        self.write_state(self.vcc, "3300mV\n")
        code, data = self.json_call(self.probe)
        self.assertEqual(code, 0)
        self.assertTrue(data["system_detected"])
        self.assertTrue(data["selection_required"])
        self.assertEqual(data["game_code"], "AF5P")
        self.assertEqual([x["size_bytes"] for x in data["read_layouts"]],
                         [1048576, 2097152, 4194304, 8388608, 16777216])

    def test_selection_and_stream(self):
        self.write_state(self.param, "1\n")
        bad = self.selection(mode=0, name=b"")
        self.assertNotEqual(self.call_plan(bad)[0], 0)
        bad = self.selection(mode=2, name=b"gba-32m")
        self.assertNotEqual(self.call_plan(bad)[0], 0)
        bad = self.selection(mode=3, name=b"")
        bad.parameter_count = 1
        bad.parameters[0].id = b"rom_mib"
        bad.parameters[0].type = 1
        bad.parameters[0].integer_value = 32
        self.assertNotEqual(self.call_plan(bad)[0], 0)
        good = self.selection()
        code, plan, err = self.call_plan(good)
        self.assertEqual(code, 0, err)
        self.assertEqual(plan.payload_bytes, 1048576)
        self.assertEqual(plan.prefix_bytes, 0)
        output = bytearray()
        @WRITE
        def write(ctx, ptr, count):
            output.extend(ctypes.string_at(ptr, count))
            return 0
        @CANCEL
        def not_cancelled(ctx):
            return 0
        io = HostIO(1, ctypes.sizeof(HostIO), None, write, not_cancelled)
        result = Result()
        result.struct_size = ctypes.sizeof(result)
        errors = ctypes.create_string_buffer(256)
        code = self.read(ctypes.byref(good), ctypes.byref(io),
                         ctypes.byref(result), errors, len(errors))
        self.assertEqual(code, 0, errors.value.decode("utf-8"))
        self.assertEqual(output, self.data)
        details = json.loads(result.details_json.decode("utf-8"))
        self.assertEqual(details["verification"]["status"], "not_checked")
        self.assertFalse(details["verification"]["verified"])
        self.assertEqual(details["read_configuration"]["rom_mib"], 1)
        self.assertEqual(result.payload_crc_known, 0)

    def test_cancellation_and_corrupt_header(self):
        self.write_state(self.param, "1\n")
        good = self.selection()
        output = bytearray()
        @WRITE
        def write(ctx, ptr, count):
            output.extend(ctypes.string_at(ptr, count))
            return 0
        @CANCEL
        def cancelled(ctx):
            return 1 if len(output) >= 512 else 0
        io = HostIO(1, ctypes.sizeof(HostIO), None, write, cancelled)
        result = Result()
        result.struct_size = ctypes.sizeof(result)
        errors = ctypes.create_string_buffer(256)
        code = self.read(ctypes.byref(good), ctypes.byref(io),
                         ctypes.byref(result), errors, len(errors))
        self.assertNotEqual(code, 0)
        self.assertEqual(len(output), 512)
        self.assertIn("cancelled", errors.value.decode("utf-8"))
        with open(self.device, "r+b") as f:
            f.seek((24 << 24) + 0xbd)
            f.write(b"\x00")
        code, error = self.json_call(self.probe)
        self.assertNotEqual(code, 0)
        self.assertIn("invalid GBA", error)

if __name__ == "__main__":
    unittest.main()
