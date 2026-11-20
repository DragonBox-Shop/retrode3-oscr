#!/bin/sh
# Run the hardware-free shared-core CI gates before publishing a commit.
set -eu
cd "$(dirname "$0")/.."

for tool in git make gcc g++ python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    fi
done

git submodule update --init third_party/oscr-firmware
sh -n tools/nes-mmc1-lab.sh
python3 -m py_compile tools/gen-nes-mmc1-catalog.py
make -f Makefile.migration test-all-shared-cores
make -f Makefile.migration native-nes-mmc1-lab
make -f Makefile.migration test-release-stage

git -C third_party/oscr-firmware diff --exit-code
test -z "$(git status --porcelain -- third_party/oscr-firmware)"

printf '\nPre-push core checks passed. Physical cartridge tests are still required for hardware changes.\n'
