# GitHub Actions Workflow

Create `.github/workflows/ci.yml`:

- **Triggers**: push to `main` / `develop`; pull requests targeting `main` / `develop`; `workflow_dispatch` (manual trigger — required for re-running stale CI without a code push, e.g. after a transient runner failure or after updating vcpkg baseline). Without `workflow_dispatch`, a broken `main` with a non-code root cause (expired cache, runner issue) cannot be re-triggered without a dummy commit.
- **Permissions block** (required for `dorny/test-reporter` to write PR annotations):

  ```yaml
  permissions:
    checks: write       # required by dorny/test-reporter to post check results
    contents: read      # required to checkout code
  ```

  Without `checks: write`, `dorny/test-reporter` receives a 403 and silently fails to publish test results. This block must appear at the workflow level (applies to all jobs) or per-job level.
- **Job timeout requirements**: All jobs must include `timeout-minutes` to prevent runaway builds from consuming runner minutes indefinitely. Recommended values: `build-linux: 30`, `build-windows: 40`, `coverage-linux: 60` (longer due to full build + instrumented tests + lcov; Phase 7 AudioSystem instrumented build + streaming tests + three-tier ctest execution exceeds the Phase 0 estimate; 60 min confirmed as sufficient headroom), `all-checks-pass: 5`. Without these limits, a hung MSVC linker or stuck xvfb process can block the runner for the GitHub Actions default 6-hour maximum, wasting all allocated minutes on the repo for that billing period.

  ```yaml
  build-linux:
    runs-on: ubuntu-latest
    timeout-minutes: 30
  ```

  Set `timeout-minutes` on the job level (not individual steps), unless a specific step (e.g., xvfb OpenGL tests) needs its own step-level timeout via `timeout-minutes` on the step.

## Supply-Chain SHA Lint Step

This step runs as the **first named step** in the `build-linux` job — before vcpkg install, before ccache setup, and before any CMake configure step. It is a regular step (no `if: always()`); if placeholder patterns are found the step exits non-zero and the entire job fails immediately, preventing a broken workflow from touching the supply chain.

**Step ordering clarification**: `actions/checkout` must run as step 1 (it makes `.github/workflows/ci.yml` available on disk). The supply-chain lint step runs as step 2, immediately after checkout and before all other steps (compiler detect, ccache, vcpkg, CMake configure). "First named step" means first among all steps after checkout — it does not mean before checkout, which would make the workflow file unavailable to grep.

**Step definition** (place at the top of `build-linux`'s `steps:` list):

```yaml
- name: Lint workflow for placeholder SHAs
  shell: bash
  run: |
    # Fail if any angle-bracket placeholders remain (e.g. @<SHA>, uses: owner/action@<VERSION_SHA>)
    if grep -P '<[A-Z_][A-Z0-9_-]*>' .github/workflows/ci.yml; then
      echo "ERROR: Unresolved angle-bracket placeholder found in ci.yml. Replace all <PLACEHOLDER> tokens with real values before merging."
      exit 1
    fi
    # Fail if any short SHA is used (fewer than 40 hex chars after '@')
    # Full 40-character SHAs are required for all pinned actions and dependencies.
    if grep -P '@[0-9a-f]{1,39}\b' .github/workflows/ci.yml; then
      echo "ERROR: Short SHA detected in ci.yml. All pinned actions must use the full 40-character commit SHA."
      exit 1
    fi
    echo "SHA lint passed — no placeholder tokens or short SHAs found."
```

**Pattern rationale**:

- `<[A-Z_][A-Z0-9_-]*>` matches angle-bracket placeholder tokens left by template authors (e.g. `actions/checkout@<CHECKOUT_SHA>`). These indicate the implementer has not yet looked up and substituted the real SHA.
- `@[0-9a-f]{1,39}\b` matches any SHA-like string after `@` that is shorter than 40 hex characters. Supply-chain attacks rely on shortened SHAs being accepted as valid references; requiring the full 40-character SHA prevents a malicious tag from silently resolving to a different commit.

**Cross-reference**: The `validate-assets` job definition (later in this file) uses `actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11` (v4.1.1, verified). The `actions/setup-python` SHA is a placeholder — the implementer must re-resolve at time of implementation — never copy a cached SHA from documentation. This lint step will catch any unresolved `@<...>` placeholder tokens at CI time.

**Scope of this lint step**: `build-linux` is the **minimum required location** for this lint step — it MUST appear in `build-linux`. Running it in additional jobs (`build-windows`, `coverage-linux`) is permitted as defense-in-depth hardening and does not introduce inconsistency: all jobs check out the same `ci.yml` from the same commit, so every instance of the step evaluates identical file contents. The minimum requirement is one instance in `build-linux`; additional instances in other jobs do not replace this requirement and do not need to be removed if present. The gate effect remains the same regardless of how many jobs carry the step: a lint failure in `build-linux` blocks `all-checks-pass` and prevents merging.

- **`build-linux` job** (`ubuntu-latest`): install xvfb + Mesa + libgl1-mesa-dev + vcpkg; CMake configure with **`-DENABLE_COVERAGE=OFF`** (coverage instrumentation disabled — this is the fast binary-verification build, not the coverage build); build; then, before running tests, verify that label routing is non-zero; then run tests in three explicitly named steps.

  **Note**: Do NOT add a "Verify shader assets" step to this job. Shader file existence checks are source-tree checks that belong in `validate-assets` (see Phase 11i phasing note below). This keeps build jobs focused on compilation and test execution.

  **Integration test routing verification (mandatory post-build step)**: After the build step and before any test execution step, add a CI step that queries the number of tests discovered under the `integration` label. If zero tests are discovered, the step exits non-zero and the job fails immediately. This prevents the false-green scenario where `gtest_discover_tests()` with a misconfigured `LABEL` silently produces zero tests and `ctest -L '^integration$'` exits 0 — a zero-test discovery does NOT constitute a passing verification:

  ```yaml
  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^integration$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."
  ```

  **Phase assignment (integration label routing)**: This `integration` label routing non-zero discovery verification step **MUST be added in Phase 1** (not deferred to Phase 3) because the `integration_tests` CMake target is first registered in Phase 1 with the `IrrlichtUIBackendCompileCheck::IsNonAbstract` compile-check test. That test registers under the `integration` label and satisfies the non-zero discovery requirement — `ctest -N -L '^integration$'` discovers exactly 1 test at Phase 1, so the verification step passes. Phase 3 co-lands additional integration tests with real domain assertions, but the target and the routing check are Phase 1 deliverables.

  **Integration routing check semantics**: The routing check verifies label correctness (non-zero discovery under the `integration` label), NOT phase-exclusion. A compile-only verification test registered under `integration` at Phase 1 is a valid and intentional use of the label — it is a special case acknowledged in `framework.md`. The check does not impose any constraint on which phases may register tests under a given label; it only ensures that at least one test is registered, preventing a misconfigured `LABEL` from silently producing zero results.

  **Phase assignment (unit label routing)**: The `unit` label routing non-zero discovery verification step is a Phase 4 deliverable (CI routing verification). At least one test in the `unit_tests` CMake target must be explicitly labelled `unit` — tests labelled `integration` or `requires-opengl` do NOT satisfy this check. The check ensures that `ctest -LE 'integration|requires-opengl'` (the unit test run step) will discover at least one test, preventing false-green CI from a misconfigured `LABEL` property that silently yields zero unit tests.

  Add a verification step before the integration routing check:

  ```yaml
  - name: Verify unit test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^unit$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Unit test routing verified: $count test(s) discovered."
  ```

  **Phase assignment (requires-opengl label routing)**: The `requires-opengl` label routing non-zero discovery verification step MAY be added in Phase 1, once `opengl_tests` is linked against `aitown_render`. The `stub_succeed.cpp` test registered in Phase 0 under `opengl_tests` satisfies the non-zero discovery requirement. This step is a Phase 1 deliverable and must not be deferred to Phase 3.

  Add an analogous verification step after the unit and integration routing checks and before the `xvfb-run` step:

  ```yaml
  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^requires-opengl$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Requires-opengl test routing verified: $count test(s) discovered."
  ```

  All three routing checks (`unit`, `integration`, `requires-opengl`) must be placed **after the CMake build step and before any ctest execution step** so that a label misconfiguration fails the job before any false-passing `ctest -L` invocation can run. Neither step requires a display or audio device — they only invoke `ctest -N` (list mode, no test execution).

  ```yaml
  - name: Run unit tests (no display)
    run: |
      mkdir -p test_results
      ctest --test-dir build -LE 'integration|requires-opengl' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      AITOWN_HEADLESS: '1'      # guard against unit tests that inadvertently trigger IrrlichtDevice init
      ALSOFT_DRIVERS: 'null'    # guard against unit tests that inadvertently trigger AudioSystem init

  - name: Run integration tests (no display, EDT_NULL)
    run: |
      mkdir -p test_results
      ctest --test-dir build -L '^integration$' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      AITOWN_HEADLESS: '1'
      ALSOFT_DRIVERS: 'null'

  - name: Run opengl tests (xvfb)
    run: |
      mkdir -p test_results
      xvfb-run --auto-servernum ctest --test-dir build -L '^requires-opengl$' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      ALSOFT_DRIVERS: 'null'    # OpenGL tests use real display via xvfb but still need null audio
      # AITOWN_HEADLESS=1 MUST NOT appear here. This env var causes application code to skip
      # IrrlichtDevice initialization. Tests in the requires-opengl bucket explicitly create
      # EDT_OPENGL devices; AITOWN_HEADLESS would cause those paths to be bypassed, producing
      # false green results.
  ```

  Note: label names are `integration` and `requires-opengl` per the Testing Strategy label conventions. **Do not use `--gtest_output` as a ctest flag** — it is a GTest binary flag and CTest silently ignores it. **`AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` are required on the integration test step** — integration tests use `EDT_NULL` which suppresses Irrlicht window creation, but `AudioSystem` initialization still attempts to open an audio device; `ALSOFT_DRIVERS=null` forces the null driver and prevents failures on headless runners without audio hardware. The unit test step includes `AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` as a defensive guard — these are zero-cost to apply and prevent accidental device instantiation from failing unit tests as the codebase grows. The OpenGL test step requires a real OpenGL context (via xvfb) but uses `ALSOFT_DRIVERS=null` to suppress audio device initialization on headless runners; `AITOWN_HEADLESS=1` must NOT be set for the OpenGL test step — it causes application code to skip `IrrlichtDevice` initialization, which means tests in the `requires-opengl` bucket that explicitly create `EDT_OPENGL` devices would have those paths bypassed, producing false green results. **`coverage-linux`** is the separate job that enables `-DENABLE_COVERAGE=ON` (see below).
- **CMakePresets.json**: All three CI jobs use named presets defined in `CMakePresets.json` at the repo root instead of long `-D` flag chains:
  - `ci-linux` — Ninja generator, `ENABLE_COVERAGE=OFF`, ccache launchers (`build-linux` job)
  - `ci-linux-coverage` — inherits `ci-linux`, `ENABLE_COVERAGE=ON` (`coverage-linux` job)
  - `ci-windows` — Ninja generator, `CMAKE_BUILD_TYPE=Release`, `ENABLE_COVERAGE=OFF` (`build-windows` job)

  Configure steps call `cmake --preset <name>` instead of `cmake -B build -S . -G ... -D...`. Local development: set `VCPKG_ROOT` then `cmake --preset ci-linux` (add `-DVCPKG_OVERLAY_PORTS=vcpkg-overlays` if using gcc-12 fallback).

- **`build-linux` ccache setup**: Include `hendrikmuhs/ccache-action` before the CMake configure step (Linux job only — `if: runner.os == 'Linux'`; ccache does not support `cl.exe` on Windows). The `ci-linux` and `ci-linux-coverage` CMake presets include `CMAKE_C_COMPILER_LAUNCHER=ccache` and `CMAKE_CXX_COMPILER_LAUNCHER=ccache` — no additional `-D` flags are needed in the configure step. See `caching.md` for the authoritative platform-specific caching rules, ccache key format, and action SHA.

  **`build-linux` job** — use the standard key (no suffix):

  ```yaml
  - name: Set up ccache
    if: runner.os == 'Linux'
    uses: hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9  # v1.2.14 — pin to SHA
    with:
      key: ${{ runner.os }}-ccache-${{ env.COMPILER_VERSION }}
  ```

  **`coverage-linux` job** — MUST use a distinct key with a `-coverage` suffix:

  ```yaml
  - name: Set up ccache
    if: runner.os == 'Linux'
    uses: hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9  # v1.2.14 — pin to SHA
    with:
      key: ${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}
  ```

  The `-coverage` suffix is mandatory. GCC emits different object code when `-fprofile-arcs -ftest-coverage` is active — coverage-instrumented objects are ABI-incompatible with non-instrumented objects. Using the same ccache key for both jobs would cause stale-cache hits that silently mix instrumented and non-instrumented objects in the coverage build, producing incorrect or missing `.gcda` output. See `caching.md` for the full rationale and authoritative platform-specific caching rules.

- **Windows job** (`windows-latest`): uses the Ninja generator (not MSBuild) via the `ci-windows` CMake preset. The `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` (v1.13.0) action runs `vcvarsall.bat x64` to place `cl.exe` and `link.exe` on `PATH` before `cmake --preset ci-windows` — Ninja does not auto-detect MSVC. DLL output lands at `build/` (not `build/Release/`) because Ninja is single-config and `CMAKE_BUILD_TYPE=Release` is set in the preset. Build step: `cmake --build build --parallel` (no `-C Release` needed for Ninja single-config, though `-C Release` is harmless and can be kept for ctest consistency).

  **Windows vcpkg DLL PATH requirement**: After the Build step and before any test step, add a step to append the vcpkg installed bin directory to `GITHUB_PATH`. This is required because the vcpkg `x64-windows` triplet builds GTest/GMock as **shared DLLs** (`gtest.dll`, `gmock.dll`) in `build/vcpkg_installed/x64-windows/bin/` — NOT in `build/` alongside the test executables. Without this, test binaries fail to start and ctest reports `No tests were found!!!` (silently, with exit code 0), so no XML is written.

  `GITHUB_PATH` writes take effect for all subsequent steps in the job (not within the same step).

  ```yaml
  - name: Add vcpkg bin to PATH
    shell: pwsh
    run: |
      "${{ github.workspace }}\build\vcpkg_installed\x64-windows\bin" >> $env:GITHUB_PATH
  ```

  **`gtest_discover_tests DISCOVERY_MODE PRE_TEST`** (mandatory for Windows): `cmake/AitownTestHelpers.cmake` MUST pass `DISCOVERY_MODE PRE_TEST` to `gtest_discover_tests`. With the default `POST_BUILD` mode, CMake runs the test binary immediately after linking (during `cmake --build`) to enumerate test cases. At build time the vcpkg bin directory has not yet been added to `PATH`, so `gtest.dll` cannot be loaded → the binary exits with a DLL load error → discovery produces empty output → ctest finds 0 tests at run time. `PRE_TEST` defers discovery to ctest time (inside the test step), where `GITHUB_PATH` already includes the vcpkg bin directory. `PRE_TEST` requires CMake ≥ 3.18; the project's `cmake_minimum_required(3.21)` satisfies this on both runners (ubuntu-latest ships CMake 3.22+; VS2022 runner ships CMake 3.28+).

  Then add a pre-test verification step to confirm Phase 7 DLLs and HRTF data are present before running tests. Both `soft_oal.dll` and `default.mhr` are hard-fails (see `hrtf-initialization.md`):

  ```yaml
  - name: Verify Phase 7 DLLs and HRTF data present
    shell: pwsh
    run: |
      if (-not (Test-Path "build\soft_oal.dll")) {
        Write-Error "soft_oal.dll not found in build\ — rename step failed or DLL was not copied."
        exit 1
      }
      if (-not (Test-Path "build\default.mhr")) {
        Write-Error "default.mhr not found in build\ — HRTF post-build copy rule failed."
        exit 1
      }
  ```

  Then create the test results directory and run tests — use **explicit label filtering** to skip `requires-opengl` tests (no display available on Windows runners). The `requires-opengl` label is Linux-only (`xvfb-run`); Windows integration tests run under `AITOWN_HEADLESS=1`:

  ```yaml
  - name: Run unit tests (no display)
    shell: pwsh
    run: |
      New-Item -ItemType Directory -Force -Path test_results
      ctest --test-dir build -C Release -LE "integration|requires-opengl" --output-on-failure
    env:
      ALSOFT_DRIVERS: "null"
      AITOWN_HEADLESS: "1"
      GTEST_OUTPUT: "xml:test_results/"

  - name: Run integration tests (no display)
    shell: pwsh
    run: |
      New-Item -ItemType Directory -Force -Path test_results
      ctest --test-dir build -C Release -L "^integration$" --output-on-failure
    env:
      ALSOFT_DRIVERS: "null"
      AITOWN_HEADLESS: "1"
      GTEST_OUTPUT: "xml:test_results/"
  ```

  `AITOWN_HEADLESS=1` suppresses both `AudioSystem` initialization and `IrrlichtDevice` window creation on headless runners. The Windows job runs both unit tests and integration tests but excludes `requires-opengl` tests (no xvfb available on Windows). The two-step structure mirrors the Linux job naming convention for clarity — a single combined `ctest -LE "requires-opengl"` step is equivalent but hides the unit/integration distinction in CI logs. The `New-Item` step is mandatory — `GTEST_OUTPUT=xml:test_results/` silently writes nothing if the directory does not exist. The `-C Release` flag is required for Ninja single-config builds. The `ALSOFT_DRIVERS=null` env var routes OpenAL Soft to the null (silent) driver so audio tests pass without audio hardware. **No coverage steps**. `dorny/test-reporter` glob: `test_results/*.xml`.
- **Test reporting**: Use `dorny/test-reporter` action to publish GTest XML results as PR annotations on all three jobs (`build-linux`, `build-windows`, `coverage-linux`). The step is **required in every job** — omitting it from any job means test failures in that job produce no PR annotations, silently hiding failures from reviewers. Add the following step after the test XML verification step in each job:

  ```yaml
  - name: Publish test results
    uses: dorny/test-reporter@31a54ee7ebcacc03a09ea97a7e5465a47b84aea5  # v1.9.1 — pin to SHA
    if: always()  # publish even on test failure
    with:
      name: Test Results (${{ github.job }})
      path: test_results/*.xml
      reporter: java-junit  # GTest XML is JUnit-compatible
      fail-on-error: false  # annotation failures must not mask test failures
  ```

  The `if: always()` is required so results are published even when tests fail. `fail-on-error: false` prevents a `dorny/test-reporter` error (e.g., no XML when tests crash early) from overwriting the real ctest exit code. **Implementation note**: The SHA `31a54ee7ebcacc03a09ea97a7e5465a47b84aea5` has been verified against the `v1.9.1` tag as of 2026-02-19. Re-verify before any baseline update using `gh release view v1.9.1 --repo dorny/test-reporter --json tagName,targetCommitish`.

  **Before `dorny/test-reporter`**, all three jobs (`build-linux`, `build-windows`, `coverage-linux`) must include a verification step that fails the job if no test XML was produced:

  ```yaml
  - name: Verify test XML output exists
    if: always()
    shell: bash
    run: |
      count=$(find test_results -name '*.xml' 2>/dev/null | wc -l)
      if [[ "$count" -eq 0 ]]; then
        echo "ERROR: No test XML files in test_results/ — GTEST_OUTPUT may not have written output"
        exit 1
      fi
      echo "Found $count test result file(s)."
  ```

  This step must use `shell: bash` (safe on both Linux and Windows runners via Git Bash) and run with `if: always()` so it catches failures even when ctest exits non-zero.
- **Artifact upload steps** (must be explicitly included in workflow YAML — not specifying these means nothing is uploaded despite retention policy requirements):

  **Artifact name uniqueness requirement**: All `upload-artifact` `name:` values MUST include `${{ github.sha }}` as a suffix. Without it, concurrent workflow runs (e.g., two PRs merging in rapid succession) upload artifacts with identical names — GitHub Actions silently overwrites the first with the second, destroying post-mortem data for the earlier run. The `${{ github.sha }}` suffix guarantees globally unique artifact names for the lifetime of the artifact retention window.

  ```yaml
  # After tests (build-linux job):
  # IMPORTANT: artifact names must be job-specific — runner.os returns "Linux" on both
  # build-linux and coverage-linux, causing a name collision. Use the job name explicitly.
  # ALL names MUST include ${{ github.sha }} for uniqueness across concurrent builds:
  #   build-linux job:    name: test-results-build-linux-${{ github.sha }}
  #   coverage-linux job: name: test-results-coverage-linux-${{ github.sha }}
  #   build-windows job:  name: test-results-windows-${{ github.sha }}
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: test-results-build-linux-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After tests (coverage-linux job) — retention-days: 14 is REQUIRED here explicitly:
  # coverage-linux runs under a separate job context; the upload step must carry its own
  # retention-days value and must NOT rely on the build-linux step definition above.
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: test-results-coverage-linux-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After tests (build-windows job):
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: test-results-windows-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After lcov (coverage-linux job):
  # Coverage HTML report uses 14-day retention — same as test XML (see retention policy below).
  - name: Upload coverage report
    if: always()  # Upload even on gate failure — the report is most valuable when coverage is below 80%
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: coverage-report-${{ github.sha }}
      path: coverage_html/
      retention-days: 14
  # After DLL verification (Windows job, on push to main only):
  # Ninja single-config: executable is at build/aitown.exe (not build/Release/aitown.exe).
  - name: Upload Windows binary
    if: github.ref == 'refs/heads/main'
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: aitown-windows-${{ github.sha }}
      path: build/aitown.exe
      retention-days: 30
  ```

- **Artifact retention**: test XML retained 14 days (all three jobs: `build-linux`, `coverage-linux`, `build-windows`); coverage HTML report retained 14 days (same as test XML — both are diagnostic artifacts consumed during the CI review window); release binaries (Windows, on push to `main` only) retained 30 days. Every `upload-artifact` step MUST carry an explicit `retention-days:` value — never rely on the GitHub Actions default (90 days) or assume another job's step definition applies.
- **`coverage-linux` is a separate, self-contained job** — it performs its own configure+build+test+lcov sequence with `-DENABLE_COVERAGE=ON`. It does NOT depend on artifacts from `build-linux` (which would require large artifact transfers). This means `coverage-linux` re-runs the full build, but with coverage instrumentation enabled; `build-linux` can run a faster non-coverage build for binary verification. Both jobs run in parallel. The `all-checks-pass` gate references both. **Naming note**: the job can be renamed `build-test-coverage-linux` for clarity, as long as the name matches in the `needs:` list.

  **Note**: Do NOT add a "Verify shader assets" step to this job. Shader file existence checks are source-tree checks that belong in `validate-assets` (see Phase 11i phasing note below). This keeps build jobs focused on compilation and test execution.

  - **`coverage-linux` must include three explicit, separately named YAML steps for ctest** (unit tests, integration tests without display, and OpenGL tests under xvfb) **before the lcov capture step**. A single combined `ctest` step cannot use both `-LE` and `-L` flags simultaneously; three named steps make coverage tracing explicit. The three ctest steps in `coverage-linux` must mirror the three ctest steps in `build-linux` exactly (same label filters `-LE "integration|requires-opengl"`, `-L "^integration$"`, `-L "^requires-opengl$"`) to ensure coverage data is collected for all test categories.

  **Label-routing verification in `coverage-linux` (mandatory)**: The `coverage-linux` job MUST include the same three label-routing non-zero discovery verification steps that `build-linux` includes — one for the `unit` label, one for the `integration` label, and one for the `requires-opengl` label. The exact step order within `coverage-linux` is:

  1. Install system dependencies (`apt-get`)
  2. Detect compiler version (write `COMPILER_VERSION` to `$GITHUB_ENV`)
  3. `actions/cache` for vcpkg (reads `COMPILER_VERSION` from step 2)
  4. `lukka/run-vcpkg` — install vcpkg packages (includes gtest and rapidcheck via vcpkg.json)
  5. `hendrikmuhs/ccache-action` — set up ccache with `-coverage` key suffix
  6. CMake configure (`cmake --preset ci-linux-coverage`)
  7. CMake build (`cmake --build build`)
  8. **Verify unit test routing (non-zero discovery)** — after build, before any ctest
  9. **Verify integration test routing (non-zero discovery)** — after build, before any ctest
  10. **Verify requires-opengl test routing (non-zero discovery)** — after build, before any ctest
  11. Run unit tests ctest step
  12. Run integration tests ctest step
  13. Run OpenGL tests ctest step (xvfb)
  14. Verify test XML output exists
  15. Publish test results (dorny/test-reporter)
  16. Capture and gate lcov coverage
  16a. Check src/ui/ zero-hit files (zero-hit coverage completeness checkpoint) — this step MUST use `if: always()` in the CI YAML so the zero-hit check runs unconditionally even when step 16 (lcov gate) exits non-zero; without `if: always()`, GitHub Actions skips step 16a after a lcov gate failure, silently bypassing dead-code detection
  16b. **Phase 6 deliverable** — `src/simulation/` SF preflight: verifies that `coverage_filtered.info` contains at least one `SF:` entry for `src/simulation/`. Placement: inside step 16 (the lcov capture-and-gate `run:` block), immediately before the 95% total gate awk step. See `architecture/testing/coverage.md` § Phase 6 for the exact bash snippet.
  16c. **Phase 11 deliverable** — `src/simulation/` per-file 85% floor gate: awk step that fails the build if any single `src/simulation/` file is below 85% line coverage. Placement: inside step 16 (the lcov capture-and-gate `run:` block), immediately after the `src/simulation/` SF preflight (step 16b) and before the 95% total gate. See `architecture/testing/coverage.md` § Phase 11 for the exact awk code.
  17. Upload coverage artifact

  Steps 8, 9, and 10 (the three label-routing verification steps) are placed **after CMake build step (7) and before the first ctest execution step (11)**. A label misconfiguration that produces zero-test discovery in `build-linux` will equally affect `coverage-linux`; without these checks, a zero-discovery run silently under-reports coverage and exits 0.

  **coverage-linux: label-routing verification YAML**

  These steps are IDENTICAL to the `build-linux` forms — copy them exactly. They are reproduced here verbatim so that an implementer building `coverage-linux` from this spec alone can derive the exact YAML without referring back to the `build-linux` documentation.

  ```yaml
  - name: Verify unit test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^unit$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Unit test routing verified: $count test(s) discovered."

  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^integration$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."

  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -c 'Test #' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^requires-opengl$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Requires-opengl test routing verified: $count test(s) discovered."
  ```

  **Phase assignment (unit label routing)**: The `unit` label routing non-zero discovery verification step is a Phase 4 deliverable. See the first occurrence of this note (in the `build-linux` section) for full rationale. Both `build-linux` and `coverage-linux` add this step in Phase 4; the steps are identical between the two jobs.

  **Phase assignment (integration label routing)**: This `integration` label routing non-zero discovery verification step **MUST be added in Phase 1** (not deferred to Phase 3) because the `integration_tests` CMake target is first registered in Phase 1 with the `IrrlichtUIBackendCompileCheck::IsNonAbstract` compile-check test. See the first occurrence of this note (in the `build-linux` section) for full rationale. Both `build-linux` and `coverage-linux` add these routing verification steps in Phase 1; the steps are identical between the two jobs.

  **Phase assignment (requires-opengl label routing)**: The `requires-opengl` label routing non-zero discovery verification step MAY be added in Phase 1, once `opengl_tests` is linked against `aitown_render`. The `stub_succeed.cpp` test registered in Phase 0 under `opengl_tests` satisfies the non-zero discovery requirement. This step is a Phase 1 deliverable and must not be deferred to Phase 3.

  All three routing checks (`unit`, `integration`, `requires-opengl`) must be placed **after the CMake build step and before the first ctest execution step** so that a label misconfiguration fails the job before any false-passing `ctest -L` invocation can run. Neither step requires a display or audio device — they only invoke `ctest -N` (list mode, no test execution).

    ```yaml
    - name: Run unit tests (no display)
      run: |
        mkdir -p test_results
        ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure
      env:
        GTEST_OUTPUT: "xml:test_results/"
        AITOWN_HEADLESS: "1"      # guard against unit tests that inadvertently trigger IrrlichtDevice init
        ALSOFT_DRIVERS: "null"    # guard against unit tests that inadvertently trigger AudioSystem init

    - name: Run integration tests (no display)
      run: |
        mkdir -p test_results
        ctest --test-dir build -L "^integration$" --output-on-failure
      env:
        GTEST_OUTPUT: "xml:test_results/"
        AITOWN_HEADLESS: "1"      # suppress Irrlicht window creation in integration tests
        ALSOFT_DRIVERS: "null"    # suppress audio device on headless CI runner

    - name: Run OpenGL tests (xvfb)
      run: |
        mkdir -p test_results
        xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure
      env:
        GTEST_OUTPUT: "xml:test_results/"
        ALSOFT_DRIVERS: "null"    # OpenGL tests use real display via xvfb but still need null audio
        # AITOWN_HEADLESS=1 MUST NOT appear here. This env var causes application code to skip
        # IrrlichtDevice initialization. Tests in the requires-opengl bucket explicitly create
        # EDT_OPENGL devices; AITOWN_HEADLESS would cause those paths to be bypassed, producing
        # false green results.

    - name: Capture and gate lcov coverage
      run: |
        BUILD_DIR=build
        # --ignore-errors mismatch: GCC 13 geninfo emits "mismatched end line" for inline
        # functions and lambdas in headers (GTest macros, fmt headers, etc.). This is a
        # known lcov/GCC 13 compatibility issue; the mismatch is benign and does not affect
        # coverage accuracy. Without this flag lcov --capture exits non-zero and the entire
        # coverage job fails before genhtml runs.
        # Use ${{ github.workspace }} (absolute path) rather than '.'.
        # On GitHub-hosted runners the shell CWD is reset to $GITHUB_WORKSPACE at the
        # start of each step, so '.' works correctly there. However, ${{ github.workspace }}
        # is preferred because: (1) it is explicit and self-documenting — the intent is
        # unambiguous in the YAML; (2) it works correctly on self-hosted runners where the
        # runner's working directory convention may differ from $GITHUB_WORKSPACE.
        # ${{ github.workspace }} is expanded by GitHub Actions at YAML evaluation time
        # and always resolves to the absolute path of the checked-out repository root.
        lcov --capture --directory build --base-directory ${{ github.workspace }} \
             --ignore-errors mismatch \
             --output-file coverage.info
        # --ignore-errors unused: lcov 2.x treats any --remove pattern matching no files
        # as a fatal error (exit 25). At Phase 0 only smoke tests exist; many patterns
        # (mock_*.h, src/audio/*, etc.) are future-proofing and match nothing yet.
        # Note: "${BUILD_DIR}/_deps/*" intentionally absent — build/_deps/ never exists
        # with FETCHCONTENT_BASE_DIR=.fetchcontent_cache.
        lcov --remove coverage.info \
          --ignore-errors unused \
          '/usr/*' \
          "${{ github.workspace }}/.fetchcontent_cache/*" \
          '*/tests/*' \
          '*/mock_*.h' '*/mock_*.cpp' \
          '*/manual_*.h' '*/manual_*.cpp' \
          '*/Mock*.h' '*/Mock*.cpp' \
          '*/Manual*.h' '*/Manual*.cpp' \
          '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
          --output-file coverage_filtered.info
        lcov --list coverage_filtered.info
        genhtml coverage_filtered.info --output-directory coverage_html/
        # PHASING NOTE: No hard coverage gate at Phase 0.
        # lcov --fail-under-percent does NOT exist in lcov 2.0 (ubuntu-latest ships 2.0;
        # the flag was added in lcov 2.1). Using it exits 1 with "Unknown option".
        # At Phase 0 the gate would be 0% anyway. Use --summary for informational output.
        # Phase 5 TODO: implement 80% gate via bash awk check or after confirming lcov 2.1+:
        #   lcov --summary coverage_filtered.info | awk '/lines/ {if ($2+0 < 80) exit 1}'
        lcov --summary coverage_filtered.info
        # Phase 4+ only — DO NOT add this block before Phase 4.
        # At Phase 1 and earlier, src/ui/ files are absent from the build entirely.
        # Adding this block before Phase 4 causes the gate to exit 1 with
        # "No src/ui/ coverage data found" on every CI run, breaking all merges.
        # Phase 4 src/ui/ coverage gate (BLOCKING): enforce a 25% floor on src/ui/ files.
        # lcov --list emits per-file coverage lines; grep filters to src/ui/ files only;
        # awk extracts the rightmost percentage field; sort -n and head -1 find the minimum.
        # If no src/ui/ files are present in the coverage data (empty grep output) the check
        # also fails — an absent src/ui/ entry is treated as 0%, not a vacuous pass.
        #
        # IMPORTANT: Do NOT use Bash integer comparison ("$pct" -lt 25). Integer comparison
        # truncates floats — 24.8% becomes 24, which incorrectly passes the gate. Use
        # float-aware awk arithmetic instead. Also validate that $pct is numeric before
        # comparison; lcov --list format changes (e.g. extra columns, missing separator) would
        # otherwise silently pass the gate with an empty or non-numeric string.
        pct=$(lcov --list coverage_filtered.info \
            | grep -E "src/ui/" \
            | grep -v "^Total" \
            | awk -F'|' '{gsub(/%/,"",$NF); print $NF+0}' \
            | sort -n | head -1)
        # head -1 takes the minimum (worst-case) src/ui/ file — intentional gate behavior
        # NOTE: awk -F'|' assumes lcov 2.x --list uses | as column delimiter.
        # If format changes, $NF+0 coercion produces 0 -> gate FAILS with misleading
        # '0% coverage' message rather than 'lcov format mismatch'.
        if [[ -z "$pct" ]]; then
          echo "ERROR: No src/ui/ coverage data found — src/ui/ files may be absent from build or excluded from coverage_filtered.info"
          exit 1
        fi
        if ! [[ "$pct" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
          echo "ERROR: src/ui/ coverage value '$pct' is not numeric — lcov --list format may have changed"
          exit 1
        fi
        result=$(echo "$pct 25" | awk '{if ($1+0 < $2+0) print "FAIL"; else print "PASS"}')
        if [[ "$result" == "FAIL" ]]; then
          echo "ERROR: src/ui/ coverage below 25% (found: ${pct}%)"
          exit 1
        fi
    ```

    The lcov capture-and-gate step must run **after all three ctest steps complete** — lcov reads the `.gcda` files produced by test execution. Running lcov before all three ctest steps complete will under-report coverage for integration-tested code paths. `BUILD_DIR` is set and used within the same `run:` block as all lcov operations, keeping variable scope self-contained.
  - **Step 17a — Check src/ui/ zero-hit files** (Phase 8 deliverable): Immediately after the lcov capture-and-gate step (step 17) and before the Upload coverage artifact step (step 18), add the following step. This step MUST use `if: always()` so it runs even when the lcov gate fails:

    ```yaml
    - name: Check src/ui/ zero-hit files
      if: always()
      shell: bash
      run: |
        python3 -c "
        import sys
        files_with_zero = []
        current_file = None
        has_hit = False
        for line in open('coverage_filtered.info'):
            line = line.strip()
            if line.startswith('SF:'):
                current_file = line[3:]
                has_hit = False
            elif line.startswith('DA:') and current_file and 'src/ui/' in current_file:
                if not line.endswith(',0'):
                    has_hit = True
            elif line == 'end_of_record' and current_file and 'src/ui/' in current_file and not has_hit:
                files_with_zero.append(current_file)
                current_file = None
        if files_with_zero:
            print('ERROR: These src/ui/ files have 0 coverage:', files_with_zero)
            sys.exit(1)
        "
    ```

  - **Step 17c — Phase 11 `src/simulation/` per-file 85% floor gate** (Phase 11 deliverable): Add this block inside the lcov capture-and-gate `run:` step (step 17), immediately after the `src/simulation/` SF preflight check (step 17b) and before the 95% total line coverage awk gate. It fails CI if any single `src/simulation/` file falls below 85% line coverage — catching under-tested outlier files that the aggregate 95% gate may not surface. For the exact awk code, see `architecture/testing/coverage.md` § Phase 11 (`**Phase 11 `src/simulation/` per-file 85% floor**`). Summary of behavior:

    - Parses `coverage_filtered.info` directly via SF/LH/LF records in a single awk pass — version-agnostic; does NOT use `lcov --list` output (whose column delimiter changed in lcov 2.0 and is unreliable for parsing). Computes per-file coverage as `LH / LF * 100` for each `src/simulation/` SF entry and tracks the minimum.
    - Preflight-fails if no `src/simulation/` SF entries are found in `coverage_filtered.info` (treats absent data as 0%, not a vacuous pass).
    - Fails CI with `"FAIL: worst src/simulation/ file coverage <pct>% < 85% Phase 11 per-file floor"` if the minimum file coverage is below 85.0%.

    **Placement constraint**: This step MUST run after the `src/simulation/` SF preflight (step 17b, Phase 6 deliverable) and before the 95% total gate awk step. The SF preflight guarantees that `src/simulation/` SF entries are present in `coverage_filtered.info` before the per-file awk runs; without it, an absent `src/simulation/` block would cause the per-file check to exit with a misleading preflight error rather than a coverage failure. Do NOT add this block before Phase 11 — the per-file floor was deferred from Phase 6 and is only enforced once Phase 11 simulation coverage is complete.

  - **`coverage-linux` test reporting steps (required)**: After all three ctest steps and **before the lcov capture step**, `coverage-linux` must include the XML verification and `dorny/test-reporter` steps. Placing these **before lcov** is intentional: if a test fails and ctest exits non-zero, the XML files may still be present; reporting them before the lcov step ensures test annotations reach the PR even when lcov subsequently fails or is skipped. Without these steps, test failures in the coverage build produce no PR annotations, silently hiding coverage-run failures from reviewers. **Step order in `coverage-linux`**: (1) unit tests ctest, (2) integration tests ctest, (3) OpenGL tests ctest under xvfb, (4) Verify test XML, (5) Publish test results via `dorny/test-reporter`, (6) lcov capture + filter + gate, (6a) check src/ui/ zero-hit files (`if: always()` — Phase 8 deliverable, see step 17a YAML above), (7) Upload coverage artifact. The `coverage-linux` YAML must include (after all ctest steps, before lcov):

    ```yaml
    - name: Verify test XML output exists
      if: always()
      shell: bash
      run: |
        count=$(find test_results -name '*.xml' 2>/dev/null | wc -l)
        if [[ "$count" -eq 0 ]]; then
          echo "ERROR: No test XML files in test_results/"
          exit 1
        fi
        echo "Found $count test result file(s)."

    - name: Publish test results
      uses: dorny/test-reporter@31a54ee7ebcacc03a09ea97a7e5465a47b84aea5  # v1.9.1 — pin to SHA
      if: always()
      with:
        name: Test Results (${{ github.job }})
        path: test_results/*.xml
        reporter: java-junit
        fail-on-error: false
    ```

- **`coverage-linux` is defined as a separate job** — a job referenced in `needs:` that does not exist causes workflow YAML parsing to fail before any job runs.

## markdown-lint job

The `markdown-lint` job enforces consistent Markdown formatting across all spec and documentation files. It runs independently of all build jobs — no dependency on vcpkg, CMake, or any C++ toolchain — and completes in seconds.

**Parallelism**: `markdown-lint` has no `needs:` dependency on any other job. It starts as soon as the workflow triggers and runs in parallel with `build-linux`, `build-windows`, and `coverage-linux`.

**Prerequisite**: `.markdownlint.json` must be present at the repository root. The `markdownlint` CLI reads this file for rule configuration; if it is absent, the tool falls back to all-default rules, which will produce different results from local runs and may spuriously fail or pass. The file is committed to the repo and is guaranteed to be present after `actions/checkout`.

**Job definition**:

```yaml
markdown-lint:
  runs-on: ubuntu-latest
  timeout-minutes: 5
  permissions:
    contents: read  # checkout only — no write access needed
  steps:
    - name: Checkout code
      uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11  # v4.1.1

    - name: Install markdownlint-cli
      run: npm install -g markdownlint-cli@0.47.0

    - name: Run markdownlint
      run: markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

**Key properties**:

- `runs-on: ubuntu-latest` — Node.js/npm are pre-installed; no additional setup required.
- `timeout-minutes: 5` — linting is fast (seconds); a 5-minute cap prevents runaway npm installs from consuming runner minutes.
- `permissions: contents: read` — the job only checks out and reads files; no artifact upload, no check annotations, no write access needed.
- The `npm install -g markdownlint-cli@0.47.0` step installs `markdownlint-cli` at the pinned version. MUST pin to a specific version; current pin: `@0.47.0`.
- The `markdownlint` command runs with the glob patterns that cover all spec and documentation files: `architecture/**/*.md`, `implementation/*.md`, and `CLAUDE.md`. The shell expands these globs on `ubuntu-latest` (bash, globstar not needed for single-level `**`). If the `implementation/` directory does not yet exist the glob silently matches nothing and the step passes — this is correct behavior for an empty phase.
- Exit code 1 on any violation — the job fails and blocks `all-checks-pass`.
- No caching step needed — `npm install -g` for a single small package takes under 10 seconds and adds no meaningful cache key complexity.
- No `dorny/test-reporter` step — `markdownlint` produces plain text output, not JUnit XML. CI log output is sufficient for diagnosis.
- No artifact upload step — no binary or report output is produced.

- **`validate-assets` job** — validates asset files using the Python validation script. Must run on every push and PR alongside the build jobs so asset errors are caught before any binary is produced. Runs on `ubuntu-latest` with a 10-minute timeout.

  **Phasing**: This job is introduced in Phase 1 running `tools/validate_assets.py` as a stub that always exits 0. It is wired into `all-checks-pass` at Phase 1 creation — not deferred to a later phase. This means the stub always passes, keeping the gate green while the real check logic is absent. In Phase 5 the script gains 18 real checks (Checks #1–#14 and Checks #16–#19) plus Check #15 as a stub placeholder; in Phase 9 two additions are made: a full implementation of the Check #15 `.meta` sidecar stub (replacing the `# TODO Phase 9` placeholder), and Check #20 (road LOD2 color validation against `RenderConstants::road_lod2_color`); in Phase 10, Checks #21–#23 (zone loop silence-floor, non-stinger WAV SFX format, HUD sprites dimensions) are added to the script; in Phase 10b, Check #24 (cloud texture format — clouds.png 1024×1024 RGBA) is added. The job definition and `all-checks-pass` wiring remain unchanged across all phases.

  **Phase 1 stub TODO comment requirements**: The Phase 1 `tools/validate_assets.py` stub MUST include the following TODO comment blocks so that Phase 5 implementers can locate all validation points via a single repository-wide search. These comments are the canonical markers — Phase 5 replaces each comment block with real validation logic in-place:

  ```python
  # TODO Phase 5: validate DDS textures (suffix-to-format table, mip chain counts)
  # TODO Phase 5: validate billboard atlas (1024x128 px, 4 mip levels, DXT5/BC3,
  #   DX10 DXGI_FORMAT BC3_UNORM_SRGB=78 — NOT FourCC-only, FourCC cannot confirm sRGB)
  # TODO Phase 5: validate audio assets (OGG Vorbis format, duration, sample rate)
  ```

  All three TODO blocks must be present in the stub at Phase 1 commit time. Omitting any block means Phase 5 implementers cannot find that validation point via `grep -r "TODO Phase 5"` and may overlook it. The blocks must appear as standalone comment lines (not inline with code) so the grep pattern matches them unambiguously.

  Job definition:

  ```yaml
  validate-assets:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    permissions:
      contents: read  # checkout only — no check annotations or artifact writes needed
    steps:
      - name: Checkout
        uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11  # v4.1.1 — verified
  ```

  The next step sets up Python 3 using `actions/setup-python`. This step MUST use a fully-resolved 40-character commit SHA pinned to the desired release tag — it must never appear in a committed `ci.yml` as a tag reference or a short SHA. To obtain the correct SHA at implementation time, run:

  ```sh
  gh release view --repo actions/setup-python --json tagName,targetCommitish
  ```

  This prints the tag name and the full 40-character commit SHA for the latest release. Record the SHA, verify it matches the tag on the `actions/setup-python` releases page, and substitute it directly into the `uses:` line. The resulting step looks like the following, where `<40-CHAR-SHA>` is replaced with the real value resolved above:

  **`python-version` MUST be set to a specific minor version (e.g., `'3.12'`) — NOT a floating major version (`'3'`).** Floating major versions break supply-chain reproducibility because `python-version: '3'` resolves to different patch versions on different runner instances — GitHub-hosted runners update their pre-installed Python over time, meaning the same workflow YAML can silently execute against `3.12.x` today and `3.13.x` next month. A floating `'3'` also interacts poorly with `actions/setup-python`'s resolution logic, which may select a different minor version depending on what is cached in the runner image at the time of execution. The pinned version MUST be documented in the YAML alongside the SHA-pinned `actions/setup-python` action reference. At Phase 1 implementation, use `python-version: '3.12'` (the current Python 3 LTS minor version).

  ```yaml
      - name: Set up Python 3
        uses: actions/setup-python@<40-CHAR-SHA>  # replace with full SHA resolved via gh release view
        with:
          python-version: '3.12'  # pin to specific minor version — never use '3' (floating major breaks reproducibility)
  ```

  **CRITICAL**: The token `@<40-CHAR-SHA>` above is illustrative prose — it is NOT a valid `uses:` value and MUST NEVER appear verbatim in a committed `ci.yml`. The supply-chain lint step in `build-linux` will match any `<...>` angle-bracket token and immediately fail the job, catching this mistake at CI time. Resolve the SHA live before committing.

  **Phase 5 Python dependencies**: the `validate-assets` job must install `mutagen` before
  running `validate_assets.py`. Add a pip install step immediately after the Python setup step:

  ```yaml
      - name: Install Python dependencies
        run: pip install mutagen
  ```

  `mutagen` is required by checks #16–#19 for OGG/WAV duration and format inspection. The
  package is available on PyPI and installs in under 5 seconds. Do NOT pin `mutagen` to a
  specific version — use `pip install mutagen` without version pinning to always use the
  latest compatible release.

  **Phase 11i deliverable — Shader asset verification** (source-tree check consolidated here from `build-linux`/`coverage-linux`; added to this job as part of Phase 11i implementation, not before): After the Python dependencies step and before `Run asset validation`, add a step that confirms both `IrrlichtUIBackend` raw-GL draw path shader files are present in the source tree. This is a pure source-tree file check — it requires no build artifacts, no C++ toolchain, and no OS-specific environment. **General rule: any CI step that checks source-tree file existence, file format, or file content and requires no compiled binary must be placed in `validate-assets`, not in `build-linux`, `build-windows`, or `coverage-linux`.** This rule prevents future duplication.

  **Ordering rationale**: `validate-assets` runs in parallel with `build-linux` and `coverage-linux` — it does NOT gate those jobs individually. This is intentional: shader files are source-tree files checked out by every CI job. If `assets/shaders/ui_quad.vert` or `assets/shaders/ui_quad.frag` are missing from the repository, build and test jobs will also fail with their own diagnostic messages. The `validate-assets` shader check provides a clear, dedicated diagnostic and gates the overall PR through `all-checks-pass`. Build/test jobs do not need to depend on `validate-assets` for correctness.

  ```yaml
      - name: Verify shader assets
        shell: bash
        run: |
          test -f assets/shaders/ui_quad.vert || { echo "Missing ui_quad.vert"; exit 1; }
          test -f assets/shaders/ui_quad.frag || { echo "Missing ui_quad.frag"; exit 1; }
  ```

  Continuing the job definition:

  ```yaml
      - name: Run asset validation
        run: python tools/validate_assets.py
  ```

  The `python tools/validate_assets.py` command must exit non-zero on any validation error — the script is responsible for printing a human-readable error message before exiting so CI logs identify the offending asset. The `validate-assets` job must be added to the `all-checks-pass` needs list simultaneously with its creation; omitting it from `needs:` means a failing asset-validation run does not block merges.

- **`all-checks-pass` gate job** — MUST include `if: always()`. **Any new CI job must be added to the `needs` list below.** See `branch-protection.md` for the full rationale and `if: always()` requirement.

  **IMPORTANT — phased implementation warning**: The `all-checks-pass` job evolves across phases. Do NOT reference a job in `needs:` before that job exists — it causes the entire workflow YAML to fail to parse before any job runs. Follow the phased forms below exactly.

  **Phasing summary for `validate-assets`**:
  - Phase 0: `validate-assets` job does not exist yet; omit from `needs:`.
  - Phase 1: `validate-assets` job is introduced running `tools/validate_assets.py` as a stub that always exits 0. Wire it into `all-checks-pass` immediately. Adding the job (even as a stub) now means the `all-checks-pass` dependency list never needs to change in later phases — only the script gains real checks.
  - Phase 5: the stub script gains 18 real checks (Checks #1–#14 and Checks #16–#19) plus Check #15 as a stub placeholder; the job definition and `all-checks-pass` wiring are unchanged.
  - Phase 9: Check #15 full implementation (replacing the Phase 5 `.meta` stub) and Check #20 (road LOD2 color validation) are added to the script; again no change to the job definition or wiring.
  - Phase 10: Checks #21–#23 (zone loop silence-floor, non-stinger WAV SFX format, HUD sprites dimensions) are added to the script; no change to the job definition or `all-checks-pass` wiring.
  - Phase 10b: Check #24 (cloud texture format gate — `clouds.png` 1024×1024 RGBA) is added to the script; no change to the job definition or `all-checks-pass` wiring.
  - Phase 11i: The "Verify shader assets" step is moved from `build-linux`/`coverage-linux` into this job (after `Install Python dependencies`, before `Run asset validation`). No change to the `all-checks-pass` wiring or the validate_assets.py script.
  - Phase 11d: Checks #25–30 are added to the script in two groups:
    - Checks #25–27 (vehicle atlas DDS format validation — `vehicles_diffuse_atlas_d.dds`
      2048×2048 BC1_UNORM_SRGB 4-mip, `vehicles_sprite_atlas_d.dds` 256×256 BC3_UNORM
      linear 1-mip, `vehicles_normal_atlas_n.dds` 2048×2048 BC3_UNORM linear 4-mip).
    - Check #28 (building atlas diffuse minimum variance): for each of the 9 zone-type wall
      cells (rows 0–2, cols 0–2) within `buildings_atlas_d.png`, compute the standard
      deviation of pixel luminance within the 496×496 px usable area; a cell with luminance
      stddev < 8.0 (0–255 scale) indicates a near-solid placeholder fill and is a CI failure.
    - Check #29 (building atlas normal map non-flat check): for each corresponding normal-map
      cell in the normal-map source PNG, compute the mean absolute deviation of the green
      channel; a value below 3.0 indicates a flat normal map with no authored surface relief
      and is a CI failure. Both checks #28 and #29 run on the source PNG (not the DDS) to
      avoid DXT1/DXT5 block-artefact noise in the measurement.
    - Check #30 (billboard atlas format and mip verification): for each `*_billboard.dds`
      file, verify (a) DDS dimensions are exactly 1024×128 px, (b) DX10 header present with
      DXGI_FORMAT BC3_UNORM_SRGB (value 78), (c) `dwMipMapCount` equals exactly 4, (d) total
      file size matches the reference 192,640 bytes for DXT5/BC3 1024×128 at 4 mip levels.

    Guard steps `Verify check_25 present`, `Verify check_26 present`, `Verify check_27
    present`, `Verify check_28 present`, `Verify check_29 present`, `Verify check_30 present`
    are added to the `validate-assets` job following the Phase 10b pattern (a `grep -q`
    step that exits 1 with a descriptive message if the named symbol is absent from
    `tools/validate_assets.py`); no change to `all-checks-pass` wiring.

    Example guard steps for checks #25–30:

    ```yaml
        - name: Verify check_25 present in validate_assets.py
          # check_25: vehicles_diffuse_atlas_d.dds — BC1_UNORM_SRGB (DXGI_FORMAT=72), 2048×2048, 4 mip levels.
          # A missing check_25 allows a misformatted vehicle diffuse atlas DDS to pass CI silently.
          run: |
            grep -q "check_25" tools/validate_assets.py || (echo "FAIL: check_25 not found in validate_assets.py — Phase 11d vehicle diffuse atlas DDS format gate missing" && exit 1)
            echo "PASS: check_25 present"

        - name: Verify check_26 present in validate_assets.py
          # check_26: vehicles_sprite_atlas_d.dds — BC3_UNORM linear (DXGI_FORMAT=77), 256×256, exactly 1 mip level.
          # A missing check_26 allows a misformatted vehicle sprite atlas DDS to pass CI silently.
          run: |
            grep -q "check_26" tools/validate_assets.py || (echo "FAIL: check_26 not found in validate_assets.py — Phase 11d vehicle sprite atlas DDS format gate missing" && exit 1)
            echo "PASS: check_26 present"

        - name: Verify check_27 present in validate_assets.py
          # check_27: vehicles_normal_atlas_n.dds — BC3_UNORM linear DXT5nm (DXGI_FORMAT=77), 2048×2048, 4 mip levels.
          # A missing check_27 allows a misformatted vehicle normal atlas DDS to pass CI silently.
          run: |
            grep -q "check_27" tools/validate_assets.py || (echo "FAIL: check_27 not found in validate_assets.py — Phase 11d vehicle normal atlas DDS format gate missing" && exit 1)
            echo "PASS: check_27 present"

        - name: Verify check_28 present in validate_assets.py
          # check_28: buildings_atlas_d.png wall-cell luminance stddev gate (< 8.0 = CI failure).
          # A missing check_28 allows a near-solid placeholder atlas to pass CI silently.
          run: |
            grep -q "check_28" tools/validate_assets.py || (echo "FAIL: check_28 not found in validate_assets.py — Phase 11d building atlas diffuse variance gate missing" && exit 1)
            echo "PASS: check_28 present"

        - name: Verify check_29 present in validate_assets.py
          # check_29: building atlas normal-map green-channel MAD gate (< 3.0 = CI failure).
          # A missing check_29 allows a flat normal map (no surface relief) to pass CI silently.
          run: |
            grep -q "check_29" tools/validate_assets.py || (echo "FAIL: check_29 not found in validate_assets.py — Phase 11d building atlas normal-map non-flat gate missing" && exit 1)
            echo "PASS: check_29 present"

        - name: Verify check_30 present in validate_assets.py
          # check_30: billboard atlas DDS dimensions/format/mip/size gate.
          # A missing check_30 allows an incorrectly formatted billboard atlas to pass CI silently.
          run: |
            grep -q "check_30" tools/validate_assets.py || (echo "FAIL: check_30 not found in validate_assets.py — Phase 11d billboard atlas format/mip verification gate missing" && exit 1)
            echo "PASS: check_30 present"
    ```

  - Phase 11e: Check #4 (UV within atlas cell) is **extended** — not a new check number — with
    a B3D UV-coordinate reader that validates UV coordinates at runtime. The extension upgrades
    the atlas grid from 4×4 (0.25 step) to 8×8 (0.125 step) and adds two new sub-validations:
    - **UV boundary gate**: for each `.b3d` model, read every UV coordinate from the mesh and
      verify it falls within the cell bounds defined by the model's `.meta` `atlas_cell`
      assignment. For a cell at `(cell_col, cell_row)` on the 8×8 grid the permitted UV range
      is `[cell_col/8, (cell_col+1)/8]` × `[cell_row/8, (cell_row+1)/8]`. Any UV coordinate
      outside this rectangle is a CI failure.
    - **Cell Assignment Table cross-check**: the `atlas_cell` row/col read from the `.meta`
      file is verified against the canonical Cell Assignment Table in
      `architecture/asset-standards/building-atlas-layout.md`. A mismatch between the `.meta`
      value and the table entry for that variant is a CI failure.

    No new check number is introduced; the guard step verifies the UV-reader logic is present
    in `tools/validate_assets.py` by searching for the joint presence of `"check_4"` and
    `"uv"` (case-insensitive). No change to the `validate-assets` job definition or
    `all-checks-pass` wiring.

    Example guard step for the Phase 11e Check #4 extension:

    ```yaml
        - name: Verify check_4 UV-coordinate reader present in validate_assets.py
          # check_4 extension (Phase 11e): B3D UV-coordinate reader validates UV coords fall
          # within [cell_col/8, (cell_col+1)/8] x [cell_row/8, (cell_row+1)/8] per atlas_cell.
          # Grid upgraded from 4x4 (0.25 step) to 8x8 (0.125 step).
          # A missing UV-reader allows out-of-bounds UVs to pass CI silently.
          run: |
            grep -q "check_4" tools/validate_assets.py && grep -qi "uv" tools/validate_assets.py || \
              (echo "FAIL: check_4 UV-coordinate reader not found in validate_assets.py — Phase 11e UV boundary gate missing" && exit 1)
            echo "PASS: check_4 UV-coordinate reader present"
    ```

  - Phase 11g: Check #31 (bitmap font XML + PNG file existence) is added to the script. For
    each of the six resolution pairs under `assets/fonts/` (`hud_font_720`, `hud_font_1080`,
    `hud_font_1440`, `hud_mono_font_720`, `hud_mono_font_1080`, `hud_mono_font_1440`), the
    check verifies that both the `.xml` descriptor and the paired `.png` glyph atlas exist on
    disk. A missing file in any resolution pair is a CI failure. No change to the
    `validate-assets` job definition or `all-checks-pass` wiring.

    Guard step (add to `validate-assets` job after the existing check_30 guard, before
    `Run asset validation`):

    ```yaml
        - name: Verify check_31 present in validate_assets.py
          # check_31: bitmap font XML + PNG existence for all six font tier pairs under assets/fonts/.
          # A missing check_31 allows absent font files to pass CI silently.
          run: |
            grep -q "check_31" tools/validate_assets.py || \
              (echo "FAIL: check_31 not found in validate_assets.py — Phase 11g bitmap font file existence gate missing" && exit 1)
            echo "PASS: check_31 present"
    ```

### PHASE 0 FORM (validate-assets not yet introduced)

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  # Phase 0 form — validate-assets job does not exist yet; add it in Phase 1.
  needs: [build-linux, build-windows, coverage-linux, markdown-lint]
  steps:
    - name: Verify all platform builds passed
      shell: bash   # REQUIRED: Bash arrays are used below; default shell on ubuntu-latest is bash but must be explicit for clarity
      run: |
        # Explicit check: only "success" is acceptable.
        # "skipped" and "cancelled" are treated as failures — GitHub branch protection
        # considers a "skipped" gate job as passing unless this gate explicitly exits 1.
        # NOTE: This step uses a Bash array — it MUST run on ubuntu-latest with shell: bash.
        # Do NOT run the all-checks-pass job on windows-latest; Bash arrays are not compatible
        # with PowerShell (the Windows default shell). The job is ubuntu-latest by design.
        results=(
          "${{ needs.build-linux.result }}"
          "${{ needs.build-windows.result }}"
          "${{ needs.coverage-linux.result }}"
          "${{ needs.markdown-lint.result }}"
        )
        failed=0
        for result in "${results[@]}"; do
          if [[ "$result" != "success" ]]; then
            echo "Job result '$result' is not 'success' — failing gate."
            failed=1
          fi
        done
        if [[ $failed -eq 1 ]]; then
          echo "One or more required jobs did not succeed (failed, skipped, or cancelled)."
          exit 1
        fi
        echo "All required jobs succeeded."
```

### PHASE 1+ FORM (validate-assets stub introduced and wired in Phase 1)

When the `validate-assets` job is added in Phase 1, update `all-checks-pass` to include it simultaneously. The job runs `tools/validate_assets.py`, which is a stub that always exits 0 at Phase 1. Wiring it in now means the `needs:` list requires no further changes in Phase 5 (real checks), Phase 9 (Check #15 full implementation and Check #20), or Phase 10 (Check #21 zone loop silence-floor) — only the script content changes, not the CI wiring.

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  needs: [build-linux, build-windows, coverage-linux, markdown-lint, validate-assets]  # ADD new jobs here
  steps:
    - name: Verify all platform builds passed
      shell: bash   # REQUIRED: Bash arrays are used below; default shell on ubuntu-latest is bash but must be explicit for clarity
      run: |
        # Explicit check: only "success" is acceptable.
        # "skipped" and "cancelled" are treated as failures — GitHub branch protection
        # considers a "skipped" gate job as passing unless this gate explicitly exits 1.
        # NOTE: This step uses a Bash array — it MUST run on ubuntu-latest with shell: bash.
        # Do NOT run the all-checks-pass job on windows-latest; Bash arrays are not compatible
        # with PowerShell (the Windows default shell). The job is ubuntu-latest by design.
        results=(
          "${{ needs.build-linux.result }}"
          "${{ needs.build-windows.result }}"
          "${{ needs.coverage-linux.result }}"
          "${{ needs.markdown-lint.result }}"
          "${{ needs.validate-assets.result }}"
        )
        failed=0
        for result in "${results[@]}"; do
          if [[ "$result" != "success" ]]; then
            echo "Job result '$result' is not 'success' — failing gate."
            failed=1
          fi
        done
        if [[ $failed -eq 1 ]]; then
          echo "One or more required jobs did not succeed (failed, skipped, or cancelled)."
          exit 1
        fi
        echo "All required jobs succeeded."
```

## `package-windows` Job

Produces an NSIS-based `.exe` installer via CPack. Runs only on push to `main` or `develop`
(`if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`).
Does NOT run on pull requests. Does NOT block `all-checks-pass`.

- **Runner**: `windows-latest`
- **Timeout**: `timeout-minutes: 60`
- **Permissions**: `contents: read`
- **Dependencies**: `needs: [build-windows]` — quality gate only; packaging rebuilds from source.

### Step sequence

1. Checkout (`actions/checkout` SHA-pinned)
2. Install NSIS via Chocolatey (`choco install nsis --no-progress -y`)
3. `ilammy/msvc-dev-cmd` (SHA-pinned) — vcvarsall for CMake configure
4. `lukka/run-vcpkg` (SHA-pinned) — restore vcpkg packages
5. CMake configure with `-DAITOWN_ASSETS_DIR=assets -DBUILD_TESTING=OFF`
6. CMake build: `cmake --build build --parallel`
7. Append `build\vcpkg_installed\x64-windows\bin` to `$env:GITHUB_PATH`
8. CPack — `cpack -G NSIS -C Release` (run inside `build/`)
9. Upload installer artifact (`name: aitown-installer-windows-<sha>`, `retention-days: 30`)

### CPack NSIS requirements in `CMakeLists.txt`

- `CPACK_PACKAGE_NAME`: `"AI Town"`
- `CPACK_PACKAGE_VENDOR`: project maintainer name
- `CPACK_PACKAGE_VERSION`: `${PROJECT_VERSION}`
- `CPACK_NSIS_DISPLAY_NAME`: `"AI Town"`
- `CPACK_NSIS_INSTALL_ROOT`: `"$PROGRAMFILES64"`
- `CPACK_NSIS_CREATE_ICONS_EXTRA`: Start Menu and Desktop shortcuts to `aitown.exe`
- `CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL`: `ON`
- `install(TARGETS aitown RUNTIME DESTINATION .)` — binary to installer root
- `install(DIRECTORY assets DESTINATION .)` — **no trailing slash**: installs `assets/` as a
  subdirectory alongside `aitown.exe`
- `install(FILES ${CMAKE_BINARY_DIR}/soft_oal.dll DESTINATION .)` — OpenAL Soft runtime
- `install(FILES ${CMAKE_BINARY_DIR}/default.mhr DESTINATION .)` — HRTF data file
- vcpkg DLLs: `install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/ DESTINATION . FILES_MATCHING PATTERN "*.dll")`
- `CPACK_GENERATOR` is NOT set in `CMakeLists.txt`; always specify `-G NSIS` on the command line
- `AITOWN_ASSETS_DIR` must be a `CACHE STRING` so `-DAITOWN_ASSETS_DIR=assets` overrides the default

## `package-linux-deb` Job

Produces `.deb` packages for four Debian/Ubuntu distros via CPack. Runs only on push to `main`
or `develop`. Does NOT run on pull requests. Does NOT block `all-checks-pass`.

### Matrix

| `matrix.distro` | `matrix.container` | `matrix.codename` |
|---|---|---|
| `debian-bookworm` | `debian:bookworm` | `bookworm` |
| `debian-trixie` | `debian:trixie` | `trixie` |
| `ubuntu-jammy` | `ubuntu:22.04` | `jammy` |
| `ubuntu-noble` | `ubuntu:24.04` | `noble` |

`fail-fast: false` — one distro failure must not cancel the other three.

- **Container**: `container: ${{ matrix.container }}`
- **Runner**: `ubuntu-latest` (host; container provides the distro environment)
- **Timeout**: `timeout-minutes: 60`
- **Permissions**: `contents: read`
- **Dependencies**: none (`needs:` omitted)

### Step sequence

1. Install system build dependencies (apt-get: build-essential, cmake, ninja-build, git,
   curl, zip, unzip, tar, pkg-config, libgl1-mesa-dev, libx11-dev, libxrandr-dev,
   libxinerama-dev, libopenal-dev, libvorbis-dev, python3, dpkg-dev, fakeroot)
2. Checkout (`actions/checkout` SHA-pinned)
3. Install vcpkg (clone + `git checkout $VCPKG_COMMIT_ID` + bootstrap)
4. Install vcpkg packages: `$VCPKG_ROOT/vcpkg install irrlicht --triplet x64-linux`
5. CMake configure with `-DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr -DAITOWN_ASSETS_DIR=/usr/share/aitown/assets`
6. CMake build: `cmake --build build --parallel`
7. CPack DEB: `cpack -G DEB` (run inside `build/`)
8. Upload `.deb` artifact (`name: aitown-deb-<distro>-<sha>`, `retention-days: 30`)

### CPack DEB requirements in `CMakeLists.txt`

- `CPACK_DEBIAN_PACKAGE_MAINTAINER`: maintainer name and email
- `CPACK_DEBIAN_PACKAGE_SECTION`: `games`
- `CPACK_DEBIAN_PACKAGE_PRIORITY`: `optional`
- `CPACK_DEBIAN_PACKAGE_DEPENDS`: `"libopenal1, libvorbis0a, libgl1, libx11-6, libxrandr2, libxinerama1"`
- `CPACK_DEBIAN_PACKAGE_SHLIBDEPS`: `ON`
- `CPACK_DEBIAN_FILE_NAME`: `DEB-DEFAULT`
- `CPACK_PACKAGING_INSTALL_PREFIX`: `/usr`
- `install(TARGETS aitown RUNTIME DESTINATION games)` — binary to `/usr/games/aitown`
- `install(DIRECTORY assets/ DESTINATION share/aitown/assets)` — **trailing slash**: copies contents
- `install(FILES ${CMAKE_BINARY_DIR}/default.mhr DESTINATION share/openal/hrtf)`
- `CPACK_GENERATOR` is NOT set; always specify `-G DEB` on the command line

### Gate status

`package-windows` and `package-linux-deb` are NOT in `all-checks-pass` `needs:`. Packaging
failures must not block PR merges — investigate before cutting a release.

## `bump-version` Job

Calculates the next patch version from git history, updates `CMakeLists.txt`, commits, and pushes the version tag on every push to `main`.

- **Trigger**: `if: github.event_name == 'push' && github.ref == 'refs/heads/main'`
- **Runner**: `ubuntu-latest`
- **Timeout**: `timeout-minutes: 5`
- **Permissions**: `contents: write` (required to push commit and tag)

### Step sequence

1. Checkout with `fetch-depth: 0` (full history required for `git describe`)
2. Calculate next version and write back to repo:

   ```bash
   CURRENT=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || echo 'v0.0.0')
   IFS='.' read -r MAJOR MINOR PATCH <<< "${CURRENT#v}"
   PATCH=$((PATCH + 1))
   NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
   sed -i "s/project(aitown VERSION [0-9.]*)/project(aitown VERSION ${NEW_VERSION})/" CMakeLists.txt
   git config user.name  "github-actions[bot]"
   git config user.email "github-actions[bot]@users.noreply.github.com"
   git add CMakeLists.txt
   git commit -m "chore: bump version to v${NEW_VERSION} [skip ci]"
   git tag "v${NEW_VERSION}"
   git push --follow-tags
   echo "AITOWN_VERSION=${NEW_VERSION}" >> "$GITHUB_ENV"
   ```

   The `[skip ci]` token in the commit message prevents the push from re-triggering the workflow. The tag is pushed atomically with the commit via `--follow-tags`.

### Versioning contract

- Versions use semantic versioning `MAJOR.MINOR.PATCH`. Only the patch component is auto-incremented by the pipeline.
- To increment MAJOR or MINOR, manually update `CMakeLists.txt` before merging to `main` (the pipeline will then increment patch from the new base).
- `CMakeLists.txt` is the canonical source of the current version at any point in time; git tags are the authoritative release markers.

## `release` Job

Creates a GitHub release with attached installer and `.deb` packages after `bump-version` and packaging jobs succeed.

- **Trigger**: `if: github.event_name == 'push' && github.ref == 'refs/heads/main'`
- **Runner**: `ubuntu-latest`
- **Timeout**: `timeout-minutes: 10`
- **Permissions**: `contents: write` (required to create releases and upload release assets)
- **Dependencies**: `needs: [bump-version, package-windows, package-linux-deb]`

### Step sequence

1. Checkout (reads updated `CMakeLists.txt` committed by `bump-version`):

   ```yaml
   - name: Checkout
     uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11  # v4.1.1
     with:
       ref: main
   ```

   `ref: main` is critical — without it, `actions/checkout` defaults to the trigger SHA (the commit that triggered the workflow), which is before `bump-version` pushed its version-bump commit. With `ref: main`, the release job checks out the main branch HEAD, which includes the version bump, and reads the updated `CMakeLists.txt`.

2. Read version:

   ```bash
   VERSION=$(grep -m1 'project(aitown VERSION' CMakeLists.txt \
     | sed 's/.*VERSION \([0-9.]*\).*/\1/')
   echo "AITOWN_VERSION=${VERSION}" >> "$GITHUB_ENV"
   ```

3. `actions/download-artifact` (SHA-pinned) — download `aitown-installer-windows-${{ github.sha }}` into `./release-assets/`
4. `actions/download-artifact` (SHA-pinned) — download `aitown-deb-debian-bookworm-${{ github.sha }}` into `./release-assets/`
5. `actions/download-artifact` (SHA-pinned) — download `aitown-deb-debian-trixie-${{ github.sha }}` into `./release-assets/`
6. `actions/download-artifact` (SHA-pinned) — download `aitown-deb-ubuntu-jammy-${{ github.sha }}` into `./release-assets/`
7. `actions/download-artifact` (SHA-pinned) — download `aitown-deb-ubuntu-noble-${{ github.sha }}` into `./release-assets/`
8. Create GitHub release via `softprops/action-gh-release` (SHA-pinned — resolve via `gh release view --repo softprops/action-gh-release --json tagName,targetCommitish` at implementation time):

   ```yaml
   - name: Create GitHub release
     uses: softprops/action-gh-release@<40-CHAR-SHA>  # resolve at implementation time
     with:
       tag_name: v${{ env.AITOWN_VERSION }}
       name: "AI Town v${{ env.AITOWN_VERSION }}"
       fail_on_unmatched_files: true
       files: release-assets/**
   ```

   `fail_on_unmatched_files: true` fails the job rather than silently publishing an incomplete release when a package artifact is missing.

### Gate status

`bump-version` and `release` are NOT in `all-checks-pass` `needs:`. Failures in these jobs must not block PR merges — investigate before the next merge to `main`.
