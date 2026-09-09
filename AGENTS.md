# WideMelon agent guide

WideMelon is a small C++17/CMake project built around a pinned melonDS checkout. The outer repository contains the build recipe, the WideMelon patch, a launcher, and renderer tests. Read `README.md` and `THIRD_PARTY.md` before changing the project. Read `projects/melonDS/BUILD.md` when working on the upstream build.

## Repository shape

- `patches/melonds-widemelon.patch` is the durable record of WideMelon's engine and Qt changes.
- `scripts/build.sh` creates the ignored dependency checkouts, applies the patch, builds melonDS, builds the tests, and runs them.
- `scripts/export-patch.py` exports local changes from `projects/melonDS/src/` back into the patch.
- `scripts/package-source.sh` creates the complete corresponding-source archive required for binary releases.
- `projects/melonDS/` is an ignored, nested git checkout of the pinned upstream commit.
- `tools/` contains ignored local dependency checkouts and installs.
- `tests/profile_test.cpp` tests the viewport profile and projection math.
- `widemelon` is only a small launcher for `build/widemelon`; it is not a second application.

Do not commit generated build directories, dependency checkouts, ROMs, BIOS or firmware files, saves, captures, or game assets. The project must remain redistributable without copyrighted game material.

## Source-of-truth workflow

When changing the engine or native configuration:

1. Run `./scripts/build.sh` if the ignored upstream checkout or dependencies are missing.
2. Make changes in `projects/melonDS/src/`.
3. Build and test the result.
4. Run `python3 scripts/export-patch.py` to regenerate `patches/melonds-widemelon.patch`.
5. Review the outer-repository diff and confirm the patch applies cleanly to the pinned upstream commit.

Do not hand-edit the generated patch. Do not commit changes only inside `projects/melonDS`; they disappear when that ignored checkout is recreated. Keep the melonDS commit pinned unless intentionally updating the upstream base. If the base changes, update the patch, `README.md`, `THIRD_PARTY.md`, and the documented commit together.

Keep the outer repository and the nested upstream checkout separate when checking status or reviewing changes:

```sh
git status --short
git -C projects/melonDS status --short
```

Do not reset, delete, or recreate the nested checkout to resolve a patch conflict until its local changes and the pinned commit have been inspected.

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
cmake -S . -B build/tests -G Ninja
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

- Keep unrelated work untouched in both the outer repository and the nested upstream checkout.
- Review generated patch changes before committing.
- Keep documentation, dependency pins, license notes, and test commands aligned with the implementation.
- Use the repository's existing commit conventions when commits are requested or part of the workflow.
