# Dragonborn's Codex

An in-game cheat and tool menu for Skyrim Special Edition / Anniversary Edition, as an SKSE plugin
(CommonLibSSE-NG + Dear ImGui). Download and full description: [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/197021).

Внутриигровое меню читов и инструментов для Skyrim SE / AE (плагин SKSE). Скачать: ссылка на Nexus выше.

## Building

Needs Visual Studio 2022 (or Build Tools) with the C++ workload and its bundled vcpkg.

1. Clone [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (built against v13.1.0) into `extern/CommonLibSSE-NG`.
2. `build_dist.bat` builds the release DLL into `build-dist/` (preset `dist`, no test bridge, no deploy).
   `build.bat` is the developer build (preset `release`, with the test bridge), copied into the MO2 mod folder set by
   `CHEATMENU_DEPLOY_DIR` in `CMakeLists.txt`.
3. `python tools/package.py` builds the Nexus archive into `dist/`.

The menu's data files (languages, fonts, icons, key glyphs) are in `assets/DragonbornsCodex/`. SkyUI's item icon atlas
(`icons/item_atlas.dds`) is not included: the menu works without it.

## Layout

- `src/` the plugin. `src/TestBridge.cpp` is a developer-only bridge that lets `tools/skytest.py` drive the game
  (`tests/*.txt`); it is compiled out of public builds (`CODEX_TEST_BRIDGE=OFF`).
- `tools/` packaging, icon export and test scripts. `PLANS.md` is the feature backlog.

## License

Copyright (C) 2026 lnocriesl

GPL-3.0-or-later, see [LICENSE](LICENSE) (CommonLibSSE-NG, which the plugin links, is GPL-3.0). The fonts Jost and Oswald
are under the SIL Open Font License 1.1 (`assets/DragonbornsCodex/fonts/OFL-*.txt`); the map marker icons and key glyphs
come from Skyrim's interface files (Bethesda Game Studios).
