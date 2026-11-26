# Retrode3 OSCR: Entwicklungs- und Integrationsregeln

Dieses Repository enthaelt `retrode3-cli`, `retrode3-corectl`,
die Linux-HAL, Core-ABIs und dynamische Sanni-basierte `.so`-Cores.
OS-/Debian-/Web-Integration gehoert nach `DragonBox-Shop/retrode3`,
Kernel/Timing nach `retrode3-kernel`, U-Boot nach `retrode3-uboot`,
Hardwareunterlagen nach `retrode3-hardware`.

## Branch- und Tag-Politik

- **`develop` ist der gemeinsame Entwicklungsbranch fuer alle Cores.**
  Neue und unfertige Funktionen und Experimente werden dort entwickelt;
  ein beliebiger HEAD muss weder kompilieren noch funktionieren. Andere
  Entwickler duerfen den neuesten HEAD nicht als stabile Basis annehmen.
- **Tags markieren abgeschlossene, grundlegend gepruefte gemeinsame
  Commits.** Jeder Tag bezieht sich auf den gesamten Quellbaum, nicht
  auf einen einzelnen Plugin-Fortschritt. Vorher soll der betreffende
  Stand baubar und fuer seinen zugesagten Umfang getestet sein. Noch
  nicht vollstaendig hardwaregetestete Funktionen duerfen Fehler haben,
  duerfen aber keine unkontrollierten Cartridge-/RAM-/Flash-Writes erlauben.
  Tags unveraenderlich halten; bevorzugt annotierte Tags.
- **Debian-Pakete aus exakt fixierten Branch-Commits bauen.** Der
  explizite Paketlauf verwendet `develop` fuer testing und `master` fuer
  stable; Tags sind nur fuer Images erforderlich. Das OS-Repository fixiert
  den OSCR-Commit und Sanni-Submodul-SHA, bei Images auch den Tag.
- Nach Paket-/Hardware-/Rollbacktests den **exakt getaggten Commit**
  nach `master` uebernehmen, nicht den inzwischen weiterentwickelten
  `develop`-HEAD. `master` ist stets funktionsfaehig und freigegeben.
  Moeglichst Fast-Forward auf denselben Tag-Commit; keine ungeprueften
  Squash-/Merge-Effekte oder Force-Pushes auf `master`.
- **Keine dauerhaften Feature- oder Plugin-Branches.** Kurzlebige
  Spezialbranches nur wenn technische Isolation wirklich noetig ist,
  z. B. eine gefaehrliche Bus-Aenderung oder ein fremder PR. Danach
  in `develop` integrieren und nach Pruefung aufraeumen.
- Dokumentiert unterscheiden: laufende Entwicklung, abgeschlossener Tag,
  gebautes Debian-Paket, hardwaregetestetes Release und veroeffentlichtes
  Kundenimage. Ein Tag allein ist **noch keine `master`-Freigabe**.

## Tests, Sicherheit, Source-Verantwortung

- Vor einem Tag/Release im vollstaendigen lokalen Checkout
  `./tools/preflight-ci.sh` sowie die jeweils betroffenen Mock-/Core-
  und Staging-Tests ausfuehren. Zwischencommits auf `develop` duerfen
  unfertig sein, auch mit roten Checks; Fehlschlaege nicht verschleiern
  oder CI durch `continue-on-error` deaktivieren.
- `third_party/oscr-firmware` ist ein gepinntes Original-Submodul.
  Sannis Lesesequenzen niemals stillschweigend umschreiben. Algorithmus-
  und Mapper-Aenderungen erfordern Review der HAL-/Bus-Sicherheit.
- Alle Hardwareoperationen ueber typisierte HAL und Kernel-Treiber;
  kein unkontrolliertes Einschalten von Spannung, Schreibfunktionen,
  SRAM/Flash oder Mapper-Registeroperationen. Slot-Sense beweist nur
  Belegung, keine Kartuschen- oder Spannungsidentitaet.
- Tests an echter Hardware vor Freigabe betroffener Funktionen:
  reprasentative Spiele, Groessen/CRC32, Timing, Spannungen und
  Save-Backups dokumentieren. Mock-CI ersetzt keine physische Pruefung.
- CLI besitzt NDJSON-Protokoll, Registry, Fortschritt, Abbruch,
  Checksummen, Ausgabe und gemeinsame Bus-Sperre. Cores melden ihre
  Features/Selektionsschemata dynamisch ueber die v3-ABI und
  validieren jede Auswahl beim Probe/Read erneut.
- Neue oder ungetestete Plugin-Funktionen duerfen nicht allein durch
  Datenbankeintraege Sicherheitsfreigaben oder Cartridge-Writes erhalten.
  Unbekannte/ambige Layouts benoetigen explizite Auswahl.

## Neue Plugin-Cores: Debian-Paket ist Pflicht

- **Jeder neue Core fuer ein Release braucht `packaging/cores/<id>.core`
  im OSCR-Git**, mit `id`, `name`, `version`, `slot`. Das `id` muss
  dem Dateinamen entsprechen (Kleinbuchstaben, Ziffern, Bindestriche).
  Beispiel: `packaging/cores/gba-md.core`.
- In `Makefile.migration` den **`stage-<id>-module`-Target** anlegen.
  Er muss unter `build/migration/stage/usr/lib/retrode3/cores-available/`
  sowohl `<id>.so` (echtes MIPS32EL-Modul, kein Mock) als auch die
  zum Git-Rezept bytegleiche `<id>.core` bereitstellen.
- **Kein neues Plugin in festen Paketlisten nachtragen.**
  `stage-all-shared-cores` und der Paket-Builder sollen die
  `packaging/cores/*.core`-Rezepte dynamisch entdecken und aus jedem
  validierten Paar `retrode3-core-<id>.deb` erzeugen. Fehlendes Target
  oder fehlende/ungueltige Dateien muessen den Build abbrechen.
- Das dynamische Meta-Paket `retrode3-cores` soll die neuen Plugin-
  Pakete automatisch als Abhaengigkeiten aufnehmen, damit bestehende
  Geraete sie via APT-Upgrade erhalten. Die Umstellung ist noch in
  separaten Entwicklungs-PRs; vor dem Release auf Fertigstellung testen.
- Core-/ABI-, Staging- und Hardwaretests nicht vergessen. Pakete
  **duerfen Cores nie automatisch aktivieren**; `cores-enabled` bleibt
  eine explizite Entscheidung. Unfertige Experimente ohne Freigabe
  bekommen keine Datei in `packaging/cores/`.
- Paketkandidaten aus fixierten OSCR-Commits bauen; Image-Tags bleiben Pflicht.
  Der Buildserver pullt/fetcht nur und pusht keine Git-Aenderungen.

## Verbindliche Runtime-Pfade

- CLI: `/usr/bin/retrode3-cli`
- Root-verwalteter Core-Schalter: `/usr/sbin/retrode3-corectl`
- Cores: `/usr/lib/retrode3/cores-available/<id>.{so,core}`
- Core-Aktivierung: `/etc/retrode3/cores-enabled/<id>.so`
- Bus-Lock: `/run/lock/retrode3/bus.lock`; Verzeichnis durch
  **SysV init**, nicht systemd-tmpfiles, im OS-Repository anlegen.

Kein Legacy-Reader unter `/usr/local/bin/oscr` oder
`retrode3-read/dump/info` wiederherstellen. Das Web darf nur
die zentrale CLI und eingeschraenkte Verwaltungshelfer nutzen.

## Fachliche Quellen statt alter Status-Duplikate

- [Projektstatus und Hardwaretests](docs/PROJECT-STATUS.md)
- [Branches, Tags und Releases](docs/branching-and-builds.md)
- [Generische v3-Cartridge-Auswahl](docs/generic-cartridge-selection-v3.md)
- [NES-Mapper und Hardware-Grenzen](docs/NES-MAPPER-STATUS.md)
- [Bauanleitung/Staging](Makefile.migration)

Diese Dokumente koennen historische Testergebnisse enthalten.
Vor Entwicklungsentscheidungen stets aktuelle Git-Revisionen,
Quellcode und reale Testergebnisse gegeneinander pruefen.
