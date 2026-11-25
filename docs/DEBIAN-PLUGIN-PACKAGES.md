# Neues Retrode3-Plugin als Debian-Paket

Die Plugin-Metadatei im OSCR-Git ist das Paket-Rezept:
`packaging/cores/<id>.core` (id, name, version, slot).
Die Pakete werden aus dem **genau getaggten** OSCR-Commit erstellt.
Nicht zur Veröffentlichung bestimmte Experimente dürfen keine Metadatei
in diesem Verzeichnis besitzen.

Ein neues Plugin benötigt:

1. `packaging/cores/<id>.core`, wobei `id` aus Kleinbuchstaben,
   Ziffern und Bindestrichen besteht.
2. Einen `stage-<id>-module`-Target in `Makefile.migration`.
   Der Target muss exakt `build/migration/stage/usr/lib/retrode3/cores-available/<id>.so`
   (MIPS32EL ELF) und `<id>.core` installieren.
3. Tests für ABI, Auslesen und, wenn vorhanden, sichere Schreibfunktionen.

`stage-all-shared-cores` entdeckt die Metadateien automatisch und
ruft sämtliche Stage-Targets auf. Fehlende Build-Targets, unvollständige
Dateipaare oder nicht passende Metadaten lassen den Paket-Build abbrechen.

Der zentrale Paketbuilder in `DragonBox-Shop/retrode3` erzeugt aus
jedem geprüften Paar ein `retrode3-core-<id>.deb`; es sind keine
Plugin-Namen im zentralen Skript zu ergänzen. Er aktualisiert die
Abhängigkeiten des Meta-Pakets `retrode3-cores` aus den tatsächlich
gefundenen Plugins. Ein `apt upgrade` dieses Meta-Pakets kann daher
neue Plugin-Pakete hinzufügen, **aktiviert** aber keine Cores automatisch.

Die Veröffentlichung erfolgt erst, wenn ein Release-Tag auf dem
gewünschten Branch existiert und der Build die Tests bestanden hat.
Auf dem Buildserver wird niemals nach GitHub gepusht.
