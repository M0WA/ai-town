# Headless CI Testing

- Unit tests: no display or audio device required (pure C++ logic via interfaces); **must not** be wrapped in `xvfb-run`
- Integration tests: use **`EDT_NULL`** Irrlicht device and OpenAL Soft null backend; label these tests with `LABELS "integration"` — they do **not** require a display and must run directly under `ctest` without `xvfb-run`. Tests that require a real OpenGL context (e.g., shader compilation verification using `EDT_OPENGL`) must use `LABELS "requires-opengl"` and run under `xvfb-run`.
- Linux CI runner requires virtual display only for requires-opengl tests: install `xvfb` + `libgl1-mesa-dev`
  - Run unit tests directly: `ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure` (`-LE` = label exclude)
  - Run integration tests directly (no display): `ctest --test-dir build -L "^integration$" --output-on-failure`
  - Run OpenGL tests under xvfb: `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure`
  - Wrapping all of `ctest` in `xvfb-run` is wasteful and can mask headless test failures

See `framework.md` for the `gtest_discover_tests()` / `aitown_add_tests()` label configuration (`LABELS "unit"`, `"integration"`, `"requires-opengl"`) that assigns the labels used by these `ctest` filter commands at CMake configure time.
