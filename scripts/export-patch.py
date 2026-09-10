#!/usr/bin/env python3
"""Record local WideMelon engine work as the redistributable patch."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = root / 'projects/melonDS'
patch = subprocess.check_output(['git', 'diff', '--', 'src'], cwd=source)
new_files = [
    'src/WideMelon.h',
    'src/frontend/qt_sdl/WideMelonSetup.h',
    'src/frontend/qt_sdl/WideMelonSetup.cpp',
    'src/frontend/qt_sdl/PhoneBridge.h',
    'src/frontend/qt_sdl/PhoneBridge.cpp',
    'src/frontend/qt_sdl/PhoneLayout.h',
    'src/frontend/qt_sdl/PhoneLayout.cpp',
    'src/frontend/qt_sdl/PhoneLayoutDialog.h',
    'src/frontend/qt_sdl/PhoneLayoutDialog.cpp',
    'src/frontend/qt_sdl/PhoneProtocol.h',
    'src/frontend/qt_sdl/PhoneScreenDialog.h',
    'src/frontend/qt_sdl/PhoneScreenDialog.cpp',
    'src/frontend/qt_sdl/phone/phone.qrc',
    'src/frontend/qt_sdl/phone/index.html',
    'src/frontend/qt_sdl/phone/app.css',
    'src/frontend/qt_sdl/phone/app.js',
    'src/frontend/qt_sdl/io.github.pruefsumme.WideMelon.desktop',
    'src/frontend/qt_sdl/io.github.pruefsumme.WideMelon.metainfo.xml',
]
for filename in new_files:
    new_file = subprocess.run(['git', 'diff', '--no-index', '--', '/dev/null', filename],
                              cwd=source, capture_output=True)
    if new_file.returncode != 1:
        raise SystemExit(f'Expected the untracked {filename} file')
    patch += new_file.stdout
(root / 'patches').mkdir(exist_ok=True)
(root / 'patches/melonds-widemelon.patch').write_bytes(patch)
