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
| Windows 10/11, x64 | `WideMelon-1.0.0-Windows-x86_64.exe` | Download and open the executable. |
| macOS 13+, Apple Silicon | `WideMelon-1.0.0-macOS-arm64.dmg` | Open the image and drag WideMelon to Applications. |
| macOS 13+, Intel | `WideMelon-1.0.0-macOS-x86_64.dmg` | Open the image and drag WideMelon to Applications. |
| Ubuntu 22.04+, x64 | `widemelon_1.0.0-1_amd64.deb` | Install with your software manager or `apt install ./widemelon_1.0.0-1_amd64.deb`. |
| Linux x64, Ubuntu 22.04 or newer equivalent | `WideMelon-1.0.0-x86_64.AppImage` | Mark executable and open it. |

The Windows build is unsigned, so SmartScreen can show an unknown-publisher
warning. macOS apps are ad-hoc signed unless optional Developer ID credentials
are configured, so macOS may require approval in Privacy & Security. Never
disable SmartScreen or Gatekeeper globally. Only approve a download obtained
from this repository after verifying its SHA-256 checksum. Linux needs an
OpenGL-capable graphics driver; AppImage users also need FUSE 2 or can run the
image with `--appimage-extract-and-run`.

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

`SHA256SUMS` covers every downloadable binary and the single
`WideMelon-1.0.0-Complete-Source.tar.zst` archive. Players do not need the source
download. It contains the exact application source, build scripts, pinned FAAD2
and ENet source, and the dependency sources and build metadata matching every
published platform binary. See `SOURCE.md` and `BUILD.md` inside it for rebuild
instructions.

WideMelon is GPL-3.0-or-later, based on melonDS 1.1, and is maintained independently
from the melonDS project, Nintendo, Game Freak, and The Pokémon Company.
