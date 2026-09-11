#!/usr/bin/env python3
# Copyright (C) 2026 WideMelon contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Combine exact application and dependency sources for one binary release."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


def extract_archive(archive: Path, destination: Path) -> None:
    unpack = destination.parent / f".{destination.name}-unpack"
    unpack.mkdir()
    with tarfile.open(archive) as source:
        source.extractall(unpack, filter="data")
    entries = list(unpack.iterdir())
    if len(entries) != 1 or not entries[0].is_dir():
        raise RuntimeError(f"Expected one top-level directory in {archive.name}")
    entries[0].rename(destination)
    unpack.rmdir()


def deduplicate_files(root: Path) -> int:
    """Replace identical regular files with hard links for compact tar output."""
    seen: dict[tuple[int, str], Path] = {}
    duplicates = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        size = path.stat().st_size
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        key = (size, digest)
        original = seen.get(key)
        if original is None:
            seen[key] = path
            continue
        path.unlink()
        os.link(original, path)
        duplicates += 1
    return duplicates


def write_manifest(root: Path) -> None:
    lines = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        lines.append(f"{digest}  {path.relative_to(root).as_posix()}\n")
    (root / "MANIFEST.sha256").write_text("".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("dist", nargs="?", default="dist")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[.-][A-Za-z0-9.-]+)?", args.version):
        parser.error("Invalid release version")

    root = Path(__file__).resolve().parent.parent
    dist = (root / args.dist).resolve()
    expected = {
        "application": dist / f"widemelon-{args.version}-source.tar.xz",
        "dependencies/linux-x86_64": dist / f"widemelon-{args.version}-third-party-source.tar.xz",
        "dependencies/windows-x86_64": dist / f"widemelon-{args.version}-windows-x86_64-dependency-source.tar.gz",
        "dependencies/macos-x86_64": dist / f"widemelon-{args.version}-macos-x86_64-dependency-source.tar.gz",
        "dependencies/macos-arm64": dist / f"widemelon-{args.version}-macos-arm64-dependency-source.tar.gz",
    }
    missing = [path.name for path in expected.values() if not path.is_file()]
    if missing:
        raise RuntimeError(f"Missing source inputs: {', '.join(missing)}")

    output = dist / f"WideMelon-{args.version}-Complete-Source.tar.zst"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="widemelon-complete-source-") as temporary:
        work = Path(temporary)
        bundle = work / f"WideMelon-{args.version}-Complete-Source"
        bundle.mkdir()
        for relative, archive in expected.items():
            destination = bundle / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            extract_archive(archive, destination)

        duplicate_count = deduplicate_files(bundle)
        (bundle / "README.txt").write_text(
            "WideMelon complete corresponding source\n"
            f"Release: {args.version}\n\n"
            "application/ contains the exact WideMelon release source and build scripts.\n"
            "dependencies/ contains the source inputs bundled or statically linked by\n"
            "each published platform binary. See application/SOURCE.md for rebuild steps.\n"
            f"Identical files represented by hard links: {duplicate_count}\n",
            encoding="utf-8",
        )
        write_manifest(bundle)
        timestamp = subprocess.run(
            ["git", "show", "-s", "--format=%ct", "HEAD"],
            cwd=root, check=True, text=True, capture_output=True,
        ).stdout.strip()
        subprocess.run(
            ["tar", "--sort=name", "--owner=0", "--group=0", "--numeric-owner",
             f"--mtime=@{timestamp}", "--zstd", "-cf", str(output),
             "-C", str(work), bundle.name],
            check=True,
        )
    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError("Complete source archive was not created")
    print(f"Created {output}")


if __name__ == "__main__":
    main()
