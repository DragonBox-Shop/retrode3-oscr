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
- **Debian-Paketkandidaten erst von getaggten, unveraenderlichen
  Commits bauen.** Das OS-Repository fixiert den verwendeten OSCR-Tag
  und Commit inklusive Sanni-Submodul-SHA im Build-Manifest. Keine
  automatischen Releases vom beweglichen `develop`-HEAD.
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
