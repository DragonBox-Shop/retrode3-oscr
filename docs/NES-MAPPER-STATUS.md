# NES-Mapping: aktueller Entwicklungsstand und Chat-Handoff

> **Stand: 2026-11-26 | Entwicklungsbranch: `develop`**
>
> **Aktueller Entwicklungsstand (ersetzt widersprechende historische Angaben unten):**
> Mapper 2 (UxROM), 3 (CNROM), 4 (MMC3) und 7 (AxROM) sind jetzt
> im regulaeren Entwicklungs-Core fuer explizite, batterielose manuelle
> Layouts aktiviert. Die v3-Capabilities und die v2/v3-Validierung
> stimmen ueberein. Keine neuen Datenbank-Auto-Freigaben: Fingerprints
> und Payload-CRC-Katalog fuer diese Mapper folgen separat.
> AxROM sucht vor JEDEM Bankwechsel eine konfliktfreie Adresse im
> aktuellen Bankfenster; UxROM/CNROM behalten ihre Vorabpruefung.
> MMC3 aktiviert kein PRG-RAM; alle neuen Mapper bleiben auf Submapper 0
> und die vorhandenen konservativen Groessen begrenzt. Bekannte MMC1-
> Batterie-Fingerprints koennen nicht per anderem banked Mapper umgangen werden.
> Keine Aenderung am gepinnten Sanni-Submodul, Kernel, CLI, Web oder Paket.
> Mock-Tests fuer regulaeren Build, v3, Konflikte und Batterie-Sperren
> erweitert; frischer CI-Lauf und physische Abnahme stehen aus.
> Vor Tag/Release: reale batterielose Boards mit passenden Geometrien
> pruefen, CRC32 vergleichen, keine Batterie-/Flash-Cartridges testen.
> Weitere Mapper erst nach individuellem Review der Originalsequenzen.
>
> Kompakter Einstieg fuer neue Chats und Entwickler. Zuerst
> [`AGENTS.md`](../AGENTS.md) und **diese Datei** lesen, danach bei
> Bedarf [`nes-mapper-stage2.md`](nes-mapper-stage2.md) und
> [`nes-fast-probe-mapper-selection.md`](nes-fast-probe-mapper-selection.md).
> Der aktuelle Quellcode hat bei Widerspruechen Vorrang vor historischen
> Abschnitten. **Implementiert != physisch verifiziert != freigegeben.**

## Physisch abgenommen: Mario & Yoshi PAL MMC1 mit CHR-ROM (2026-11-20)

**Mit der echten Retrode3 und der regulaeren NES-v3-Ausleseroutine
erfolgreich bestaetigt:** PRG-ROM 128 KiB, CHR-ROM 32 KiB,
iNES-Dateigroesse 163856 Bytes. Die CRC32 stimmt in allen
drei Teilpruefungen exakt mit der dokumentierten Referenz ueberein:

- **PRG-ROM:** `1406783D` (MATCH)
- **CHR-ROM:** `FC0BBA23` (MATCH)
- **PRG+CHR:** `836685C4` (MATCH)

Damit ist neben Rad Racer (128KiB PRG/CHR-RAM) nun auch
ein echtes MMC1-Spiel mit CHR-ROM hardwarevalidiert.
Mario & Yoshi war zuvor zweimal `empty/unreadable`, aber
fuenf aufeinanderfolgende Probes ohne Cartridge-Bewegung
lieferten reproduzierbar `$8000:29D1D575` und
`$E000:9A78A49D`. Nach komplett CRC-validiertem Dump
sind diese **echten Hardware-Fingerprints** jetzt in der
regionale Zusatzdatenbank hinterlegt. Kein Sanni-DB-Umbau,
keine Aenderung am gemeinsamen CLI oder Web-v3-Vertrag.

**Code und Regressionstests auf `develop` implementiert, CI
NES und Core-Registry gruen am Commit `2435f2af`.**
Das erneute **automatische** Probe auf echter Hardware mit dem
aktualisierten NES-Core steht noch aus. Erwartet:
`Mario & Yoshi (Europe)`, Layout `m1-128-32`,
`selection_required:false`; anschliessendes v3-`auto`
sollte `verified:true` ausgeben.

Siehe [regional diagnosis & physical CRC proof](nes-probe-regional-diagnostics.md).

## Hardware-Rueckmeldung NES v3: Regionaltitel und Probe-Diagnose (2026-11-20)

Der Nutzer hat auf der echten Retrode3 Folgendes beobachtet:
**Rad Racer PAL** wird als *Highway Star (Japan) (Famicom 3D
System)* gemeldet und anschliessend unpassend ausgelesen;
**Mario & Yoshi PAL** und **Faxanadu PAL** werden nicht erkannt;
**Dr. Mario PAL** wird korrekt erkannt.

**Auf `retrode3-oscr/develop` bereits korrigiert,
noch NICHT erneut physisch getestet:**

- `Rad Racer (Europe)` 128 KiB PRG / 0 KiB CHR, exakte
  Payload-CRC `654F4E90`, als weitere **moegliche**
  128/0-MMC1-Kandidatvariante zum Highway-Star-Fingerprint.
  `Rad Racer (USA)` 128/0, CRC `8B9D3E9C`, nur
  Post-Dump-Verifikation (kein erfundener Fingerprint).
- `Mario & Yoshi (Europe)` 128 KiB PRG / 32 KiB CHR,
  CRC `836685C4`, nur Post-Dump-Verifikation nach manueller
  Layoutwahl; Sannis gepinntes DB hat diese PAL-Version nicht.
- **Faxanadu Europe** ist bereits in Sannis DB
  (256 KiB PRG / 0 KiB CHR, CRC `76C161E3`).
  Fehlende automatische Erkennung **noch nicht geklaert**.
- Der **v3-Probe** liefert die OHNE zusaetzliche Lesezugriffe
  ermittelten `probe_fingerprints.cpu_8000_crc32` und
  `cpu_e000_crc32`. Der v2-Callback behaelt unveraendert
  seine alte C-ABI. Auto-Erkennung bleibt schreibfrei;
  eindeutige PRG+CHR-CRC nach vollem Dump entscheidet erst die
  **verifizierte** ROM-Identitaet.
- NES- und Core-Registry-GitHub-Actions **gruen** auf
  `4ec3cd327e02d28cb2b854f8b0f52b6aecf34524`.
  Regressionstests decken synthetische CRC-Treffer fuer alle
  drei Spiele plus falsch gewaehlten JP-Kandidaten ab.
- **Keine** NES-v3-CLI oder Weboberflaechen-Aenderung fuer
  diesen Fix notwendig; nur den regulären NES-Plugin-Core
  aus `develop` aktualisieren, nicht den alten Labor-Build.

Ausfuehrliche Analyse, Quellen, Referenz-CRCs und der interaktive
Hardware-Probe-Aufruf stehen in
[nes-probe-regional-diagnostics.md](nes-probe-regional-diagnostics.md).
`master` bleibt unberuehrt.

## Jetzt tatsaechlich im normalen Core / Web v2 (2026-11-20)

**Neue Entwicklungsimplementierung, noch NICHT auf echter Retrode3 physisch
abgenommen:** Mapper 1 MMC1 wird durch das regulaere
`make -f Makefile.migration native-nes-module` gebaut und ist in
`build/migration/nes.so` fuer die unterstuetzten, nicht
batteriegepufferten Layouts lesbar. Kein `native-nes-mmc1-lab`,
kein Umstecken einer Labor-`.so` und kein Runtime-Acknowledgement
mehr notwendig. Die CLI meldet Mapper 1 in `get_capabilities`
als `read_supported:true`. Die 695 gepinnten Sanni-Datenbankeintraege
werden beim schreibfreien 1024-Byte-Fast-Probe ausgewertet, die
`read_layouts` und `candidates` an den Web-v2-Jobmanager
gegeben. Das Web zeigt bei verschiedenen Layouts die Auswahl
(Mapper/PRG/CHR) und bietet bei unbekannten Modulen einen manuellen
Override in den regulaeren **Read ROM**-Optionen an.
Nach dem Auslesen erfolgt der komplette Payload-CRC-Abgleich.
32/32 und 128/32 KiB PRG/CHR sind mit C++-Mock und Web-/Backend-
Tests abgedeckt. Das installierte `/usr/bin/retrode3-cli` und
die installierte `nes.so` sowie Web v2 muessen auf dem Testgeraet
tatsaechlich aktualisiert werden; ein `git pull` allein reicht nicht.

**Bewusste Grenze:** Ein MMC1-Layout mit Batterie wird wegen
noch nicht SRAM-sicher verifizierter Kernel-M2/ROMSEL-Schreibpulse
als nicht lesbar gemeldet. Andere Mapper 2/3/4/7 bleiben
experimentell und in regulaeren Binaries gesperrt.
Beim Einlegen erfolgen keinerlei Mapper-Registerwrites.

**CI:** NES <https://github.com/DragonBox-Shop/retrode3-oscr/actions/workflows/native-nes-migration.yml>
und Web v2 <https://github.com/DragonBox-Shop/retrode3/actions/workflows/web-v2-checks.yml>.
Die folgenden Abschnitte ueber den getrennten MMC1-Labor-Core sind
**historische Zwischenstaende** und werden durch diesen Abschnitt
ersetzt. Das Rad-Racer-128/0-Hardwareergebnis bleibt gueltig.

## Arbeitsregeln

- Entwicklung nur auf **`develop`**, `master` bleibt bis zu Review und
  Hardwarefreigabe stabil. Gepruefte Aenderungen spaeter per Squash-PR.
- **Ein** gemeinsames `/usr/bin/retrode3-cli`, dynamische
  `snes.so`, `md.so`, `nes.so`. Kein neuer eigenstaendiger NES-Reader,
  keine separaten Protokolle pro Core.
- NDJSON, Core-Registry, Dateiausgabe, CRC, Fortschritt, Abbruch und Bus-Lock
  gehoeren dem gemeinsamen Host, nicht dem NES-Modul.
- Das gepinnte originale Sanni-`third_party/oscr-firmware`-Submodule
  nicht veraendern. Geaenderte Algorithmen und Bus-Operationen pruefen.
- Originales Zielgeraet: Retrode3 2.9.5, GCC 4.9/C++11 und Python 3.4.
  OS-Dienste verwenden **SysV init**, nicht systemd.
- Keine experimentellen Mapper-Schreibzugriffe, SRAM/Flash-Aktionen oder
  automatischen Bankwechsel beim Einlegen einer Cartridge.

## Aktueller Mapper-Status

| Mapper | Auf `develop` | Mock-Tests | Physisch validiert | Normaler Build |
| --- | --- | --- | --- | --- |
| **0 NROM** | Linear: PRG 16/32 KiB, CHR 0/8 KiB | Vorhanden | **Stage 1 ja; v2 noch nicht** | Read-only freigegeben |
| **1 MMC1** | 695 Sanni-DB-Datensaetze; 32-512KiB PRG, 0-128KiB CHR; CLI- und Web-v2-Mapperauswahl | Native C++11- und Web-Mocks gruen | Rad Racer PAL 128/0 zuvor verifiziert; neue 32/32 & 128/32 offen | **Normaler `nes.so`-Build**, ohne Lab-Opt-in; Batterie-Layouts gesperrt |
| **2 UxROM** | 16-KiB-PRG-Bankswitch, letzte Bank fix | Vorhanden | Nein | Registerwrites gesperrt |
| **3 CNROM** | CHR-Bankswitch je 8 KiB | Vorhanden | Nein | Registerwrites gesperrt |
| **4 MMC3** | 8-KiB-PRG (R6), 1-KiB-CHR (R2) | Vorhanden | Nein | Registerwrites gesperrt |
| **7 AxROM** | 32-KiB-PRG-Bankswitch | Vorhanden | Nein | Registerwrites gesperrt |
| Andere | Nicht als lesbar implementiert/freigegeben | Nicht allgemein | Nein | Gesperrt |

- Mapper 2/3/4/7 nutzen auf `develop` konservative
  Sanni-abgeleitete Routinen in `src/retrode3/oscr_nes_mapper.cpp`.
  Registerzugriffe sind ausschliesslich in `RETRODE3_TESTING`
  oder dem **separaten** Flag
  `RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL` aktiv;
  dieses Flag ist im normalen Build **nicht gesetzt**.
  `get_capabilities` muss dort `read_supported:false` melden.
- MMC1 besitzt nun einen **vollstaendigen** eingebundenen gepinnten
  Sanni-OSCR-Datenbankkatalog mit **695 Mapper-1-Eintraegen**,
  darunter Fingerprints aus zwei 512-Byte-PRG-Probes und originale
  Titel, Header, Submapper und Payload-CRCs. Sanni `iprg` ist
  16KiB- und `ichr` **4KiB**-Einheit. Eintraege ohne Fingerprint
  koennen nach dem vollen Dump anhand der CRC verifiziert werden.
- Auf `develop`: MMC1-PRG 32/64/128/256/512 KiB,
  CHR 0/8/16/32/64/128 KiB; Untermapper 0 und 5 (nur 32KiB PRG)
  im separat gesperrten `native-nes-mmc1-lab`-Build.
  CHR-ROM wird in 8KiB-Banks per explizitem Control-Register
  geschaltet; CHR-RAM (CHR=0) wird nicht als ROM ausgelesen.
- `fast_probe` bleibt **immer read-only** und liest genau 1024 Bytes.
  Eindeutige, als lesbar gemeldete Layouts koennen vom gemeinsamen
  CLI per `layout_id` gewaehlt werden; Ambiguitaeten erhalten
  Kandidaten-/Layout-Dropdown-Auswahl und manuelles Override.
  Die Weboberflaeche muss diese v2-Felder noch implementieren.
- **Physisch bisher nur Rad Racer PAL 128/0 bestanden**.
  Fuer die beiden anderen vorhandenen MMC1-Cartridges wurden
  128KiB PRG + 32KiB CHR beziehungsweise 32KiB PRG + 32KiB CHR
  mitgeteilt. Die alten CRCs `F378619E`/`9FB32DEA` wurden
  mit falscher 128/0-Geometrie erzeugt und sind nicht beweiskraeftig.
  Die korrekten neuen Dumps stehen aus.
- Laborstart: `tools/nes-mmc1-lab.sh` (read-only DB-Probe und
  optionales manuelles Layout, `.so`-Swap mit automatischer
  Ruecksicherung). Nur nach Bestätigung **keine Batterie**,
  kein wertvoller fluechtiger RAM-Inhalt. Opt-in:
  `RETRODE3_MMC1_TEST_ACK=NO_BATTERY_TEST_ONLY`.
  Alte `nes-mmc1-sgrom-lab.sh` bleibt fuer Rad-Racer-Regression.
  Ausfuehrliche Testanleitung:
  [MMC1 allgemeiner Laborpfad](nes-mmc1-general-lab.md).
- Die REG-/M2-/ROMSEL-Signalsequenz fuer PRG-WRAM ist weiterhin
  nicht save-sicher verifiziert. Batteriegepufferte MMC1-Boards
  bleiben trotz Benutzerwunsch gesperrt, solange SRAM-Backup,
  Restore und Kernel-Timing fehlen. Normale `nes.so` bleibt
  MMC1-gesperrt und `get_capabilities` meldet weiterhin false;
  `master`, OS-Pakete und Kernel bleiben unberuehrt.
- UxROM/CNROM pruefen zunaechst buskonfliktfreie Adressen;
  fehlt eine sichere Adresse, wird vor jedem Registerwrite abgebrochen.
  Beim MMC3-ROM-Dump wird kein PRG-RAM-Write-Enable benutzt.
- Die dokumentierten PRG-/CHR-Groessen und Mapper-Untervarianten
  sind **Software-Grenzen**, nicht der Beweis elektrischer Sicherheit.
  Details: [NES Stage 2](nes-mapper-stage2.md).

## Bereits implementierte NES-v2-Funktionen

- Additive C-ABI `include/retrode3/core_operations_v2.h`, kompatibel mit
  den bisherigen SNES-/MegaDrive-v1-Cores.
- **Fast-Probe liest 1024 Bytes** (erste/letzte 512 Bytes PRG) statt
  eines kompletten ROMs. Ergebnis:
  `identification_status`, `candidates`, `read_layouts`,
  `selection_required`, Layout- und Mapper-Informationen.
- Ein interaktives `read_rom` akzeptiert `candidate_id`,
  `layout_id` oder ein vollstaendiges, strikt validiertes
  manuelles Layout. Ein nicht erkanntes/modifiziertes ROM darf als
  `verified:false` gespeichert werden.
- Einmaliger Payload-Dump mit CRC, nachtraeglicher iNES-Header-Auswahl
  und atomarer Dateipublikation.
- **Die automatische Fingerprint-Datenbank kennt derzeit nur
  Mapper 0/NROM**. Mehr Mapper in den Sanni-Datenbanken bedeuten
  weder Erkennung noch elektrische Freigabe.
- **Web-v2-Dropdown/Override sind noch nicht integriert**.
  Diese UI-Aufgabe gehoert ins OS-Repository `DragonBox-Shop/retrode3`;
  Web v2 muss Core-Kandidaten nutzen und unlesbare Layouts deaktivieren.

## Reproduzierbare Entwicklerpruefungen

```sh
git checkout develop
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-nes-mmc1-plan
make -f Makefile.migration test-native-nes-module
make -f Makefile.migration test-all-shared-cores
make -f Makefile.migration test-release-stage
```

- Die NES-Mock-Suite ist in `tests/test_nes_mapper_banks.py`,
  der Mock-Bus in `tests/mock_nes_kernel.cpp`.
  Abgedeckt sind u. a. Bankreihenfolge, kompletter Output/CRC,
  Probe ohne Writes, fehlende buskonfliktfreie Adressen,
  MMC1-Sperre, falsche Geometrie und Fehlerbereinigung.
- `tools/verify-nes-mapper-sequences.py` verifiziert die gepinnten
  Sanni-Sequenzen. Die Tests **existieren**; dieser Handoff behauptet
  **keinen frisch durchgefuehrten Testlauf**.
- `test-release-stage` erzeugt nur Build-/Staging-Dateien,
  **keine Installation, Aktivierung oder Paketfreigabe**.

## Echthardware-Referenz und noch offene Gates

### Neuer physischer Mapper-1-Referenzdump (2026-11-20)

Vom Projektinhaber am echten Retrode3-Geraet gelieferter vollstaendiger
CLI-Laborlauf mit Rad Racer (PAL), manuelles MMC1-SGROM-Layout
`mapper:1,submapper:0,prg_kib:128,chr_kib:0,battery:0`.
Das Laborskript verlangte die SGROM-/No-PRG-WRAM-Bestaetigung;
eine unabhaengige PCB-Foto-/Schaltungspruefung liegt nicht im Log vor.

| Messwert | Originales CLI-Ergebnis |
| --- | --- |
| Event | `result`, `ok:true`, `system:nes` |
| Datenlaenge | **131088 Bytes** inkl. 16-Byte-iNES-Header |
| PRG-Payload-CRC32 | **`654F4E90`** (bekannte PAL-Rad-Racer-Referenz: **`654F4E90`**) |
| Vollstaendige Datei-CRC32 | `B78FDA39` |
| Layout | `m1-128-0` |
| Titel/DB | `Unverified NES ROM (manual layout)`, `verified:false`, `matched_candidate_id:""` |
| Ausgabedatei | `/tmp/retrode3-rad-racer-mmc1-20261120-040057.nes` |
| Ruecksicherung | `Original NES core restored.` gemeldet |

**Ergebnis:** MMC1-PRG-Bankswitching hat auf diesem SGROM-Testmodul
einen korrekten vollstaendigen ROM-Payload geliefert. `verified:false`
ist die noch fehlende Mapper-1-Katalogidentifikation, kein
CRC-Fehler. Dieser Hardwarebeleg erlaubt **keine** allgemeine
MMC1-/PRG-WRAM-Freigabe: Kernel-`NES_REG`-/M2-/ROMSEL-Timing ist fuer
RAM-Boards weiter nicht geklaert, elektrische Oszilloskopmessung fehlt.
Es ist ein erfolgreicher einzelner physischer Labor-Dump, keine
Serien-/Wiederholbarkeitspruefung. Vollstaendige CI-Logausgabe wurde
nicht mitgeliefert.

**Frueher erfolgreich auf echter Retrode3, vor NES-v2:**

| Core | Modul | Referenz |
| --- | --- | --- |
| NES Mapper 0 | Excitebike (Europe) | 24.592 Bytes inkl. iNES; Datei-CRC32 `F749E780`, Payload `8A0C7337` |
| SNES HiROM | Terranigma D | 4 MiB; CRC32 `6FB8A4DF` |
| MegaDrive linear | Snake Rattle n Roll | 524.288 Bytes; CRC32 `543BED30` |

**Noch offen und in dieser Reihenfolge:**

1. Aktuellen `develop`-Stand bauen und **Mapper-0-Fast-Probe/v2-Dump**
   auf echter Hardware gegen Excitebike-CRC und Header pruefen;
   SNES-/MD-Regression sicherstellen.
2. Kernel-/Hardware-Revision, Slotspannung und Registerwrite-Timing
   fuer Mapper 2/3/4/7 **vor** jedem experimentellen Schreibzugriff
   konkret pruefen. MMC1-Timing separat klaeren.
3. Erst nach technischer Freigabe gezielte, kontrollierte
   Read-only-Dumps mit unkritischen echten Mapper-Cartridges
   (separater Experimental-Build), Groesse/CRC, Stabilitaet,
   Entfernen/Abbruch und unversehrtes Save-RAM dokumentieren.
4. Nach verifizierten Mappern den DB-Fingerprint-Katalog
   kontrolliert erweitern, Mehrdeutigkeiten zwischen Layouts
   simulieren und Mapper-/Layout-Auswahl in Web v2 integrieren.
5. Faehigkeiten erst nach dokumentierten Hardware-Tests freischalten;
   danach Verpackung/Installations- und Reboot-Tests im OS-Repo
   und erst zuletzt Promotion nach `master`.

### Verfuegbare NES-Test-Cartridges des Projektinhabers

Justice Duel, Tennis, MetalStorm, Rad Racer, WWF Wrestlemania,
Ferrari Grand Prix Challenge, Duck Tales, Mario & Yoshi, Dr. Mario,
Alwa's Awakening, Battletoads & Double Dragon, Dushlan, Expedition,
Little Medusa, Sam's Journey, StarTropics, Super Mario Bros.,
Super Mario Bros. 2, Super Mario Bros. 3, Tiny Toon Adventures.

**Wichtig:** Hier sind absichtlich keine Mappernummern behauptet.
Region/Revision/PCB koennen Unterschiede bewirken. Vor Tests die
konkrete Cartridge identifizieren und ein nachweislich passendes
Leselayout bestimmen. Kein riskanter Versuch nur anhand des Spieletitels.

## Naechste Entwicklungsstufe: NES-SRAM-Schutztests

**Ziel:** Vor allgemeinen MMC1-Dumps an Save-Cartridges eine reproduzierbare
Vorher/Nachher-Pruefung und einen separat erprobten Restore-Pfad.

### Gepruefter Kernel-Iststand (2026-11-20)

- `retrode3-kernel/drivers/misc/retrode3/retrode3_cart.c` behandelt
  `NES_RAM` und `NES_WRAM` im **read**-Switch derzeit als
  `mode not supported`. Der Modus ist im `retrode3-lib.h` zwar
  definiert, aber nicht als Lesezugriff implementiert.
- `NES_PRG` ist ein vorhandener lesender CPU-Bus-Modus. Ein Zugriff
  auf `$6000-$7FFF` koennte auf manchen MMC1-Boards PRG-RAM
  ausgeben. Ob die Chip-Select-/M2-/ROMSEL-Signale am echten
  Retrode3 in diesem Bereich passen, ist **noch nicht verifiziert**.
  Diesen Pfad zunaechst nur mit nachweislich *read-only* Testcode
  und nicht-sensibler Cartridge pruefen; keinen Erfolg behaupten.
- Manche MMC1-Varianten sperren PRG-RAM per Register. **Keine**
  generischen Mapperwrites fuer den ersten Backup-Versuch aktivieren.

### Freigabereihenfolge

1. **Read-only SRAM-Prototyp:** Bestehende Kernel-`NES_PRG`-Leseoperation
   bei `$6000-$7FFF` mit Testcartridge/Bus-Lock pruefen;
   keine Register- oder RAM-Schreibzugriffe. Bei fehlendem
   oder instabilem RAM-Signal stattdessen einen klar getrennten
   Kernel-Patch fuer *nur* den `NES_WRAM`-Leseweg untersuchen.
2. **Sichern:** Gemeinsames `retrode3-cli` um einen typisierten
   `read_save`-Pfad erweitern (kein permanenter zweiter Reader).
   Datei atomar/no-overwrite, 8192 Bytes nur wo korrekt,
   mit CRC32, Stromausfall-/Abbruch-/Fehlerbehandlung und Bus-Lock.
   Zwei identische Dumps reichen **nicht** als Beweis fuer echtes
   RAM: auch Open-Bus-/ROM-Mirroring, Save-Format und Power-Cycle
   muessen kontrolliert werden.
3. **Vergleichen:** Gesichertes Original vor dem ROM-Dump, danach
   und nach Aus-/Einschalten erneut lesen. Byte-fuer-Byte-Diff,
   CRC und Game-/Emulator-Plausibilitaet pruefen.
4. **Wiederherstellen:** Erst getrennten Restore-Modus entwickeln,
   **nur** auf entbehrlicher Testcartridge mit eigenen Testdaten
   erproben, danach Readback + Power-Cycle verifizieren.
   Eine Backup-Datei ohne getesteten Restore rettet einen
   bereits beschaedigten Save nicht automatisch.
5. **Mapper-Freigabe:** Erst nach wirksamer Save-Sicherung/Restore,
   korrekter Register-Timing-Pruefung und wiederholten erfolgreichen
   Hardwaretests die MMC1-Unterstuetzung fuer PRG-WRAM-Boards
   ausweiten. Rad Racer SGROM bleibt der bekannte PRG-CRC-Test.

**Testhardware laut Projektinhaber (2026-11-20):** Mehrere
MMC1-Cartridges **ohne Speicherbatterie** sind vorhanden, deren
konkrete Mapper-/PCB-/RAM-Ausstattung aber noch einzeln zu
identifizieren ist. Ausserdem stehen japanische **Famicom**
Final-Fantasy-I- und Final-Fantasy-II-Cartridges mit Batterie zur
Verfuegung. Fuer diese wird ein **Famicom-60-Pin-auf-NES-72-Pin-Adapter**
benoetigt; Pinbelegung, Ausrichtung, CPU-Bus/M2/ROMSEL und 5-V-Versorgung
vor dem Einsatz pruefen. Nicht auf batteriegepufferte Module gehen,
bis der Lese-/Backup- und **separat hardwareverifizierte Restore-Pfad**
funktioniert. StarTropics (MMC6) bleibt eine getrennte Mapper-4-Aufgabe.

**Dokumentierte Referenzplatinen, nur nach Abgleich mit den tatsaechlichen
Cartridges verwenden:**
- Final Fantasy I (JP, SQF-FF): HVC-SNROM, MMC1, PRG **256 KiB**,
  CHR-RAM **8 KiB**, batteriegepuffertes PRG-WRAM **8 KiB**.
  Varianten mit MMC1A und MMC1B2 dokumentiert.
- Final Fantasy II (JP, SQF-FY): HVC-SNROM, MMC1/oft MMC1A,
  PRG **256 KiB**, CHR-RAM **8 KiB**, batteriegepuffertes
  PRG-WRAM **8 KiB**.
- Falls statt zwei separaten Spielen die japanische Kombi
  **Final Fantasy I & II** (SQF-FO, 1994) gemeint ist:
  **HVC-SXROM**, MMC1, PRG **512 KiB**, batteriegepuffertes
  PRG-WRAM **32 KiB**. Hier sind zusaetzliche RAM-Banks zu
  pruefen; niemals einfach als einzelnes 8-KiB-SNROM behandeln.

Quellen: nesCartDB Profile 3605, 3435 und 3950; NESdev MMC1
ASIC-Revisionsdokumentation und Cartridge-Connector-Pinbelegung.

**Empfohlene Reihenfolge:** Zuerst SRAM-Read-Probe mit ungefaehrdetem
Modul, dann byteidentische Doppel-Backups mit bereits gepruefter
Busverriegelung, dann entbehrliche Save-Daten und Restore testen,
zuletzt FF1/FF2 (nur mit geprueftem Adapter).
`master` und alle regulaeren NES-Capabilities bleiben unberuehrt.

## Handoff bei neuem Chat

1. `retrode3-oscr/develop/AGENTS.md` und **diese Datei** lesen;
   aktuellen Code der betroffenen Funktionen pruefen.
2. **Nur eine abgegrenzte Aufgabe** bearbeiten und relevante
   Mock-/Build-Tests ausfuehren. `develop` ist die gemeinsame Linie.
3. Nach jeder abgeschlossenen Aenderung Commit/Dateien, Tests
   (**Mock vs. echte Hardware**), bekannte Risiken und naechsten
   sicheren Schritt **in dieser Datei aktualisieren**.
4. Chat-Antwort knapp halten: Commit-Link, konkret geaenderte
   Funktionen, Testergebnis, Hardware-Gate. Keine vollstaendigen
   Quelltexte in den Chat kopieren.
5. **Keine** Behauptung eines erfolgreichen physischen Tests, ohne
   dokumentierte reale Retrode3-Ausgabe und CRC.

### Letzte Aenderung (2026-11-20)

- **MMC1 fuer PRG+CHR-ROMs implementiert**: 32/32 und 128/32,
  plus die konservative PRG-/CHR-Geometriematrix aus Sanni NES.cpp.
  CHR-Modus wird vor Bankwechsel explizit auf 8KiB gesetzt.
- Alle **695 MMC1-Datensaetze** aus gepinntem Sanni-DB-Blob
  `b07cfd2943b986f8ffc85e61722dbb2e440cec37` als generierter
  C++-Katalog. Reproduzierbares Skript:
  `tools/gen-nes-mmc1-catalog.py`. Keine ROM-Bytes importiert.
- Fast-Probe weiter nur 1024 Bytes, kein Mapper-Write; manuelle und
  DB-basierte Layoutwahl; Full-Payload-CRC fuer eindeutige Verifikation.
  Der CLI-Host wurde fuer `m1s5-32-32`-NES2-Untermapper-Layoutnamen
  angepasst.
- Mock-Kernel emuliert MMC1-PRG-/CHR-Banks und volle ROMs;
  Dr. Mario Europe/Mapper1/Submapper5-Datenbank-Fingerprint wird
  mit synthetischem, copyrightfreiem 512-Byte-Testblock geprüft.
  GitHub NES Workflow auf `develop` **erfolgreich am Commit
  `54caadc9`** ([CI-Lauf 44675866914](https://github.com/DragonBox-Shop/retrode3-oscr/actions/runs/44675866914)):
  Mock-PRG/CHR-Bankfolge, Dr.-Mario-Europe-Datenbank-Fingerprint,
  `verified:true` durch synthetische komplette Payload-CRC
  `9735D267`, submapper-5-NES2-Header fuer unbekannte ROMs,
  regulaerer NES-Core-Build und separater Labor-Build,
  Shell-Syntaxpruefung. Physische MIPS-Testlauf-Protokolle folgen
  erst nach Nutzertests.
- Neues `tools/nes-mmc1-lab.sh`: gesperrtes Testmodul einlegen,
  no-battery-Bestaetigung, automatische DB-Erkennung oder manuelle
  PRG/CHR-Werte, temporäre `nes.so`, automatische Restore-Falle.
- Synthetische Header fuer unbekannte, manuell gewaehlte NES-2.0
  MMC1-Submapper werden korrekt codiert (insbesondere Submapper 5,
  32KiB PRG/32KiB CHR); eigener Mock-Test vorhanden.
- **Echtgeraet bisher**: Rad Racer PAL 128/0, Payload `654F4E90`,
  Gesamt `B78FDA39`, temporaere `.so` wiederhergestellt.
  **Echtgeraet ausstehend**: PRG 128+CHR 32, PRG 32+CHR 32.
  Batterie-Saves, PRG-WRAM-Timing, Kernel-SRAM-Backup/Restore offen.
- `master`, reguliere Cores und Debian-Pakete nicht modifiziert.

### Aktueller naechster Schritt

`develop` auf der Retrode3 aktualisieren,
`test-native-nes-module`, `native-core-registry` und
`native-nes-mmc1-lab` bauen, dann mit **den beiden vorhandenen
MMC1-Modulen OHNE Batterie** per `tools/nes-mmc1-lab.sh`
erst `probe` automatisch, danach bei unbekanntem/mehrdeutigem
Layout `128 32` beziehungsweise `32 32` manuell testen.
Vollstaendige NDJSON-Ausgabe/Dateigroesse/Payload-CRC dokumentieren,
danach sichere SRAM-Routinen und andere Mapper.
**Keine allgemeine MMC1-Freigabe.**
