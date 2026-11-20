# Retrode3 OSCR: current development contract

**Current as of 2026-11-20.** This file is the first entry point for
contributors and assistant chats. Also read [docs/PROJECT-STATUS.md](docs/PROJECT-STATUS.md)
for the dated physical acceptance record and
[docs/branching-and-builds.md](docs/branching-and-builds.md) for releases.
Old feature branches, stacked PRs and historical paragraphs in feature
documents are **not** the current production/development status.

## Verbindliche Vorabpruefung vor GitHub-Pushes

Vor jedem Push, der native Cores, ABI, CLI, Tests oder Makefile aendert, im
vollstaendigen lokalen Checkout zuerst `./tools/preflight-ci.sh` ausfuehren.
Das Skript initialisiert das gepinnte OSCR-Submodul und prueft alle drei
Cores, generische v3-Auswahl, MMC1-Laborbuild sowie das Debian-Staging
ohne Zugriff auf echte Cartridge-Hardware. Ein Fehler blockiert den Push;
erst lokal reparieren und erneut den gesamten Testlauf ausfuehren.

Keine bewusst ungetesteten Compiler-/Mapper-Zwischenstaende direkt auf
`develop` pushen. Zusammengehoerige Aenderungen vorab lokal buendeln.
Wenn nur GitHub-Connector-Zugriff ohne lokalen Checkout/Testmoeglichkeit
vorliegt, fuer riskante Aenderungen einen kurzlebigen PR-Branch gegen
`develop` nutzen, Checks abwarten und erst danach squash-merge durchfuehren.
Keine CI-Fehler durch Auskommentieren von Pruefungen oder `continue-on-error`
unterdruecken. Hardwaretests bleiben zusaetzlich verpflichtend.
## Repositories and branches

- [retrode3-oscr](https://github.com/DragonBox-Shop/retrode3-oscr):
  this repo, single native Linux CLI, core ABI, Sanni-based console cores.
- [retrode3](https://github.com/DragonBox-Shop/retrode3):
  Debian-based OS, the *separate* Web v2 project, SysV init, package
  and future flashable-image build pipeline. Read its **root AGENTS.md**
  and web handoff before changing the web frontend.
- [retrode3-kernel](https://github.com/DragonBox-Shop/retrode3-kernel):
  slot devices, timing and electrical controls.
- [retrode3-hardware](https://github.com/DragonBox-Shop/retrode3-hardware):
  hardware revisions, adapter schematics.
- Upstream `oscartreader/oscr-firmware` is a **pinned, unmodified
  submodule** at `third_party/oscr-firmware`. Never silently rewrite
  original source or accept a changed mapper/bus algorithm without review.

**`master` is stable; `develop` is the shared ongoing development branch
for all cores.** Avoid permanent per-plugin branches. Promote only reviewed,
tested groups of changes from `develop` to `master` through a PR with
**squash merge** (avoid flooding master with incremental experiment commits).
After promotion, align `develop` to the resulting master commit, preserving
any genuinely unfinished work. Never force-push a branch without checking
for concurrent commits/leases. GitHub branches may have legacy names; they
do not designate the current development line.

## Physically verified current runtime (Retrode3 2.9.5, 2026-11-19)

All three original-Sanni-derived Stage-1 **dynamic `.so` modules**
have passed a physical full ROM read through **one shared CLI**, not merely
mock or partial standalone-reader tests:

| Core | Scope physically tested | Full output CRC32 |
|---|---|---|
| `snes` | HiROM Terranigma D, 4 MiB | `6FB8A4DF` |
| `md` | Linear Snake Rattle n Roll, 524288 bytes | `543BED30` |
| `nes` | Mapper-0 NROM Excitebike Europe, 24592-byte .nes | `F749E780` |

NES probe catalog payload CRC32 (without the 16-byte iNES header) is
`8A0C7337`. The common host's final CRC32 includes that header.
All three were shown `installed:true`, `enabled:true`, `ready`,
`usable:true`, with `probe` and `read_rom`. This does **not** mean
all SNES enhancement chips, MD special mappers or NES mappers other than 0
work; cartridge save/restore, flash, SRAM and mapper writes are not
approved by these Stage-1 tests.

## Runtime interface: DO NOT revive old executable paths

The actual development device was manually cleaned and verified:

- Main CLI: **`/usr/bin/retrode3-cli`** (never
  `/usr/local/bin/retrode3-cli`, `oscr-snes-native`,
  `oscr-md-native`, or the removed `/usr/local/bin/oscr`).
- Root-only core toggle: **`/usr/sbin/retrode3-corectl`**.
- Root-owned installed metadata and modules:
  **`/usr/lib/retrode3/cores-available/{snes,md,nes}.{so,core}`**.
- Enabled symlinks: **`/etc/retrode3/cores-enabled/<id>.so`**,
  targeting exactly the matching absolute installed path.
- Shared lock: **`/run/lock/retrode3/bus.lock`**. The CLI uses
  `open(O_CREAT)` and nonblocking `flock()`, but does **not** create
  its parent directory. The device uses **SysV init, NOT systemd**.
  Boot provisioning belongs to the OS repository's tested
  `retrode3-core-runtime/etc/init.d/retrode3-core-runtime`;
  its package deployment/on-device post-reboot verification are separate.
  Do not reintroduce `systemd-tmpfiles`.

Common CLI examples:

```sh
/usr/bin/retrode3-cli --list-cores
/usr/bin/retrode3-cli --protocol ndjson-v1 --core nes --action probe
/usr/bin/retrode3-cli --protocol ndjson-v1 --core nes --action read-rom --output /tmp/known-safe.nes
/usr/bin/retrode3-cli --protocol ndjson-v1 --interactive
/usr/sbin/retrode3-corectl enable nes
```

`retrode3-corectl` must only be executed as root via tightly restricted
privilege delegation; never give a browser generic sudo privileges.

One CLI owns NDJSON v1 events, registry, progress, cancellation,
output publication and checksums. **Console `.so` libraries do not emit
per-core NDJSON.** The browser/backend must use the central interactive
CLI and dynamic registry, not hard-code an allowlist of three consoles.
A slot `sense` indication is occupancy only, not a known mapper, game,
adapter ID or safe voltage. No blind writes on insertion.

## Building and shipping

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-all-shared-cores
make -f Makefile.migration test-release-stage
```

`test-release-stage` stages the two executables and the three
`.so/.core` pairs into `build/migration/stage/usr/{bin,sbin,lib}`
using the final absolute paths above, but **does not install or enable**
anything. Python 3.4 and GCC 4.9/C++11 remain actual target limits.
The Debian packages, image builder, secure Web privilege bridge and
automated upstream-core conversions are planned in the OS repo;
passing mock CI alone does not release a cartridge feature.

The original `Cart_Reader/` and old C++/Python reader *source*
remain as regression/reference material. Old binaries were removed
from the development device. Do **not** recreate legacy binaries or
links in `/usr/local/bin` during web deployment.

## Web v2 handoff and release gate

Web v2 currently lives in the separate OS repo's
`feature/web-control-v2` branch (many commits, not yet integrated).
It still has old CLI defaults/installer assumptions and requires an
explicit update to the new paths and removal of old-reader fallbacks.
See the OS repo's `docs/WEB-V2-HANDOFF.md` and `AGENTS.md`.
Do not merge the unverified Web UI straight into `master`.
Integrate its changes with OS `develop`, test on the actual MIPS
Retrode3 (incl. SNES/MD/NES, auto-insertion/removal, cancellation,
permissions, reboot), then squash-promote to OS `master`.

## NES v2 Fast-Probe auf develop (2026-11-20)

Das gemeinsame CLI und der NES-Core besitzen jetzt eine **additive v2-C-ABI**
in `include/retrode3/core_operations_v2.h`. Der vorhandene v1-Pfad
für SNES/MD bleibt erhalten. Der NES-Fast-Probe liest **genau 1024 Bytes**
(erste und letzte 512 Bytes PRG) statt des kompletten ROMs und meldet
`identification_status`, Kandidaten und erforderliche Leselayouts.
Unbekannte ROMs sind mit einem expliziten, validierten manuellen NROM-Layout
lesbar. `read_rom` streamt den PRG-/CHR-Payload einmal, ermittelt CRC,
setzt den originalen DB-Header erst danach, berechnet die Dateiprüfsumme
und veröffentlicht die fertige .nes-Datei atomar. Unbekannte oder veränderte
ROMs werden nur als `verified:false` ausgegeben.

**Produktionsseitig bleibt nur Mapper 0/Submapper 0, PRG 16/32 KiB,
CHR 0/8 KiB freigegeben.** Auf `develop` sind zusätzlich konservative,
Sanni-abgeleitete Banklese-Sequenzen für **Mapper 2 (UxROM), 3 (CNROM),
4 (MMC3) und 7 (AxROM)** im gemeinsamen NES-Core eingebaut. Deren
CPU-Register-Schreibzugriffe sind im normalen Build **deaktiviert** und
laufen nur in der Mock-CI oder einer expliziten experimentellen
Build-Konfiguration. **Mapper 1/MMC1 ist jetzt fest im regulaeren `native-nes-module`
(`build/migration/nes.so`) integriert.** Kein separater
Labor-Build oder Environment-Opt-in ist fuer normale MMC1-ROM-Dumps
erforderlich. Sanni-abgeleitete Registerbankfolgen lesen PRG-ROM
32/64/128/256/512 KiB sowie CHR-ROM 0/8/16/32/64/128 KiB
(CHR=0 bedeutet CHR-RAM, kein CHR-Dump). MMC1-Submapper 0 sowie
Submapper 5 mit 32KiB PRG sind abgedeckt; die CHR-Routine stellt
den MMC1-Control-Modus explizit auf 8 KiB ein.

**Sicherheitsgrenze:** Das aktuelle M2/ROMSEL-Timing fuer $E000/$F000
ist nicht auf batteriegepufferten PRG-WRAM-Boards verifiziert.
MMC1-Layouts mit Batterie sind noch `read_supported:false`, auch
im normalen Core. Keine SRAM/Flash/Save-Schreibfunktion. Andere
Mapper (2/3/4/7) bleiben hinter ihren bisherigen Schreib-Gates.
Beim Einlegen werden weiterhin NUR zwei PRG-Fenster mit insgesamt
1024 Bytes *ohne* Registerwrites gelesen, beim eigentlichen ROM-Dump
wird gebankt. Vor allgemeiner Freigabe auf der echten Retrode3
PRG+CHR-Layouts physisch verifizieren.
Sannis gepinnte `oscartreader/oscr-databases`-NES-Datenbank
(commit `87a48948...`) ist als `oscr_nes_mmc1_catalog.cpp`
(695 Mapper-1-Metadatensaetze) integriert. `iprg` ist 16-KiB,
`ichr` ist **4-KiB-Einheit**. Der Fast-Probe bleibt read-only,
mit zwei 512-Byte-PRG-Fenstern; anschliessend exakter
Voll-Payload-CRC-Titelvergleich. Manche Eintraege haben keine
Fast-Probe-Fingerprints und werden erst nach erfolgreichem Dump
erkannt; mehrdeutige Layouts verlangen manuellen Override.
Die CLI meldet MMC1 in `get_capabilities` als
`read_supported:true`; der normale NES-v2-Probe liefert
`candidates` und `read_layouts`. `retrode3/develop` Web v2
reicht die Auswahl ueber die zentrale JobManager-API an dieselbe
CLI weiter und zeigt die manuelle Layoutauswahl an, wenn
der Fingerprint keinen eindeutigen Treffer liefert.
Auf `master` ist diese neue Integration nicht freigegeben.
Siehe [docs/NES-MAPPER-STATUS.md](docs/NES-MAPPER-STATUS.md).
**Physisch bestanden 2026-11-20:** Rad Racer PAL MMC1/SGROM,
128 KiB PRG/0 KiB CHR, 131088 Bytes einschliesslich iNES,
PRG-Payload CRC32 `654F4E90` (PAL-Referenztreffer),
volle Datei-CRC32 `B78FDA39`; regulaere NES-`.so` wiederhergestellt.
**Physisch ebenfalls bestanden 2026-11-20:** Mario & Yoshi PAL,
NES-SFROM MMC1 mit 128 KiB PRG + 32 KiB CHR (163856 Bytes iNES).
**Beide ROM-Teile separat verifiziert:** PRG `1406783D`,
CHR `FC0BBA23`, zusammen `836685C4`. Fuenf stabile reale
v3-Fast-Probes am selben Modul lieferten
`$8000:29D1D575` und `$E000:9A78A49D`.
Beide Fingerprints stehen nun in der regionalen NES-DB-Erweiterung
auf `develop`; automatisches Erkennen nach Core-Update noch
auf echter Hardware zu pruefen. Der alte 128/0-Fehl-Dump
(`F378619E`/`9FB32DEA`) war keine Referenz.

**MMC1-Mapper-1-Fingerprints werden automatisch erkannt und ihr
Layout zum Auslesen angeboten** (gemeinsames CLI-v3-Protokoll).
Das Plugin gibt bei unterschiedlichen Layouts eine Auswahl vor.
Ob ein Spiel wirklich identisch ist, entscheidet erst die vollstaendige
ROM-CRC nach dem Dump. Andere experimentelle Mapper 2/3/4/7
bleiben gesperrt, und MMC1-Layouts mit Batterie bleiben wegen
fehlender Save-RAM-sicherer Kernel-Timings gesperrt.

Siehe [docs/nes-mapper-stage2.md](docs/nes-mapper-stage2.md) für die
Sequenzen, Buskonflikt-Schutzmaßnahmen, CI-Tests und Hardware-Gates.
Mock-/CI-Test ist **kein** Hardware-Akzeptanztest; Excitebike sowie SNES/MD
müssen auf der Retrode3 erneut validiert werden. Web v2 im OS-Repository
muss die neuen Probe-/Auswahl-Felder noch auswerten.

Die Detailanforderungen und CLI-Kommandos stehen in
[docs/nes-fast-probe-mapper-selection.md](docs/nes-fast-probe-mapper-selection.md).
Vorherige Stand-1-NROM-Beschreibungen gelten nur für den Legacy-v1-Pfad.

## Safety and change discipline

Read the latest source and CI runs before edits; prefer focused
commits and leave clean worktrees. All bus operations must use the
typed kernel HAL and explicit capabilities. Keep Sanni source pristine;
fail generation on an unknown upstream algorithm. Use
`io.open`/Python 3.4-compatible tests on the device.
Avoid speculative mapper, SRAM, save or flash writes. Check hardware
revision, voltage and mapper constraints before declaring support.
Record real sizes/CRC32 and mark `mock tested`, `physically tested`
and `released in a Debian package` as three different states.

## Generic cartridge selection: v3 adapter contract (2026-11-20)

**Mandatory for all newly developed cores and adapter variants.** Read
[docs/generic-cartridge-selection-v3.md](docs/generic-cartridge-selection-v3.md)
and `include/retrode3/core_operations_v3.h` before adding any new system.
The shared CLI owns NDJSON framing, cancellation, atomic publication, CRC
and host locks. A new core exposes selection and override fields through the
optional v3 adapter ABI, not a new console-specific host parser.
Legacy v1 and NES v2 remain additive/compatible during migration.

Selection is explicit: `auto`, `candidate`, `layout`, `override`.
Never infer override from incidental fields. Candidate/title ambiguity and
layout ambiguity are different: only the latter blocks auto reading.
The adapter MUST revalidate every selected field and combination at plan
and read time, reject unapproved mapper/bank writes and any RAM/save/flash
writes, and never interpret a catalogue entry as hardware authorization.
A successful dump with no unique CRC match is NOT verified.

**Recommended packaging:** keep Sanni upstream untouched; use a Retrode3
adapter plus optional package-owned read-only JSON describing UI labels,
parameter types/dependencies and static adapter slot. The executable core's
validation remains authoritative; a JSON file cannot enable unsafe hardware
operations. Physical `slot` is already mandatory in both `.core` and
descriptor; use separate IDs for different adapter routes (e.g. `sms-snes`
vs `sms-md`), never guess the slot from the logical console name.
All new JSON schema revisions must be discoverable without changes to
the web frontend. Generic common-CLI changes must preserve v1/v2 tests.
No `master` promotion before target hardware and regression acceptance.
