<p align="center">
  <img src="assets/widemelon.png" alt="WideMelon logo" width="220">
</p>

<h1 align="center">WideMelon</h1>

<p align="center">
  An experimental widescreen build of melonDS.
</p>

<p align="center">
  <a href="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml"><img src="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="GPL-3.0-or-later"></a>
</p>

WideMelon expands the Nintendo DS 3D viewport while keeping native 2D elements
and the touchscreen at their original proportions. It is currently an alpha
focused on Linux and primarily tested with Pokémon Black.

## Features

- Native 4:3, 16:10, 16:9, 21:9, 32:9, and custom viewports
- 1×–8× internal 3D render scale
- Native Qt setup dialog with ROM selection and saved profiles
- Integer scaling, fullscreen, and common output resolutions
- Configuration and saves isolated from a stock melonDS installation
- Existing melonDS menus, input mapping, save states, and controller support

## Build and run

WideMelon currently provides a source build for Linux. On Ubuntu 24.04, install
the build dependencies with:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev \
  libarchive-dev libenet-dev libzstd-dev libfaad-dev libegl1-mesa-dev \
  libgl1-mesa-dev libwayland-dev qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev libqt6svg6-dev
```

Then build and start WideMelon:

```sh
./scripts/build.sh
./widemelon
```

The build script downloads the pinned melonDS source, applies the WideMelon
patch, builds the emulator, and runs the tests. It does not download a ROM,
BIOS, or firmware.

Pass a ROM on the command line to pre-fill it in the setup dialog:

```sh
./widemelon /path/to/game.nds
```

## Downloads

Development AppImages are available from the
[Package Linux workflow](https://github.com/pruefsumme/widemelon/actions/workflows/package-linux.yml).
Tagged builds publish the AppImage and its matching complete source archive on
the [Releases page](https://github.com/pruefsumme/widemelon/releases).

## How it works

A normal DS 3D target is 256 × 192. WideMelon allocates a wider target and
adjusts the projection so the original center keeps the same scale:

```text
normal:    [       256 pixels       ]
widescreen:[ extra ][ 256 native ][ extra ]
```

The extra columns contain only geometry submitted by the game. Native 2D
layers are composited over the centered 256-pixel region instead of being
stretched.

## Limitations

- Widescreen requires the classic OpenGL renderer.
- Games may apply their own distance culling or clipping.
- Battles, videos, menus, and effects can remain 4:3 or render incorrectly.
- Compatibility outside Pokémon Black is experimental.
- Windows and macOS packaging is not available yet.

WideMelon does not include ROMs, commercial BIOS files, firmware dumps, or game
assets. Use only game and system files that you are legally entitled to use.

## Development

The durable WideMelon changes live in
[`patches/melonds-widemelon.patch`](patches/melonds-widemelon.patch). The
upstream checkout under `projects/` is generated and intentionally ignored.

After editing `projects/melonDS/src/`, export the changes with:

```sh
python3 scripts/export-patch.py
./scripts/build.sh
```

Headless profiles can skip the setup dialog:

```sh
WIDEMELON_VIEW_WIDTH=448 WIDEMELON_SCALE=4 \
  ./build/widemelon /path/to/game.nds
```

The patch is based on melonDS commit
`906e9ebb27da8c6a715cd7abab4abfe8a8d29427`.

For a binary release, generate the matching complete source archive after
committing the release state:

```sh
./scripts/package-source.sh 0.2.0-alpha
```

## Licence

WideMelon is a modified version of melonDS. The WideMelon modifications were
first published in 2026 under GPL-3.0-or-later. WideMelon is independent and is
not affiliated with or endorsed by the melonDS project, Nintendo, Game Freak,
or The Pokémon Company.

See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for the complete
licence and attribution information.
