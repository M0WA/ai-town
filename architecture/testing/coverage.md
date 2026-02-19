# Coverage (Linux only)

- **Minimum 80% line coverage** on `src/simulation/`, `src/terrain/`, `src/ui/`
  - `src/ui/` coverage is achievable only via an `IUIBackend` interface (see `testability-architecture.md`). Direct Irrlicht calls in `UIManager` must be abstracted behind `IUIBackend` before `src/ui/` is added to the coverage gate.
- Coverage enforcement scoped to the Linux GCC/Clang build only. Windows builds run all tests but do not gate on coverage percentage.
- `gcov` / `lcov` on Linux; enable via `-DENABLE_COVERAGE=ON`
```bash
# --base-directory must be the repo root; $(pwd) assumes you run this command from the repo root.
lcov --capture --directory build --base-directory $(pwd) --output-file coverage.info
# --base-directory normalizes all source file paths to be relative to the repo root,
# preventing path mismatches between the source tree and gcov .gcda paths that would
# cause lcov to report 0 coverage for correctly-compiled files.
lcov --remove coverage.info \
  '/usr/*' \
  "${BUILD_DIR}/_deps/*" \
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
# PHASED ROLLOUT: The --fail-under-percent 80 gate is NOT applied from Phase 0.
# At Phase 0, use --fail-under-percent 0 (the smoke test gives trivial coverage;
# the 80% gate is not meaningful until Phase 2+ logic is covered).
# Change the gate to 80 as part of Phase 2 delivery.
lcov --fail-under-percent 0 --summary coverage_filtered.info
```
**Mock exclusion patterns**: `'*/mock_*.h'` and `'*/mock_*.cpp'` are required exclusions even though mock files live under `tests/` (which is already excluded). Template instantiations of `StrictMock<MockAudioSystem>` and similar types may produce coverage data attributed to the mock header paths (`tests/*/mock_*.h`) rather than the test `.cpp` file — the mock exclusion patterns ensure these do not contribute to the gate even if the `*/tests/*` glob misses them due to path normalization differences.
**Manual test double exclusion patterns**: `'*/manual_*.h'` and `'*/manual_*.cpp'` must be added alongside the mock exclusion patterns. `ManualRNG` (`tests/simulation/manual_rng.h`), `ManualClock` (`tests/simulation/manual_clock.h`), and similar hand-written test doubles follow the `manual_` naming convention and must be excluded from the coverage gate for the same reason as mocks — gcov may attribute their template/inline method coverage to the header file path rather than the calling test `.cpp`. Without this exclusion, manual test doubles can appear as partially-uncovered files and incorrectly lower the gate percentage. Both `mock_*` and `manual_*` patterns must appear in every lcov `--remove` invocation: in `coverage.md` (local developer script), in the `coverage-linux` CI job YAML, and in the `CLAUDE.md` coverage command reference.
- **Use `${BUILD_DIR}/_deps/*`** (not library-name globs like `*/googletest/*` or `*/irrlicht/*`) — this glob covers FetchContent sources only when `FETCHCONTENT_BASE_DIR` is NOT overridden (i.e., sources land inside the build tree). In CI where `FETCHCONTENT_BASE_DIR` is set to `.fetchcontent_cache`, the `${BUILD_DIR}/_deps/*` glob will NOT match those sources; the additional exclusion below is required. Both exclusions together prevent googletest and RapidCheck sources from counting against the coverage gate regardless of environment:
  - **CI (in GitHub Actions `run:` block)**: use `"${{ github.workspace }}/.fetchcontent_cache/*"` (absolute path — the `${{ ... }}` expression is resolved by GitHub Actions before the shell executes the command)
  - **Local developer**: use `"*/.fetchcontent_cache/*"` (glob — matches `.fetchcontent_cache` under any ancestor directory in the local workspace)
- **`src/audio/` exclusion rationale and guidance**: The `src/audio/` exclusion from the lcov gate is intentional — `AudioSystem` directly wraps OpenAL and cannot be headlessly tested without OS-boundary mocking. Complex pure-logic audio math (constant-power crossfade curve, bar-boundary calculation, duck state machine transition logic) SHOULD be extracted into standalone header-only utility functions in `src/audio/audio_math.h` or equivalent. These remain in `src/audio/` and are excluded from the gate. Unit tests in `tests/audio/` provide functional verification even without gate enforcement. Implementers should aim for high test coverage of `tests/audio/` test files even though the corresponding source files are excluded from the lcov gate.
