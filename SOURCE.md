# Obtaining and modifying the source

The exact WideMelon application source for each binary release is published
beside that binary at:

https://github.com/pruefsumme/widemelon/releases

Release source archives include the build scripts and the pinned FAAD2 and ENet
sources used for the application. A separate third-party source archive covers
copyleft shared libraries bundled into the Linux AppImage.

The AppImage uses dynamically linked Qt libraries. To inspect or replace them:

```sh
./WideMelon-*.AppImage --appimage-extract
cp /path/to/compatible/libQt5*.so.5 squashfs-root/usr/lib/
./squashfs-root/AppRun
```

No signature, checksum, or technical restriction prevents a compatible rebuilt
library from being substituted in the extracted application directory.
