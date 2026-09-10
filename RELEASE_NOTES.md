<!-- Copyright (C) 2026 WideMelon contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# WideMelon 1.0.0

The first WideMelon release for Windows, macOS, and Linux.

- Wider 3D views from 4:3 through 32:9, with native proportions for 2D interfaces.
- Native startup settings for viewport, resolution, fullscreen, and 1×–8× rendering.
- Optional paired phone bottom screen and touch controller, with a visual layout editor.
- Session-only pairing, private-network checks, stream diagnostics, and firewall guidance.
- Separate settings and saves from a standard melonDS installation.

## Downloads

| System | Download | Run |
| --- | --- | --- |
| Windows 10/11, x64 | `WideMelon-1.0.0-windows-x86_64.zip` | Extract the whole folder, then open `widemelon.exe`. |
| macOS 13+, Apple Silicon | `WideMelon-1.0.0-macos-arm64.zip` | Extract and move `WideMelon.app` to Applications. |
| macOS 13+, Intel | `WideMelon-1.0.0-macos-x86_64.zip` | Extract and move `WideMelon.app` to Applications. |
| Linux x64, Ubuntu 22.04 or newer equivalent | `WideMelon-1.0.0-x86_64.AppImage` | Mark executable and open it. |

The Windows build is unsigned. macOS apps are ad-hoc signed unless optional
Developer ID credentials are configured, so macOS may require a one-time
approval in Privacy & Security. Only approve a download obtained from this
repository. Linux needs an OpenGL-capable graphics driver and FUSE 2, or can
run the image with `--appimage-extract-and-run`.

## Compatibility and limitations

Widescreen expansion remains experimental and game-dependent. Games can cull
objects outside the original view, and battles, videos, menus, or some effects
can remain 4:3. The classic OpenGL renderer is required for expanded views;
native 4:3 remains the compatibility profile.

The optional phone bridge sends JPEG video at up to 30 FPS without audio. Use
it only on a trusted private home network: pairing is required, but transport
is not encrypted. Wi-Fi performance and browser behavior vary by device.

No ROMs, commercial BIOS, firmware, saves, or game assets are included. Automated
tests cover profile math and phone controls/transport; they do not prove game
compatibility or every graphics driver. Supply your own legally obtained games.

## Source and verification

`SHA256SUMS` covers every downloadable binary and source archive. The application
source archive includes pinned FAAD2 and ENet. Linux's third-party source archive
and each native platform's dependency-source archive contain the matching
bundled dependency sources and build metadata. See `SOURCE.md` and `BUILD.md`
in the source archive for rebuilding instructions.

WideMelon is GPL-3.0-or-later, based on melonDS 1.1, and is maintained independently
from the melonDS project, Nintendo, Game Freak, and The Pokémon Company.
