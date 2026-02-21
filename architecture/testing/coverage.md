# Coverage (Linux only)

- **Minimum 80% line coverage** on `src/simulation/`, `src/terrain/`, `src/ui/`
  - `src/ui/` coverage is achievable only via an `IUIBackend` interface (see `testability-architecture.md`). Direct Irrlicht calls in `UIManager` must be abstracted behind `IUIBackend` before `src/ui/` is added to the coverage gate.
- Coverage enforcement scoped to the Linux GCC/Clang build only. Windows builds run all tests but do not gate on coverage percentage.
- `gcov` / `lcov` on Linux; enable via `-DENABLE_COVERAGE=ON`

```bash
# --base-directory must be the repo root; $(pwd) assumes you run this command from the repo root.
# --ignore-errors mismatch: GCC 13 geninfo emits "mismatched end line" for inline functions
# and lambdas in headers (GTest macros, fmt). Benign; does not affect accuracy.
lcov --capture --directory build --base-directory $(pwd) \
     --ignore-errors mismatch \
     --output-file coverage.info
# --base-directory normalizes all source file paths to be relative to the repo root,
# preventing path mismatches between the source tree and gcov .gcda paths that would
# cause lcov to report 0 coverage for correctly-compiled files.
# NOTE: Do NOT include "${BUILD_DIR}/_deps/*" when FETCHCONTENT_BASE_DIR is set outside
# the build tree (as in CI). FetchContent then writes to .fetchcontent_cache/, not
# build/_deps/, so the pattern is unused. Newer lcov (2.x) treats unused patterns as
# errors (exit code 25). Use "*/.fetchcontent_cache/*" instead.
# --ignore-errors unused: lcov 2.x treats any --remove pattern matching no files as a
# fatal error (exit 25). Many patterns are future-proofing (mock_*.h, src/audio/*, etc.)
# and match nothing until Phase 2+ code exists. Keep all patterns; suppress the error.
lcov --remove coverage.info \
  --ignore-errors unused \
  '/usr/*' \
  "*/.fetchcontent_cache/*" \
  '*/tests/*' \
  '*/mock_*.h' '*/mock_*.cpp' \
  '*/manual_*.h' '*/manual_*.cpp' \
  '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
  --output-file coverage_filtered.info
# List files and line-coverage summary for debugging gate failures.
# MUST appear BEFORE --fail-under-percent: if the gate fails (exits non-zero),
# shell execution stops and --list output is never printed, making gate failures
# impossible to diagnose. Required order: --remove → --list → genhtml → --fail-under-percent
lcov --list coverage_filtered.info
# Generate HTML report BEFORE the gate check — if --fail-under-percent runs first and exits
# non-zero, genhtml is skipped and the coverage HTML artifact is lost.
genhtml coverage_filtered.info --output-directory coverage_html/
# PHASED ROLLOUT: No hard coverage gate at Phase 0. lcov --fail-under-percent does
# not exist in lcov 2.0 (ubuntu-latest ships 2.0; the flag was added in 2.1).
# At Phase 0 the gate would be 0% anyway (smoke tests give trivial coverage).
# Use --summary for informational output. Phase 5 adds a real gate.
# TODO Phase 5: implement 80% gate (bash awk check or upgrade to lcov 2.1+).
lcov --summary coverage_filtered.info
```

**Mock exclusion patterns**: `'*/mock_*.h'` and `'*/mock_*.cpp'` are required exclusions even though mock files live under `tests/` (which is already excluded). Template instantiations of `StrictMock<MockAudioSystem>` and similar types may produce coverage data attributed to the mock header paths (`tests/*/mock_*.h`) rather than the test `.cpp` file — the mock exclusion patterns ensure these do not contribute to the gate even if the `*/tests/*` glob misses them due to path normalization differences.
**Manual test double exclusion patterns**: `'*/manual_*.h'` and `'*/manual_*.cpp'` must be added alongside the mock exclusion patterns. `ManualRNG` (`tests/simulation/manual_rng.h`), `ManualClock` (`tests/simulation/manual_clock.h`), and similar hand-written test doubles follow the `manual_` naming convention and must be excluded from the coverage gate for the same reason as mocks — gcov may attribute their template/inline method coverage to the header file path rather than the calling test `.cpp`. Without this exclusion, manual test doubles can appear as partially-uncovered files and incorrectly lower the gate percentage. Both `mock_*` and `manual_*` patterns must appear in every lcov `--remove` invocation: in `coverage.md` (local developer script), in the `coverage-linux` CI job YAML, and in the `CLAUDE.md` coverage command reference.

- **Do NOT use `${BUILD_DIR}/_deps/*`** when `FETCHCONTENT_BASE_DIR` is set outside the build tree. With `FETCHCONTENT_BASE_DIR=.fetchcontent_cache`, FetchContent writes to `.fetchcontent_cache/` not `build/_deps/`, so that glob matches nothing. Newer lcov (2.x) treats unused `--remove` patterns as fatal errors (exit code 25). Use the FetchContent cache path instead:
  - **CI (in GitHub Actions `run:` block)**: use `"${{ github.workspace }}/.fetchcontent_cache/*"` (absolute path — the `${{ ... }}` expression is resolved by GitHub Actions before the shell executes the command)
  - **Local developer**: use `"*/.fetchcontent_cache/*"` (glob — matches `.fetchcontent_cache` under any ancestor directory in the local workspace)
- **`src/audio/` exclusion rationale and guidance**: The `src/audio/` exclusion from the lcov gate is intentional — `AudioSystem` directly wraps OpenAL and cannot be headlessly tested without OS-boundary mocking. Complex pure-logic audio math (constant-power crossfade curve, bar-boundary calculation, duck state machine transition logic) SHOULD be extracted into standalone header-only utility functions in `src/audio/audio_math.h` or equivalent. These remain in `src/audio/` and are excluded from the gate. Unit tests in `tests/audio/` provide functional verification even without gate enforcement. Implementers should aim for high test coverage of `tests/audio/` test files even though the corresponding source files are excluded from the lcov gate.

## Phase 4 src/ui/ Coverage Baseline

The Phase 4 `src/ui/` coverage baseline is expected to be low (stub-heavy code). The MINIMUM
acceptable Phase 4 baseline is **25%** — achievable with `UIManagerDrawOrderTest`, 6 UIScaler
tests, and 8 CameraController tests.

**Phase 1 prerequisite**: The Phase 4 25% gate assumes all Phase 1 tests (14 tests: 8
CameraController tests in `tests/ui/camera_controller_test.cpp` + 6 UIScaler tests in
`tests/ui/ui_scaler_test.cpp`) are present and passing. The `IrrlichtUIBackendCompileCheck`
compile test is registered under `integration_tests` (label `integration`), not `ui_tests`,
and does not directly contribute to the `src/ui/` line-coverage percentage — but it MUST
also be present and passing before the Phase 4 gate is evaluated, per
`implementation/phase-1.md`. Phase 1 tests must be delivered per `implementation/phase-1.md`
before the Phase 4 gate applies. If Phase 1 tests are missing, the 25% gate will fail even
if Phase 4 code is complete — `UIManagerDrawOrderTest` alone is insufficient to reach 25%
without the CameraController and UIScaler tests supplying coverage of their respective
`src/ui/` translation units.

If Phase 4 baseline is below 25%, this indicates test registration or stub-body errors that
must be corrected before Phase 5 begins. Likely causes include:

- Panel stub `draw()` methods that are no-op bodies (anti-no-op requirement: each test-stub
  panel's `draw()` MUST call at least one `IUIBackend` method — typically `setElementVisible`
  — so that `UIManager::draw()` dispatch produces measurable line coverage; an empty `draw()`
  body `{}` makes the draw-order test vacuously green and produces zero coverage on the
  `UIManager::draw()` dispatch path, dragging `src/ui/` coverage below the 25% floor)
- `ui_tests` CMake target missing source files (e.g., `ui_scaler_test.cpp` or
  `camera_controller_test.cpp` not listed in `add_executable(ui_tests ...)`)
- `gtest_discover_tests()` label misconfiguration causing tests to be excluded from the
  `ctest -LE "integration|requires-opengl"` run that feeds the coverage report

The Phase 5 80% gate assumes a Phase 4 baseline of at least 25%. If Phase 4 delivers only
10% or below, Phase 5 must include additional `ui_tests` source files (e.g.,
`UIManagerStateTransitionTest`) to make the 80% gate achievable.

**Below 25% is a BLOCKING defect, not a MEDIUM risk.** Do not advance to Phase 5 until the
Phase 4 `src/ui/` baseline meets or exceeds 25%.

**Phase 4 `src/ui/` 25% gate — awk pipeline**:

```bash
# Preflight check 1: verify src/ui/ entries exist in lcov --list output.
# If this fails, coverage_filtered.info contains no src/ui/ data — check that
# src/ui/ source files were compiled with -DENABLE_COVERAGE=ON and that the
# lcov --remove step did not inadvertently exclude src/ui/.
if ! lcov --list coverage_filtered.info | grep -q "src/ui/"; then
  echo "PREFLIGHT FAIL: No src/ui/ entries in lcov --list output."
  echo "Check coverage_filtered.info generation and --remove patterns."
  exit 1
fi
# Preflight check 2: verify lcov --list uses '|' as column delimiter.
# If this fails, the lcov version may have changed its output format.
# Validate the output format manually: run 'lcov --list coverage_filtered.info'
# and inspect whether column separator is '|'. Update awk -F'|' if format changed.
if ! lcov --list coverage_filtered.info | grep -E "src/ui/" | grep -qF "|"; then
  echo "PREFLIGHT FAIL: lcov --list output missing '|' column delimiter."
  echo "lcov version may have changed output format — validate manually."
  exit 1
fi
#
# Format dependency: Assumes lcov 2.x --list output uses '|' as column delimiter.
# If the format changes, $NF+0 coercion produces 0 -> gate FAILS with misleading
# '0% coverage' message rather than 'lcov format mismatch'. Validate the lcov
# --list output format manually if the pipeline produces unexpected results.
# Preflight checks above catch the empty-data and format-mismatch cases explicitly.
#
# head -1 semantics: head -1 takes the minimum (worst-case) src/ui/ file coverage
# — this is intentional; the gate enforces that even the least-covered src/ui/
# file meets the threshold.
lcov --list coverage_filtered.info \
  | grep -E "src/ui/" \
  | grep -v "^Total" \
  | awk -F'|' '{print $NF+0}' \
  | sort -n \
  | head -1 \
  | awk '{if ($1 < 25.0) { print "FAIL: src/ui/ worst-file coverage " $1 "% < 25% Phase 4 gate"; exit 1 } else { print "PASS: src/ui/ worst-file coverage " $1 "% >= 25%"; exit 0 }}'
```
