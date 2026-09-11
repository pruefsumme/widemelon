# Third-party notices

WideMelon retains third-party code inherited from melonDS and dynamically links
libraries supplied by its build platform. Canonical license texts for inherited
components are installed beside this notice and remain in the source tree.

| Component | License | Canonical notice in the source tree |
| --- | --- | --- |
| melonDS | GPL-3.0-or-later | `LICENSE` |
| Dolphin-derived JIT utilities | GPL-2.0-or-later | `src/dolphin/license_dolphin.txt` and source headers |
| blip-buf | LGPL-2.1-or-later | `src/blip-buf/license.txt` and `blip_buf.c` |
| FatFs | BSD-style | `src/fatfs/LICENSE.txt` |
| libslirp | BSD-3-Clause and per-file notices | `src/net/libslirp/COPYRIGHT` |
| Teakra | MIT | `src/teakra/LICENSE` |
| tiny-AES-c | Unlicense/public domain | `src/tiny-AES-c/unlicense.txt` |
| FreeBIOS replacement | BSD-2-Clause | `freebios/drastic_bios_readme.txt` |
| xxHash | BSD-2-Clause | `src/xxhash/xxhash.h` |
| toml11 | MIT | `src/frontend/qt_sdl/toml/toml.hpp` |
| gif-h | Public domain | `src/frontend/qt_sdl/gif-h/gif.h` |
| SHA-1 implementation | Public domain | `src/sha1/sha1.c` |
| glad generated loaders | Public domain/WTFPL/CC0; Khronos portions Apache-2.0 | `src/frontend/glad/` source headers |
| QR Code generator library, commit `3c6d0b3cefb4e049dc337e82237c9644399716a8` | MIT | `licenses/QRCodeGenerator-LICENSE` |
| ENet | MIT | included in each release source archive |
| FAAD2 | GPL-2.0-or-later | included in each release source archive |

Linux AppImages also contain copyright files from the distribution packages
whose shared libraries are bundled. Matching sources for bundled copyleft
libraries are supplied in the release's complete corresponding-source archive.

Windows and macOS packages statically link their vcpkg dependencies. The macOS
bundle includes the installed ports' copyright and license texts. The Windows
executable exposes its GPL and source location in the About dialog, and the
official release page accompanies it with the same notices and complete source.
Matching source downloads, pinned build recipes, patches, resolved package
versions, and build provenance are supplied in the release's single complete
corresponding-source archive. These inputs allow modified libraries to be
rebuilt and relinked into WideMelon.
