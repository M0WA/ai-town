# Coverage (Linux only)

- **Coverage target range: 95–98% total line coverage** on `src/simulation/`, `src/terrain/`, `src/ui/`.
  Minimum hard gate: **95%** (Phase 6+). Aspirational target: **98%**.
  `make test` enforces the 95% gate locally; CI `coverage-linux` job enforces the same threshold.
  - `src/ui/` coverage is achievable only via an `IUIBackend` interface (see `testability-architecture.md`). Direct Irrlicht calls in `UIManager` must be abstracted behind `IUIBackend` before `src/ui/` is added to the coverage gate.
- Coverage enforcement scoped to the Linux GCC/Clang build only. Windows builds run all tests but do not gate on coverage percentage.
- `gcov` / `lcov` on Linux; enable via `-DENABLE_COVERAGE=ON`

```bash
# --base-directory must be the repo root; $(pwd) assumes you run this command from the repo root.
# --ignore-errors mismatch,inconsistent,version:
#   mismatch     — GCC 13 geninfo emits "mismatched end line" for inline functions/lambdas
#                  in headers (GTest macros, fmt). Benign; does not affect accuracy.
#   inconsistent — suppresses inconsistent line-count warnings in GCC 13 + lcov 2.x.
#   version      — suppresses GCC/gcov version-string mismatch when build and capture
#                  gcov versions differ (e.g. built with B33*, capturing with B42*).
lcov --capture --directory build --base-directory $(pwd) \
     --ignore-errors mismatch,inconsistent,version \
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
  '*/Mock*.h' '*/Mock*.cpp' \
  '*/Manual*.h' '*/Manual*.cpp' \
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
# Use --summary for informational output. Phase 5 replaces this with the awk gate below.
lcov --summary coverage_filtered.info
```

## Phase 5 — Total Line Coverage 80% Gate

The Phase 5 gate measures **total line coverage** across all files remaining in
`coverage_filtered.info` after the `lcov --remove` exclusions (i.e., `src/simulation/`,
`src/terrain/`, `src/ui/` — excludes `src/rendering/`, `src/audio/`, `src/platform/`,
and all test files).

**Why total coverage (not worst-file)**: `src/ui/` contains many panel stub headers
with no testable lines in Phase 5. Worst-file across all three directories is
unachievable at 80% at this stage. Total line coverage measures the aggregate health
of the simulation and terrain code that IS tested in Phase 5.

**CI step in `coverage-linux`** (replaces the Phase 0 `lcov --summary` informational step):

```bash
# Parse coverage_filtered.info directly — version-agnostic (works with lcov 1.x and 2.x).
# LH = lines hit total, LF = lines found total across all SF entries.
total=$(awk '
  /^LH:/ { lh+=substr($0,4)+0 }
  /^LF:/ { lf+=substr($0,4)+0 }
  END { if (lf>0) printf "%.2f", (lh/lf)*100; else print 0 }
' coverage_filtered.info)
if [ -z "$total" ]; then
  echo "PREFLIGHT FAIL: No coverage data found in coverage_filtered.info."; exit 1
fi
awk -v pct="$total" 'BEGIN {
  if (pct+0 < 80.0) {
    print "FAIL: total line coverage " pct "% < 80% Phase 5 gate"; exit 1
  } else {
    print "PASS: total line coverage " pct "% >= 80%"
  }
}'
```

This step replaces the current `lcov --summary coverage_filtered.info` informational step.
Do NOT use `lcov --fail-under-percent` — it does not exist in lcov 2.0 (ubuntu-latest).

**Mock exclusion patterns**: `'*/mock_*.h'` and `'*/mock_*.cpp'` are required exclusions even though mock files live under `tests/` (which is already excluded). Template instantiations of `StrictMock<MockAudioSystem>` and similar types may produce coverage data attributed to the mock header paths (`tests/*/mock_*.h`) rather than the test `.cpp` file — the mock exclusion patterns ensure these do not contribute to the gate even if the `*/tests/*` glob misses them due to path normalization differences. **Phase 10b renames all test-helper class headers to CamelCase** (`MockAudioSystem.h`, `MockRenderer.h`, `ManualClock.h`, etc.) — the lowercase `mock_*` and `manual_*` globs will not match CamelCase filenames on Linux (case-sensitive). CamelCase variants `'*/Mock*.h'`, `'*/Mock*.cpp'`, `'*/Manual*.h'`, `'*/Manual*.cpp'` MUST be added alongside the lowercase patterns in Phase 10b Feature 3.
**Manual test double exclusion patterns**: `'*/manual_*.h'` and `'*/manual_*.cpp'` must be added alongside the mock exclusion patterns (and their CamelCase counterparts after Phase 10b). `ManualRNG`, `ManualClock`, and similar hand-written test doubles must be excluded from the coverage gate for the same reason as mocks — gcov may attribute their template/inline method coverage to the header file path rather than the calling test `.cpp`. Without this exclusion, manual test doubles can appear as partially-uncovered files and incorrectly lower the gate percentage. All four prefix patterns (`mock_*`, `manual_*`, `Mock*`, `Manual*`) must appear in every lcov `--remove` invocation: in `coverage.md` (local developer script), in the `coverage-linux` CI job YAML, and in the `CLAUDE.md` coverage command reference.

- **Do NOT use `${BUILD_DIR}/_deps/*`** when `FETCHCONTENT_BASE_DIR` is set outside the build tree. With `FETCHCONTENT_BASE_DIR=.fetchcontent_cache`, FetchContent writes to `.fetchcontent_cache/` not `build/_deps/`, so that glob matches nothing. Newer lcov (2.x) treats unused `--remove` patterns as fatal errors (exit code 25). Use the FetchContent cache path instead:
  - **CI (in GitHub Actions `run:` block)**: use `"${{ github.workspace }}/.fetchcontent_cache/*"` (absolute path — the `${{ ... }}` expression is resolved by GitHub Actions before the shell executes the command)
  - **Local developer**: use `"*/.fetchcontent_cache/*"` (glob — matches `.fetchcontent_cache` under any ancestor directory in the local workspace)
- **`src/audio/` exclusion rationale and guidance**: The `src/audio/` exclusion from the lcov gate is intentional — `AudioSystem` directly wraps OpenAL and cannot be headlessly tested without OS-boundary mocking. Complex pure-logic audio math (constant-power crossfade curve, bar-boundary calculation, duck state machine transition logic) SHOULD be extracted into standalone header-only utility functions in `src/audio/audio_math.h` or equivalent. These remain in `src/audio/` and are excluded from the gate. Unit tests in `tests/audio/` provide functional verification even without gate enforcement. Implementers should aim for high test coverage of `tests/audio/` test files even though the corresponding source files are excluded from the lcov gate.

## Phase 4 src/ui/ Coverage Baseline

The Phase 4 `src/ui/` coverage baseline is expected to be low (stub-heavy code). The MINIMUM
acceptable Phase 4 baseline is **25%** — achievable with `UIManagerDrawOrderTest`, 6 UIScaler
tests, and 8 CameraController tests.

**IMPLEMENTER CONSTRAINT (Phases 1-3)**: All panel stub `draw()` bodies added in
Phases 1-3 MUST call at least one `IUIBackend` method (e.g., `setElementVisible`) —
empty `draw()` bodies `{}` will cause the Phase 4 25% `src/ui/` coverage gate to fail.
An empty `draw()` body makes the draw-order test vacuously green but produces zero
line coverage on the `UIManager::draw()` dispatch path. This constraint applies to
every panel stub registered with `UIManager` during Phases 1-3, regardless of whether
full panel implementation is deferred to a later phase.

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

## Phase 6 — Total Line Coverage 95% Gate

Phase 6 raises the **total line coverage** target from 80% (Phase 5) to **95%** across all
files remaining in `coverage_filtered.info` after the `lcov --remove` exclusions (i.e.,
`src/simulation/`, `src/terrain/`, `src/ui/`).

**Rationale**: Phase 6 delivers the complete V1 simulation engine (economy, traffic, zoning,
population, service coverage). All critical simulation paths are expected to be covered by
the Phase 6 unit and property-based test suite. The 95% gate enforces this completeness
and blocks Phase 7+ from inheriting under-tested simulation logic.

**Phase 6 CI deliverable**: Update `.github/workflows/ci.yml` `coverage-linux` job —
change the existing `80.0` threshold in the `Enforce 80% total line coverage gate` step
to `95.0`, and rename the step to `Enforce 95% total line coverage gate`. This MUST be
applied during Phase 6 implementation (not before — changing the threshold before Phase 6
tests are written would break CI).

**CI step** (replaces Phase 5 `awk` gate in `coverage-linux` job at Phase 6 completion):

```bash
awk -v pct="$total" 'BEGIN {
  if (pct+0 < 95.0) {
    print "FAIL: total line coverage " pct "% < 95% Phase 6 gate"; exit 1
  } else {
    print "PASS: total line coverage " pct "% >= 95%"
  }
}'
```

**What achieves 95%**: The Phase 6 simulation test suite (economy, traffic, zoning, population,
service coverage) combined with the Phase 5 terrain and Phase 4 UI coverage. Files excluded from
the gate (`src/rendering/`, `src/audio/`, `src/platform/`) do not count toward this percentage.
Any simulation file below 85% individual coverage is a candidate for additional targeted tests
before merging Phase 6.

**`src/simulation/` preflight (Phase 6 CI deliverable)**: Add a preflight step before the 95%
gate that verifies `src/simulation/` SF entries are present in `coverage_filtered.info`. Without
this check, a broken `simulation_tests` registration (binary crash, discovery timeout, CMakeLists
omission) would cause `lcov` to produce zero simulation coverage data — the gate computes 95%
across only `src/terrain/` and `src/ui/`, silently passing while all simulation code is uncovered:

```bash
if ! grep -q "SF:.*src/simulation/" coverage_filtered.info; then
  echo "PREFLIGHT FAIL: No src/simulation/ SF entries in coverage_filtered.info."
  echo "Check simulation_tests CMake registration and DISCOVERY_TIMEOUT setting."
  exit 1
fi
```

**Phase 11 `src/simulation/` per-file 85% floor** (Deferred from Phase 6; implemented in Phase 11) (CI enforcement step, runs after the src/simulation/ SF preflight and before the 95% total gate):

Add this step to the `coverage-linux` job immediately after the `src/simulation/` SF preflight step:

```bash
# Per-file 85% floor for src/simulation/ — any file below 85% is a blocking defect.
# Direct .info file parsing (SF/LH/LF records) — version-agnostic; does NOT use
# lcov --list output (whose column delimiter changed in lcov 2.0 and is unreliable).
awk '
  /^SF:/ { in_sim=0; fname="" }
  /^SF:.*src\/simulation\// { in_sim=1; fname=$0; sub(/^SF:/, "", fname); lh=0; lf=0 }
  in_sim && /^LH:/ { lh=$0; sub(/^LH:/, "", lh) }
  in_sim && /^LF:/ { lf=$0; sub(/^LF:/, "", lf) }
  in_sim && /^end_of_record/ {
    if (lf+0 > 0) {
      pct = lh/lf*100
      if (min_pct == "" || pct < min_pct+0) { min_pct=pct; min_file=fname }
    }
    in_sim=0
  }
  END {
    if (min_pct == "") { print "PREFLIGHT FAIL: No src/simulation/ files found in coverage_filtered.info"; exit 1 }
    if (min_pct+0 < 85.0) {
      printf "FAIL: worst src/simulation/ file %s coverage %.1f%% < 85%% Phase 11 per-file floor\n", min_file, min_pct; exit 1
    } else {
      printf "PASS: worst src/simulation/ file %s coverage %.1f%% >= 85%%\n", min_file, min_pct
    }
  }
' coverage_filtered.info
```

This step fails CI if any single `src/simulation/` file falls below 85% — catching under-tested simulation paths that the 95% total gate may not surface (a high-coverage majority can mask a low-coverage outlier).

**95% gate denominator**: The gate measures **total** line coverage across all files in
`coverage_filtered.info` — this includes `src/simulation/`, `src/terrain/`, AND `src/ui/`. The
`src/ui/` Phase 4 baseline (~25%) and Phase 5 terrain coverage must both be maintained. If the
denominator including `src/ui/` makes 95% unachievable (Phase 8 panel stubs are still stub-heavy),
Phase 6 implementers must add additional `ui_tests` source coverage to raise `src/ui/` to the
level needed. The gate is deliberately challenging — any simulation directory below 85% is a
blocker that must be fixed before merging Phase 6.

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

## Coverage Test Placement Convention

**Tests belong in semantically correct files, never in gap files.**

All coverage-gap test files (`coverage_gap_test.cpp`, `simulation_coverage_gap_test.cpp`,
`query_panel_coverage_test.cpp`) have been dissolved. Every test that was in those files has
been moved into the appropriate semantically correct test file.

### Mapping: simulation tests

| Topic | Target file |
|---|---|
| Speed/smoothstep/traffic overload | `tests/simulation/traffic_test.cpp` |
| `maxPopulationForTile` Commercial/Industrial | `tests/simulation/population_test.cpp` |
| `TimeOfDay` tick path | `tests/simulation/population_test.cpp` |
| `placeServiceBuilding`, service alerts, water/power loss, `queryTile` | `tests/simulation/service_coverage_test.cpp` |
| `undoLastAction`, undo expiry, difficulty refund clamping | `tests/simulation/undo_system_test.cpp` |
| Economy paths, forced loan, debt, bond, earthworks SFX, road/signal, `queryTile` road | `tests/simulation/economy_test.cpp` |

### Mapping: UI tests

| Topic | Target file |
|---|---|
| Hotkeys, toolbar, hover, overlay, query tool, `getActiveTool`, drag, sub-panel | `tests/ui/world_interaction_test.cpp` |
| Demolish with confirm modal | `tests/ui/world_interaction_test.cpp` |
| Escape from Paused / MainMenu | `tests/ui/world_interaction_test.cpp` |
| `consumeStartGameRequest` via `update()` | `tests/ui/world_interaction_test.cpp` (`ValidHandleWorldInteractionTest`) |
| `ForcedLoanIssued` notification dialog | `tests/ui/notification_system_test.cpp` |
| Inspector `populate()`, `draw()`, `getBounds()`, road tile | `tests/ui/query_panel_test.cpp` |

### Rule

When adding new coverage tests, always place them in the file that matches the
subject's domain. Do not create new `*_coverage_gap_test.cpp` files. If no
appropriate file exists, create a properly named file (e.g., `save_system_test.cpp`
for save/load coverage). The `coverage_gap` naming convention is permanently retired.

**Exception — stub/real split**: if an existing `*_test.cpp` uses local stub classes
whose names conflict with the real class headers (e.g. `save_system_test.cpp` defines
`ISaveSystem` and `ICitySimulationSerializable` as local stubs), create a companion
`*_real_test.cpp` that includes and exercises the real implementation. Both files belong
to the same CMake test target. This pattern applies to `save_system_real_test.cpp`.
