<!-- Copyright (C) 2026 WideMelon contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# WideMelon 1.0.2

WideMelon 1.0.2 introduces a polished home screen that puts recent games and
the most useful setup actions directly in the emulator window, with fixes for
the idle and ROM-launch transitions on Wayland.

## What's new

- A new WideMelon home screen replaces the original melonDS idle splash.
- Up to ten recent ROMs are shown in a larger, readable list and open with a
  double-click.
- Compact icon buttons provide direct access to display and resolution
  settings, phone pairing, and ROM selection.
- Display settings no longer open the phone connection dialog automatically.
- The home screen stays visible above the OpenGL renderer while idle.
- Opening a recent ROM no longer hides the renderer or crashes on Wayland.
- Product descriptions and documentation now reflect WideMelon's stable status.

## Downloads

| System | Download | Run |
| --- | --- | --- |
| Windows 10/11, x64 | `WideMelon-1.0.2-Windows-x86_64.exe` | Download and open the executable. |
| macOS 13+, Apple Silicon | `WideMelon-1.0.2-macOS-arm64.dmg` | Open the image and drag WideMelon to Applications. |
| macOS 13+, Intel | `WideMelon-1.0.2-macOS-x86_64.dmg` | Open the image and drag WideMelon to Applications. |
| Ubuntu 22.04+, x64 | `widemelon_1.0.2-1_amd64.deb` | Install with your software manager or `apt install ./widemelon_1.0.2-1_amd64.deb`. |
| Linux x64 | `WideMelon-1.0.2-x86_64.AppImage` | Mark the file executable and open it. |

Arch Linux users can install `widemelon`, `widemelon-bin`, or `widemelon-git`
from the AUR.

## Widescreen and phone controller

WideMelon expands supported 3D scenes while keeping native 2D interfaces,
menus, videos, and touchscreen content centered at their original proportions.
Expanded views require the classic OpenGL renderer. Results remain
game-dependent because some titles cull geometry outside the original view.

The optional phone bridge sends the bottom screen and customizable DS controls
to a paired browser on the same trusted private network. It is disabled by
default, uses session-only credentials, and does not stream audio.

## Source and verification

`SHA256SUMS` covers every downloadable binary and the
`WideMelon-1.0.2-Complete-Source.tar.zst` archive. The source archive contains
the exact application source, build scripts, pinned dependency sources, and
platform build metadata for this release.

WideMelon includes no ROMs, commercial BIOS or firmware files, saves, or game
assets. Use your own legally obtained games.

WideMelon is GPL-3.0-or-later, based on melonDS 1.1, and maintained independently
from the melonDS project, Nintendo, Game Freak, and The Pokémon Company.
