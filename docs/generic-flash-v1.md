# Retrode3 generic cartridge flash contract (pre-kernel, v1)

Status: userspace foundation only. NO physical erase/program operation is enabled.
This is intentionally separate from the read-only core ABI v3 and SRAM/savegame operations.

## Workflow

1. Developer explicitly selects an installed/enabled console core in **Write Cart**. Do
   not run CFI on insertion or under the read-only cartridge auto-probe.
2. Host acquires the existing cross-core cartridge bus lock. All CFI query mode
   transitions require bus-write cycles, even though the probe is non-destructive.
3. An optional flash core identifies the CFI `QRY` signature, primary command
   set, capacity, bus interface, erase regions and timing metadata. Manufacturer
   and device IDs are additional checks and must not substitute for CFI.
4. Core applies its own adapter/board profile, bus width, address remapping,
   voltage and command-set allowlist. CFI alone does not identify a circuit board.
   Unknown or inconsistent geometries must be rejected, not guessed.
5. Host validates uploaded ROM size and keeps its contents in a private staging
   file. Flash write requires explicit confirmation and the full bus lock.
6. Core re-probes immediately before erasing; mismatch, card removal, or voltage
   error aborts. Erase/program operations are bounded by timeouts and checked.
7. After programming, read back the entire programmed range, compare EVERY
   byte to the source and validate erased padding as appropriate. A CRC alone
   is not an excuse to skip byte comparison. On cancel or error, report the
   cartridge's content as indeterminate rather than claiming success.

The read-only ROM probe is unchanged. Flash and SRAM are separate capabilities.

## CFI parsing

`flash_cfi_v1.h` parses a hardware-independent CFI byte table. Each table
index is a *CFI offset*. For the x16 bus in `flashkit.c`, queries are issued
at twice the byte offset: QRY at physical addresses `0x20/0x22/0x24`, device
size exponent at `0x4e`, erase regions from `0x5a` onward.

The decoder refuses bad QRY signatures, impossible size exponents, overlong
region lists, zero block sizes, and a geometry sum that does not equal the
reported capacity. Its erase planner includes full erase blocks covering a
selected ROM, not arbitrary byte writes.

Timeout values from the chip are untrusted bounds/estimates until a specific
command-set backend defines and clamps them. A CFI geometry is not proof that
write operations are electrically supported or authorized.

## Optional plugin ABI

`core_flash_v1.h` defines `retrode3_core_get_flash_operations_v1`, separate
from `retrode3_core_get_operations_v3`. Future implementations may supply:
`capabilities_json` (metadata only), `probe` (explicit command-mode CFI),
`plan_write`, and `write_verify` (erase/program/full compare). The host owns
source-file paths, progress, cancellation, bus exclusivity, and authorization.
No file handles or C++ objects cross the ABI.

No core may advertise `write_supported=1` until its kernel transport, bus
write cycles, chip command set, board mapping, power and readback are
hardware-tested. Until then, the frontend should present Write Cart as
unsupported or unavailable, NOT attempt to flash over ROM-read endpoints.

## Kernel handoff / GBx coordination

Keep kernel modifications out of this phase. The parallel GBx work currently
lives on `retrode3-kernel/feature/gbx-md-read-7.3` and changes
`retrode3_cart.c`, `retrode3_bus.h`, `retrode3_adapter_mode.h` and `retrode3.c`.
When ready, base the Flash kernel feature on the merged/tested GBx work or
rebase it onto that branch, then add isolated flash-cycle functions with
minimal changes to shared bus dispatch. Do not copy/replace GBx files.

Target hardware: MIPS32 R2 little endian, kernel `7.3.0-rc5-letux-mips-retrode3+`.
Build only `M=drivers/misc/retrode3 modules` with the existing prepared
`.config`/`Module.symvers`; test `.ko` separately with backup before replacing.
No kernel build or hardware permission change is part of this userspace work.

## Test gate

`c++ -std=c++11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude tests/flash_cfi_v1_test.cpp -o /tmp/flash-cfi-test && /tmp/flash-cfi-test`

This is an offline parser test, NOT a flash hardware validation. Before merging
core/CLI changes, run `./tools/preflight-ci.sh` and the usual MIPS build and
real-device testing. Development stays on a PR branch until those gates pass.