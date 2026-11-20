#!/bin/sh
# Manual, operator-controlled physical MMC1 SGROM experiment.
# ONLY for verified Rad Racer NES-SGROM (128 KiB PRG, 8 KiB CHR-RAM,
# zero PRG-WRAM). Not safe for unknown, battery-backed or WRAM cartridges.
# Run as root: sh tools/nes-mmc1-sgrom-lab.sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root on the Retrode3." >&2
    exit 1
fi
if [ ! -t 0 ]; then
    echo "Interactive board confirmation is required." >&2
    exit 1
fi
cd "$(dirname "$0")/.."
lab_so="build/migration/mmc1-lab/nes.so"
cli="build/migration/retrode3-cli"
live="/usr/lib/retrode3/cores-available/nes.so"
enabled="/etc/retrode3/cores-enabled/nes.so"
for item in "$lab_so" "$cli" "$live"; do
    if [ ! -f "$item" ]; then
        echo "Missing $item (build first; original NES core must be installed)." >&2
        exit 1
    fi
done
if [ ! -L "$enabled" ] ||
   [ "$(readlink "$enabled")" != "$live" ]; then
    echo "NES core must be enabled with the expected absolute symlink." >&2
    exit 1
fi
echo "MMC1 LAB ONLY: this temporarily exchanges the installed NES .so."
echo "The kernel's normal NES PRG write pulse is NOT safe for PRG-WRAM."
echo "Only use a confirmed NES-SGROM Rad Racer: 128 KiB PRG, CHR-RAM,"
echo "NO PRG-WRAM and NO battery. Do not use another MMC1 cartridge."
echo "Stop other cartridge/Web reading jobs and close the Web UI first."
printf 'Enter SGROM-128-NOWRAM after checking the physical board: '
IFS= read -r answer
if [ "$answer" != "SGROM-128-NOWRAM" ]; then
    echo "Cancelled; the installed core was not changed."
    exit 1
fi

backup_dir="$(mktemp -d /tmp/retrode3-mmc1-core.XXXXXX)"
cp -p "$live" "$backup_dir/nes.so"
installed=0
restore() {
    if [ "$installed" -eq 1 ] && [ -f "$backup_dir/nes.so" ]; then
        cp -p "$backup_dir/nes.so" "$live.restore"
        mv -f "$live.restore" "$live"
        echo "Original NES core restored."
    fi
    rm -f "$live.restore"
    rm -rf "$backup_dir"
}
trap 'restore' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# Atomic replacement: do not write into a loaded shared-library inode.
install -m 0644 "$lab_so" "$live.lab-new"
mv -f "$live.lab-new" "$live"
installed=1

out="/tmp/retrode3-rad-racer-mmc1-$(date +%Y%m%d-%H%M%S).nes"
if [ -e "$out" ]; then
    echo "Output already exists, refusing overwrite: $out" >&2
    exit 1
fi
echo "Reading SGROM via experimental MMC1 serial bank switching..."
printf '{"command":"read_rom","core":"nes","request_id":"mmc1-sgrom-lab","output":"%s","mapper":1,"submapper":0,"prg_kib":128,"chr_kib":0,"mirroring":"horizontal","battery":0}\n' "$out" |
    env RETRODE3_MMC1_NO_PRG_RAM_ACK=RAD_RACER_SGROM_128K_CHRRAM_NO_WRAM \
    "$cli" --protocol ndjson-v1 --interactive

if [ ! -f "$out" ]; then
    echo "No ROM file published. See CLI error above." >&2
    exit 1
fi
python3 - "$out" <<'PY'
import io
import sys
import zlib
path = sys.argv[1]
with io.open(path, "rb") as src:
    data = src.read()
if len(data) != 131088 or data[:4] != b"NES\x1a":
    sys.stderr.write("Wrong ROM size/header; check CLI output.\n")
    sys.exit(1)
payload_crc = "%08X" % (zlib.crc32(data[16:]) & 0xffffffff)
file_crc = "%08X" % (zlib.crc32(data) & 0xffffffff)
print("MMC1 lab result: %s (%s bytes)" % (path, len(data)))
print("PRG CRC32: %s (PAL reference 654F4E90; US reference 8B9D3E9C)" % payload_crc)
print("Full iNES CRC32: %s" % file_crc)
if payload_crc not in ("654F4E90", "8B9D3E9C"):
    print("WARNING: PRG CRC does not match the known Rad Racer references.")
PY
