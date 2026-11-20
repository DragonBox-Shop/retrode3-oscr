# Retrode3: Gemeinsamer Linux-Cartridge-Host (Hauptentwicklung)

Die Retrode3 verwendet jetzt einen **gemeinsamen NDJSON-v1-Host**
(`src/retrode3/retrode3_cli.cpp`) mit aktivierbaren original-OSCR-basierten
`snes.so`, `md.so` und `nes.so`. Dateiausgabe, Fortschritt, CRC32,
Abbruch und Bus-Lock gehören dem gemeinsamen CLI, nicht den Modulen.

Die drei Stage-1-Systeme sind auf Retrode3 2.9.5 mit echten ROMs
verifiziert: Terranigma (SNES) CRC32 `6FB8A4DF`, Snake Rattle n Roll
(MegaDrive) `543BED30`, Excitebike (NES) ganze .nes-Datei `F749E780`.
**Auf `master` nicht freigegeben:** Spezialchips, zusätzliche
NES-Mapper und SRAM. Auf `develop` existiert inzwischen ein
**explizit gesperrter NES-MMC1-Laborbuild** mit dem gepinnten
Sanni-Datenbankkatalog (695 MMC1-Datensaetze), PRG-/CHR-Bankleser und
manueller Layoutauswahl. Rad Racer PAL (128 KiB PRG/CHR-RAM) wurde
auf echter Retrode3 per CRC verifiziert; 32-KiB-CHR-ROM-Cartridges
sind bisher mock-/CI-getestet, aber nicht auf Hardware abgenommen.
Das normale NES-`.so` ist weiterhin MMC1-schreibgesperrt.
Siehe [MMC1 Testanleitung](docs/nes-mmc1-general-lab.md) und
[NES Mapper Status](docs/NES-MAPPER-STATUS.md).

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-all-shared-cores
make -f Makefile.migration stage-all-shared-cores
```

Alte Quellcodepfade bleiben **vollständig erhalten**: `Cart_Reader/`,
`tools/retrode3-readtest.py` und die `oscr-*-native`-Testreader.
Es erfolgt kein automatischer Austausch der laufenden OS-Installation.
Die späteren Legacy-Binaries werden im OS-Paket in einem getrennten
Verzeichnis aufbewahrt, anstatt überschrieben zu werden.

Weiteres: [Branching, Debian-Pakete und SD-Images](docs/branching-and-builds.md),
[Master-Integration und Rollback](docs/master-integration.md),
[Projektstatus](docs/PROJECT-STATUS.md).

---

## Ursprüngliche Beschreibung des OSCR-Forks (historisch)

# This is part of the Retrode3 project and is a command line wrapper of the OSCR software

This repository is a clone of the https://github.com/sanni/cartreader and adds these components:
1. a Linux command line interface instead of controlling the OSCR hardware user interface (knob and display)
2. a Retrode.lib for wrapping the details of accessing the Cart Slots of a Retrode3 through Linux syscalls
3. patches for MD.ino, NES.ino, SNES.ino to use the Retrode.lib instead of doing direct hardware control

For details see the README.md in the Cart_Reader subdirectory

# --- original README ---
<br>    
<br>    
<br>   
<br>
We have moved, please visit us at https://github.com/oscartreader 🚀
<br>    
<br>    
<br>   
<br>
<br>    
<br>    
<br>   
![image](https://dl.dropboxusercontent.com/s/ioc5oewzcuvs8nz/logos.png?dl=1)

# Open Source Cartridge Reader
This project represents a community-driven effort to provide an easy to build and easy to modify cartridge dumper.
Its main purpose is to dump a video game's ROM and save file to an SD card without the need of a PC.

For any questions you can use the [Discussions](https://github.com/sanni/cartreader/discussions) and/or [Issues](https://github.com/sanni/cartreader/issues) section here on GitHub, visit the accompanying thread in the [Arduino Forum](http://forum.arduino.cc/index.php?topic=158974.9001) or check out our [community discord server](https://discord.gg/YVxv7BnzXt), where you can reach most of the devs directly.

Also be sure to check the guides in the [Wiki](https://github.com/sanni/cartreader/wiki) too.

Happy making. 🔧🔨😊

![image](https://dl.dropboxusercontent.com/s/3lrn7xh3f7h6jre/HW5_front.png?dl=1)

#### Features:
- Modular design
- Stand-alone operation
- Easy to modify open-source code
- Portable when used together with a power bank

![image](https://dl.dropboxusercontent.com/s/w99hewh6ors3awb/HW5_side.png?dl=1)

#### Supported Systems:
- NES/Famicom/Family Basic
- Super Nintendo/Super Famicom (including SF Memory, Satellaview, Sufami Turbo, and Game Processor RAM Cassettes)
- Nintendo 64 (including Controller Pak, Gameshark, and Xplorer 64)
- Game Boy Color (including GB Memory, Codebreaker, and Gameshark)
- Game Boy Advance
- Sega Mega Drive/Genesis
- Sega Master System

![image](https://dl.dropboxusercontent.com/s/oi7c2radgblylyz/HW5_slots.png?dl=1)

#### Supported with adapters:
- Virtual Boy
- Pokémon Mini
- Sega Game Gear
- Sega Mark III
- Sega SG-1000/SC-3000
- Sega Cards
- PC Engine/TurboGrafx-16/SuperGrafx
- WonderSwan (Color)
- NeoGeo Pocket (Color)
- Intellivision
- ColecoVision
- Benesse Pocket Challenge W
- Benesse Pocket Challenge V2
- Watara Supervision
- Atari 8-bit Computer (400/800/XL/XE)
- Atari 2600/5200/7800
- Atari Lynx
- Atari Jaguar
- Emerson Arcadia 2001
- Fairchild Channel F
- Magnavox Odyssey 2/Philips Videopac+
- Super A'Can
- MSX
- Casio PV-1000
- Casio Loopy
- Commodore VIC-20
- Commodore 64
- Vectrex
- Bally Astrocade
- Bandai Little Jammer
- Bandai Little Jammer Pro
- LeapFrog Leapster (L-MAX/Leapster2)
- RCA Studio II
- Texas Instruments TI-99
- Tomy Pyuuta
- TRS-80 Color Computer
- Vtech V.Smile (Baby/V.Motion)
- CPS3

#### Open Source Licenses:
- Software(everything in Cart_Reader folder) = GPL v3
- Hardware(everything in hardware folder) = CC BY 4.0
- Documentation(everything in the Wiki) = CC0 1.0
