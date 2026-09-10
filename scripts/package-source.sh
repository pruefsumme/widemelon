#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-}
faad_commit=673a22a3c7c33e96e2ff7aae7c4d2bc190dfbf92
enet_commit=2662c0de09e36f2a2030ccc2c528a3e4c9e8138a

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]]; then
    printf 'Usage: %s VERSION (for example 0.1.0-alpha)\n' "$0" >&2
    exit 2
fi

for checkout in "$root" "$root/.deps/faad2" "$root/.deps/enet"; do
    if [[ ! -d "$checkout/.git" ]]; then
        printf 'Required Git checkout is missing: %s\nRun ./scripts/build.sh first.\n' "$checkout" >&2
        exit 1
    fi
done

if [[ -n $(git -C "$root" status --short) ]]; then
    printf 'Commit the release state before generating its source archive.\n' >&2
    exit 1
fi

check_revision() {
    local checkout=$1 expected=$2 name=$3
    if [[ $(git -C "$checkout" rev-parse HEAD) != "$expected" ]]; then
        printf 'Unexpected %s revision in %s.\n' "$name" "$checkout" >&2
        exit 1
    fi
}

check_revision "$root/.deps/faad2" "$faad_commit" FAAD2
check_revision "$root/.deps/enet" "$enet_commit" ENet

temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
bundle_name="widemelon-$version-source"
bundle="$temporary/$bundle_name"
mkdir -p "$bundle/.deps/faad2" "$bundle/.deps/enet"

git -C "$root" archive HEAD | tar -x -C "$bundle"
git -C "$root/.deps/faad2" archive "$faad_commit" | tar -x -C "$bundle/.deps/faad2"
git -C "$root/.deps/enet" archive "$enet_commit" | tar -x -C "$bundle/.deps/enet"
printf '%s\n' "$faad_commit" > "$bundle/.deps/faad2/.widemelon-source-revision"
printf '%s\n' "$enet_commit" > "$bundle/.deps/enet/.widemelon-source-revision"

mkdir -p "$root/dist"
tar -C "$temporary" -cJf "$root/dist/$bundle_name.tar.xz" "$bundle_name"
printf 'Created %s\n' "$root/dist/$bundle_name.tar.xz"
