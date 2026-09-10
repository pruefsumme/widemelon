#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
jobs=${WIDEMELON_BUILD_JOBS:-6}
deps="$root/.deps"
prefix="$deps/local"
faad_commit=673a22a3c7c33e96e2ff7aae7c4d2bc190dfbf92
enet_commit=2662c0de09e36f2a2030ccc2c528a3e4c9e8138a
cmake_options=(-DENABLE_LTO_RELEASE=OFF)
if [[ ${WIDEMELON_USE_QT6:-1} == 0 ]]; then
    cmake_options+=(-DUSE_QT6=OFF)
fi
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p "$deps"

prepare_dependency() {
    local name=$1 checkout=$2 repository=$3 ref=$4 expected=$5
    if [[ ! -f "$checkout/CMakeLists.txt" ]]; then
        git clone --depth 1 --branch "$ref" "$repository" "$checkout"
    fi
    if [[ -d "$checkout/.git" ]]; then
        if [[ $(git -C "$checkout" rev-parse HEAD) != "$expected" ]]; then
            printf 'Unexpected %s revision in %s.\n' "$name" "$checkout" >&2
            exit 1
        fi
    elif [[ ! -f "$checkout/.widemelon-source-revision" ]] ||
         [[ $(<"$checkout/.widemelon-source-revision") != "$expected" ]]; then
        printf 'Unverified %s source in %s.\n' "$name" "$checkout" >&2
        exit 1
    fi
}

prepare_dependency FAAD2 "$deps/faad2" https://github.com/knik0/faad2.git 2.11.2 "$faad_commit"
cmake -S "$deps/faad2" -B "$deps/faad2/build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF
cmake --build "$deps/faad2/build" -j "$jobs"
cmake --install "$deps/faad2/build"

prepare_dependency ENet "$deps/enet" https://github.com/lsalzman/enet.git v1.3.18 "$enet_commit"
cmake -S "$deps/enet" -B "$deps/enet/build" -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$deps/enet/build" -j "$jobs"
cmake --install "$deps/enet/build"
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

cmake -S . -B build -G Ninja "${cmake_options[@]}"
cmake --build build -j "$jobs"
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests -j "$jobs"
ctest --test-dir build/tests --output-on-failure
printf '\nRun ./widemelon to play.\n'
