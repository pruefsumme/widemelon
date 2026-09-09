#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
jobs=${WIDEMELON_BUILD_JOBS:-6}
prefix="$root/tools/local"
melon_commit=906e9ebb27da8c6a715cd7abab4abfe8a8d29427
faad_commit=673a22a3c7c33e96e2ff7aae7c4d2bc190dfbf92
enet_commit=2662c0de09e36f2a2030ccc2c528a3e4c9e8138a
cmake_options=(-DENABLE_LTO_RELEASE=OFF)
if [[ ${WIDEMELON_USE_QT6:-1} == 0 ]]; then
    cmake_options+=(-DUSE_QT6=OFF)
fi
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p tools projects
if ! pkg-config --exists faad2; then
    if [[ ! -f tools/faad2/CMakeLists.txt ]]; then
        git clone --depth 1 --branch 2.11.2 https://github.com/knik0/faad2.git tools/faad2
    fi
    if [[ -d tools/faad2/.git ]] && [[ $(git -C tools/faad2 rev-parse HEAD) != "$faad_commit" ]]; then
        printf 'Unexpected FAAD2 revision in tools/faad2.\n' >&2
        exit 1
    fi
    cmake -S tools/faad2 -B tools/faad2/build -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF
    cmake --build tools/faad2/build -j "$jobs"
    cmake --install tools/faad2/build
fi
if ! pkg-config --exists libenet; then
    if [[ ! -f tools/enet/CMakeLists.txt ]]; then
        git clone --depth 1 --branch v1.3.18 https://github.com/lsalzman/enet.git tools/enet
    fi
    if [[ -d tools/enet/.git ]] && [[ $(git -C tools/enet rev-parse HEAD) != "$enet_commit" ]]; then
        printf 'Unexpected ENet revision in tools/enet.\n' >&2
        exit 1
    fi
    cmake -S tools/enet -B tools/enet/build -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix"
    cmake --build tools/enet/build -j "$jobs"
    cmake --install tools/enet/build
    mkdir -p "$prefix/lib/pkgconfig"
    cat > "$prefix/lib/pkgconfig/libenet.pc" <<PC
prefix=$prefix
libdir=\${prefix}/lib/static
includedir=\${prefix}/include
Name: libenet
Description: ENet reliable UDP
Version: 1.3.18
Cflags: -I\${includedir}
Libs: -L\${libdir} -lenet
PC
fi
if [[ ! -f projects/melonDS/CMakeLists.txt ]]; then
    git clone https://github.com/melonDS-emu/melonDS.git projects/melonDS
    git -C projects/melonDS checkout --detach "$melon_commit"
fi
if [[ -d projects/melonDS/.git ]]; then
    if [[ $(git -C projects/melonDS rev-parse HEAD) != "$melon_commit" ]]; then
        printf 'Unexpected melonDS revision in projects/melonDS.\n' >&2
        exit 1
    fi
    if ! git -C projects/melonDS apply --reverse --check "$root/patches/melonds-widemelon.patch" 2>/dev/null; then
        git -C projects/melonDS apply --check "$root/patches/melonds-widemelon.patch"
        git -C projects/melonDS apply "$root/patches/melonds-widemelon.patch"
    fi
elif [[ ! -f projects/melonDS/.widemelon-patched-source ]]; then
    printf 'projects/melonDS is neither a Git checkout nor a WideMelon source bundle.\n' >&2
    exit 1
fi
cmake -S projects/melonDS -B build -G Ninja "${cmake_options[@]}"
cmake --build build -j "$jobs"
cmake -S . -B build/tests -G Ninja
cmake --build build/tests -j "$jobs"
ctest --test-dir build/tests --output-on-failure
printf '\nRun ./widemelon to play.\n'
