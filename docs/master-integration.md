# Retrode3: Master-Integration, Referenzen und Rollback

## Quellcode und OS-Installation strikt trennen

Diese Zusammenführung macht den neuen, real hardwaregetesteten Linux-Host
zum primären Entwicklungsstand auf `master`. Sie installiert aber nichts:
Die Retrode3 OS- und Web-Pakete liegen separat unter `DragonBox-Shop/retrode3`.
Die bisherigen Binaries und Webdienste bleiben auf der Retrode3 unverändert.
Ein späteres Deployment braucht eigene Paketinstallation, Root-/Webdienst-
Trennung, Boot-Lock-Verzeichnis und gesondert getesteten Rollback.

## Was gehört in die Hauptentwicklung?

- `src/retrode3/retrode3_cli.cpp`: zentraler NDJSON v1 Host
- `src/retrode3/retrode3_corectl.cpp`: rootgebundener Core-Schalter
- `src/retrode3/oscr_{snes,md,nes}_adapter.cpp`: Cores mit geteilter ABI
- `include/retrode3/`: versionierte Interfaces und Hardware-HAL
- `packaging/cores/`: Core-Manifeste für Offline-Installationen
- `third_party/oscr-firmware`: unveränderter gepinnter Sanni-Quellcode

## Legacy und Rollback

Die bisherigen Reader und ursprünglichen Source-Dateien werden **nicht
entfernt oder umbenannt**, da sie Regressionstests und Referenzdaten
liefern: `Cart_Reader/`, `tools/retrode3-readtest.py`,
`src/retrode3/oscr_snes_native.cpp`, `oscr_md_native.cpp`.
Legacy-Programme werden erst beim separaten OS-Paketumbau in ein eigenes
Verzeichnis kopiert und bleiben als Rückfallmöglichkeit nutzbar.
Der Merge in GitHub verändert weder /usr/local/bin noch den alten Webserver.

## Verifikation

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-all-shared-cores
make -f Makefile.migration stage-all-shared-cores
```

Die drei echten Cartridge-Tests (Retrode3 2.9.5) bestanden:
SNES HiROM Terranigma (4194304 Bytes, CRC32 `6FB8A4DF`),
MegaDrive Snake Rattle n Roll (524288 Bytes, `543BED30`),
NES Mapper 0 Excitebike (24592 Bytes einschließlich iNES-Header,
komplette CRC32 `F749E780`).

Die neuen Cores können derzeit nur die geprüften Leseoperationen.
SNES-Spezialchips, MegaDrive-Bankswitching und NES-Mapper außer 0,
SRAM und Cartridge-Schreiboperationen benötigen eigene Tests.

Für die Weboberfläche muss `retrode3-corectl` über einen stark
begrenzten Privilegienweg ausführbar sein; nicht die ganze Webanwendung
als root starten. Core-Bibliotheken bleiben root-owned.

## Integration von Parallelzweigen

Der gemeinsame Branch baut auf dem hardwarebestätigten MegaDrive-Branch
auf und nimmt den unabhängigen NES-Branch als zweiten Git-Eltern-Commit
auf. So bleibt die Historie beider Cores erhalten, ohne dass ihre
Makefiles, Tests oder Workflows sich gegenseitig überschreiben.
`master` wird erst nach vollständig grüner gemeinsamer CI aktualisiert.
