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
- Local builds dynamically link Qt 5/6 (including Qt WebSockets), SDL2,
  libarchive, zstd, and other host libraries. Linux AppImages bundle selected
  shared libraries; their package copyright files are embedded in the AppImage
  and their matching distribution sources are published in the release's
  third-party source archive.
- QR Code generator library: https://github.com/nayuki/QR-Code-generator
  commit `3c6d0b3cefb4e049dc337e82237c9644399716a8` (MIT).

The input configuration uses original WideMelon schematic controller diagrams
under GPL-3.0-or-later. They replace melonDS's former Dimensions.com-derived
artwork and carry no noncommercial restriction.

Pokémon and Nintendo names identify compatibility. WideMelon is an independent
project and is not affiliated with Nintendo, Game Freak, or The Pokémon Company.
No game ROM, commercial BIOS, firmware, or game assets are distributed here.
