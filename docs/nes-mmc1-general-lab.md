# NES MMC1: ROM-Bankleser, OSCR-Datenbank und erste PRG+CHR-Hardwaretests

**Stand 2026-11-20, Branch `develop`, KEINE allgemeine Freigabe.**

## Erreicht

- Pinned `oscartreader/oscr-databases` (commit
  `87a48948c6a04260205f148d313614cc44e05cd5`):
  695 MMC1-Datenbankeintraege mit PRG/CHR-Layout, NES2-Submapper,
  Fingerprints, originalem 16-Byte-Header und kompletter Payload-CRC.
  `iprg` ist in 16-KiB-Banks, `ichr` in **4-KiB-Banks** codiert.
  Reproduzierbare Quelle: `tools/gen-nes-mmc1-catalog.py`.
- `fast_probe` liest weiterhin nur zwei 512-Byte-PRG-Fenster.
  Es werden **keine** Mapper-Register geschrieben, auch nicht zur
  Identifikation. Eine unbekannte Cartridge darf unbekannt bleiben.
  Untermapper, Kandidaten, Header-Konflikte und mehrere moegliche
  Layouts koennen eine **manuelle Auswahl** erfordern.
- MMC1-ROM-Dump: aus Sannis gepinnter `NES.cpp` abgeleitete
  16-KiB-PRG-/8-KiB-CHR-Bankfolgen, bei 32 KiB PRG linearer
  32-KiB-PRG-Dump. Fuer CHR wird der 8-KiB-Modus vor jedem
  Bankwechsel explizit gesetzt, damit er nicht vom vorherigen
  MMC1-Registerzustand abhaengt.
- Abgegrenzte und getestete PRG-Groessen **32/64/128/256/512 KiB**
  und CHR-ROM-Groessen **0/8/16/32/64/128 KiB**. CHR=0 bedeutet
  **CHR-RAM**, das beim ROM-Dump nicht gelesen oder geschrieben wird.
  Submapper 0 und Submapper 5 (32 KiB PRG) sind im Labor erlaubt.
  Die Datenbank kann weitere Untermapper enthalten, die sichtbar,
  aber **nicht lesbar** bleiben.
- Nach dem einmaligen kompletten Dump wird die Payload-CRC32 mit
  passenden Mapper-1-Datenbankeintraegen verglichen. Auch Titel
  **ohne Fast-Probe-Fingerprint** koennen so erkannt werden. Bei
  exaktem eindeutigem Treffer liefert das CLI `verified:true`;
  bei Mehrdeutigkeit oder unbekanntem ROM `verified:false`.
  Bei manueller unbekannter Konfiguration ist der erzeugte iNES-Header
  synthetisch, daher ist die Mirroring-Angabe vor Emulatortests zu pruefen.

## Hardware-Verifikation

Der vorangegangene **Rad-Racer-PAL-SGROM**-Hardwaretest:
128 KiB PRG-ROM / 0 KiB CHR-ROM, Payload-CRC32 **`654F4E90`**,
vollstaendige Datei-CRC32 `B78FDA39`, regulärer NES-Core nach
Laborversuch wiederhergestellt.

**Die neuen 32+32- und 128+32-KiB-Bankleser sind bislang NUR
mock-/CI-getestet.** Die genaue Cartridge/PCB und ihr Status
bzgl. Batterie und RAM muss vor dem Versuch real kontrolliert werden.
Die zwei frueheren Test-CRCs aus dem alten *falschen* 128+0-Leser
(`F378619E`, `9FB32DEA`) sind **keine** gueltigen ROM-Referenzen.

Sannis spezieller `write_reg_byte`-Pfad ist fuer MMC1-Register
`$E000-$FFFF` notwendig, damit auf Boards mit batteriegepuffertem
PRG-RAM keine ungewollten RAM-Writes auftreten. Der aktuelle
Retrode3-Kernel bietet diese Impulsfolge noch nicht sicher an.
Das separate Labor nutzt `NES_MODE_PRG`, ist daher NUR fuer
**persoenlich gepruefte Cartridges OHNE BATTERIE und ohne wertvolle
fluechtige RAM-Daten** gedacht. Es erfolgt kein Save-RAM-Backup
und kein Save-RAM-Restore. Fuer normale Benutzer, Saves und
batteriegepufferte Module ist der Code **nicht freigegeben**.

## Auf echter Retrode3 bauen

```sh
cd /usr/local/src/retrode3-oscr
git status --short
git fetch origin
git checkout develop
git pull --ff-only origin develop
git submodule update --init third_party/oscr-firmware

make -f Makefile.migration test-native-nes-module
make -f Makefile.migration native-core-registry
make -f Makefile.migration native-nes-mmc1-lab
```

Bei lokalen Git-Aenderungen erst pruefen; bei fehlgeschlagenen
Tests keinen Hardwareversuch durchfuehren.

## Auslesen

Nach Pruefen der eingelegten Cartridge auf **keine Batterie**,
als root und ohne parallele Web-/Cartridge-Jobs:

```sh
# Erst versuchen, automatisch zu erkennen:
sh tools/nes-mmc1-lab.sh

# Bei unbekannter/mehrdeutiger Fingerprint-Erkennung explizit:
sh tools/nes-mmc1-lab.sh 128 32
sh tools/nes-mmc1-lab.sh 32 32

# Fuer bekannte 32-KiB-SNROM-/Submapper-5-Varianten:
sh tools/nes-mmc1-lab.sh 32 32 5
```

Das Tool verlangt die exakte interaktive Bestaetigung
`NO-BATTERY-TEST`. Es tauscht die installierte
`/usr/lib/retrode3/cores-available/nes.so` nur voruebergehend
gegen `build/migration/mmc1-lab/nes.so` aus. Das CLI fuer den
Test ist `build/migration/retrode3-cli`, der Produktions-CLI-
Binary `/usr/bin/retrode3-cli` wird nicht ersetzt.
Danach wird die urspruengliche NES-`.so` via Trap wiederhergestellt.
**Ein harter Stromausfall kann den Restore verhindern.**
Bei Abbruch/Fehler kein ROM als erfolgreich betrachten.

Das CLI liefert das Ergebnis mit Titel, `verified`, Layout,
`payload_crc32`, `crc32`, Dateipfad und Laenge. Erwartete
Laengen:
- 128 KiB PRG + 32 KiB CHR + 16-Byte-iNES = **163856 Bytes**.
- 32 KiB PRG + 32 KiB CHR + 16-Byte-iNES = **65552 Bytes**.
- Rad Racer SGROM 128 KiB PRG + 0 KiB CHR = **131088 Bytes**.

## Noch offen

- Echte Hardware-CRCs fuer 128+32 und 32+32 ermitteln.
- MMC1-Revisions-/PCB-Timing und RAM-Sicherung/Restore entwickeln,
  bevor Cartridges mit Batterie verwendet werden.
- Web-v2-Mapperauswahl aus `candidates`/`read_layouts`,
  Datenbank-Fingerprints und `read_supported` (OS-Repository).
- Nach wiederholbaren echten Tests Produktion/Packaging pruefen;
  `master` bis dahin nicht aendern.
