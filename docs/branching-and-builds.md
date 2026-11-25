# Entwicklung, stabile Releases und Build-Verantwortung

## Branch-Modell

| Referenz | Bedeutung |
| --- | --- |
| `develop` | Gemeinsame laufende Entwicklung und Experimente; HEAD darf unfertig und defekt sein |
| unveraenderlicher Git-Tag auf `develop` | Abgeschlossener, grundlegend gepruefter Gesamtstand; Grundlage fuer Debian-Paketkandidaten |
| `master` | Ausschliesslich ausfuehrlich getesteter, freigegebener Tag-Stand; muss funktionieren |
| `feature/*`, `fix/*`, `integration/*` | Ausnahmen fuer notwendige temporaere Isolation, nicht normale Entwicklungsstrategie |
| `upstream/*` | Externe Upstream-Verfolgung; nie automatisch in Released-Code uebernehmen |

Es gibt **keine dauerhaften branches pro Core oder Plugin**. Unfertige
Entwicklungen gehen in `develop`, auch wenn CI noch rot ist. Solche
Zwischenstaende sind keine Releases und koennen andere Funktionen
voruebergehend beeintraechtigen. Fehlgeschlagene Pruefungen sichtbar
lassen statt sie zu umgehen.

## Tags, Pakete und Master-Freigabe

1. Funktionen auf `develop` gemeinsam entwickeln. Vor einem
   Checkpoint die betroffenen Tests inklusive `tools/preflight-ci.sh`,
   Cross-Build und gegebenenfalls Hardware-Smoke-Tests ausfuehren.
2. Einen vorzugsweise **annotierten, unveraenderlichen Tag** auf dem
   bewusst abgeschlossenen Commit setzen. Ein Tag ist ein Snapshot
   des gesamten Repositories, nicht bloss ein Feature-Marker.
   Wesentliche zugesagte Funktionen sollen dort funktionieren;
   noch unbekannte Bugs sind moeglich. Unfertige Hardware-Risiken
   muessen standardmaessig deaktiviert bzw. sicher abgeschirmt sein.
3. **Debian-Paketkandidaten ausschliesslich aus exakt diesem Tag/Commit**
   bauen. Alle verknuepften Quell- und Sanni-Revisionen und Checksummen
   ins Manifest aufnehmen. Nicht den aktuellsten `develop`-HEAD
   waehrend eines Builds nachziehen. Der derzeitige OS-Buildserver
   verfolgt teils noch Branch-HEADs und muss fuer diese Policy erst
   umgestellt werden.
4. Paketinstallation, Upgrade, Rollback, USB/Web/Cartridge-Verhalten,
   sowie die betroffenen Hardwarepfade ausfuehrlich pruefen.
5. **Genau den getaggten Commit** nach `master` uebernehmen.
   `develop` darf sich unterdessen weiterbewegen. Den Tag nicht
   nachtraeglich verschieben; kein blindes Squash-Merge mit anderem
   Tree, keine unkontrollierten Force-Pushes. Wenn die Historie
   divergiert, zuerst einen sicheren, nachvollziehbaren Weg planen.
6. Ein Paket-Build, eine APT-Veroeffentlichung, ein Hardwaretest und
   ein Kundenimage sind verschiedene Freigabestufen. Neue Images
   benoetigen zusaetzlich einen erfolgreichen separaten SD-Kaltstart.

`master` wird niemals automatisch mit `develop` synchronisiert.
GitHub Rulesets sollten direkte Master-Pushes und Force-Pushes
technisch verhindern; Dokumentation allein ist keine Branch-Sperre.

## Zuständigkeiten zwischen den Repositories

- **`retrode3-oscr`**: ein zentraler CLI-Host, versionierte Plugin-ABI,
  geprüfte OSCR-Konverter, `.so`-Cores und offline-staged
  Core-Metadaten/Binärdateien. Keine separaten NDJSON-Reader pro System.
- **`retrode3-kernel`**: Bus-, GPIO-, Spannungs- und Timing-Treiber.
  Das OS-Build bindet eine **konkrete Kernel-Revision** ein.
- **`retrode3`**: Debian-Paketrezepte, Abhängigkeiten,
  Root-/Webprivilegien, Systemdienste, Updates und Integration.
  Das SD-Karten-Image setzt sich ausschließlich aus versionierten
  Paketen und fixierten Kernel-/RootFS-Quellen zusammen.
- **`retrode3-hardware`**: Schaltpläne/Adapter-Pinouts und
  dokumentierte Hardwarerevisionen für die jeweilige Zielplattform.

Künftige Debian-Pakete sollten den CLI-Host (`retrode3-cli`),
administrativen Core-Schalter (`retrode3-corectl`) und die Core-Pakete
(`retrode3-core-snes`, `retrode3-core-md`, `retrode3-core-nes`)
klar voneinander trennen. `.core` und `.so` müssen zusammen versioniert
und auf eine kompatible ABI geprüft werden. Die drei vorhandenen
`stage-*-module`-Targets sind **Vorstufen**, noch keine fertigen
Debian-Buildrezepte. Keine stillen Modul-Aktivierungen bei Installation
ohne festgelegte Policy; Webzugriff nur über eingeschränkten Helper.

## Automatische OSCR-Aktualisierung (sicherer Ablauf)

Ein periodischer Upstream-Check darf eine **PR-Vorlage** auf
`upstream/*` mit alter/neuer Commit-ID, geänderten Sanni-Quellroutinen,
Konverter-Diff und OSCR-Datenbankänderungen erzeugen. Er darf **nicht**
ungeprüft das gepinnte Submodul austauschen.

Die Konverter sind bewusst strikt: bei veränderten originalen
Lesealgorithmen oder GPIO-/Bus-Sequenzen wird die automatische
Umwandlung gestoppt, bis jemand die HAL-Anpassung überprüft.
Danach Mock-Tests, Hardwaretests der betroffenen Systeme, `develop`
und erst zum Schluss `master`. So bleibt die Firmware aktualisierbar,
ohne ungeprüfte Cartridge-Schreiboperationen auf Geräte loszulassen.

## Legacy-Rückfallmöglichkeit

Die alten `Cart_Reader/`-Dateien, Python-Reader und nativen
Akzeptanzprogramme bleiben im Quellbaum. Im späteren OS-/Debian-Build
werden Legacy-Binaries in einem eigenen Verzeichnis installiert, ohne
die primären Programme zu überschreiben. Für den tatsächlichen
Paket-/SD-Image-Rollback werden die letzten freigegebenen
Versionen und ein testbarer Rückweg dokumentiert.

## Verbindliches Laufzeit-Staging (seit 2026-11-19 in `develop`)

Mit `make -f Makefile.migration stage-retrode3-runtime` werden der
Host, der Root-Core-Schalter und **alle drei Stage-1-Cores** in genau
diesen Debian-Filesystembaum gebaut:

```text
build/migration/stage/
  usr/bin/retrode3-cli
  usr/sbin/retrode3-corectl
  usr/lib/retrode3/cores-available/
    snes.so  snes.core
    md.so    md.core
    nes.so   nes.core
```

`make -f Makefile.migration test-release-stage` prüft die acht
Dateien, Rechte und Core-IDs. Das Staging aktiviert keinen Core und
fasst die vorhandenen Legacy-Reader nicht an. Diese **exakten Pfade**
sind der Übergabevertrag an `DragonBox-Shop/retrode3` für die späteren
separaten Debian-Pakete. Dabei ist `/etc/retrode3/cores-enabled/`
**Konfiguration/Laufzeitstatus**, kein von diesem Build automatisch
gefülltes Aktivierungsverzeichnis. `/run/lock/retrode3/` wird beim
Boot über die OS-Integration verwaltet, nicht im Debian-Payload.

Der Host ist `/usr/bin/retrode3-cli` (kein per-Plugin-CLI), der nur
privilegiert zu startende Schalter heißt
`/usr/sbin/retrode3-corectl`. Die bisherige
`/usr/local/bin/oscr`-Installation bleibt bis zum Paket-Upgrade als
Legacy-System unangetastet. Nach dem MIPS-Build muss das OS-Repo die
Dateien unter getrennten Paketstaging-Wurzeln verarbeiten.
