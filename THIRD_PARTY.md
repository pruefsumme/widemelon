WideMelon is GPL-3.0-or-later. It incorporates a modified melonDS engine, also
GPL-3.0-or-later. Keep the license, complete corresponding source, and
modification notices with any redistribution of the engine. WideMelon contains
no ROMs, commercial BIOS or firmware files, or game assets.

Pinned source dependencies:

- melonDS: https://github.com/melonDS-emu/melonDS
  `906e9ebb27da8c6a715cd7abab4abfe8a8d29427`
  WideMelon's Git history descends from this commit and records all local
  modifications as ordinary commits. Includes upstream bundled components;
  their license files and copyright notices remain in the source tree.
- FAAD2: https://github.com/knik0/faad2, tag `2.11.2`
  `673a22a3c7c33e96e2ff7aae7c4d2bc190dfbf92` (GPL-2.0-or-later).
- ENet: https://github.com/lsalzman/enet, tag `v1.3.18`
  `2662c0de09e36f2a2030ccc2c528a3e4c9e8138a` (MIT).
- Qt 5/6 (including Qt WebSockets), SDL2, libarchive, zstd, and other system
  libraries are linked using the host's development packages. Their respective
  licenses apply.

The Nintendo DS diagrams in melonDS's input configuration are derived from an
illustration by Dimensions.com and are used with the copyright holder's
permission for a free, open-source community project. Retain
`src/frontend/qt_sdl/InputConfig/resources/LICENSE.md` and its backlink to
https://www.dimensions.com/ in source and binary distributions. Obtain separate
permission or replace those diagrams before a commercial distribution.

Pokémon and Nintendo names identify compatibility. WideMelon is an independent
project and is not affiliated with Nintendo, Game Freak, or The Pokémon Company.
No game ROM, commercial BIOS, firmware, or game assets are distributed here.
