<p align="center">
  <img src="assets/widemelon.png" alt="WideMelon logo" width="220">
</p>

<h1 align="center">WideMelon</h1>

<p align="center">
  Nintendo DS games in widescreen — with an optional phone touchscreen and controller. Enjoy DS games like never before.
</p>

<p align="center">
  <a href="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml"><img src="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
  <a href="https://github.com/pruefsumme/widemelon/releases/latest"><img src="https://img.shields.io/github/v/release/pruefsumme/widemelon?display_name=tag" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-5b7fa3" alt="Windows, macOS, and Linux">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="GPL-3.0-or-later"></a>
</p>

WideMelon is a Nintendo DS emulator built from melonDS that gives supported
games a genuinely wider 3D view. It reveals more of the game world at the
sides while keeping menus, sprites, videos, and the touchscreen at their
original proportions.

For a more console-like setup, WideMelon can also send the bottom screen to
your phone and use it as a touch controller. No separate mobile app is needed:
scan the QR code and play from your phone's browser.

## Highlights

- True widescreen 3D views from native 4:3 through 32:9.
- Unstretched 2D interfaces, menus, sprites, videos, and touchscreen content.
- Optional phone bottom screen, touch input, and customizable DS controls.
- Render scales from 1× to 8×, fullscreen, and integer scaling.
- Recent-ROM home screen, save states, drag and drop, and familiar melonDS tools.
- Separate configuration and saves from a standard melonDS installation.

## Download

Download the latest build from the
[WideMelon Releases page](https://github.com/pruefsumme/widemelon/releases/latest).

| Platform | Download |
| --- | --- |
| Windows | Download the x64 `.exe` and open it. |
| macOS | Download the Apple Silicon or Intel `.dmg`, then drag WideMelon to Applications. |
| Linux | Install the Debian package, use the AppImage, or choose an AUR package below. |

Development builds from the newest commit are available from the
[Release workflow](https://github.com/pruefsumme/widemelon/actions/workflows/release.yml).

### Linux

#### Ubuntu and Debian

Download and install the current package:

```sh
VERSION=1.0.2
wget "https://github.com/pruefsumme/widemelon/releases/download/v${VERSION}/widemelon_${VERSION}-1_amd64.deb"
sudo apt install "./widemelon_${VERSION}-1_amd64.deb"
```

WideMelon will appear in your application menu and can also be started with
`widemelon`.

#### AppImage

The AppImage works on most x86_64 Linux distributions:

```sh
VERSION=1.0.2
wget "https://github.com/pruefsumme/widemelon/releases/download/v${VERSION}/WideMelon-${VERSION}-x86_64.AppImage"
chmod +x "WideMelon-${VERSION}-x86_64.AppImage"
./"WideMelon-${VERSION}-x86_64.AppImage"
```

#### Arch Linux and AUR

Choose one package. All three install the same `widemelon` command and desktop
entry, so they cannot be installed together.

Stable release, built locally:

```sh
yay -S widemelon
```

Stable release, prebuilt binary:

```sh
yay -S widemelon-bin
```

Newest development revision from `main`:

```sh
yay -S widemelon-git
```

## Quick start

1. Open WideMelon and choose your viewport, window resolution, and render scale.
2. Open a legally obtained `.nds` ROM with the folder button or **File > Open ROM**.
3. The next time you start WideMelon, double-click the game in **Recent ROMs**.
4. For phone play, select the phone button and follow the pairing steps below.

You can also drag a ROM directly onto the WideMelon window. WideMelon does not
include games, ROMs, commercial BIOS or firmware files, or saves.

## Phone screen and controller

WideMelon can move the physical bottom screen and DS controls to a phone while
the wide top screen stays on your computer.

1. Select the phone button on the home screen, or open
   **Config > Phone screen & controller…**.
2. Choose the private Wi-Fi or Ethernet address shared with your phone.
3. Start the connection and scan the QR code with your phone.
4. Use **Edit controller layout…** to move, resize, or customize the controls.

The bridge is off by default and requires session-only pairing. Keep the phone
and computer on the same trusted, non-guest network. If the phone disconnects,
the bottom screen automatically returns to the desktop window. The phone bridge
does not stream audio.

See the [phone setup and troubleshooting guide](BUILD.md#phone-screen-and-controller)
for firewall help, diagnostics, and network details.

## How widescreen works

A Nintendo DS screen is normally 256 × 192 pixels. WideMelon expands the 3D
render target and adjusts the projection so compatible games reveal additional
geometry on both sides. The original view remains centered and keeps the same
scale; native 2D layers are composited over the middle without stretching.

Results depend on how each game draws its scene. Some games expose a great deal
of additional world detail, while others may cull objects outside the original
view. Menus, battles, videos, and special effects can remain 4:3 by design. The
expanded profiles require the classic OpenGL renderer; native 4:3 remains
available as the compatibility profile.

> **Compatibility note:** Widescreen support is game-dependent. It may not work correctly or provide much benefit in every game, because results depend on how that game renders its 3D scene.

## Build from source

WideMelon is a C++17 and CMake project. The complete source, pinned dependencies,
platform prerequisites, build commands, test workflow, and packaging notes are
documented in [BUILD.md](BUILD.md).

On a supported Linux development system, the normal workflow is:

```sh
./scripts/build.sh
./widemelon
```

## Project and credits

WideMelon is based on [melonDS](https://github.com/melonDS-emu/melonDS) and is
maintained as an independent project. Thanks to the melonDS contributors and
everyone testing WideMelon, reporting compatibility results, and improving the
experience.

## License

WideMelon is free software released under the
[GNU General Public License v3.0 or later](LICENSE). Third-party components and
their licenses are documented in [THIRD_PARTY.md](THIRD_PARTY.md).
