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
# and match nothing until Phase 1+ code exists. Keep all patterns; suppress the error.
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
# Use --summary for informational output. Phase 2 adds a real gate.
# TODO Phase 2: implement 80% gate (bash awk check or upgrade to lcov 2.1+).
lcov --summary coverage_filtered.info
```
**Mock exclusion patterns**: `'*/mock_*.h'` and `'*/mock_*.cpp'` are required exclusions even though mock files live under `tests/` (which is already excluded). Template instantiations of `StrictMock<MockAudioSystem>` and similar types may produce coverage data attributed to the mock header paths (`tests/*/mock_*.h`) rather than the test `.cpp` file — the mock exclusion patterns ensure these do not contribute to the gate even if the `*/tests/*` glob misses them due to path normalization differences.
**Manual test double exclusion patterns**: `'*/manual_*.h'` and `'*/manual_*.cpp'` must be added alongside the mock exclusion patterns. `ManualRNG` (`tests/simulation/manual_rng.h`), `ManualClock` (`tests/simulation/manual_clock.h`), and similar hand-written test doubles follow the `manual_` naming convention and must be excluded from the coverage gate for the same reason as mocks — gcov may attribute their template/inline method coverage to the header file path rather than the calling test `.cpp`. Without this exclusion, manual test doubles can appear as partially-uncovered files and incorrectly lower the gate percentage. Both `mock_*` and `manual_*` patterns must appear in every lcov `--remove` invocation: in `coverage.md` (local developer script), in the `coverage-linux` CI job YAML, and in the `CLAUDE.md` coverage command reference.
- **Do NOT use `${BUILD_DIR}/_deps/*`** when `FETCHCONTENT_BASE_DIR` is set outside the build tree. With `FETCHCONTENT_BASE_DIR=.fetchcontent_cache`, FetchContent writes to `.fetchcontent_cache/` not `build/_deps/`, so that glob matches nothing. Newer lcov (2.x) treats unused `--remove` patterns as fatal errors (exit code 25). Use the FetchContent cache path instead:
  - **CI (in GitHub Actions `run:` block)**: use `"${{ github.workspace }}/.fetchcontent_cache/*"` (absolute path — the `${{ ... }}` expression is resolved by GitHub Actions before the shell executes the command)
  - **Local developer**: use `"*/.fetchcontent_cache/*"` (glob — matches `.fetchcontent_cache` under any ancestor directory in the local workspace)
- **`src/audio/` exclusion rationale and guidance**: The `src/audio/` exclusion from the lcov gate is intentional — `AudioSystem` directly wraps OpenAL and cannot be headlessly tested without OS-boundary mocking. Complex pure-logic audio math (constant-power crossfade curve, bar-boundary calculation, duck state machine transition logic) SHOULD be extracted into standalone header-only utility functions in `src/audio/audio_math.h` or equivalent. These remain in `src/audio/` and are excluded from the gate. Unit tests in `tests/audio/` provide functional verification even without gate enforcement. Implementers should aim for high test coverage of `tests/audio/` test files even though the corresponding source files are excluded from the lcov gate.
