<p align="center">
  <img src="assets/widemelon.png" alt="WideMelon logo" width="220">
</p>

<h1 align="center">WideMelon</h1>

<p align="center">
  An experimental widescreen build of melonDS.
</p>

<p align="center">
  <a href="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml"><img src="https://github.com/pruefsumme/widemelon/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="GPL-3.0-or-later"></a>
</p>

WideMelon lets Nintendo DS games show more of their 3D world on a widescreen
display. Menus, sprites, videos, and the touchscreen stay at their normal size
instead of being stretched.

## Download

Get Windows x64, macOS (Apple Silicon or Intel), and Linux x64 builds from the
[Releases page](https://github.com/pruefsumme/widemelon/releases). Extract the
Windows ZIP and open `widemelon.exe`; on macOS, move `WideMelon.app` to
Applications; on Linux, mark the AppImage executable and open it. The release
notes include system requirements and first-run instructions. Development
builds are available from the
[Release workflow](https://github.com/pruefsumme/widemelon/actions/workflows/release.yml).

## Play

1. Open WideMelon.
2. Choose the viewport, window resolution, and render scale you want.
3. Select **Start melonDS**.
4. Drag a `.nds` file onto the melonDS window, or use **File > Open ROM**.

## What you get

- 4:3, 16:10, 16:9, 21:9, 32:9, and custom views
- Sharper 3D rendering from 1× to 8× scale
- Fullscreen, integer scaling, and common display resolutions
- The normal melonDS menus, controls, save states, and drag-and-drop support
- An optional phone bottom screen and touch controller over your local network
- Separate settings and saves, so a normal melonDS installation is untouched

## Phone screen and controller

WideMelon can place the physical bottom screen and DS controls on one phone.
The bridge is off by default and never starts without an explicit action.

1. Open **Phone screen…** beside the resolution selector, or later choose
   **Config > Phone screen & controller…**.
2. Select the private IPv4 address shared with your phone and choose
   **Enable for this session** or **Start server**.
3. Scan the displayed QR code with current Android Chrome or iOS Safari, or
   open the displayed `http://` address and enter its 10-digit pairing code.

While the phone is connected, the desktop uses the wide top screen by itself.
While waiting, after a disconnect, or after any network/capture failure, the
small desktop bottom screen remains available. Keyboard and physical-controller
input continue to work alongside the phone.

Choose **Edit controller layout…** in the phone-screen settings to arrange the
phone controls visually. The editor shows the current landscape layout: drag
controls to move them, drag the highlighted corner or use the mouse wheel to
resize them, and double-click empty space to add an emulator-action button.
Use **Ctrl+Z** to undo layout changes and **Ctrl+Shift+Z** to redo them.
Custom buttons can trigger melonDS actions such as fast-forward, pause, frame
step, screen swapping, or audio mute. ABXY moves and scales as one cluster, the
directional control can use a D-pad or analog-stick appearance, and the status,
FPS, and frame text can be hidden. Applying a layout updates a connected phone
immediately and saves it for later sessions.

The bridge sends the native `256 × 192` bottom screen as JPEG at up to 30 FPS.
The webpage and controls share the single displayed TCP port, which must be
allowed by the host firewall. **Firewall setup guide…** opens a native wizard
while the bridge stays running. On Linux, it detects common firewall tools and
lets you choose firewalld, UFW, or general system guidance. For firewalld, run the
shown zone checks in a terminal and enter the applicable zone; WideMelon never
guesses it or invokes an authorization-capable query. Generated rules require a
verified private IPv4 subnet and limit access to the selected address and TCP
port. UFW rules also select the interface. Both include persistent setup,
verification, and removal commands; neither reloads, disables, or resets the
firewall. Revisit the rule if the network, address, zone, or port changes.

WideMelon never runs administrator commands or changes firewall rules itself.
The user reviews and runs the shown commands. Automatic firewalld checks detect
installation only, because even read-only queries can trigger PolicyKit. UFW
checks use unprivileged status or its boot configuration, with an unknown result
when access is unavailable. Unsupported firewall tools receive general guidance;
the wizard includes native system-settings directions for Windows/macOS
builds, not automated firewall configuration on those platforms. If the phone
already connects, no new firewall rule is needed. The bridge does not stream audio.

> **Network warning:** use only on a private home network you trust. Pairing
> prevents other devices from connecting, but the connection is not encrypted.

WideMelon creates a new QR secret and independent 10-digit code every time the
bridge starts. Credentials are held only for that session and also change when
you revoke pairing or generate a new code. Repeated failures are temporarily
rate-limited; they never rotate a valid code.

If no private network address is available, join the same Wi-Fi network on both
devices or create a hotspot with your operating system. WideMelon does not
change system network settings. Browser screen-wake-lock support normally
requires HTTPS, so you may need to adjust the phone's auto-lock setting.

## What to expect

Widescreen compatibility remains experimental. Some games hide objects outside the original
screen area, and some effects may not cover the added space. Battles, videos,
and menus can remain 4:3.

The widescreen mode uses the classic OpenGL renderer. Native 4:3 is the
compatibility profile when a game does not work well with expanded views.

## Build from source

See [BUILD.md](BUILD.md) for Windows, macOS, and Linux build and release instructions.

On Ubuntu 24.04, install the build tools and libraries:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev \
  libarchive-dev libzstd-dev libegl1-mesa-dev \
  libgl1-mesa-dev libwayland-dev qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev libqt6svg6-dev qt6-websockets-dev
```

Then build and run:

```sh
./scripts/build.sh
./widemelon
```

The build script fetches WideMelon's pinned FAAD2 and ENet dependencies,
builds the checked-in emulator source, and runs the automated tests.
It uses Qt 6 by default; set `WIDEMELON_USE_QT6=0` to build the emulator and
tests with Qt 5.15. Install Node.js 18 or newer to run the browser regression
tests as well. CI covers both Qt versions, including the production bridge's
loopback transport and the native pairing dialog with Qt's offscreen platform.

An optional real-browser input smoke test uses installed Chromium and Node.js
22 or newer. It checks simultaneous button holds and continuous touch through
the production bridge while streaming a generated test pattern; no ROM is needed:

```sh
node tests/phone_browser_smoke.js build/tests/phone_bridge_test /usr/bin/chromium
```

Add `--benchmark --dialog` to measure sustained streaming during idle, continuous
touch, and simultaneous button holds, with the settings dialog open. The test
reports per-stage FPS and decode/delivery timing and fails below 28.5 displayed
FPS. Enable it explicitly with `-DWIDEMELON_ENABLE_STREAM_BENCHMARK=ON`; this
keeps ordinary builds independent of local browser DevTools configuration.
For a browser-only A/B comparison, set `WIDEMELON_BENCH_REVISION` to a commit hash;
the test substitutes that revision's browser script while keeping the same bridge.
`WIDEMELON_BENCH_QUALITY=100` and `WIDEMELON_BENCH_CPU=8` select JPEG quality and
Chromium CPU throttling for controlled comparisons. These synthetic/loopback
measurements do not prove Wi-Fi performance or gameplay GPU capture performance.

## Technical overview

A Nintendo DS screen is normally 256 × 192 pixels. WideMelon creates a wider
3D target and adjusts the projection to reveal extra geometry on both sides:

```text
normal:     [       256 pixels       ]
widescreen: [ extra ][ 256 native ][ extra ]
```

The original view keeps its scale and center. Native 2D layers are placed over
the middle 256 pixels, so the interface and touchscreen are not widened.

The viewport width is fixed when the process starts. This keeps CPU geometry,
OpenGL buffers, shaders, and compositing on the same dimensions, which is why
profile changes require a restart.

The phone bridge is frontend-only. It crops the centered physical bottom layer
from the OpenGL output, downsamples it to native resolution, and uses a bounded
asynchronous readback/encoder pipeline. Acknowledgements make old frames drop
instead of accumulating latency. A one-second heartbeat releases every remote
button and touch and restores the desktop fallback after a failed connection.
Before that pipeline is enabled for a phone, the browser must authenticate with
the session-only QR secret or manual code and originate from the selected local
subnet. Unauthenticated clients receive neither layout nor screen frames and
cannot submit controls.

This is an authorization boundary for ordinary devices on a trusted home LAN,
not encrypted hostile-network transport. A device able to sniff or actively
modify local traffic, or compromised network infrastructure, can still observe
or interfere with the session. Do not expose the port to the internet or use it
on public, guest, school, workplace, or otherwise untrusted networks. VPN and
firewall status are advisory diagnostics rather than proof of reachability or
trust.

The phone configurator includes a generated test pattern, live bridge logs,
frame/encode/drop/RTT metrics, optional rotating file logs, synchronous GPU
readback for driver diagnosis, and a sanitized JSON diagnostics export. The
export retains up to 60 recent timing samples: capture/encode/send/ACK rates,
GPU capture time, encoder-to-GUI delivery delay, frame acknowledgement time,
phone decode time, input rate, GUI timer delay, and queued socket bytes. Export
during or immediately after an FPS drop, before restarting the bridge. Frame
acknowledgement time includes transport and phone processing; it is distinct
from the existing heartbeat RTT. Timing collection does not change the frame
cap, acknowledgement policy, or latest-frame replacement.

The following environment overrides change diagnostics only and never start the
network listener:

```sh
WIDEMELON_PHONE_LOG_LEVEL=debug WIDEMELON_PHONE_LOG_FILE=1 ./widemelon
```

## Development

WideMelon is maintained as a GitHub fork of melonDS. The actual modified
emulator source is checked in under `src/`; there is no patch generation step
or nested source checkout.

Build and test changes with:

```sh
./scripts/build.sh
```

Maintainers can add `https://github.com/melonDS-emu/melonDS.git` as an
`upstream` remote and merge selected upstream commits on a dedicated update
branch. Upstream updates are never automatic and must pass the full renderer,
frontend, and packaging verification described in `CONTRIBUTING.md`.

Automated or headless runs can supply a profile through environment variables:

```sh
WIDEMELON_VIEW_WIDTH=448 WIDEMELON_SCALE=4 \
  ./build/widemelon /path/to/game.nds
```

WideMelon's source history is based on melonDS commit
`906e9ebb27da8c6a715cd7abab4abfe8a8d29427`; later upstream merges remain
visible in Git history.

For a tagged binary release, commit the release state and create its matching
complete source archive:

```sh
./scripts/package-source.sh 1.0.0
```

## Licence

WideMelon and its melonDS base are GPL-3.0-or-later. WideMelon is independent
and is not affiliated with or endorsed by the melonDS project, Nintendo, Game
Freak, or The Pokémon Company.

See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for complete licence
and attribution information.
