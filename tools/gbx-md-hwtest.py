#!/usr/bin/env python3
"""Manual ROM-only GBx-0011 MD bench for the experimental Letux 7.3 kernel.
Python 3.4+. Never use as an automatic cart-family detector.
"""
from __future__ import print_function
import argparse
import os
import sys

DEV = "/dev/slot-md"
VCC = "/sys/class/retrode3/slot-md/vcc"
PARAM = "/sys/module/retrode3/parameters/gbx_md_test"
GB = 23
GBA_LOW = 24
GBA_HIGH = 25
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


def probe_gba(fd):
    h = get(fd, GBA_LOW, 0, 0xc0)
    if sum(bytearray(h[4:0xa0])) != 0x4b1b:
        raise RuntimeError("GBA logo sum failed")
    chk = (-sum(bytearray(h[0xa0:0xbd])) - 0x19) & 255
    if chk != h[0xbd]:
        raise RuntimeError("GBA header checksum failed (%02x/%02x)" %
                           (chk, h[0xbd]))
    title = h[0xa0:0xac].split(b"\0", 1)[0].strip().decode("ascii", "replace")
    code = h[0xac:0xb0].decode("ascii", "replace")
    return (title, code, h[0xbd])


def dump(fd, family, length, path):
    partial = path + ".partial"
    if os.path.exists(path) or os.path.exists(partial):
        raise RuntimeError("Output or partial file already exists")
    try:
        with open(partial, "xb") as output:
            for offset in range(0, length, 512):
                if family == "gb":
                    buf = get(fd, GB, offset, 512)
                else:
                    mode = GBA_LOW if offset < WINDOW else GBA_HIGH
                    buf = get(fd, mode, offset % WINDOW, 512)
                output.write(buf)
            output.flush()
            os.fsync(output.fileno())
        os.rename(partial, path)
    except BaseException:
        if os.path.exists(partial):
            os.unlink(partial)
        raise


def main():
    parser = argparse.ArgumentParser(description="GBx-0011 MD header test")
    parser.add_argument("system", choices=("arm", "gb", "gba"))
    parser.add_argument("--dump", help="ROM output path; optional")
    parser.add_argument("--mib", type=int, choices=(1, 2, 4, 8, 16, 32),
                        help="GBA ROM size, required for GBA dump")
    a = parser.parse_args()
    if a.system == "arm" and (a.dump or a.mib):
        parser.error("arm takes no dump parameters")
    if a.system == "gba" and a.dump and not a.mib:
        parser.error("GBA dump requires --mib")
    if a.system != "gba" and a.mib:
        parser.error("--mib is only for GBA")

    # Both header probes ALWAYS use 3.3 V.
    voltage(3300)
    if a.system == "arm":
        # No GB/GBC/GBA cartridge may be inserted during arm.
        with open(PARAM, "w") as file:
            file.write("1\n")
        fd = os.open(DEV, os.O_RDONLY)
        try:
            get(fd, GBA_LOW, 0, 1)  # first cycle makes A19 high-Z
        finally:
            os.close(fd)
        print("GBx bench armed at 3.3 V, A19 high-Z.")
        print("Insert the GB/GBC or GBA cartridge, then run the header test.")
        return

    fd = os.open(DEV, os.O_RDONLY)
    try:
        if a.system == "gb":
            header = probe_gb(fd)
            print("GB/GBC at 3.3V: title=%r type=%02X ROM=%02X RAM=%02X" %
                  header[:4])
            if a.dump:
                if header[1] not in (0, 8, 9) or header[2] != 0:
                    raise RuntimeError("Stage 1 supports 32 KiB no-MBC only")
                voltage(5000)
                try:
                    if probe_gb(fd) != header:
                        raise RuntimeError("GB header changed at 5 V")
                    dump(fd, "gb", 32768, a.dump)
                finally:
                    voltage(3300)
                print("Wrote 32768 bytes: " + a.dump)
        else:
            header = probe_gba(fd)
            print("GBA at 3.3V: title=%r game_code=%s" % header[:2])
            if a.dump:
                if probe_gba(fd) != header:
                    raise RuntimeError("GBA header changed")
                dump(fd, "gba", a.mib * 1024 * 1024, a.dump)
                print("Wrote %d MiB: %s" % (a.mib, a.dump))
    finally:
        os.close(fd)


if __name__ == "__main__":
    try:
        main()
    except (OSError, IOError, ValueError, RuntimeError) as error:
        print("GBx test failed: " + str(error), file=sys.stderr)
        sys.exit(1)
