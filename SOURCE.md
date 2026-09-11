# Obtaining and modifying the source

The exact corresponding source for every WideMelon binary release is published
beside the end-user downloads at:

https://github.com/pruefsumme/widemelon/releases

`WideMelon-VERSION-Complete-Source.tar.zst` is the authoritative release source
download. GitHub's automatic Source code ZIP and tarball contain only a snapshot
of the repository. The complete archive additionally includes pinned sources,
dependency-manager recipes and patches, resolved package versions, build
provenance, and the sources bundled or statically linked into each platform
binary. Players do not need to download it.

The archive contains `application/` for WideMelon, its build scripts, FAAD2,
and ENet. `dependencies/linux-x86_64/` contains sources for shared libraries
bundled in the AppImage. The other platform directories contain the matching
vcpkg source downloads, pinned registry, ports, patches, and installed metadata
for Windows x64, macOS Intel, and macOS Apple Silicon.

To rebuild a native release with modified dependencies:

1. Extract the complete source archive and enter its `application` directory.
2. From the matching platform directory, create `.deps/vcpkg` in the
   application source tree and extract `vcpkg.tar` into it. Copy that platform
   directory's `downloads` folder to `.deps/vcpkg/downloads`.
3. Follow the native build commands in `BUILD.md` using that platform's triplet.
   vcpkg verifies cached downloads against the pinned recipes. Compilers,
   operating-system SDKs, and build tools must be installed separately.
4. Apply library changes through a vcpkg overlay port, including an updated
   source checksum, rebuild that port, and rebuild WideMelon to relink it.

There is no required vendor signature or technical restriction on rebuilding
or running a modified application. macOS rebuilds can be ad-hoc signed with
`codesign --force --deep --sign - build/native/WideMelon.app`.

The AppImage uses dynamically linked Qt libraries. To inspect or replace them:

```sh
./WideMelon-*.AppImage --appimage-extract
cp /path/to/compatible/libQt5*.so.5 squashfs-root/usr/lib/
./squashfs-root/AppRun
```

No signature, checksum, or technical restriction prevents a compatible rebuilt
library from being substituted in the extracted application directory.
