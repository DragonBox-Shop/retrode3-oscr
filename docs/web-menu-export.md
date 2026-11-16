# OSCR menu-to-web export (experimental)

Run from the repository root after initializing the upstream firmware and its nested database submodule:

```sh
node tools/oscr-export-web-menu.js --output generated/oscr-web-menu.json
node tests/oscr-web-menu.test.js
```

The script inspects the known C++ menu arrays and corresponding enum definitions of the pinned NES, MegaDrive and SNES OSCR cores. It resolves original English labels, records conditional compilation guards and fails loudly if menus change in unexpected ways. Unknown new menu arrays require a deliberate addition to the audited `menuMap`; they do not become executable commands automatically.

The NES mapper dropdown comes from the upstream `databases/data/nes-mappers.json` and the supported memory sizes come from the NES core. These mapper records describe **capabilities, not game recognition**. Auto-detection continues to use OSCR's original cartridge database (`nes.json`) and remains the default; override only if detection fails or is incorrect.

All actions are `implemented:false` because no new OSCR Linux CLI exists yet. The eventual web backend must validate requested actions and all parameters, using argv rather than shell command interpolation. Save writing and flash support stay visible as metadata, but will be implemented and enabled **after** successful ROM/save reading tests.
