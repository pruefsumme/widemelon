#!/usr/bin/env bash
# Copyright (C) 2026 WideMelon contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-}
appdir=${2:-}

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]] ||
   [[ ! -d $appdir/usr/lib ]]; then
    printf 'Usage: %s VERSION APPDIR\n' "$0" >&2
    exit 2
fi
for command_name in apt-get dpkg-query tar; do
    command -v "$command_name" >/dev/null || {
        printf 'Required command is missing: %s\n' "$command_name" >&2
        exit 1
    }
done

temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
bundle_name="widemelon-$version-third-party-source"
bundle="$temporary/$bundle_name"
mkdir -p "$bundle/packages" "$bundle/copyright"

typeset -A binary_packages=()
while IFS= read -r copyright_file; do
    package_name=$(basename "$(dirname "$copyright_file")")
    binary_packages[${package_name%%:*}]=1
    cp "$copyright_file" "$bundle/copyright/$package_name.txt"
done < <(find "$appdir/usr/share/doc" -mindepth 2 -maxdepth 2 -type f -name copyright -print 2>/dev/null | sort)

: > "$bundle/libraries.tsv"
: > "$bundle/unresolved-libraries.txt"
while IFS= read -r library; do
    library_name=$(basename "$library")
    owner=$(dpkg-query -S "*/$library_name" 2>/dev/null | head -n 1 || true)
    if [[ -z $owner ]]; then
        printf '%s\n' "${library#"$appdir/"}" >> "$bundle/unresolved-libraries.txt"
        continue
    fi
    package_name=${owner%%:*}
    binary_packages[$package_name]=1
    printf '%s\t%s\n' "${library#"$appdir/"}" "$package_name" >> "$bundle/libraries.tsv"
done < <(find "$appdir/usr/lib" -type f -name '*.so*' -print | sort)

if [[ -s $bundle/unresolved-libraries.txt ]]; then
    printf 'Could not identify packages for these bundled libraries:\n' >&2
    cat "$bundle/unresolved-libraries.txt" >&2
    exit 1
fi
rm "$bundle/unresolved-libraries.txt"

: > "$bundle/packages.tsv"
for package_name in "${!binary_packages[@]}"; do
    dpkg-query -W -f='${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n' "$package_name"
done | sort -u > "$bundle/packages.tsv"

while IFS=$'\t' read -r binary_name binary_version source_name source_version; do
    source_name=${source_name:-${binary_name%%:*}}
    source_version=${source_version:-$binary_version}
    printf '%s\t%s\n' "$source_name" "$source_version"
done < "$bundle/packages.tsv" | sort -u > "$bundle/sources.tsv"

while IFS=$'\t' read -r source_name source_version; do
    (
        cd "$bundle/packages"
        apt-get source --download-only "$source_name=$source_version"
    )
done < "$bundle/sources.tsv"

mkdir -p "$root/dist"
tar -C "$temporary" -cJf "$root/dist/$bundle_name.tar.xz" "$bundle_name"
printf 'Created %s\n' "$root/dist/$bundle_name.tar.xz"
