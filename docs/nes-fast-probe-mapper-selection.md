# NES: schneller Probe, Mapper-Auswahl und manueller Override

**Status 2026-11-20: NROM-Fast-Probe und Auswahl im gemeinsamen CLI
auf `develop` implementiert; zusätzliche Mapper und Web-v2-Auswahl
noch offen. Hardware-Revalidierung nach dieser Änderung ausstehend.**

### Tatsächlich implementierte NDJSON-v1-Erweiterung (NES v2 Core-ABI)

Der `probe`-Befehl liefert im NES-Core
`identification_status` (`unique`/`ambiguous`/`unknown`),
`candidates` mit `id`, `title`, `layout_id`, `mapper`,
`submapper`, `prg_kib`, `chr_kib`, `read_supported` und
`payload_crc32_expected`, sowie `read_layouts`. Hinzu kommen
`selection_required`, `candidate_lookup:"fingerprint_only"` und
`checksum:null` (vor dem Dump absichtlich nicht bekannt).
Der Ergebnis-Event enthält `verified`, `payload_crc32`, `crc32`,
`layout_id` und `matched_candidate_id`.

Für `read_rom` sind `candidate_id` oder `layout_id` optional
(wenn nur eine sichere Konfiguration existiert). Ein manueller Override
benötigt **alle** Integer-Felder `mapper`, `submapper`,
`prg_kib`, `chr_kib`, `battery` (0/1) und den String
`mirroring` (`horizontal`/`vertical`/`four_screen`).
Nur NROM-Layouts mit PRG 16/32 KiB und CHR 0/8 KiB bestehen
die aktuelle physische Support-Validierung. Keine freie Bankumschaltung.
Die manuelle Option ist auch bei `unknown` zulässig.

Bei v2-Clients muss die Weboberfläche `checksum:null` akzeptieren und
Datenbanktreffer erst nach der tatsächlichen Dump-CRC als verifiziert
anzeigen. Der Payload wird einmal gelesen; der finale Header wird nur
in der privaten Ausgabedatei korrigiert. Tests prüfen die exakte Zahl
der Bus-Lesebytes. Versionierter ABI-Zusatz:
`include/retrode3/core_operations_v2.h`. Die untenstehenden
Spezifikationsabschnitte sind die übergeordneten Ziele, soweit sie noch
nicht umgesetzt oder hardwarevalidiert sind.


Ziel: automatische Web-Erkennung darf nicht das gesamte NES-ROM
lesen. Die spätere NES-Core-Erweiterung geschieht im gemeinsamen
`retrode3-cli`/`nes.so`, nicht als separater NES-Reader. Web UI
liegt im anderen Repository `DragonBox-Shop/retrode3`.

## Nachgewiesene Sanni-Daten

Die **gepinnte** `oscartreader/oscr-databases`-Revision
`87a48948c6a04260205f148d313614cc44e05cd5` bietet:
- `data/nes.json`: Spiele/Fingerprints/ROM-CRCs und iNES-/NES2-Header
  (Mapper-/Submapper-Bits, PRG-/CHR-Größen, weitere Headerdaten);
- `data/nes-mappers.json`: 129 Mapper-Definitionen mit expliziten
  Feldern `mapper`, `submapper`, `prglo`, `prghi`,
  `chrlo`, `chrhi`, `ramlo`, `ramhi` (KiB-Grenzen).
  Dies ist eine DB-Liste, **keine Liste der auf Retrode3
  implementierten/elektrisch geprüften Mapper**.

Der derzeit generierte `src/retrode3/oscr_nes_catalog.cpp` enthält
nur 226 zugelassene **Mapper-0-/Submapper-0-NROM**-Records und
`src/retrode3/oscr_nes_stage1.cpp` identifiziert aktuell per
1 KiB Fingerprints plus vollständiger PRG/CHR-CRC. Der `probe`
liest das gesamte Kandidaten-ROM; vor `read_rom` erfolgen
weitere vollständige Prüfungen. Dies ist der zu beseitigende
Performance-/Abbruch-Nachteil, insbesondere vor größeren Mappers.

## Verbindliches Zielverhalten

1. **Fast probe** bei Modul-Insertion: nur notwendige feste/sichere
   kleine Fingerprintfenster lesen, passende Kandidaten auflisten,
   ohne vollständigen PRG-/CHR-CRC-Scan. Keine unsicheren
   Bank-/RAM-/Flash-Schreiboperationen im Hintergrund. Kein
   wiederholtes Neu-Proben durch das 300-ms-Web-Polling, solange
   der physische Slot unverändert ist.
2. Kandidaten haben stabile **datenbankinterne IDs** und mindestens
   Titel, Mapper, Submapper, PRG- und CHR-Größen sowie sichere,
   derzeit verfügbare Lesefähigkeit. UI muss nur die Daten des
   Cores zeigen, nicht selbst ROM-/Mapper-Logik implementieren.
3. **Kein Dropdown**, wenn nur ein lesbares Layout für alle
   Kandidaten nötig ist. Mehrere Regional-/Revisionstitel mit
   identischer sicherer Leser-Konfiguration lassen sich nach
   dem Dump per CRC präzisieren.
4. **Dropdown**, wenn Kandidaten verschiedene Mapper, Submapper,
   benötigte PRG-/CHR-Größen, Bank-Layouts oder sonstige für
   das sichere Lesen relevante Parameter verlangen. Es zeigt
   diese Informationen und markiert nicht implementierte Mapper
   als **nicht auswählbar**. Bloß dieselbe Mappernummer garantiert
   noch keine identische Lese-Konfiguration.
5. **Manueller Override jederzeit** (auch für unbekannte oder
   offenbar falsch erkannte Cartridges). Vom Benutzer bestätigt:
   unterstützter Mapper, Submapper, PRG-/CHR-Größe und weitere
   mapperabhängig nötige Angaben. Override muss auch dann
   möglich sein, wenn der Fast-Probe **keinen Treffer** meldet.
   Kein frei wählbarer Mapper allein reicht für einen sicheren Dump.
6. **Nur sichere und freigegebene Konfigurationen ausführen:**
   Die DB enthält auch nicht implementierte Mapper. Der Core
   validiert alle Benutzereingaben gegen seine **wirklichen**
   `get_capabilities` und die jeweiligen Hardware-/Kernel-
   Beschränkungen. Für unbekannte Hardware/Spannungen,
   unbewertete Bankswitch-Schreibzugriffe und nicht freigegebene
   Mapper bleibt `read_rom` gesperrt. Derzeit ist ausschließlich
   Mapper 0 / Submapper 0 read-only physisch belegt.
7. **Genau ein ROM-Daten-Durchlauf zum Dump:** beim Dump
   inkrementell Payload-CRC für Datenbankidentifikation und
   fertige Datei-CRC berechnen, keine vollständige Identifikation
   im vorgelagerten `probe` und keine erneute `detect()`-
   Volllesung in `read_rom`. Eine Änderung des Moduls oder
   ungültige Leseparameter müssen weiterhin sicher behandelt
   werden, ohne ungeprüfte Bank-Schreibaktionen.
8. Der 16-Byte-iNES-/NES2-Header ist **versionsabhängig**.
   Bei zunächst mehrdeutigen Kandidaten rohe PRG-/CHR-Daten
   sicher temporär puffern oder Platzhalterheader am Ende
   korrigieren; nach Payload-CRC den **zutreffenden**
   Datenbankheader wählen. Für unbekannte/modifizierte ROMs
   einen technisch gültigen Header **nur aus bestätigten**
   Konfigurationsdaten erstellen, fehlende erforderliche Header-
   Parameter abfragen. Temporäre Dateien nicht als fertigen
   `.nes`-Dump veröffentlichen. Kein vorhandenes Ziel überschreiben.
9. Ein nicht zur ausgewählten Identität passender ROM-CRC-Wert
   ist kein Grund, technisch korrekt und sicher ausgelesene
   Homebrew-/modifizierte ROMs stillschweigend zu verwerfen:
   Datei als `unverified`/unbekannt kennzeichnen und die
   erwartete vs. tatsächliche CRC melden. Verifizierungsstatus
   und Leserfolg bleiben getrennt. Eine Hardware-/Leseoperation
   darf dabei weiterhin bei echten Fehlern scheitern.
10. Automatische Dateinamen erst nach der zuverlässigen
    Identifikation vervollständigen. Vom Benutzer geänderte
    Namen behalten; Dateinamen im Backend sanitizen; Ausgabe
    über sichere serverdefinierte Pfade, atomar und ohne
    Überschreiben.

## CLI-/ABI-Schnittstelle (Design, noch keine gültige Syntax)

Der aktuelle gemeinsame `src/retrode3/ndjson_protocol.cpp`-Parser
erlaubt nur `mapping:"lo"|"hi"` (aus der SNES-Phase) und ein
SNES-begrenztes `size_bytes`. Er versteht **keinen**
NES-Mapper, Submapper, Kandidaten-ID oder PRG-/CHR-Sizes.
`retrode3_probe_v1` besitzt lediglich
`rom_size_bytes/title/mapping/checksum`; sein
`emit_probe` sendet fest
`candidates:[],candidate_lookup:"not_integrated"`.
Der alte NES-Adapter `read_rom` ruft
`nesstage1::detect()` erneut auf.

Erforderliche *neue* typed API:
- Fast-Probe mit `identification_status` (unique/ambiguous/unknown),
  möglichen `candidates` und einem oder mehreren sicheren
  `read_layouts`. Unbekannt ≠ genereller Probe-Fehler.
- Capabilities enthalten unterstützte Mapper/Submapper und
  layoutspezifische Größen, nicht die komplette Sanni-Datenbank.
- `read_rom` akzeptiert entweder eine vom Core ausgegebene
  `candidate_id`/`layout_id` oder eine **streng validierte**
  explizite `manual_layout`-Konfiguration. Die Browser-Angaben
  dürfen niemals direkt zu Hardwareoperationen führen.
- Das Core-ABI braucht dafür eine **versionierte Erweiterung**
  statt einer inkompatiblen Änderung an
  `retrode3_core_operations_v1`/`retrode3_probe_v1`.
  Bestehende SNES-/MD-Cores, deren Tests und Protokoll müssen
  während der Migration funktionieren; NDJSON muss neue Felder
  sauber validieren und Fähigkeitserkennung erlauben.
- Result enthält tatsächlich verwendete Konfiguration,
  Payload-CRC (zur NES-DB-Identifikation), Datei-CRC,
  ID-/Header-Auswahl und getrennten `verified`-Status.
  Benutzer-Cancel muss auch während längerer Hardware-Reads
  regelmäßig geprüft werden.

**Die beispielhaften Feldnamen oben sind eine Anforderungs-Skizze,
nicht bereits implementierte NDJSON-Befehle.** Vor Umsetzung ein
konkretes kompatibles Schema mit Mock-Fällen/Tests festlegen.

## Test- und Release-Gates

- Simulierte Cases: eindeutiger Treffer; Excitebike EU/Japan/USA
  mit identischem Leserlayout; identische Fingerprints und
  *verschiedene* Mapper; gleiche Mappernummer und andere
  PRG-/CHR-Größe; kein Treffer; false positive; manueller
  Override; unsupported mapper; Modulwechsel; Cancel;
  manipulierte Eingaben; CRC-Mismatch; ungültiger Header.
- Fast-Probe muss beweisbar nur wenige Bytes lesen; besonders
  bei potentiell großen ROMs niemals Hintergrund-Vollscan.
  Der ROM-Dump soll seine Payload genau einmal lesen.
- Echthardware-Regression mit NES Excitebike (Europe) NROM:
  bestehender kompletter `.nes`-CRC `F749E780`,
  Payload-CRC `8A0C7337`, Original-Header erhalten.
  SNES Terranigma `6FB8A4DF` und MD Snake Rattle n Roll
  `543BED30` dürfen nicht beeinträchtigt werden.
- Nicht getestete Mapper oder Schreibfunktionen nicht als
  funktionsfähig melden. Änderungen zunächst in
  `retrode3-oscr/develop` und Web-Anbindung in
  `retrode3/develop`, Echthardware-Test vor
  `master`-/Debian-Paketfreigabe.

**Wichtig:** Die Datenbank gibt möglichen Mapper/ROM-Aufbau
an, beweist aber nicht, dass das echte eingelegte Modul
diesen Aufbau zuverlässig besitzt. Manuelle Angaben sind
keine Hardware-Sicherheitsfreigabe.
