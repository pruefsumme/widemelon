# Obtaining and modifying the source

The exact WideMelon application source for each binary release is published
beside that binary at:

https://github.com/pruefsumme/widemelon/releases

Release source archives include the build scripts and the pinned FAAD2 and ENet
sources used for the application. A separate third-party source archive covers
copyleft shared libraries bundled into the Linux AppImage.

Windows and macOS binaries use static libraries from the pinned vcpkg registry.
Each native platform has a separate `dependency-source.tar.gz` archive holding
the dependency source downloads, `vcpkg.tar` (the pinned dependency manager,
ports and patches), resolved package versions, and build provenance. The
application source archive contains the matching `vcpkg.json`, overlays, and
the release workflow with compiler/configuration options.

To rebuild a native release with modified dependencies:

1. Extract the application source archive and the matching native dependency
   source archive.
2. Create `.deps/vcpkg` in the application source tree and extract `vcpkg.tar`
   into it. Copy the dependency archive's `downloads` directory to
   `.deps/vcpkg/downloads`.
3. Follow the native build commands in `BUILD.md` using that platform's triplet.
   vcpkg verifies cached downloads against the pinned recipes. Compilers,
   operating-system SDKs, and build tools must be installed separately.
4. Apply library changes through a vcpkg overlay port (including any updated
   source checksum), rebuild that port, then rebuild WideMelon to relink it.

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
