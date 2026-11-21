#!/usr/bin/env python3
"""Read-only diagnostic: capture the directly visible first 32 KiB of SMS-0007.

NOT a complete 128-KiB dump: the Sega mapper must be switched for later banks.
No voltage changes, mapper register writes or CLI/plugin modifications.
Mode 20 is the already tested fixed D0-D7 MD adapter read lane.

This bench utility intentionally refuses:
- voltages other than 3300 mV
- an inactive legacy-bridge high-Z kernel gate
- any other populated cartridge slot
- nonidentical duplicated physical MD samples
- inconsistent repeated snapshots
"""
from __future__ import print_function

import binascii
import os
import sys
import zlib

SLOT_DEVICE = "/dev/slot-md"
OUTPUT = "/tmp/sms-md-first32k-readonly.bin"
SIZE = 32768
CHUNK = 512
SIGNALS = (1 << 22) | (1 << 17) | (1 << 18)
MODE = 20


def read_text(path):
    with open(path, "r") as stream:
        return stream.read().strip()


def check_hardware():
    gate = read_text("/sys/module/retrode3/parameters/legacy_bridge_hiz_test")
    if gate not in ("Y", "1"):
        raise RuntimeError("legacy_bridge_hiz_test must be enabled")
    if read_text("/sys/class/retrode3/slot-md/vcc") != "3300mV":
        raise RuntimeError("MD slot must stay at 3300mV")
    if read_text("/sys/class/retrode3/slot-md/sense") != "active":
        raise RuntimeError("MD slot does not contain a cartridge")
    for name in ("snes", "nes"):
        path = "/sys/class/retrode3/slot-%s/sense" % name
        if os.path.exists(path) and read_text(path) != "empty":
            raise RuntimeError("Remove cartridge from %s slot first" % name)


def phys_offset(sms_addr):
    if sms_addr < 0 or sms_addr + CHUNK > SIZE or sms_addr % CHUNK:
        raise ValueError("invalid SMS read window")
    # The adapter wires MD A1..A16 to SMS A0..A15; MD A0 is unused.
    # MD A22 (/WR) high throughout; A17 and A18 disable RAM/banking.
    return (MODE << 24) | SIGNALS | (sms_addr << 1)


def read_prefix(pread_fn):
    blocks = []
    for addr in range(0, SIZE, CHUNK):
        physical = pread_fn(CHUNK * 2, phys_offset(addr))
        if len(physical) != CHUNK * 2:
            raise RuntimeError("short physical read at SMS $%04X" % addr)
        even, odd = physical[0::2], physical[1::2]
        if even != odd:
            raise RuntimeError("unstable paired samples at SMS $%04X" % addr)
        blocks.append(even)
    return b"".join(blocks)


def crc(data):
    return "%08X" % (zlib.crc32(data) & 0xffffffff)


def self_test():
    def fake_pread(size, offset):
        if size != 1024 or (offset >> 24) != MODE:
            raise AssertionError("unexpected lane/length")
        start = (offset & 0xffff) >> 1
        logical = bytes(bytearray((start + i) & 255 for i in range(CHUNK)))
        return b"".join(bytes(bytearray((x, x))) for x in bytearray(logical))
    expected = bytes(bytearray(i & 255 for i in range(SIZE)))
    assert read_prefix(fake_pread) == expected
    assert phys_offset(0) == ((MODE << 24) | SIGNALS)
    assert phys_offset(SIZE - CHUNK) & (1 << 22)
    try:
        read_prefix(lambda size, offset: b"")
    except RuntimeError:
        pass
    else:
        raise AssertionError("short read was accepted")
    print("SMS read-only prefix self-test: OK (no hardware accessed)")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
        return 0
    if len(sys.argv) != 1:
        sys.stderr.write("Usage: python3 tools/sms-md-readonly-prefix.py [--self-test]\n")
        return 2

    # This tool cannot send mapper writes: the cartridge FD is O_RDONLY.
    check_hardware()
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(SLOT_DEVICE, flags)
    try:
        first = read_prefix(lambda n, at: os.pread(fd, n, at))
        check_hardware()
        second = read_prefix(lambda n, at: os.pread(fd, n, at))
    finally:
        os.close(fd)
    if first != second:
        raise RuntimeError("two 32-KiB read-only passes differ; file not written")

    with open(OUTPUT, "wb") as stream:
        stream.write(first)
    print("Captured: %s (%d bytes, first 32 KiB ONLY)" % (OUTPUT, len(first)))
    print("Pass 1 CRC32: %s" % crc(first))
    print("Pass 2 CRC32: %s" % crc(second))
    for addr in (0x1ff0, 0x3ff0, 0x7ff0):
        line = binascii.hexlify(first[addr:addr + 16]).decode("ascii")
        print("%04X: %s" % (addr, line))
    print("No mapper writes, no voltage change, no complete ROM claim")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        sys.stderr.write("SMS snapshot refused/failed: %s\n" % exc)
        sys.exit(1)
