# AUR packaging

<!-- Copyright (C) 2026 WideMelon contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

These directories are the canonical AUR recipes for `widemelon`,
`widemelon-git`, and `widemelon-bin`. The checked-in `@...@` values are release
template tokens, not checksums that may be published. On a tagged release,
`scripts/generate-aur.py` downloads or consumes the exact tag archive and
published AppImage, replaces every token, and asks Arch's `makepkg` to recreate
each `.SRCINFO` from its `PKGBUILD`.

Generate release-ready recipes inside an up-to-date Arch environment:

```sh
python scripts/generate-aur.py 1.0.1 --output build/aur
```

The release workflow separately verifies and clean-builds all three generated
recipes before it copies only `PKGBUILD` and `.SRCINFO` to AUR.

## CI SSH key

AUR publication uses a dedicated Ed25519 key. Add its public half to the
maintainer's AUR account and save only its private half as the GitHub Actions
repository secret `AUR_SSH_PRIVATE_KEY`. Do not reuse a personal workstation
key.

The workflow accepts the AUR Ed25519 host key only when `ssh-keygen` reports
the documented fingerprint:

```text
SHA256:RFzBCUItH9LZS0cKB5UE6ceAYhBD5C8GeOBip8Z11+4
```

The pin was recorded from AUR's Ed25519 host key on 2026-09-11. Confirm any
future rotation through an authenticated Arch announcement before changing it.
The [ArchWiki authentication guidance](https://wiki.archlinux.org/title/AUR_submission_guidelines#Authentication)
also recommends a dedicated AUR key pair. The workflow deliberately does not
disable strict host-key checking.

Review package changes against the current ArchWiki
[AUR submission](https://wiki.archlinux.org/title/AUR_submission_guidelines)
and [CMake packaging](https://wiki.archlinux.org/title/CMake_package_guidelines)
guidance before a tagged release.
