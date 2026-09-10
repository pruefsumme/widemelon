#!/usr/bin/env python3
# Copyright (C) 2026 WideMelon contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a tested static native build and its exact vcpkg source inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("platform", choices=["windows-x86_64", "macos-x86_64", "macos-arm64"])
    parser.add_argument("triplet")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[.-][A-Za-z0-9.-]+)?", args.version):
        parser.error("Invalid release version")
    if not re.fullmatch(r"[A-Za-z0-9-]+", args.triplet):
        parser.error("Invalid vcpkg triplet")
    if run("git", "status", "--porcelain", capture_output=True).stdout:
        raise RuntimeError("Commit the release state before packaging")
    revision = run("git", "rev-parse", "HEAD", capture_output=True).stdout.strip()
    baseline = json.loads((root / "vcpkg.json").read_text())["vcpkg-configuration"]["default-registry"]["baseline"]
    vcpkg = root / ".deps/vcpkg"
    actual = run("git", "-C", str(vcpkg), "rev-parse", "HEAD", capture_output=True).stdout.strip()
    if actual != baseline:
        raise RuntimeError(f"Unexpected vcpkg revision: {actual}")
    build = root / "build/native"
    installed = build / "vcpkg_installed"
    share = installed / args.triplet / "share"
    downloads = vcpkg / "downloads"
    if not share.is_dir() or not downloads.is_dir():
        raise RuntimeError("Missing native dependencies or their source download cache")
    dist = root / "dist"
    dist.mkdir(exist_ok=True)
    name = f"WideMelon-{args.version}-{args.platform}"
    with tempfile.TemporaryDirectory(prefix="widemelon-package-") as temporary:
        work = Path(temporary)
        stage = work / name
        run("cmake", "--install", str(build), "--prefix", str(stage))
        mac = args.platform.startswith("macos-")
        app = stage / "Applications/WideMelon.app" if mac else stage
        docs = app / "Contents/Resources/doc" if mac else app / "doc"
        docs.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / "RELEASE_NOTES.md", docs)
        licenses = docs / "dependencies"
        licenses.mkdir()
        for copyright in sorted(share.glob("*/copyright")):
            shutil.copy2(copyright, licenses / f"{copyright.parent.name}.txt")
        if not list(licenses.iterdir()):
            raise RuntimeError("No vcpkg dependency licenses found")
        provenance = {"version": args.version, "platform": args.platform,
                      "commit": revision, "vcpkg_commit": baseline, "triplet": args.triplet}
        (docs / "build-info.json").write_text(json.dumps(provenance, indent=2) + "\n")
        shutil.copy2(installed / "vcpkg/status", docs / "dependency-versions.txt")
        if mac:
            executable = app / "Contents/MacOS/WideMelon"
            linked = run("otool", "-L", str(executable), capture_output=True).stdout
            for line in linked.splitlines()[1:]:
                library = line.strip().split(" (", 1)[0]
                if not library.startswith(("/usr/lib/", "/System/Library/")):
                    raise RuntimeError(f"Non-system dependency in static bundle: {library}")
            run("codesign", "--force", "--deep", "--sign", "-", str(app))
            run("codesign", "--verify", "--deep", "--strict", str(app))
            run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(dist / f"{name}.zip"))
        else:
            with zipfile.ZipFile(dist / f"{name}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
                for file in sorted(stage.rglob("*")):
                    if file.is_file():
                        archive.write(file, Path(name) / file.relative_to(stage))

        # Keep the actual downloaded inputs, pinned port recipes/patches and
        # resolved package metadata, including for statically linked libraries.
        source_name = f"widemelon-{args.version}-{args.platform}-dependency-source"
        source = work / source_name
        source.mkdir()
        with (source / "vcpkg.tar").open("wb") as output:
            subprocess.run(["git", "-C", str(vcpkg), "archive", baseline], stdout=output, check=True)
        shutil.copytree(downloads, source / "downloads", ignore=shutil.ignore_patterns("tools", "*.part", "*.lock"))
        shutil.copytree(share, source / "installed-share")
        shutil.copy2(installed / "vcpkg/status", source / "dependency-versions.txt")
        shutil.copy2(docs / "build-info.json", source)
        shutil.copy2(root / "SOURCE.md", source)
        # vcpkg verifies these source archives against the hashes in its ports.
        sums = []
        for file in sorted((source / "downloads").rglob("*")):
            if file.is_file():
                with file.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                sums.append(f"{digest}  {file.relative_to(source).as_posix()}\n")
        if not sums:
            raise RuntimeError("Source cache is empty; do not release binaries without dependency sources")
        (source / "SHA256SUMS").write_text("".join(sums))
        with tarfile.open(dist / f"{source_name}.tar.gz", "w:gz", compresslevel=6) as archive:
            archive.add(source, arcname=source_name)
    print(f"Packaged {name} and matching dependency sources")


if __name__ == "__main__":
    main()
