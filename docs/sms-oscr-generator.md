# SMS core: pinned upstream extraction (development only)

**Build contract:** The SMS source of truth is the pristine submodule
`third_party/oscr-firmware/src/cores/SMS.cpp` pinned at
`e5a08326dfc8fff67193f7fc5a7bc36b5282b28c`.
`tools/build-linux-sms-core.py` verifies Git blob
`e28a1903fa20d2501721ec07935ad766d1d96a48` before generating either
header. On *any* upstream byte change it fails closed rather than
silently changing an algorithm. Never manually edit the generated headers
or the upstream submodule. A reviewed update changes the pin/check and
tests deliberately.

`build/migration/oscr_sms_size.h` contains the **original** `getCartInfo()`
ROM-size switch. The bounded 16-byte `TMR SEGA` header check remains a
Retrode3 read-only probe policy, not the full upstream getCartInfo() routine.
The upstream bank-mirror size detection performs cartridge register writes,
so it is not yet authorized or included in the live probe. Report only a
size *hint*, never a verified game identity.

`build/migration/oscr_sms_read.h` retains the **original** `readROM()` bank
and 512-byte buffer loop structure. The deterministic generator performs
only these reviewed substitutions:

1. `readByte()`'s 512 AVR port reads become one typed kernel-mode-20 512-byte
   read. The common CLI still owns output, progress, cancellation and CRC.
2. `writeByte(0xFFFF, currBank)` routes to `bus.select_page2(currBank)`,
   which is **disabled on real hardware** until kernel mapper write cycles
   are reviewed. Sanni's banked loop remains testable against mocks.
3. For 8, 16 and 32 KiB linear ROMs the existing OSCR 32-KiB linear
   branch is extended to smaller supported sizes and mapper writes are
   suppressed. **This differs intentionally from OSCR's cartOn()**, which
   always writes `$FFFC-$FFFF` to initialize mapper state. No mapper
   initialization or size mirror detection is performed in this lab path.
   Hardware acceptance of linear ROMs therefore requires known cartridge
   bank state and a separate CRC32 comparison. The linear read isn't a
   universal substitute for original OSCR's initialization.
4. AVR UI progress and SD writes are supplied by the shared CLI host.

Sanni's original `cartOn()` also initializes voltage and bus pins. These
are **not** ported blindly into userspace; `/dev/slot-md`, the kernel HAL,
5-V check and explicit experimental opt-in handle them. The current
kernel driver selects the slot before safely prepositioning A21 (`/WR`)
and may cause a first-select glitch, so **do not execute physical SMS
reads until that kernel sequence is fixed and checked on a device**.

`native-sms-md-module` depends on the pinned source and generator and
compiles only `sms-md.so`. `test-sms-md-module` verifies generated files,
rejects unexpected source drift, and runs existing real V3 ABI and mock
transport tests, including 8/16/32 KiB linear and banked mock cases.
GCC 8 CI compiles the same generated source using `-Werror`.

No automatic install/enable, SRAM writes, banked mapper writes, Game Gear,
full original `getCartInfo()` bank scanning, or production release.
Develop only until target MIPS and real-cartridge acceptance is complete.