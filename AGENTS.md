# WideMelon agent guide

WideMelon is a C++17/CMake derivative of melonDS. The repository contains the complete modified emulator source and retains melonDS Git ancestry. Read `README.md`, `THIRD_PARTY.md`, and `BUILD.md` before changing the project.

## Repository shape

- `src/` is the durable source of truth for both inherited melonDS code and WideMelon changes.
- `scripts/build.sh` creates pinned dependency checkouts under `.deps/`, builds WideMelon, builds the tests, and runs them.
- `scripts/package-source.sh` creates the complete corresponding-source archive required for binary releases.
- `.deps/` contains ignored local dependency checkouts, builds, and installs.
- `tests/profile_test.cpp` tests the viewport profile and projection math.
- `widemelon` is only a small launcher for `build/widemelon`; it is not a second application.

Do not commit generated build directories, dependency checkouts, ROMs, BIOS or firmware files, saves, captures, or game assets. The project must remain redistributable without copyrighted game material.

## Source and upstream workflow

When changing the engine or native configuration:

1. Make changes directly in `src/` and the related frontend, build, test, or documentation files.
2. Run the focused tests while iterating.
3. Run `./scripts/build.sh` before committing engine, shader, Qt, dependency, or packaging changes.
4. Review the ordinary Git diff and keep WideMelon-specific changes in focused commits.

The canonical upstream remote is `https://github.com/melonDS-emu/melonDS.git`. Upstream updates must be explicit merges on a dedicated branch:

```sh
git remote add upstream https://github.com/melonDS-emu/melonDS.git
git fetch upstream
git switch -c update-melonds
git merge upstream/master
```

Resolve conflicts in the actual source files, then run the complete build and manual renderer/frontend checks. Do not treat a clean merge as proof that renderer behavior remains correct.

## Renderer invariants

Preserve these behaviors unless the task explicitly changes them:

- Native DS content is `256 × 192`.
- A wider profile is an even width from `256` through `768` pixels.
- The selected width is fixed for the lifetime of the process so geometry and OpenGL allocations agree.
- Projection keeps the original world scale and center; extra columns reveal additional 3D geometry rather than stretching the native view.
- The 2D layers, sprites, menus, videos, and touchscreen remain centered at their native proportions.
- The expanded path requires the classic OpenGL renderer. Native 4:3 remains the compatibility profile.
- Environment profiles and the native Qt startup dialog must produce the same validated configuration.

When changing projection, framebuffer sizes, shaders, compositing, or screen layout, check both the math and the rendered result. Do not fix a 3D-width issue by stretching or repositioning the native 2D interface.

## Configuration and Qt behavior

`WIDEMELON_*` environment variables exist for automated and headless runs. Validate them at the boundary, keep safe fallbacks, and preserve the documented ranges:

- `WIDEMELON_VIEW_WIDTH`: even values from `256` to `768`.
- `WIDEMELON_SCALE`: render scale from `1` to `8`.
- `WIDEMELON_WINDOW_WIDTH`: `640` to `7680`.
- `WIDEMELON_WINDOW_HEIGHT`: `480` to `4320`.
- `WIDEMELON_INTEGER`: boolean-like integer setting.

The startup UI should use the existing native Qt controls and melonDS configuration paths. Keep ROM selection readable and explicit, preserve drag-and-drop and command-line behavior, and do not add a separate launcher, web interface, or unrelated UI system.

## Implementation rules

- Keep changes small and local to the affected renderer, shader, Qt, build, or test boundary.
- Search the upstream code and existing helpers before adding new abstractions.
- Preserve upstream behavior outside the WideMelon feature.
- Use existing melonDS and Qt facilities instead of duplicating configuration, rendering, input, or file-validation logic.
- Keep C++ ownership, lifetime, integer conversions, and error paths explicit; renderer dimensions must not disagree across CPU math, framebuffer allocation, shaders, and compositing.
- Treat ROM paths, environment variables, command-line arguments, and file contents as untrusted input.
- Keep GPL notices and third-party attribution intact when copying or modifying upstream code.

## Verification

For changes to tests or profile math, the focused loop is:

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

For engine, shader, Qt, dependency, or build-script changes, run the complete repository workflow:

```sh
./scripts/build.sh
```

If a built executable and a readable ROM are available, perform a manual smoke test with the native dialog and, where useful, a headless profile such as:

```sh
WIDEMELON_VIEW_WIDTH=448 WIDEMELON_SCALE=4 ./build/widemelon /path/to/game.nds
```

The automated tests do not contain a ROM and cannot prove game compatibility, shader output, or native Qt behavior. Report those limits clearly. Do not silently skip a failed build or claim visual correctness from unit tests alone.

## Git and changes

- Keep unrelated work untouched.
- Preserve upstream copyright and license notices. Mark new WideMelon-owned files GPL-3.0-or-later.
- Keep documentation, dependency pins, license notes, and test commands aligned with the implementation.
- Use the repository's existing commit conventions when commits are requested or part of the workflow.
