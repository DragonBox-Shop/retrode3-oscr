# NES-Mapping: aktueller Entwicklungsstand und Chat-Handoff

> **Stand: 2026-11-20 | Entwicklungsbranch: `develop`**
>
> Kompakter Einstieg fuer neue Chats und Entwickler. Zuerst
> [`AGENTS.md`](../AGENTS.md) und **diese Datei** lesen, danach bei
> Bedarf [`nes-mapper-stage2.md`](nes-mapper-stage2.md) und
> [`nes-fast-probe-mapper-selection.md`](nes-fast-probe-mapper-selection.md).
> Der aktuelle Quellcode hat bei Widerspruechen Vorrang vor historischen
> Abschnitten. **Implementiert != physisch verifiziert != freigegeben.**

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
| **1 MMC1** | Bankplan + abgeschotteter SGROM-Labortest mit echten Registerwrites (128 KiB PRG / CHR-RAM) | Isolierter C++11-Test lokal bestanden; neue End-to-End-Mocks/CI ausstehend | Nein | Gesperrt (Laborbuild getrennt) |
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
- MMC1 ist nun in einer **separaten, manuell bestaetigten
  Labor-.so** fuer echte PRG-Registerwrites verdrahtet:
  `make -f Makefile.migration native-nes-mmc1-lab`.
  Nur Mapper 1/Submapper 0, PRG 128 KiB, CHR 0 KiB, Batterie 0
  und eine **nachweislich PRG-WRAM-freie Rad-Racer-NES-SGROM-Platine**
  sind als erstes Testziel vorgesehen.
- Das separate `RETRODE3_NES_MMC1_LAB`-Build braucht zusaetzlich
  `RETRODE3_MMC1_NO_PRG_RAM_ACK=RAD_RACER_SGROM_128K_CHRRAM_NO_WRAM`.
  `get_capabilities` bleibt fuer Mapper 1 `read_supported:false`;
  es gibt keine Web-/Auto-Freigabe.
- Der normale NES-Core und `stage-retrode3-runtime` bleiben ohne
  ausfuehrbare MMC1-Schreibfreigabe. `master` ist unberuehrt.
- Sannis spezielle `NES_REG`-/M2-/ROMSEL-Pulsfolge ist im Kernel
  noch **nicht sicher implementiert**. Das Labor verwendet deshalb
  nur den allgemeinen PRG-Write auf einer Cartridge **ohne PRG-WRAM**.
  Keine anderen MMC1-Platinen ausprobieren; vor allgemeiner Freigabe
  braucht der Kernel weiterhin einen elektrisch sicheren Registerpfad.
- Testanleitung mit temporaerem `.so`-Swap, automatischer
  Ruecksicherung und CRCs: [MMC1-SGROM-Labor](nes-mmc1-sgrom-lab.md).
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

- C++11-MMC1-Bankplan im getrennten Labor-Build an den gemeinsamen
  NES-Core und CLI angeschlossen; SGROM-128-KiB-PRG-Layout freigegeben
  **nur nach manuellem Runtime-Opt-in**. Kein Web-/Auto-Auslesen.
- Vollstaendigen Mock fuer MMC1-5-Bit-Schieberegister, 8 PRG-Banks und
  Fehler-/Schutzfaelle hinzugefuegt. Tests in
  `tests/test_nes_mapper_banks.py` stehen auf `develop`.
- `native-nes-mmc1-lab` erzeugt eine separate, nicht paketierte
  `build/migration/mmc1-lab/nes.so`. Die normale `nes.so` bleibt
  MMC1-gesperrt. Automatischer Restore nach Test ueber
  `tools/nes-mmc1-sgrom-lab.sh`.
- GitHub Actions fuer NES laeuft nun auch auf Push nach `develop`
  und baut die Labor-`.so` zur Compiler-Pruefung.
- **Nachgewiesen:** isolierter C++11-Bankplan-Test lokal erfolgreich.
  **Ausstehend:** vollstaendige GitHub-CI/Mock-Integration und
  GCC-4.9-MIPS-Build, echter SGROM-Dump/CRC, elektrische Pruefung.
- `master`, regulare Core-Capabilities, Pakete und Kernel nicht geaendert.

### Aktueller naechster Schritt

`make -f Makefile.migration test-native-nes-module` und
`native-nes-mmc1-lab` auf der Retrode3 ausfuehren. Danach nur
mit physisch bestaetigter, PRG-WRAM-freier NES-SGROM-Rad-Racer-Platine
den dokumentierten Laborversuch machen. Danach Mapper-0-v2-
und SNES/MD-Regression nachholen. Keine allgemeine MMC1-Freigabe.
