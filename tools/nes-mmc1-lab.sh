#!/bin/sh
# MMC1 developer experiment; all normal NES modules remain untouched after run.
# No SRAM backup exists yet. ONLY use on personally checked NO-BATTERY carts.
# Usage: sh tools/nes-mmc1-lab.sh [PRG_KIB CHR_KIB [SUBMAPPER]]
# No arguments = read-only database probe and auto-select a unique safe layout.
set -eu
if [ "$(id -u)" -ne 0 ] || [ ! -t 0 ]; then
    echo "Run as root from an interactive Retrode3 terminal." >&2
    exit 1
fi
if [ "$#" -ne 0 ] && [ "$#" -ne 2 ] && [ "$#" -ne 3 ]; then
    echo "Usage: sh tools/nes-mmc1-lab.sh [32|64|128|256|512] [0|8|16|32|64|128] [submapper 0|5]" >&2
    exit 2
fi
prg="" chr="" sub=""
if [ "$#" -ge 2 ]; then
    prg="$1"; chr="$2"; sub="${3:-0}"
    case "$prg" in 32|64|128|256|512) ;; *) echo "Unsupported PRG size" >&2; exit 2;; esac
    case "$chr" in 0|8|16|32|64|128) ;; *) echo "Unsupported CHR size" >&2; exit 2;; esac
    case "$sub" in
        0) ;;
        5) if [ "$prg" != 32 ]; then echo "Submapper 5 requires 32 KiB PRG" >&2; exit 2; fi ;;
        *) echo "Unsupported MMC1 submapper" >&2; exit 2;;
    esac
fi
cd "$(dirname "$0")/.."
lab="build/migration/mmc1-lab/nes.so"
cli="build/migration/retrode3-cli"
live="/usr/lib/retrode3/cores-available/nes.so"
enabled="/etc/retrode3/cores-enabled/nes.so"
for f in "$lab" "$cli" "$live"; do
    if [ ! -f "$f" ]; then echo "Missing $f" >&2; exit 1; fi
done
if [ ! -L "$enabled" ] || [ "$(readlink "$enabled")" != "$live" ]; then
    echo "Unexpected NES enabled core symlink; no changes performed." >&2
    exit 1
fi
echo "MMC1 developer test. Existing M2/ROMSEL timing is NOT SRAM-safe."
echo "This tool never writes CHR-RAM or save-RAM data, but MMC1 register"
echo "writes can inadvertently modify PRG-RAM on battery-backed boards."
echo "Only proceed if the inserted cartridge has NO BATTERY and is yours."
echo "Stop other cartridge reads and close the web interface first."
printf 'Confirm NO-BATTERY-TEST after checking the actual cartridge: '
IFS= read -r confirm
if [ "$confirm" != "NO-BATTERY-TEST" ]; then
    echo "Aborted; original NES core unchanged."
    exit 1
fi
backup="$(mktemp -d /tmp/retrode3-mmc1-lab.XXXXXX)"
cp -p "$live" "$backup/nes.so"
replaced=0
restore() {
    if [ "$replaced" -eq 1 ] && [ -f "$backup/nes.so" ]; then
        cp -p "$backup/nes.so" "$live.mmc1-restore"
        mv -f "$live.mmc1-restore" "$live"
        echo "Original NES core restored."
    fi
    rm -f "$live.mmc1-restore" "$live.mmc1-new"
    rm -rf "$backup"
}
trap 'restore' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
install -m 0644 "$lab" "$live.mmc1-new"
mv -f "$live.mmc1-new" "$live"
replaced=1

out="/tmp/retrode3-mmc1-$(date +%Y%m%d-%H%M%S).nes"
if [ -e "$out" ]; then echo "Output already exists: $out" >&2; exit 1; fi
# Real CLI owns the bus lock, output CRC, cancellation and atomic publication.
# In auto mode: first probe the first/last 512 PRG bytes READ ONLY.
# If multiple layouts are possible, stop BEFORE any mapper-register writes.
python3 - "$cli" "$out" "$prg" "$chr" "$sub" <<'PY'
from __future__ import print_function
import io
import json
import os
import subprocess
import sys
import zlib

cli, output, prg, chr_size, sub = sys.argv[1:]
env = os.environ.copy()
env["RETRODE3_MMC1_TEST_ACK"] = "NO_BATTERY_TEST_ONLY"

def run(command):
    p = subprocess.Popen(
        [cli, "--protocol", "ndjson-v1", "--interactive"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, env=env)
    data = json.dumps(command, separators=(",", ":")).encode("utf-8") + b"\n"
    stdout, stderr = p.communicate(data)
    events = []
    for line in stdout.decode("utf-8", "replace").splitlines():
        if not line.strip():
            continue
        print(line)
        try:
            events.append(json.loads(line))
        except ValueError:
            pass
    if p.returncode != 0 or not events:
        print("CLI failed: %s" % stderr.decode("utf-8", "replace"), file=sys.stderr)
        sys.exit(1)
    return events[-1]

probe = run({"command": "probe", "core": "nes", "request_id": "mmc1-probe"})
if probe.get("event") != "probe_result":
    print("Read-only probe failed; no mapper register writes.", file=sys.stderr)
    sys.exit(1)
matching = [x for x in probe.get("read_layouts", [])
            if x.get("mapper") == 1 and x.get("read_supported")]
if prg:
    # A MANUAL override is intentional if DB cannot uniquely identify
    # the cartridge or the operator wishes to test a known ROM geometry.
    command = {"command": "read_rom", "core": "nes",
               "request_id": "mmc1-manual", "output": output,
               "mapper": 1, "submapper": int(sub),
               "prg_kib": int(prg), "chr_kib": int(chr_size),
               "mirroring": "horizontal", "battery": 0}
else:
    if len(matching) != 1:
        print("Auto-detect is ambiguous or unsupported (%d MMC1 layouts)."
              % len(matching), file=sys.stderr)
        print("Check candidate names and re-run with PRG_KIB CHR_KIB [SUBMAPPER].",
              file=sys.stderr)
        sys.exit(1)
    command = {"command": "read_rom", "core": "nes",
               "request_id": "mmc1-auto", "output": output,
               "layout_id": matching[0]["id"]}
result = run(command)
if result.get("event") != "result" or not result.get("ok"):
    print("ROM read failed. Original .so will be restored.", file=sys.stderr)
    sys.exit(1)
if not os.path.exists(output):
    print("No output ROM published.", file=sys.stderr)
    sys.exit(1)
with io.open(output, "rb") as f:
    data = f.read()
if len(data) != result["bytes_written"]:
    print("ROM size differs from CLI result!", file=sys.stderr)
    sys.exit(1)
print("MMC1 dump: %s" % output)
print("ROM bytes: %d; payload CRC32: %08X; full CRC32: %08X" %
      (len(data), zlib.crc32(data[16:]) & 0xffffffff,
       zlib.crc32(data) & 0xffffffff))
print("Identified: %s (verified=%s)" %
      (result.get("title"), result.get("verified")))
PY
