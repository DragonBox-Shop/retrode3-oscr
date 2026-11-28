# NES-Mapping: aktueller Entwicklungsstand und Chat-Handoff

## Homebrew und Submapper-Familien (2026-11-28)

- 8 weitere Mapper-IDs: 21 (VRC4a, Submapper 1), 25 (VRC4b/d,
  Submapper 1/2), 28 (Action 53), 30 (UNROM 512, Submapper 0/1/2),
  78 (Submapper 1/3), 85 (VRC7b, Submapper 1), 94 (UN1ROM),
  210 (Namco 175/340, Submapper 1/2). 47 von 195 Upstream-PRG-IDs.
- Gemeinsame Submapper-Maske fuer HAL und v3-Schema; Action 53 bis
  1024 KiB PRG. Nur freigegebene Geometrien und keine Batterie.
  Mapper 30: konservative Konfliktpruefung auch ohne Buskonflikte,
  ausschliesslich Banklatch-Writes ab C000, kein Flash-Kommandofenster.
  Batterie-Flag kann Flash kennzeichnen und bleibt gesperrt.
  Manuelle NES2-Overrides fuer 30 nehmen konventionell 32 KiB
  CHR-RAM an; verifizierte Katalogheader gehen vor.
- Original-Sanni-Sequenzen und Generator unveraendert. Action 53
  verwendet die vorhandenen CPU-Expansion-Writes 5000; keine
  generische Freigabe niedriger Adressen. Kernel-Short-M2-Pfad
  umfasst 5000/8000 auf qualifizierter Hardware; andere Adressen
  koennen noch Legacy-Timing verwenden. Keine elektrische Messung.
- VRC4c, VRC7a und VRC23-Sonderheuristik noch nicht freigegeben;
  MMC5 braucht besonders Review der Reset-/M2-Timing-Anforderungen.
  Mapper 29 benoetigt sichere EXP0-Flashsteuerung und abweichende
  Bankwerte; nicht pauschal aktivieren.
- Quellen: https://www.nesdev.org/wiki/INES_Mapper_028 ,
  https://www.nesdev.org/wiki/INES_Mapper_030 ,
  https://www.nesdev.org/wiki/NES_2.0_submappers ,
  https://www.nesdev.org/wiki/INES_Mapper_210 .
- Synthetische Payload- und Registertests erweitert. Build/CI und
  physische Tests noch offen.

## Weitere Familienfreigaben (2026-11-28)

- 16 zusaetzliche Mapper-IDs: 18/22/24/26/32/33/48/65/68/112/117
  (direkte ASIC-Register) und 70/89/152/180/241 (diskrete Latches).
  Insgesamt 39 von 195 Upstream-PRG-Mappern fuer begrenzte ROM-Profile.
- Direkte Register-Policies begrenzen Adressen und Daten; keine
  SRAM/Flash-/Expansion-Writes. Latch-Policies suchen vor jedem
  Schreibzugriff einen passenden ROM-Wert im aktuellen Mapping.
  Sannis Bankalgorithmen bleiben unveraendert. CHR=0 in Profil-/Schema-
  Generierung unterstuetzt; gemeinsame Tabelle liefert Web-Auswahl.
- Neue synthetische Registermodelle und Payload-/Batterie-Regressionen
  hinzugefuegt. Build/CI und Hardwaretest stehen aus. Keine Freigabe
  von Mapper 30 Flashvarianten, VRC-Submappern oder RAM-Overlays.

## Familienfreigaben (2026-11-28)

- Hardware-Rueckmeldung: Metal Storm Retro-Bit mit kurzem M2-Schreibzyklus
  (Kernel-Commit 50ccec4) erfolgreich im Web ausgelesen und CRC-verifiziert;
  Datei-CRC 33AF86D9. Weitere vorhandene Mapper laut Entwickler erfolgreich
  nachgetestet. Neue Familien in diesem Commit sind noch ungetestet.
- Sanni-Stand e5a0832: 195 explizite PRG-Mapper, 142 CHR-Mapper.
  Jetzt 23 Mapper-IDs fuer eingeschraenkte ROM-Profile freigegeben.
  Zusaetzliche Familienprofile 9/10, 64/76/88/95/118/119/154/158, 69
  verwenden gemeinsame Register-HALs und eine Geometrietabelle, die auch
  die v3-Auswahl erzeugt. Upstream und Generator bleiben unveraendert.
- Nur Submapper 0, konservative ROM-Groessen; Batterie-Sperren bleiben.
  TQROM-Override kodiert 8 KiB CHR-RAM in NES 2.0; nur CHR-ROM gelesen.
  Keine neuen SRAM-, Flash- oder Low-Address-Writes.
- Mock-Tests fuer Payload, Header, schreibfreie Probe, erlaubte Adressen
  und Batterie-Sperre hinzugefuegt. Build/CI und Hardwaretest noch offen.
- Naechste Gruppen: diskrete Latches mit Buskonflikten, weitere
  ASIC-Registerfamilien, Expansion/WRAM-Overlays, spezielle M2-Pulse
  und SRAM-sichere Write-Primitive. Nicht allein aufgrund einer
  gemeinsamen Upstream-Switch-Gruppe freigeben.

## Metal Storm: Original-Registersequenz (2026-11-27)

- Hardware: 64 identische 8-KiB-Baenke, Payload-CRC 4CC537AA;
  Kernel-M2-Lese- und Schreib-Timing-Experimente aendern CRC nicht,
  die Turbo-Leseroutine beschleunigt den Dump.
- Dokumentiertes Retro-Bit-PCB: 2 MiB Flash mit wiederholtem 512-KiB
  Payload, zwei 128-KiB CHR-RAMs, kein separates PRG-RAM:
  https://nerdlypleasures.blogspot.com/2020/01/product-review-retro-bits-metal-storm.html
- Nur bei eindeutiger frischer Auto-/Katalogerkennung dieses Titels
  (CRC-Referenz 1BE6552C, Mapper 268.0, 512/0) reicht die HAL
  Sannis A001=80 und beide Outer-Fenster 5000-5003/6000-6003 unveraendert
  durch. Keine neuen Bankalgorithmen oder Werte, keine Flash-Kommandos.
  Manuelle und andere SMD133-Profile behalten A001=00 und ihr Fenster.
  NESdev dokumentiert WRAM-unabhaengige Outer-Register; die Aenderung
  ist ein gezielter Vergleich mit Upstream, keine gesicherte Fehlerursache.
- Mock erwartet fuer den bekannten Titel die Originalsequenz;
  andere Profile bleiben unveraendert. CI und Hardwaretest offen.

## Metal Storm: regulaerer Auto-Probe/Read (2026-11-27)

- Supplement zum gepinnten Sanni-Katalog: Metal Storm (World) (Retro-Bit),
  512 KiB PRG, 0 CHR-ROM, 256 KiB CHR-RAM, Mapper 268 Submapper 0,
  horizontales Mirroring, keine Batterie, Payload-CRC32 1BE6552C.
  Referenz: NewRisingSun NES 2.0 XML DB, Modern/Gravity Trooper
  Metal Storm (Collector's Edition), Spiegel:
  https://github.com/Eggmansworld/Eggmans_NRSVault_Mini/blob/main/files/nes20db.xml
  SHA1 618854CA960CD8A3B35ED4D2B482C528F26423D9; kein Overdump.
- Gemessenes Probe-Paar 0E94BBA9/B2AA7578. Letzterer Wert ist nur
  zusaetzliche Bestaetigung (512 Nullbytes); niemals allein matchen.
  Normale Web-/CLI-Autoauswahl, Original-Sanni-Switch und CRC-Pruefung.
  Falscher CRC wird als mismatch ausgegeben; keine Verifikation vor Dump.
  Andere SMD133-Boards behalten ihre bestehenden Freigaberegeln.
- Mock mit synthetischen CRC-Patches (keine Original-ROM-Daten) prueft
  Auto-Probe, Auto-Dump, NES2-Header und CRC-Mismatch. Hardware/CI offen.
  Vorherige Angaben zu manueller Auswahl sind fuer diesen Titel ueberholt.

## CPU-Register-HAL / SMD133 (2026-11-27)

- `nesmapper` trennt PRG-Register, CPU-Expansion ($4020-$5FFF)
  und Register-Overlay im WRAM-Fenster ($6000-$7FFF). Alle verwenden
  den vorhandenen NES_MODE_PRG (10) CPU-Schreibzyklus, der im Kernel
  ROMSEL anhand A15 setzt; keine neue Kernel-ABI und keine generische
  Freigabe fuer niedrige Adressen, SRAM oder Flash.
- Mapper 268 verwendet den generierten Sanni-Switch, fuer den
  manuellen 512 KiB PRG / 0 KiB CHR-ROM / 256 KiB CHR-RAM-Profiltyp.
  Submapper 0: nur $6000-$6003, Submapper 1: nur $5000-$5003.
  Die HAL unterdrueckt die vom Upstream parallel angesprochene
  unpassende Adressgruppe. A001=80 wird auf A001=00 abgebildet,
  um WRAM deaktiviert zu halten; Outer-Register nur mit genau
  zugelassenen Werten, MMC3 nur 8000=06 und 8001=0..63.
  Kein Auto-Read fuer 268, keine Batterie, kein CHR-ROM, keine
  anderen Submapper/Groessen, keine Flash-Kommandos.
- NES 2.0 Header kodiert jetzt auch Mapper >255 bei Submapper 0;
  beim manuellen SMD133-Profil Byte 11 = 0C (256 KiB CHR-RAM).
  Erwartete Dateilaenge 524304 Bytes. Keine CRC oder Identitaet
  fuer Metal Storm erfunden. Der konkrete Submapper der echten
  Cartridge muss ueber PCB/5-6K-Bruecke oder Referenzheader
  feststehen. Stromlos neu einlegen vor jedem physischen Versuch.
- Mock testet beide Adressfenster, vollstaendiges PRG-Payload,
  NES2-Header, WRAM-disable vor Outer-Writes, keine Probe-Writes
  und Ablehnung nicht freigegebener Layouts. Noch nicht ausgefuehrt.
  Kernel/Hardware-Timing und CI bleiben ungetestet.
- Quellen: https://www.nesdev.org/wiki/COOLBOY (Registerdekodierung
  und WRAM-Unabhaengigkeit), https://github.com/sanni/cartreader/discussions/290
  (512 KiB Metal Storm). Historische Aussagen zur fehlenden HAL
  weiter unten beziehen sich auf den vorherigen Stand.

## Weitere generierte Mapper-Familien (2026-11-27)

- Entwicklerversion: 11 (Color Dreams), 13 (CPROM), 66 (GxROM),
  71 (Camerica) und 206 (DxROM), jeweils nur Submapper 0 und die
  konservativen ROM-Geometrien in `nesmapper::geometry`.
- Keine neuen ROM-Bankalgorithmen: alle Sequenzen stammen aus dem
  automatisch generierten Sanni-Switch. Die HAL verwendet die bestehende
  konfliktfreie PRG-Registeroperation fuer diskrete Latches und die
  MMC3-Registeroperation fuer DxROM. CPROM liest ausschliesslich PRG.
  Keine Freigabe von RAM-/Flash-Writes oder Adressen unter $8000.
- Headerpruefung unterstuetzt nun auch Mapper >15 und NES-2.0-Groessen.
  Durch den vollen Katalog ist Rad Racer / Highway Star jetzt bewusst
  layout-mehrdeutig (Mapper 312/418 haben gleiche Fingerprints).
  Explizite Wahl des MMC1-Layouts bleibt moeglich; kein Auto-Write.
- Neue Mock-Regressionsfaelle in test_nes_mapper_banks.py, echte
  Hardwarepruefung und CI-Ausfuehrung noch offen. Master unberuehrt.
- Mapper 268 (SMD133) erfordert weiterhin sichere separate
  Outer-Registeroperationen unterhalb $8000, Submapper-Pruefung und
  eine passende RAM-/Flash-Schutzstrategie. Kein pauschales Freischalten.

## Vollstaendiger gepinnter NES-Katalog (2026-11-27)

- 5562 Originaldatensaetze fuer 269 Mapper aus DB-Blob
  b07cfd2943b986f8ffc85e61722dbb2e440cec37, inkl. Eintraegen ohne
  Fingerprints und NES-2.0-Sondergroessen. 226 bisherige
  NROM-IDs, 695 MMC1- und 1696 banked-IDs bleiben erhalten;
  521 normale NROM- und 2424 weitere Datensaetze ergaenzt.
- `tools/gen-nes-full-catalog.py` reproduziert die vier generierten
  Katalogteile offline aus dem gepinnten JSON. PRG/CHR-Groessen kommen
  aus den originalen Headern, nicht aus gerundeten OSCR-Bankfeldern.
  NROM-CRC-Abgleich umfasst jetzt auch Datensaetze ohne Probe-Fingerprint.
- Die v2-ABI bleibt unveraendert. Mehr als acht unterschiedliche
  Fingerprint-Layouts liefern Kandidaten, aber keine automatische
  Layoutfreigabe. Hardwarefreigaben bleiben getrennt: kein zusaetzlicher
  Mapper-Schreibzugriff, kein SRAM-/Flash-Support. Mapper 268 ist im
  Katalog und im generierten Upstream-Switch enthalten, aber dessen
  Outer-Register und Sondervarianten sind noch nicht HAL-freigegeben.
- Noch kein Build-/Hardwaretest dieses Stands. CI prueft die generierten
  Dateien gegen den Originalblob; reale Regressionstests folgen.

## Metal Storm Retro-Bit (2026-11-27)

Echthardware-Probe: $8000 0E94BBA9, $E000 B2AA7578.
Letzterer ist die CRC32 von 512 Nullbytes und hat vier falsche
Kandidaten geliefert. Konstante 512-Byte-Fenster werden nun in v2/v3
nicht mehr zum Katalogabgleich verwendet; Diagnose-CRC bleibt sichtbar.
Mock-Regression hinzugefuegt, CI/Hardware-Nachtest offen.

Der Nutzer hat die Retro-Bit-Neuauflage bestaetigt. Der alte 384-KiB
MMC3-Dump (Payload 3BFF454A) ist kein gueltiger Referenzdump.
Mapper 268 (SMD133), 512 KiB PRG, 0 CHR-ROM, 256 KiB CHR-RAM
und NES 2.0 Header sind erforderlich. Sannis gepinnter Code hat eine
268-Sequenz mit Outer-Registern unter $8000; vor Portierung konkrete
Submapper-/Registerdekodierung und RAM-/Flash-Schutz pruefen.
Kein Mapper-268-Schreibzugriff aktiviert; keine neue Titel-CRC erfunden.
Quellen: https://github.com/sanni/cartreader/discussions/290
und https://www.nesdev.org/wiki/COOLBOY .

> **Stand: 2026-11-26 | Entwicklungsbranch: `develop`**
>
> **Aktueller Entwicklungsstand (ersetzt widersprechende historische Angaben unten):**
> Mapper 2 (UxROM), 3 (CNROM), 4 (MMC3) und 7 (AxROM) sind jetzt
> im regulaeren Entwicklungs-Core fuer explizite, batterielose manuelle
> Layouts aktiviert. Die v3-Capabilities und die v2/v3-Validierung
> stimmen ueberein. Keine neuen Datenbank-Auto-Freigaben: Fingerprints
> und Payload-CRC-Katalog fuer diese Mapper sind jetzt ebenfalls
> integriert (1696 gepinnte Sanni-DB-Eintraege, inklusive nicht
> freigegebener Layouts). Submapper 0/1/2 fuer 2/3/7 sind explizit
> geprueft und verwenden weiterhin konfliktfreie Adressen.
> DuckTales PAL: 2/2, 128/0, CRC D029F841;
> WWF WrestleMania PAL: 7/1, 128/0, CRC 6C4A9735.
> Erkennung beruht weiterhin auf zwei schreibfreien Fingerprints;
> erst der Payload-CRC bestaetigt den Titel. Hardwaretest ausstehend.
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
