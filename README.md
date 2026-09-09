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

WideMelon lets Nintendo DS games show more of their 3D world on a widescreen
display. Menus, sprites, videos, and the touchscreen stay at their normal size
instead of being stretched.

## Download

Get the latest Linux AppImage from the
[Releases page](https://github.com/pruefsumme/widemelon/releases). Development
builds are available from the
[Package Linux workflow](https://github.com/pruefsumme/widemelon/actions/workflows/package-linux.yml).

## Play

1. Open WideMelon.
2. Choose the viewport, window resolution, and render scale you want.
3. Select **Start melonDS**.
4. Drag a `.nds` file onto the melonDS window, or use **File > Open ROM**.

## What you get

- 4:3, 16:10, 16:9, 21:9, 32:9, and custom views
- Sharper 3D rendering from 1× to 8× scale
- Fullscreen, integer scaling, and common display resolutions
- The normal melonDS menus, controls, save states, and drag-and-drop support
- Separate settings and saves, so a normal melonDS installation is untouched

## What to expect

WideMelon is still an alpha. Some games hide objects outside the original
screen area, and some effects may not cover the added space. Battles, videos,
and menus can remain 4:3.

The widescreen mode uses the classic OpenGL renderer. Ready-made builds are
currently available for Linux only.

## Build from source

On Ubuntu 24.04, install the build tools and libraries:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev \
  libarchive-dev libenet-dev libzstd-dev libfaad-dev libegl1-mesa-dev \
  libgl1-mesa-dev libwayland-dev qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev libqt6svg6-dev
```

Then build and run:

```sh
./scripts/build.sh
./widemelon
```

The build script fetches the pinned melonDS source and dependencies, applies
the WideMelon patch, builds everything, and runs the automated tests.

## Technical overview

A Nintendo DS screen is normally 256 × 192 pixels. WideMelon creates a wider
3D target and adjusts the projection to reveal extra geometry on both sides:

```text
normal:     [       256 pixels       ]
widescreen: [ extra ][ 256 native ][ extra ]
```

The original view keeps its scale and center. Native 2D layers are placed over
the middle 256 pixels, so the interface and touchscreen are not widened.

The viewport width is fixed when the process starts. This keeps CPU geometry,
OpenGL buffers, shaders, and compositing on the same dimensions, which is why
profile changes require a restart.

## Development

The durable engine and Qt changes live in
[`patches/melonds-widemelon.patch`](patches/melonds-widemelon.patch). The
generated upstream checkout under `projects/` is intentionally ignored.

After editing `projects/melonDS/src/`, export and verify the patch with:

```sh
python3 scripts/export-patch.py
./scripts/build.sh
```

Automated or headless runs can supply a profile through environment variables:

```sh
WIDEMELON_VIEW_WIDTH=448 WIDEMELON_SCALE=4 \
  ./build/widemelon /path/to/game.nds
```

The patch is based on melonDS commit
`906e9ebb27da8c6a715cd7abab4abfe8a8d29427`.

For a tagged binary release, commit the release state and create its matching
complete source archive:

```sh
./scripts/package-source.sh 0.2.0-alpha
```

## Licence

WideMelon and its melonDS base are GPL-3.0-or-later. WideMelon is independent
and is not affiliated with or endorsed by the melonDS project, Nintendo, Game
Freak, or The Pokémon Company.

See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for complete licence
and attribution information.
