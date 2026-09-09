# WideMelon

WideMelon is a small melonDS build with an experimental ultrawide 3D renderer.
It keeps the Nintendo DS interface at its native proportions while allowing
the submitted 3D scene to use a wider horizontal viewport.

WideMelon is a modified version of melonDS based on commit
`906e9ebb27da8c6a715cd7abab4abfe8a8d29427`. The WideMelon modifications were
first published in 2026 under GPL-3.0-or-later. WideMelon is an independent
project and is not affiliated with or endorsed by the melonDS project,
Nintendo, Game Freak, or The Pokémon Company.

The project does not contain a ROM, commercial BIOS files, or a separate
launcher application. The patched melonDS executable opens a small native Qt
configuration dialog when started without a WideMelon environment profile.

## Quick start

On Linux, install the development packages listed in `projects/melonDS/BUILD.md`
after the build script checks out melonDS. CMake, Ninja, a C++17 compiler, Qt 6
development packages, SDL2, libarchive, zstd, libcurl, libpcap, OpenGL/EGL,
Wayland/X11, and extra-cmake-modules are needed.

```sh
./scripts/build.sh
./widemelon
```

The first command downloads the pinned melonDS source and the missing FAAD2 and
ENet dependencies into ignored `projects/` and `tools/` directories. It
applies `patches/melonds-widemelon.patch`, builds melonDS, and runs the tests.
It does not install system packages or require a ROM.

On startup, choose any readable Nintendo DS ROM. WideMelon does not enforce a
game whitelist or SHA-1 checksum. Pokémon Black is the primary tested game;
other 3D games are useful for experimentation.

## Native settings

The startup dialog is part of melonDS and uses ordinary native Qt controls:

- ROM file, with drag-and-drop and basic header information
- Native 4:3, 16:10, 16:9, Ultrawide 21:9, Superwide 32:9, or custom viewport
- 1×–8× 3D render scale
- Common output resolutions or a custom window size
- Integer scaling and fullscreen

Settings, saves, save states, and the last selected ROM are stored under a
dedicated `WideMelon` directory in the platform's normal configuration location.
They are deliberately kept separate from a stock melonDS installation so both
applications can be used side by side without changing each other's settings.
The existing melonDS menus remain available after the game starts, including
input remapping, screen layouts, save states, and controller configuration.

The `widemelon` script is only a small convenience wrapper around
`build/widemelon`. A ROM can also be passed as a command-line argument; it will
be pre-filled in the native dialog.

## What the renderer changes

A normal DS 3D target is 256 × 192. WideMelon allocates a wider target, such as
448 × 192 for the 21:9 profile, and adjusts the 3D projection so the original
center remains the same size:

```text
normal:    [       256 pixels       ]
ultrawide: [ extra ][ 256 native ][ extra ]
```

The compositor maps the original 2D layers back onto the centered native
region. The extra columns show only expanded 3D geometry. The touchscreen and
native 2D artwork therefore keep their original proportions instead of being
stretched.

Only geometry submitted by the game can appear in the extra columns. Game-side
distance culling, 2D clipping, battles, videos, and special effects can still
limit or break the result. Widescreen requires the classic OpenGL renderer;
Native 4:3 is the compatibility profile.

## Development

The outer repository contains the launcher-free build recipe, patch, and
renderer tests. The upstream checkout is intentionally ignored:

```text
patches/melonds-widemelon.patch   renderer and native-dialog changes
scripts/build.sh               dependency checkout and build
scripts/export-patch.py        export local melonDS changes
tests/profile_test.cpp         viewport projection tests
```

For engine development, edit `projects/melonDS/src/`, then run:

```sh
python3 scripts/export-patch.py
```

The patch is based on melonDS commit
`906e9ebb27da8c6a715cd7abab4abfe8a8d29427`.

For automated/headless runs, setting `WIDEMELON_VIEW_WIDTH` skips the dialog and
uses the other `WIDEMELON_*` profile variables. For example:

```sh
WIDEMELON_VIEW_WIDTH=448 WIDEMELON_SCALE=4 ./build/widemelon /path/to/game.nds
```

## Verification

```sh
ctest --test-dir build/tests --output-on-failure
```

## Distribution

Source-only releases may use this repository's pinned patch workflow. Every
compiled release must also provide the complete corresponding source used to
build it; a patch and an upstream link are not sufficient for a binary release.
After committing the exact release state, generate that source archive with:

```sh
./scripts/package-source.sh 0.1.0-alpha
```

The resulting archive is written to `dist/` and contains the patched melonDS,
FAAD2, and ENet source trees together with the WideMelon build and licence
files. Publish it next to the matching binary at no additional charge.

No game ROMs, commercial BIOS files, firmware dumps, or game assets are
distributed. WideMelon and its melonDS modifications are GPL-3.0-or-later; see
`LICENSE` and `THIRD_PARTY.md`.
