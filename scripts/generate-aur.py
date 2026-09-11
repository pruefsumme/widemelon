#!/usr/bin/env python3
"""Render release-ready AUR recipes and regenerate their .SRCINFO files."""

# Copyright (C) 2026 WideMelon contributors
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path


REPOSITORY = "https://github.com/pruefsumme/widemelon"
PACKAGES = ("widemelon", "widemelon-git", "widemelon-bin")
VERSION_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")


def download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "WideMelon-AUR-generator"})
    with urllib.request.urlopen(request) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_source_archive(path: Path, version: str) -> None:
    expected = f"widemelon-{version}/CMakeLists.txt"
    with tarfile.open(path, "r:gz") as archive:
        try:
            cmake = archive.extractfile(expected)
        except KeyError as error:
            raise RuntimeError(f"source archive does not contain {expected}") from error
        if cmake is None:
            raise RuntimeError(f"source archive entry is not a file: {expected}")
        contents = cmake.read().decode("utf-8")
    if f'set(WIDEMELON_VERSION "{version}")' not in contents:
        raise RuntimeError("source archive WIDEMELON_VERSION does not match the requested release")


def validate_appimage(path: Path) -> None:
    with path.open("rb") as appimage:
        if appimage.read(4) != b"\x7fELF":
            raise RuntimeError("AppImage is not an ELF executable")


def tagged_commit(root: Path, version: str) -> str:
    tag = f"v{version}^{{commit}}"
    result = subprocess.run(
        ["git", "rev-parse", "--verify", tag], cwd=root, check=True,
        stdout=subprocess.PIPE, text=True,
    )
    return result.stdout.strip()


def render(root: Path, output: Path, replacements: dict[str, str]) -> None:
    templates = root / "packaging" / "aur"
    if output.resolve() == templates.resolve():
        raise RuntimeError("refusing to replace the checked-in release templates")
    output.mkdir(parents=True, exist_ok=True)
    for package in PACKAGES:
        destination = output / package
        destination.mkdir(parents=True, exist_ok=True)
        text = (templates / package / "PKGBUILD").read_text(encoding="utf-8")
        for token, value in replacements.items():
            text = text.replace(f"@{token}@", value)
        if re.search(r"@[A-Z0-9_]+@", text):
            raise RuntimeError(f"unresolved template token in {package}/PKGBUILD")
        (destination / "PKGBUILD").write_text(text, encoding="utf-8")

        result = subprocess.run(
            ["makepkg", "--printsrcinfo"], cwd=destination, check=True,
            stdout=subprocess.PIPE, text=True,
        )
        (destination / ".SRCINFO").write_text(result.stdout, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="stable release version (X.Y.Z)")
    parser.add_argument("--output", type=Path, default=Path("build/aur"))
    parser.add_argument("--source-archive", type=Path,
                        help="already-downloaded GitHub tag archive")
    parser.add_argument("--appimage", type=Path,
                        help="already-downloaded release AppImage")
    args = parser.parse_args()
    if VERSION_RE.fullmatch(args.version) is None:
        parser.error("version must be a semantic X.Y.Z release without leading zeroes")

    root = Path(__file__).resolve().parent.parent
    commit = tagged_commit(root, args.version)
    with tempfile.TemporaryDirectory(prefix="widemelon-aur-") as temporary:
        temporary_path = Path(temporary)
        source = args.source_archive.resolve() if args.source_archive else temporary_path / "source.tar.gz"
        appimage = args.appimage.resolve() if args.appimage else temporary_path / "widemelon.AppImage"
        if args.source_archive is None:
            download(f"{REPOSITORY}/archive/refs/tags/v{args.version}.tar.gz", source)
        if args.appimage is None:
            download(
                f"{REPOSITORY}/releases/download/v{args.version}/"
                f"WideMelon-{args.version}-x86_64.AppImage",
                appimage,
            )
        if not source.is_file() or not appimage.is_file():
            raise RuntimeError("source archive and AppImage must both be regular files")
        validate_source_archive(source, args.version)
        validate_appimage(appimage)
        render(root, args.output.resolve(), {
            "VERSION": args.version,
            "SOURCE_SHA256": sha256(source),
            "APPIMAGE_SHA256": sha256(appimage),
            "GIT_PKGVER": f"{args.version}.r0.g{commit[:7]}",
        })
    print(f"Generated AUR recipes for {args.version} in {args.output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
