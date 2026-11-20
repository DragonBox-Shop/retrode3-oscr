# MMC1 / Rad Racer SGROM: erster Hardware-Labortest

**Stand 2026-11-20. Nur `retrode3-oscr/develop`, keine Releasefreigabe.**

> **Historischer Rad-Racer-spezifischer Laborlauf.** Fuer aktuelle
> PRG+CHR-ROMs und den eingebundenen Sanni-MMC1-Katalog jetzt
> [den allgemeinen MMC1-Lesetest](nes-mmc1-general-lab.md) verwenden.
> Die damalige `verified:false`-Aussage galt vor Einbau des neuen
> Mapper-1-Datenbankkatalogs. Die Rad-Racer-Pruefsummen bleiben korrekt.


Dies ist ein **echter experimenteller Mapper-1-ROM-Auslesetest**, keine
Simulation und kein neues Produktionsfeature. Der Standardbuild von
`nes.so` aktiviert MMC1 nicht. Die separate Labor-`.so` ist **nicht**
Teil von `stage-retrode3-runtime` und nicht fuer Debian-Pakete vorgesehen.

## Echthardware-Ergebnis: Rad Racer PAL erfolgreich (2026-11-20)

Der Projektinhaber hat den separaten Laborbuild auf der echten Retrode3
ausgefuehrt und die vollstaendige NDJSON-Ausgabe zurueckgemeldet:

- `event:"result"`, `ok:true`; `bytes_written:131088`
  (16-Byte-iNES-Header + 131072 Bytes PRG)
- `payload_crc32:"654F4E90"` **exakt wie die PAL-Rad-Racer-Referenz**
- `crc32:"B78FDA39"` fuer die vollstaendige .nes-Datei
- `layout_id:"m1-128-0"`, `verified:false`
  (der automatisierte Katalog enthaelt weiterhin nur Mapper 0)
- `/tmp/retrode3-rad-racer-mmc1-20261120-040057.nes`
- Das Skript meldete nach dem Dump: `Original NES core restored.`

**Abnahmeumfang:** Der gesamte MMC1-PRG-Payload wurde fuer **diese
eine PAL-SGROM-Cartridge** mit passender Referenz-CRC gelesen.
Die manuelle SGROM-/No-WRAM-Bestaetigung erfolgte ueber das Laborskript;
eine unabhaengige PCB-Fotopruefung liegt nicht vor.
Kein Oszilloskop-Timing, kein Langzeittest und keine Freigabe
fuer MMC1-Cartridges mit PRG-WRAM. Die normale `nes.so` bleibt gesperrt.
Eine vollstaendige CI-Logausgabe wurde nicht uebermittelt.

## Warum nur Rad Racer auf NES-SGROM?

Belegte Originalboards fuer Rad Racer (PAL-EEC/PAL-NOE/USA) verwenden
NES-SGROM mit MMC1, **128 KiB PRG-ROM, 8 KiB CHR-RAM, 0 KiB PRG-WRAM**
und ohne Batterie. Beispiele:

- <https://nescartdb.com/profile/view/2698/rad-racer>
- <https://nescartdb.com/profile/view/4438/rad-racer>
- <https://nescartdb.com/profile/view/111/rad-racer>

**Die genaue Cartridge/PCB muss der Bediener manuell pruefen.** Ein Spielname,
das Fehlen einer Batterie oder das UI-Fingerprint-Probe sind **kein**
Nachweis fuer das Fehlen von PRG-WRAM. **Nicht** mit Super Mario Bros. 2,
Modulen mit PRG-WRAM, anderen MMC1-Boards oder unbekannten Modulen testen.

Sannis pinned `NES.cpp` schreibt die MMC1-PRG-Register `$E000` normalerweise
mit `write_reg_byte()`, weil das Timing von `M2` und `/ROMSEL`
sonst **PRG-WRAM korrumpieren** kann. Der vorhandene Retrode3-Kernel bietet
fuer `NES_REG` keine verifizierte schnelle Umschaltung; der
`NES_MODE_REG`-Pfad ist deshalb **nicht** geeignet.
Diese Laborversion benutzt bewusst **nur** die vorhandene
`nes_write(..., NES_MODE_PRG)`-Schreibfolge und ist darum **auf
nachweislich PRG-WRAM-freie SGROM-Boards beschraenkt**. Das Signal-Timing
selbst ist noch nicht mit Oszilloskop/GPIO-Tracing validiert. Der
**erste Versuch war fuer Rad Racer PAL erfolgreich**. Die Laborversion
ist weiterhin nicht fuer normale Cartridge-Besitzer vorgesehen.

## Implementierung und Schutzgrenzen

- `src/retrode3/nes_mmc1_plan.cpp` liefert abstrakte Bankoperationen.
- `src/retrode3/oscr_nes_mapper.cpp` fuehrt diese nur im
  `RETRODE3_NES_MMC1_LAB`-Build (oder isoliertem Test-Mock) aus.
- **Zusaetzlicher Runtime-Opt-in:** exakt
  `RETRODE3_MMC1_NO_PRG_RAM_ACK=RAD_RACER_SGROM_128K_CHRRAM_NO_WRAM`.
  Ohne dieses Merkmal gibt es **keine** MMC1-Mapperwrites.
- Ausschliesslich manueller CLI-Override mit **Mapper 1, Submapper 0,
  PRG 128 KiB, CHR 0 KiB, Battery 0**. Kein automatischer Mapper-1-Treffer
  in der Fingerprint-DB, keine Freigabe in `get_capabilities`/Web.
- Der PRG-Lesedurchlauf verwendet Sannis Reset- und 5-Bit-Seriell-Folge
  je 16-KiB-Bank. Das CHR-RAM wird weder gelesen noch beschrieben.
- Volle CRC- und atomare No-Overwrite-Dateiausgabe durch den
  gemeinsamen CLI-Host. Bei unbekannter DB bleibt `verified:false`.
- Normales `nes.so`, Master, OS-Pakete und Kernel bleiben unveraendert.

## Auf der Retrode3 bauen

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

Bei lokalen Aenderungen `git status` erst klaeren; nicht ueberschreiben.
Falls die Tests scheitern, **nicht** mit dem Hardwareversuch fortfahren.
Dieser Laborbuild ist gesondert unter
`build/migration/mmc1-lab/nes.so`; der neue CLI liegt unter
`build/migration/retrode3-cli`.

## Echten Test durchfuehren

1. Auf dem **konkreten Cartridge-PCB** `NES-SGROM`, 128-KiB PRG,
   8-KiB CHR-RAM und **kein PRG-WRAM** bestaetigen. Im Zweifelsfall
   **kein Test**.
2. Nur diese Cartridge in den NES-Slot einlegen. Keine laufenden
   Web-/anderen Cartridge-Jobs. CLI/Web-Lock nicht umgehen.
3. Als root direkt auf dem Retrode3-Terminal ausfuehren:

```sh
cd /usr/local/src/retrode3-oscr
sh tools/nes-mmc1-sgrom-lab.sh
```

4. Das Skript verlangt die interaktive Eingabe `SGROM-128-NOWRAM`,
   bevor es die installierte `nes.so` **zeitweise** austauscht.
   Der neue CLI wird direkt aus dem Build-Verzeichnis aufgerufen.
   Danach wird die urspruengliche `nes.so` via Shell-`trap`
   wiederhergestellt, auch bei Fehlerrueckgabe/INT/TERM.
   **Ein harter Stromausfall** kann die automatische Ruecksicherung
   verhindern; die originale `.so` muss dann manuell aus dem Build
   oder Backup wiederhergestellt werden.
5. Das erzeugte ROM steht unter
   `/tmp/retrode3-rad-racer-mmc1-<timestamp>.nes`.
   Es muss **131088 Bytes** (16 Header + 131072 PRG) lang sein.
   Das Skript zeigt die **PRG-Payload-CRC32** und volle Datei-CRC32.
   Dokumentierte Rad-Racer-PRG-Referenzen: **PAL `654F4E90`**,
   **USA `8B9D3E9C`**. Andere Revisionen nicht blind als Fehler
   interpretieren, sondern Board und Region gegenpruefen.

Falls die reale CRC nicht passt, Tests abbrechen und den NDJSON-Fehler
sowie Kernel-Logs dokumentieren. Nicht durch Veraendern von Timing
oder mehrfaches Wiederholen an einer wertvollen Cartridge
`auf gut Glueck` beheben.

## Mock-/CI-Status und weiterer Weg

- `tests/test_nes_mapper_banks.py` enthaelt Tests fuer das
  exakte MMC1-Seriell-Protokoll, den kompletten Mock-ROM-Dump/CRC,
  fehlenden Opt-in, Batterie-Layout-Block und Schreibfehler.
- `tests/mock_nes_kernel.cpp` emuliert MMC1-Register und 16-KiB-Banks.
- GitHub Actions `native-nes-migration.yml` prueft auf `develop`
  die NES-Suite und kompiliert die eigene Labor-`.so`, **ohne Installation**.
- Ein erfolgreicher **Echtgeraete-CRC-Dump fuer Rad Racer PAL** ist nun
  dokumentiert; er belegt den MMC1-Lesepfad fuer diesen einen SGROM-Fall,
  ersetzt jedoch weder PCB-/Signalpruefung noch allgemeine Freigabe.
- Vor allgemeiner MMC1-Freigabe weiterhin notwendig:
  verifizierte sichere Kernel-Bussequenz fuer PRG-WRAM-Boards,
  physische Timinganalyse, Regression und Freigabe je Board-Variante.
