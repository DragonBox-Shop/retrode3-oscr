# Entwicklung, stabile Releases und Build-Verantwortung

## Branch-Modell

| Branch | Zweck | Freigabe |
| --- | --- | --- |
| `master` | stabiler, auf Hardware verifizierter Hauptstand, Quelle für *freigegebene* Builds | nur überprüfter PR aus `develop` (bei der ersten Umstellung: Integrations-PR) |
| `develop` | gemeinsamer Entwicklungszweig für **alle** Cores, Konverter und Release-Build-Vorbereitung | fortlaufende CI, Hardwareprüfungen vor `master` |
| `feature/*`, `fix/*` | **nur bei Bedarf** isolierte riskante oder größere Änderungen, keine dauerhaften Core-Branches | PR gegen `develop`, ggf. echte Cartridge-Tests |
| `upstream/*` | Vorschläge für neue gepinnte OSCR-Versionen | niemals automatisch in `master` übernehmen |
| `release/*` | optionaler Kandidat für Debian-Pakete und SD-Images | nur mit vollständigem Release-Test |

**Keine direkt auf `master` geschobenen Entwicklungscommits.**
`master` wird nicht automatisch mit `develop` synchronisiert.
Ein PR gegen `master` bündelt mehrere bereits geprüfte Einzelarbeiten,
standardmäßig als **Squash-Merge**. So wird aus einer langen Folge von
Korrekturcommits ein verständlicher Integrationsschritt; die Branch-/PR-
Historie enthält weiterhin die Einzelarbeit.

Die Branch-Regeln sollten zusätzlich in GitHub Rulesets technisch
durchgesetzt werden: PR-Pflicht, erforderliche Statuschecks,
keine Force-Pushes, keine direkte Änderung an `master`,
passende Reviewer/Administrationsausnahmen. Workflow-Dateien allein
sind **keine Branch-Sperre**. Derzeit sind diese Regeln im Repo
dokumentiert; die GitHub-Einstellungen müssen separat kontrolliert
und aktiviert werden.

## Tests und Freigabestufen

1. **Gemeinsames `develop` (oder optionaler Entwickler-PR):** Quellgeneratoren und gepinnte
   Upstream-Revision prüfen, ABI und Build, Tests mit Mock-Kernel,
   Registry/Host, Abbruch, Dateisicherheit und alle betroffenen Cores.
2. **Hardware-Freigabe:** Wenn neue Bus-Kommandos, Mapper, Chips,
   Spannungsumschaltung, Cartridge-Schreibzugriffe oder Timing betroffen
   sind, verpflichtende reale Kartuschentests und dokumentierte
   Referenz-CRC32 bzw. Save-Backup-Vergleiche. Nicht anhand bloßer
   GitHub-CI-Erfolge automatisch freigeben.
3. **Release-Kandidat:** Pakete auf der Zielarchitektur bauen,
   Install/Upgrade/Rollback testen und danach das komplette
   flashbare SD-Karten-Image erzeugen. Eine echte Boot-/USB-/Web-/
   Cartridge-Prüfung ist Pflicht, bevor ein Image für Nutzer freigegeben
   wird. *Diese Paket- und Image-Automation existiert noch nicht als
   vollständige CI-Kette; sie ist eine eigene nächste Aufgabe.*
4. **Promotion `develop` → `master`:** Nur nach erfolgreichen
   für den Scope relevanten Tests und abgenommenen Freigabekriterien.
   Ein Source-Merge allein installiert keine Software auf dem Gerät.
5. **Release-Tag:** Nach freigegebenem Build und dokumentiertem
   Rollback, Artifacts mit Version/Commit/Prüfsummen und verwendeten
   Sanni-Revisionen veröffentlichen.

Ein grüner Commit auf `master` ist **kein** Beleg, dass daraus bereits
ein freigegebenes Debian-Paket oder Bootimage gebaut wurde.

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
