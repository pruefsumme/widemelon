<!-- Copyright (C) 2026 WideMelon contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Building WideMelon

Clone `https://github.com/pruefsumme/widemelon.git` or extract the complete
application source archive from a release. No game files are needed to build
or run the automated tests. C++17, CMake, Ninja, and Git are required.

## Linux

On Ubuntu 24.04:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config nodejs \
  extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev \
  libarchive-dev libzstd-dev libegl1-mesa-dev libgl1-mesa-dev libwayland-dev \
  qt6-base-dev qt6-base-private-dev qt6-multimedia-dev libqt6svg6-dev \
  qt6-websockets-dev
./scripts/build.sh
./widemelon
```

The script builds pinned FAAD2 2.11.2 and ENet 1.3.18 from `.deps/`, then builds
the emulator and runs its tests. It accepts `WIDEMELON_BUILD_JOBS` (default 6).
Use `WIDEMELON_USE_QT6=0` with Qt 5.15 development libraries to build with Qt 5.
Linux release AppImages use Qt 5 on Ubuntu 22.04 for broader compatibility.

## Windows and macOS release builds

Native releases statically link dependencies built from the registry and
overlays pinned in `vcpkg.json`. Qt WebSockets, Concurrent, and JPEG support
are required for the phone bridge. The release workflow builds on Windows
x64, Intel macOS, and Apple Silicon macOS separately.

On Windows, install Visual Studio 2022 or newer with the C++ desktop workload,
Clang/LLVM, CMake, Ninja, Git, Python 3.11+, and Node.js 22. Run from a developer
shell with `clang.exe`, `clang++.exe`, and `llvm-rc.exe` on PATH. The workflow
uses Git Bash after activating Visual Studio. On macOS, install Xcode's command
line tools and `brew install cmake ninja autoconf automake autoconf-archive
libtool pkg-config python node`.

Prepare the pinned dependency manager (use Git Bash on Windows):

```sh
git clone https://github.com/microsoft/vcpkg.git .deps/vcpkg
git -C .deps/vcpkg checkout 9b965a116838c6cdcd36bca60d1b81b030c8ab8d
```

Select the preset and triplet for the host:

| System | Preset | Triplet |
| --- | --- | --- |
| Windows x64 | `release-windows-x86_64` | `x64-windows-static-release` |
| macOS Intel | `release-mac-x86_64` | `x64-osx-13-release` |
| macOS Apple Silicon | `release-mac-arm64` | `arm64-osx-13-release` |

For example, on Apple Silicon:

```sh
cmake --preset release-mac-arm64 -B build/native \
  -DVCPKG_ROOT="$PWD/.deps/vcpkg" -DUSE_RECOMMENDED_TRIPLETS=OFF \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-13-release \
  -DVCPKG_HOST_TRIPLET=arm64-osx-13-release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DENABLE_LTO_RELEASE=OFF
cmake --build build/native --parallel 3
```

Use the corresponding preset and triplet on other hosts; omit the macOS
deployment argument on Windows. The output is `build/native/widemelon.exe`
or `build/native/WideMelon.app`. The first dependency build can take an hour.
The selected Qt version requires Windows 10 1809+ or macOS 13+; see
[Qt's platform requirements](https://doc.qt.io/qt-6/supported-platforms.html).

Configure tests using the same static dependencies (replace `TRIPLET`):

```sh
cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUSE_QT6=ON -DVCPKG_MANIFEST_MODE=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.deps/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_INSTALLED_DIR="$PWD/build/native/vcpkg_installed" \
  -DVCPKG_TARGET_TRIPLET=TRIPLET
cmake --build build/tests --parallel 3
ctest --test-dir build/tests --output-on-failure
```

On Windows also pass `-DCMAKE_CXX_COMPILER=clang++` and
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` when configuring tests. On macOS
pass `-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0`. The bridge tests use Qt's offscreen
platform and generated frames; they do not require a ROM. The normal bridge test
uses loopback. To exercise the production listener on a real private adapter,
run it with `WIDEMELON_PHONE_TEST_ADDRESS` set to the host's active IPv4 address;
the same HTTP, WebSocket, subnet, and pairing checks then run over that address.

## Release packaging

`.github/workflows/release.yml` builds all four artifacts, runs tests, packages
licenses and exact dependency sources, and publishes only after every platform
succeeds. Branch and manual runs only upload development artifacts. A `v1.0.0`
tag must match `WIDEMELON_VERSION` in `CMakeLists.txt` to publish. Release notes
come from `RELEASE_NOTES.md`. `SHA256SUMS` covers every published asset.

Run `./scripts/build.sh` before committing release changes. Once committed:

```sh
./scripts/package-source.sh 1.0.0
```

The native packaging command, after building and testing on that host, is:

```sh
python scripts/package-native.py 1.0.0 macos-arm64 arm64-osx-13-release
```

Use the matching platform/triplet for Windows or Intel macOS. On macOS, this
creates both `dist/WideMelon.app` and the release ZIP; the former is the direct
application artifact exposed by the development workflow. The ZIP contains
only `WideMelon.app`. The dependency-source archive is a separate artifact and
release asset. Keep the vcpkg download cache: it is the source input for that
archive. Native release CI disables separate binary caching and restores
installed libraries only together with their downloaded sources. The archive
includes pinned vcpkg recipes and patches, original source downloads, package
versions, and the application commit. See `SOURCE.md` for rebuilding from those
archives.

Developer ID signing and notarization are optional. To enable them, provide all
six GitHub Actions secrets: `MACOS_CERTIFICATE_P12` (base64-encoded Developer
ID Application certificate), `MACOS_CERTIFICATE_PASSWORD`,
`MACOS_SIGNING_IDENTITY`, `MACOS_NOTARY_KEY_ID`, `MACOS_NOTARY_ISSUER_ID`, and
`MACOS_NOTARY_PRIVATE_KEY` (the App Store Connect API key in `.p8` format).
Without those secrets, branch, manual, and tagged macOS runs publish an
ad-hoc-signed bundle that may require a one-time Open confirmation. Windows
executables remain unsigned. Native packages include licenses and source access
instructions. Never add ROMs, BIOS/firmware dumps, saves, game assets, or
generated build directories to a release.

Automated tests cannot prove gameplay, GPU output, or behavior on every driver.
Before publishing, smoke-test the native setup dialog, ROM selection, 4:3 and
expanded OpenGL profiles, and phone pairing with a locally available game.
