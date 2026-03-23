# Headless CI Testing

- Unit tests: no display or audio device required (pure C++ logic via interfaces); **must not** be wrapped in `xvfb-run`
- Integration tests: use **`EDT_NULL`** Irrlicht device and OpenAL Soft null backend; label these tests with `LABELS "integration"` — they do **not** require a display and must run directly under `ctest` without `xvfb-run`. Tests that require a real OpenGL context (e.g., shader compilation verification using `EDT_OPENGL`) must use `LABELS "requires-opengl"` and run under `xvfb-run`.
- Linux CI runner requires virtual display only for requires-opengl tests: install `xvfb` + `libgl1-mesa-dev` + `libxxf86vm-dev`
  - `libxxf86vm-dev` provides the `xf86vmode` library required by Irrlicht's X11 display mode enumeration. Without it, Irrlicht fails to build with `cannot find -lXxf86vm` even when only `EDT_NULL` is used at runtime — the portfile links `Xxf86vm` unconditionally.
  - Run unit tests directly: `ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure` (`-LE` = label exclude)
  - Run integration tests directly (no display): `ctest --test-dir build -L "^integration$" --output-on-failure`
  - Run OpenGL tests under xvfb: `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure`
  - Wrapping all of `ctest` in `xvfb-run` is wasteful and can mask headless test failures

See `framework.md` for the `gtest_discover_tests()` / `aitown_add_tests()` label configuration (`LABELS "unit"`, `"integration"`, `"requires-opengl"`) that assigns the labels used by these `ctest` filter commands at CMake configure time.

## Containerised CI (Phase 11b)

When `build-linux` and `coverage-linux` switch to `container: image: ghcr.io/...` mode
(Phase 11b), xvfb is pre-installed in the CI base image — no `apt-get install xvfb` step is
needed inside those jobs.

Test label routing is identical inside container jobs; the commands are unchanged:

- `ctest -LE "integration|requires-opengl"` — unit tests, no display
- `ctest -L "^integration$"` — integration tests, no display
- `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — OpenGL tests, virtual display

The Phase 11b spike PR introduces a temporary `test-container-xvfb` job (Model A) that runs
`xvfb-run --auto-servernum ctest -L "^requires-opengl$"` inside the target container image.
This job must pass before the main `build-linux` and `coverage-linux` jobs are switched to
container mode.
