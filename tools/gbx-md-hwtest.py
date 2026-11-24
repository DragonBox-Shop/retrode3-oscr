#!/usr/bin/env python3
"""Manual ROM-only GBx-0011 MD bench for the experimental Letux 7.3 kernel.
Python 3.4+. Never use as an automatic cart-family detector.
"""
from __future__ import print_function
import argparse
import os
import sys
import time
import zlib

DEV = "/dev/slot-md"
VCC = "/sys/class/retrode3/slot-md/vcc"
PARAM = "/sys/module/retrode3/parameters/gbx_md_test"
GB = 23
GBA_LOW = 24
GBA_HIGH = 25
GB_PINS = 26  # diagnostic GPIO mask (A8..A18), GB address * 2
GBA_TRACE = 27  # 8 byte record per ROM word, four 16-bit GPIO snapshots
GBA_BURST_LOW = 28  # optional sequential burst, benchmark against mode 24
GBA_BURST_HIGH = 29  # optional sequential burst, benchmark against mode 25
WINDOW = 0x1000000


def voltage(mv):
    with open(VCC, "w") as file:
        file.write("%d\n" % mv)
    with open(VCC, "r") as file:
        actual = file.read().strip()
    if actual != "%dmV" % mv:
        raise RuntimeError("MD voltage mismatch: " + actual)


def get(fd, mode, addr, count):
    if addr < 0 or addr + count > WINDOW:
        raise ValueError("Read crosses GBx 16 MiB window")
    data = os.pread(fd, count, (mode << 24) | addr)
    if len(data) != count:
        raise IOError("Short GBx read (%d/%d)" % (len(data), count))
    return data


def probe_gb(fd):
    h = get(fd, GB, 0x100, 0x50)
    chk = 0
    for b in bytearray(h[0x34:0x4d]):
        chk = (chk - b - 1) & 255
    if chk != h[0x4d]:
        raise RuntimeError("GB/GBC header checksum failed (%02x/%02x)" %
                           (chk, h[0x4d]))
    title = h[0x34:0x44].split(b"\0", 1)[0].strip().decode("ascii", "replace")
    return (title, h[0x47], h[0x48], h[0x49], h[0x4d])


def probe_gba(fd, mode=GBA_LOW):
    h = get(fd, mode, 0, 0xc0)
    if sum(bytearray(h[4:0xa0])) != 0x4b1b:
        raise RuntimeError("GBA logo sum failed")
    chk = (-sum(bytearray(h[0xa0:0xbd])) - 0x19) & 255
    if chk != h[0xbd]:
        raise RuntimeError("GBA header checksum failed (%02x/%02x)" %
                           (chk, h[0xbd]))
    title = h[0xa0:0xac].split(b"\0", 1)[0].strip().decode("ascii", "replace")
    code = h[0xac:0xb0].decode("ascii", "replace")
    return (title, code, h[0xbd])


def diagnose_gb_pins(fd):
    # Nintendo logo provides a known alternating D7 pattern.
    # This diagnostic reads physical input GPIO levels during GB /RD.
    expected = (
        (0x0100, 0x00), (0x0101, 0xc3),
        (0x0104, 0xce), (0x0105, 0xed),
        (0x0106, 0x66), (0x0107, 0x66),
        (0x0108, 0xcc), (0x0109, 0x0d),
        (0x010a, 0x00), (0x010b, 0x0b),
        (0x010f, 0x83), (0x0118, 0x88),
        (0x0119, 0x89), (0x0134, 0x50))
    print("GB INPUT GPIO SNAPSHOT: A8..A18, 3.3V, /RD active")
    print("Address  Expected  D7  MD pins HIGH (A8..A18)   mask")
    for addr, known in expected:
        raw = get(fd, GB_PINS, addr * 2, 2)
        mask = raw[0] | (raw[1] << 8)
        high = ",".join("A%d" % pin for pin in range(8, 19)
                        if mask & (1 << (pin - 8)))
        print("%04X       %02X        %d   %-27s %03X" %
              (addr, known, known >> 7, high or "(none)", mask))


def diagnose_gba_timing(fd):
    """Read the four GBA data GPIO states in the same /CS + /RD cycle."""
    addresses = (0x0000, 0x0002, 0x0004, 0x0006, 0x0008,
                 0x000A, 0x00A0, 0x00AC, 0x00B0)
    print("GBA ROM bus trace at 3.3V (16-bit GPIO values)")
    print("Byte addr  Address driven  /CS low  Inputs /RD high  /RD low")
    for byte_addr in addresses:
        # Mode-27 address = 8 bytes per physical GBA 16-bit word.
        data = bytearray(get(fd, GBA_TRACE, (byte_addr // 2) * 8, 8))
        values = tuple(data[i] | (data[i + 1] << 8)
                       for i in range(0, 8, 2))
        print("%08X      %04X            %04X      %04X             %04X" %
              ((byte_addr,) + values))
    print("If /RD low still reads FFFF after inputs are released,")
    print("the cartridge is not driving MD D0..D15 in this cycle.")


def dump(fd, family, length, path, fast=False):
    partial = path + ".partial"
    if os.path.exists(path) or os.path.exists(partial):
        raise RuntimeError("Output or partial file already exists")
    crc = 0
    try:
        with open(partial, "xb") as output:
            for offset in range(0, length, 512):
                if family == "gb":
                    buf = get(fd, GB, offset, 512)
                else:
                    mode = ((GBA_BURST_LOW if offset < WINDOW
                             else GBA_BURST_HIGH) if fast
                            else (GBA_LOW if offset < WINDOW else GBA_HIGH))
                    buf = get(fd, mode, offset % WINDOW, 512)
                output.write(buf)
                crc = zlib.crc32(buf, crc)
            output.flush()
            os.fsync(output.fileno())
        os.rename(partial, path)
        return crc & 0xffffffff
    except BaseException:
        if os.path.exists(partial):
            os.unlink(partial)
        raise


def main():
    parser = argparse.ArgumentParser(description="GBx-0011 MD header test")
    parser.add_argument("system", choices=("arm", "gb", "gba"))
    parser.add_argument("--dump", help="ROM output path; optional")
    parser.add_argument("--raw", action="store_true",
                        help="Print raw header bytes even if header validation fails")
    parser.add_argument("--pins", action="store_true",
                        help="GB only: inspect read-only GPIO A8..A18 during /RD (kernel mode 26)")
    parser.add_argument("--trace", action="store_true",
                        help="GBA only: show four data-bus stages per ROM word (kernel mode 27)")
    parser.add_argument("--fast", action="store_true",
                        help="GBA only: experimental sequential burst reader (modes 28/29)")
    parser.add_argument("--mib", type=int, choices=(1, 2, 4, 8, 16, 32),
                        help="GBA ROM size, required for GBA dump")
    a = parser.parse_args()
    if a.system == "arm" and (a.dump or a.mib or a.raw or a.pins or a.trace or a.fast):
        parser.error("arm takes no dump parameters")
    if a.system == "gba" and a.dump and not a.mib:
        parser.error("GBA dump requires --mib")
    if a.system != "gba" and a.mib:
        parser.error("--mib is only for GBA")
    if a.system != "gb" and a.pins:
        parser.error("--pins only works for GB/GBC")
    if a.system != "gba" and a.trace:
        parser.error("--trace only works for GBA")
    if a.system != "gba" and a.fast:
        parser.error("--fast only works for GBA")

    # Both header probes ALWAYS use 3.3 V.
    voltage(3300)
    if a.system == "arm":
        # No GB/GBC/GBA cartridge may be inserted during arm.
        # Some images forbid runtime writes to module parameters. In that
        # case load the test .ko with gbx_md_test=1 and use this arm command.
        with open(PARAM, "r") as file:
            armed = file.read().strip().lower() in ("1", "y", "yes")
        if not armed:
            try:
                with open(PARAM, "w") as file:
                    file.write("1\n")
            except OSError as error:
                raise RuntimeError(
                    "Cannot enable gbx_md_test through sysfs (%s). "
                    "With the cartridge and adapter removed, reload the "
                    "test module using: insmod "
                    "/root/retrode3-gbx-test.ko gbx_md_test=1; "
                    "then rerun this arm command." % error)
        with open(PARAM, "r") as file:
            if file.read().strip().lower() not in ("1", "y", "yes"):
                raise RuntimeError("gbx_md_test did not become active")
        fd = os.open(DEV, os.O_RDONLY)
        try:
            get(fd, GBA_LOW, 0, 1)  # first cycle makes A19 high-Z
        finally:
            os.close(fd)
        print("GBx bench armed at 3.3 V, A19 high-Z.")
        print("Insert the GB/GBC or GBA cartridge, then run the header test.")
        return

    if a.system == "gba":
        # Match Sanni GameBoyAdvance::setROM() initial 500ms settle.
        # This is a delay only; no voltage or writing changes.
        time.sleep(0.5)

    fd = os.open(DEV, os.O_RDONLY)
    try:
        if a.trace:
            diagnose_gba_timing(fd)
        if a.raw:
            mode, pos, n = ((GB, 0x100, 0x50) if a.system == "gb"
                            else (GBA_BURST_LOW if a.fast else GBA_LOW,
                                  0, 0xc0))
            raw = get(fd, mode, pos, n)
            for i in range(0, n, 16):
                print("%04X: %s" % (pos + i,
                      " ".join("%02X" % byte for byte in
                               bytearray(raw[i:i + 16]))))
        if a.system == "gb":
            if a.pins:
                diagnose_gb_pins(fd)
            header = probe_gb(fd)
            print("GB/GBC at 3.3V: title=%r type=%02X ROM=%02X RAM=%02X" %
                  header[:4])
            if a.dump:
                if header[1] not in (0, 8, 9) or header[2] != 0:
                    raise RuntimeError("Stage 1 supports 32 KiB no-MBC only")
                try:
                    voltage(5000)
                    if probe_gb(fd) != header:
                        raise RuntimeError("GB header changed at 5 V")
                    crc = dump(fd, "gb", 32768, a.dump)
                finally:
                    voltage(3300)
                print("Wrote 32768 bytes: %s CRC32=%08X" % (a.dump, crc))
        else:
            mode = GBA_BURST_LOW if a.fast else GBA_LOW
            header = probe_gba(fd, mode)
            print("GBA at 3.3V: title=%r game_code=%s mode=%s" %
                  (header[0], header[1], "burst" if a.fast else "legacy"))
            if a.dump:
                if probe_gba(fd, mode) != header:
                    raise RuntimeError("GBA header changed")
                crc = dump(fd, "gba", a.mib * 1024 * 1024,
                           a.dump, fast=a.fast)
                print("Wrote %d MiB: %s CRC32=%08X" % (a.mib, a.dump, crc))
    finally:
        os.close(fd)


if __name__ == "__main__":
    try:
        main()
    except (OSError, IOError, ValueError, RuntimeError) as error:
        print("GBx test failed: " + str(error), file=sys.stderr)
        sys.exit(1)
