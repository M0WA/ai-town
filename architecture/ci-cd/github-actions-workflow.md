# GitHub Actions Workflow

Create `.github/workflows/ci.yml`:

- **Triggers**: push to `main` / `develop`; pull requests targeting `main` / `develop`; `workflow_dispatch` (manual trigger — required for re-running stale CI without a code push, e.g. after a transient runner failure or after updating vcpkg baseline). Without `workflow_dispatch`, a broken `main` with a non-code root cause (expired cache, runner issue) cannot be re-triggered without a dummy commit.
- **Permissions block** (required for `dorny/test-reporter` to write PR annotations and for Linux
  container jobs to pull the GHCR image):

  ```yaml
  permissions:
    checks: write       # required by dorny/test-reporter to post check results
    contents: read      # required to checkout code
    packages: read      # required by build-linux and coverage-linux to pull GHCR image
  ```

  Without `checks: write`, `dorny/test-reporter` receives a 403 and silently fails to publish test
  results. Without `packages: read`, the Linux container jobs cannot pull
  `ghcr.io/m0wa/aitown-ci-linux` and fail with a 401 at job startup.
  This block must appear at the workflow level (applies to all jobs) or per-job level.
  Individual jobs may override permissions where required: `compute-version` and `release` use
  `contents: write` (tag and release creation); `prepare` uses `contents: none`.
- **Job timeout requirements**: All jobs must include `timeout-minutes` to prevent runaway builds
  from consuming runner minutes indefinitely. Deployed values for all jobs with explicit timeouts:

  | Job | `timeout-minutes` | Notes |
  |---|---|---|
  | `prepare` | 2 | Lightweight env-export job |
  | `validate-assets` | 10 | Python validation + asset checks |
  | `build-linux` (compile) | 30 | Compile-only container job |
  | `test-linux` | 30 | Test execution inside `_test-linux.yml`; `build-linux` pipeline path can take up to 60 min total (compile + test in sequence) |
  | `asan-linux` | 45 | Configure + ASAN/UBSan compile + xvfb `requires-opengl` tests |
  | `build-windows` | 40 | |
  | `coverage-linux` | 60 | Full build + instrumented tests + lcov; Phase 7 AudioSystem instrumented build + streaming tests + three-tier ctest exceeds Phase 0 estimate; 60 min confirmed as sufficient headroom |
  | `markdown-lint` | 5 | Fast linting step |
  | `all-checks-pass` | 5 | Gate job |
  | `compute-version` | 5 | Tag increment logic |
  | `release` | 10 | Multi-artifact download + gh release create |
  | `package-linux-deb` | 90 | apt installs in bare containers + vcpkg-from-source bootstrap |
  | `package-windows` | 60 | NSIS + vcpkg restore + CPack |
  | `docker-ci-image` build-and-push | 120 | Cold vcpkg compilation from source can exceed 1 hour |
  | `supply-chain-lint` | (none set — gap) | Currently inherits GitHub Actions 6-hour default; recommend adding `timeout-minutes: 5` inside `_supply-chain-lint.yml` |

  Note: `compute-version` is intentionally excluded from `all-checks-pass` — it is skipped on PRs
  and `workflow_dispatch`, which would cause the gate to fail on those triggers.

  **Timeout placement for reusable workflows**: for jobs called via `uses:`,
  `timeout-minutes` must be set INSIDE the reusable workflow's job definition (e.g., inside
  `_supply-chain-lint.yml`), NOT in the `ci.yml` caller entry. Setting it in the caller has no
  effect.

  Without these limits, a hung MSVC linker or stuck xvfb process can block the runner for the
  GitHub Actions default 6-hour maximum, wasting all allocated minutes on the repo for that billing
  period.

  ```yaml
  build-linux:
    runs-on: ubuntu-latest
    timeout-minutes: 30
  ```

  Set `timeout-minutes` on the job level (not individual steps), unless a specific step (e.g., xvfb OpenGL tests) needs its own step-level timeout via `timeout-minutes` on the step.

## ci.yml Job Structure and Dependency Graph

`ci.yml` defines **thirteen jobs**:

| Job | Trigger | Purpose |
|---|---|---|
| `prepare` | All events | Exports `VCPKG_COMMIT_ID` as a job output so reusable-workflow `with:` blocks can reference it via `needs` context. (`env` context is unavailable in `jobs.<id>.with`.) |
| `compute-version` | Push to main/develop only | Auto-increments patch version on main; appends `-develop` suffix on develop. NOT in `all-checks-pass`. |
| `supply-chain-lint` | All events | Calls `_supply-chain-lint.yml` — validates SHA pins and container digest pins across all workflow files. |
| `validate-assets` | All events | Calls `_validate-assets.yml` — Python asset validation + structural checks. |
| `build-linux` | All events | Calls `_build-linux.yml` — compiles in GHCR container, delegates test execution to `test-linux` via `_test-linux.yml`. |
| `build-windows` | All events | Calls `_build-windows.yml` — compiles and tests on Windows. |
| `coverage-linux` | All events | Calls `_coverage-linux.yml` — build + test + lcov in GHCR container. |
| `asan-linux` | All events | Calls `_asan-linux.yml` — configures + compiles with ASAN/UBSan instrumentation and runs `requires-opengl` tests under xvfb. |
| `markdown-lint` | All events | Calls `_markdown-lint.yml` — markdownlint via `npx`. |
| `all-checks-pass` | All events | Gate job (`if: always()`). |
| `package-windows` | Push to main/develop | Calls `_package-windows.yml`. NOT in `all-checks-pass`. |
| `package-linux-deb` | Push to main/develop | Calls `_package-linux-deb.yml`. NOT in `all-checks-pass`. |
| `release` | Push to main only | Creates GitHub release. NOT in `all-checks-pass`. |

**Full `needs:` dependency graph**:

- `validate-assets` has `needs: [supply-chain-lint, prepare]`
- `build-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `build-windows` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `coverage-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `compute-version` has NO `needs:` — runs independently, gated only by push event `if:` condition
- `package-windows` has `needs: [build-windows, prepare, compute-version]` — `prepare` for its `vcpkg_commit_id` output
- `package-linux-deb` has `needs: [build-linux, prepare, compute-version]` — same rationale
- `asan-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `all-checks-pass` has `needs: [prepare, supply-chain-lint, validate-assets, build-linux, build-windows, coverage-linux, markdown-lint, asan-linux]`

**Rationale — `validate-assets` gates all build jobs**: all three build jobs wait for
`validate-assets` before starting. A failing `validate-assets` blocks ALL build jobs — not just
`all-checks-pass`. This avoids wasting build minutes compiling code with missing or malformed
assets.

**Rationale — `compute-version` excluded from `all-checks-pass`**: `compute-version` is skipped
on PRs and `workflow_dispatch`. Including it would cause `all-checks-pass` to fail on those
triggers where it is legitimately skipped.

## Supply-Chain SHA Lint

The supply-chain lint runs as a dedicated **`supply-chain-lint` job** defined in
`.github/workflows/_supply-chain-lint.yml` and called by `ci.yml` via
`uses: ./.github/workflows/_supply-chain-lint.yml`. It is NOT an inline step inside
`build-linux`. The `build-linux` job has no inline lint step.

The `supply-chain-lint` job is a separate upstream job in `ci.yml` and is listed in
`all-checks-pass`'s `needs:` list (see Phase 0 and Phase 1+ forms below). A lint failure in this
job blocks `all-checks-pass` and prevents merging.

### Three lint checks

The lint runs three sequential checks against ALL `*.yml` files under `.github/workflows/`:

1. **Placeholder angle-bracket tokens**: greps all workflow files for `<[A-Z_][A-Z0-9_-]*>` —
   unresolved `@<SHA>` or `@<VERSION_SHA>` tokens left by template authors. Exits non-zero if any
   are found.

2. **Short SHAs on `uses:` lines**: greps only `uses:` lines for `@[0-9a-f]{1,39}\b` — any
   SHA-like string shorter than 40 hex characters after `@`. The check is scoped to `uses:` lines
   (not all occurrences), because short hexadecimal strings in run scripts are common and
   intentional.

3. **Container image digest pins**: greps `container: image:` lines for the pattern
   `@sha256:[0-9a-f]{64}` — all container image references must include a full 64-hex sha256
   digest pin. An image reference without a digest pin allows the image to change between runs
   without a workflow update.

**Early-exit known gap**: the SHA lint loop uses a `failed=1` accumulator and reports all SHA pin
failures before calling `exit 1`. However, if any SHA lint failure is found, the step terminates
before the container image digest check (check 3) runs. Container digest failures are only reported
when the SHA lint passes. Future improvement: use a shared `failed=1` accumulator across all three
loops so all errors surface in one execution.

**Pattern rationale**:

- `<[A-Z_][A-Z0-9_-]*>` matches angle-bracket placeholder tokens left by template authors (e.g.,
  `actions/checkout@<CHECKOUT_SHA>`).
- `@[0-9a-f]{1,39}\b` on `uses:` lines matches shortened SHAs; requiring the full 40-character SHA
  prevents a malicious tag from silently resolving to a different commit.
- Container digest pins prevent silent image substitution on GHCR.

- **`build-linux` job**: defined in `.github/workflows/_build-linux.yml` as a reusable workflow.
  It has TWO internal jobs: `build-linux` (compile only, runs in the pre-baked GHCR container
  `ghcr.io/m0wa/aitown-ci-linux:vcpkg-b2f068f@sha256:...`, `options: --user root`) and
  `test-linux` (delegates to `_test-linux.yml` via `uses:`). The `build-linux` compile job does
  NOT run ctest — it uploads the build directory as an artifact (`build-linux-${{ github.sha }}`,
  `retention-days: 1`), which `test-linux` downloads and runs. There is no `apt-get install` step
  — all system packages are pre-installed in the container image. The compile job also uploads
  `compile-commands-linux-${{ github.run_id }}` (normalized `compile_commands.json` for SonarCloud,
  `retention-days: 14`) and, on push to main/develop, `aitown-staging-linux-${{ github.sha }}`
  (binary + HRTF, `retention-days: 30`).

  CMake configure uses **`-DENABLE_COVERAGE=OFF`** (this is the fast binary-verification build,
  not the coverage build). Coverage is in the separate `coverage-linux` job.

  SonarCloud-related steps in `build-linux`:
  - "Verify libGLEW.a artifact (CI-1)": `find /opt/vcpkg_installed -name "libGLEW.a" | grep -q "libGLEW.a"` — targets the container's pre-baked vcpkg path at `/opt/vcpkg_installed` (not `build/vcpkg_installed/`), uses a glob to be triplet-agnostic, guards against `glew` removal from `vcpkg.json`.
  - "Verify default.mhr HRTF data": `test -f build/default.mhr` — relative path (preferred over
    `${{ github.workspace }}` in container jobs to avoid host-path mismatch).
  - "Normalize compile_commands.json for SonarCloud": calls
    `python3 .github/scripts/normalize_compile_commands.py` to strip container-internal path
    prefixes before uploading.
  - "Upload compile_commands.json": uploads `compile-commands-linux-${{ github.run_id }}` for use by
    `sonarcloud.yml` (uses `run_id`, not `sha` — `sonarcloud.yml` downloads via
    `github.event.workflow_run.id`, which matches the build job's `run_id` context).

  The `_test-linux.yml` reusable workflow inputs: `build_artifact_name`, `build_dir`,
  `reporter_name`, `test_artifact_name`. The `test-linux` job uses `BUILD_DIR` as a job-level
  env var set from `inputs.build_dir`. The artifact upload name is `${{ inputs.test_artifact_name
  }}` — the sha uniqueness suffix is applied at the call site.

  **ASAN job (`_asan-linux.yml`)**: ASAN/UBSan instrumentation requires compile-time
  flags, so the ASAN job is a standalone reusable workflow `_asan-linux.yml` (NOT a
  second call to `_test-linux.yml` — that workflow downloads pre-built artifacts and
  has no configure or build steps). `_asan-linux.yml` runs its own configure step with
  `cmake --preset ci-linux-asan` (with `VCPKG_MANIFEST_INSTALL=OFF` and
  `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed`), builds the ASAN-instrumented binary,
  and runs only the `requires-opengl` tests under `xvfb-run`. The `asan-linux` job in
  `ci.yml` calls `_asan-linux.yml` with `needs: [supply-chain-lint, validate-assets, prepare]`
  and is listed in `all-checks-pass`'s `needs:` list. ASAN instrumentation sets
  `ASAN_OPTIONS: halt_on_error=1:detect_leaks=0` and `LSAN_OPTIONS: detect_leaks=0`
  (leak detection disabled because the CI container environment produces false positives
  with Irrlicht's global state). The ASAN run does NOT generate coverage reports —
  `ENABLE_COVERAGE=OFF` is baked into the `ci-linux-asan` preset because ASAN and gcov
  runtimes conflict. The preset also clears `CMAKE_C_COMPILER_LAUNCHER` and
  `CMAKE_CXX_COMPILER_LAUNCHER` to empty strings — this disables the ccache launchers
  inherited from `ci-linux` and prevents ASAN-instrumented object files from entering
  the shared ccache (see the CMakePresets.json bullet below for the full rationale).
  The `_asan-linux.yml` workflow therefore does NOT include the
  `hendrikmuhs/ccache-action` step. `timeout-minutes: 45` (configure + compile + xvfb test execution).
  Like the other Linux container jobs, `_asan-linux.yml` sets
  `container: image: ghcr.io/m0wa/aitown-ci-linux@sha256:<digest>` with
  `options: --user root`; this provides the pre-baked `/opt/vcpkg_installed` that the
  configure step requires.

  **Container `options: --user root`**: All four Linux container jobs (`_build-linux.yml`,
  `_test-linux.yml`, `_coverage-linux.yml`, `_asan-linux.yml`) set `options: --user root`
  on their `container:` block. This is required because the pre-baked GHCR image's default user is non-root, which
  causes permission failures when writing to `$GITHUB_WORKSPACE` and `/home/runner`. Without it,
  artifact upload and workspace writes fail with permission denied.

  **Container-pull permissions for reusable workflows**: reusable workflow jobs that pull GHCR
  container images must declare `packages: read` in their own job-level permissions block inside
  the reusable workflow — callers cannot delegate this by setting it only in their own permissions
  block.

  **Shader assets step**: This step is in `validate-assets`, not in `build-linux`/`coverage-linux`
  (consolidated in Phase 11i). Build jobs are focused on compilation and test execution only.

  **Integration test routing verification (mandatory post-build step)**: After the build step and before any test execution step, add a CI step that queries the number of tests discovered under the `integration` label. If zero tests are discovered, the step exits non-zero and the job fails immediately. This prevents the false-green scenario where `gtest_discover_tests()` with a misconfigured `LABEL` silently produces zero tests and `ctest -L '^integration$'` exits 0 — a zero-test discovery does NOT constitute a passing verification:

  ```yaml
  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -cE 'Test +#' || true)
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
      count=$(ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -cE 'Test +#' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^unit$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Unit test routing verified: $count test(s) discovered."

  - name: Verify simulation_tests registration
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -i 'simulation' | grep -cE 'Test +#' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: no simulation tests discovered under the unit label — simulation_tests may be misconfigured'
        exit 1
      fi
      echo "simulation_tests routing verified: $count test(s) discovered."
  ```

  **Note on grep patterns**: the deployed `_test-linux.yml` uses `grep -cE 'Test +#'` for the
  unit and simulation_tests checks, and `grep -cE 'Test +#'` for all checks. Use
  `grep -cE 'Test +#'` uniformly — this is future-proof as CTest emits two spaces before `#`
  for test counts above 9. The `Test #` literal pattern would silently undercount in those cases.

  **Step ordering in `_test-linux.yml`**: the deployed order is integration check → requires-opengl
  check → unit check (→ simulation_tests check) — not unit-first as in earlier spec text. The
  ordering has no functional consequence but this note reflects the deployed reality.

  Also required: a "Restore execute permissions on test binaries" step immediately after
  downloading the build artifact in `_test-linux.yml`, before any ctest invocation:

  ```yaml
  - name: Restore execute permissions on test binaries
    run: find $BUILD_DIR -maxdepth 2 -type f -name '*_tests' -exec chmod +x {} \;
  ```

  `actions/upload-artifact` strips file permissions when archiving; without this step, test
  binaries downloaded from the build artifact cannot be executed by ctest.

  **Phase assignment (requires-opengl label routing)**: The `requires-opengl` label routing non-zero discovery verification step MAY be added in Phase 1, once `opengl_tests` is linked against `aitown_render`. The `stub_succeed.cpp` test registered in Phase 0 under `opengl_tests` satisfies the non-zero discovery requirement. This step is a Phase 1 deliverable and must not be deferred to Phase 3.

  Add an analogous verification step after the unit and integration routing checks and before the `xvfb-run` step:

  ```yaml
  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -cE 'Test +#' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^requires-opengl$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Requires-opengl test routing verified: $count test(s) discovered."
  ```

  All three routing checks (`unit`, `integration`, `requires-opengl`) must be placed **after the
  CMake build step and before any ctest execution step** so that a label misconfiguration fails
  the job before any false-passing `ctest -L` invocation can run. Neither step requires a display
  or audio device — they only invoke `ctest -N` (list mode, no test execution).

  **"Create test results directory" step** (Linux container jobs): Both `_test-linux.yml` and
  `_coverage-linux.yml` use a single dedicated "Create test results directory" step
  (`run: mkdir -p test_results`) placed before all three ctest steps. Each ctest step's `run:`
  contains only the `ctest` invocation — no inline `mkdir -p`. The Windows job
  (`_build-windows.yml`) uses `New-Item -Force` inline in each test step. The inline form for
  Windows and the single pre-test step for Linux are both valid; the Linux container jobs use
  the single pre-test step for clarity.

  ```yaml
  - name: Create test results directory
    run: mkdir -p test_results

  - name: Run unit tests (no display)
    run: ctest --test-dir build -LE 'integration|requires-opengl' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      AITOWN_HEADLESS: '1'      # guard against unit tests that inadvertently trigger IrrlichtDevice init
      ALSOFT_DRIVERS: 'null'    # guard against unit tests that inadvertently trigger AudioSystem init

  - name: Run integration tests (no display, EDT_NULL)
    run: ctest --test-dir build -L '^integration$' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      AITOWN_HEADLESS: '1'
      ALSOFT_DRIVERS: 'null'

  - name: Run opengl tests (xvfb)
    run: xvfb-run --auto-servernum ctest --test-dir build -L '^requires-opengl$' --output-on-failure
    env:
      GTEST_OUTPUT: 'xml:test_results/'
      ALSOFT_DRIVERS: 'null'    # OpenGL tests use real display via xvfb but still need null audio
      # AITOWN_HEADLESS=1 MUST NOT appear here. This env var causes application code to skip
      # IrrlichtDevice initialization. Tests in the requires-opengl bucket explicitly create
      # EDT_OPENGL devices; AITOWN_HEADLESS would cause those paths to be bypassed, producing
      # false green results.
  ```

  Note: label names are `integration` and `requires-opengl` per the Testing Strategy label conventions. **Do not use `--gtest_output` as a ctest flag** — it is a GTest binary flag and CTest silently ignores it. **`AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` are required on the integration test step** — integration tests use `EDT_NULL` which suppresses Irrlicht window creation, but `AudioSystem` initialization still attempts to open an audio device; `ALSOFT_DRIVERS=null` forces the null driver and prevents failures on headless runners without audio hardware. The unit test step includes `AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` as a defensive guard — these are zero-cost to apply and prevent accidental device instantiation from failing unit tests as the codebase grows. The OpenGL test step requires a real OpenGL context (via xvfb) but uses `ALSOFT_DRIVERS=null` to suppress audio device initialization on headless runners; `AITOWN_HEADLESS=1` must NOT be set for the OpenGL test step — it causes application code to skip `IrrlichtDevice` initialization, which means tests in the `requires-opengl` bucket that explicitly create `EDT_OPENGL` devices would have those paths bypassed, producing false green results. **`coverage-linux`** is the separate job that enables `-DENABLE_COVERAGE=ON` (see below).
- **CMakePresets.json**: All four CI jobs use named presets defined in `CMakePresets.json` at the repo root instead of long `-D` flag chains:
  - `ci-linux` — Ninja generator, `ENABLE_COVERAGE=OFF`, ccache launchers (`build-linux` job)
  - `ci-linux-coverage` — inherits `ci-linux`, `ENABLE_COVERAGE=ON` (`coverage-linux` job)
  - `ci-linux-asan` — inherits `ci-linux`; adds `-fsanitize=address,undefined -fno-omit-frame-pointer` to `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`; sets `ENABLE_COVERAGE=OFF` (ASAN and gcov runtimes conflict); sets `CMAKE_C_COMPILER_LAUNCHER: ""` and `CMAKE_CXX_COMPILER_LAUNCHER: ""` (empty strings) to disable ccache — the parent `ci-linux` preset enables ccache launchers, and ASAN-instrumented object files must not enter the shared ccache because `build-linux` uses the same ccache key; without this override, a subsequent `build-linux` run could restore ASAN-instrumented `.o` files from the cache, producing a non-ASAN binary silently linked against sanitizer-instrumented objects.
  - `ci-windows` — Ninja generator, `CMAKE_BUILD_TYPE=Release`, `ENABLE_COVERAGE=OFF` (`build-windows` job)

  Configure steps call `cmake --preset <name>` instead of `cmake -B build -S . -G ... -D...`. Local development: set `VCPKG_ROOT` then `cmake --preset ci-linux` (add `-DVCPKG_OVERLAY_PORTS=vcpkg-overlays` if using gcc-12 fallback).

- **`build-linux` ccache setup**: Include `hendrikmuhs/ccache-action` before the CMake configure
  step. In Linux-only dedicated reusable workflows (`_build-linux.yml`, `_coverage-linux.yml`) the
  `if: runner.os == 'Linux'` guard is unnecessary and is omitted in the deployed source — these
  workflows always run in a Linux container. The guard is only required in mixed-OS workflows
  (e.g., a monolithic `ci.yml` where Linux and Windows steps coexist in the same job). The
  `ci-linux` and `ci-linux-coverage` CMake presets include `CMAKE_C_COMPILER_LAUNCHER=ccache`
  and `CMAKE_CXX_COMPILER_LAUNCHER=ccache` — no additional `-D` flags are needed in the configure
  step. See `caching.md` for the authoritative platform-specific caching rules, ccache key format,
  and action SHA.

  **`build-linux` job** — use the standard key (no suffix):

  ```yaml
  - name: Set up ccache
    uses: hendrikmuhs/ccache-action@33522472633dbd32578e909b315f5ee43ba878ce  # v1.2.22 — pin to SHA
    with:
      key: ${{ runner.os }}-ccache-${{ env.COMPILER_VERSION }}
  ```

  **`coverage-linux` job** — MUST use a distinct key with a `-coverage` suffix:

  ```yaml
  - name: Set up ccache
    uses: hendrikmuhs/ccache-action@33522472633dbd32578e909b315f5ee43ba878ce  # v1.2.22 — pin to SHA
    with:
      key: ${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}
  ```

  The `-coverage` suffix is mandatory. GCC emits different object code when `-fprofile-arcs -ftest-coverage` is active — coverage-instrumented objects are ABI-incompatible with non-instrumented objects. Using the same ccache key for both jobs would cause stale-cache hits that silently mix instrumented and non-instrumented objects in the coverage build, producing incorrect or missing `.gcda` output. See `caching.md` for the full rationale and authoritative platform-specific caching rules.

- **Windows job** (`windows-latest`): uses the Ninja generator (not MSBuild) via the `ci-windows`
  CMake preset. The `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` (v1.13.0)
  action runs `vcvarsall.bat x64` to place `cl.exe` and `link.exe` on `PATH` before
  `cmake --preset ci-windows` — Ninja does not auto-detect MSVC. DLL output lands at `build/`
  (not `build/Release/`) because Ninja is single-config and `CMAKE_BUILD_TYPE=Release` is set in
  the preset. Build step: `cmake --build build --parallel` (no `-C Release` needed for Ninja
  single-config, though `-C Release` is harmless and can be kept for ctest consistency).

  **`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true`** must be set at the **job `env:` level** to
  suppress node20-deprecation warnings from `ilammy/msvc-dev-cmd` and `lukka/run-vcpkg`, which
  have no node24 release. This is a job-level env var, not a step-level env var. Setting it at
  job level suppresses the warnings for all steps in the job.

  **Windows vcpkg DLL PATH requirement**: After the Build step and before any test step, add a step to append the vcpkg installed bin directory to `GITHUB_PATH`. This is required because the vcpkg `x64-windows` triplet builds GTest/GMock as **shared DLLs** (`gtest.dll`, `gmock.dll`) in `build/vcpkg_installed/x64-windows/bin/` — NOT in `build/` alongside the test executables. Without this, test binaries fail to start and ctest reports `No tests were found!!!` (silently, with exit code 0), so no XML is written.

  `GITHUB_PATH` writes take effect for all subsequent steps in the job (not within the same step).

  ```yaml
  - name: Add vcpkg bin to PATH
    shell: pwsh
    run: |
      "${{ github.workspace }}\build\vcpkg_installed\x64-windows\bin" >> $env:GITHUB_PATH
  ```

  **`gtest_discover_tests DISCOVERY_MODE PRE_TEST`** (mandatory for Windows): `cmake/AitownTestHelpers.cmake` MUST pass `DISCOVERY_MODE PRE_TEST` to `gtest_discover_tests`. With the default `POST_BUILD` mode, CMake runs the test binary immediately after linking (during `cmake --build`) to enumerate test cases. At build time the vcpkg bin directory has not yet been added to `PATH`, so `gtest.dll` cannot be loaded → the binary exits with a DLL load error → discovery produces empty output → ctest finds 0 tests at run time. `PRE_TEST` defers discovery to ctest time (inside the test step), where `GITHUB_PATH` already includes the vcpkg bin directory. `PRE_TEST` requires CMake ≥ 3.18; the project's `cmake_minimum_required(3.21)` satisfies this on both runners (ubuntu-latest ships CMake 3.22+; VS2022 runner ships CMake 3.28+).

  The DLL verification step ("Verify required DLLs and HRTF data (all hard-fail)") runs AFTER
  test execution and after test result publication, immediately before staging artifact upload
  (not before tests as in an earlier spec revision). All four files are hard-fails:

  ```yaml
  - name: Verify required DLLs and HRTF data (all hard-fail)
    shell: pwsh
    run: |
      if (-not (Test-Path "build\Irrlicht.dll")) {
        Write-Error "Irrlicht.dll not found in build\ — Phase 1 post-build copy rule failed."
        exit 1
      }
      if (-not (Test-Path "build\GLEW32.dll")) {
        Write-Error "GLEW32.dll not found in build\ — Phase 1 post-build copy rule failed."
        exit 1
      }
      if (-not (Test-Path "build\soft_oal.dll")) {
        Write-Error "soft_oal.dll not found in build\ — rename step failed or DLL was not copied."
        exit 1
      }
      if (-not (Test-Path "build\default.mhr")) {
        Write-Error "default.mhr not found in build\ — HRTF post-build copy rule failed."
        exit 1
      }
  ```

  `Irrlicht.dll` and `GLEW32.dll` are copied by post-build rules in Phase 1 CMakeLists.txt.
  All four checks use `if (-not (Test-Path ...)) { exit 1 }` syntax — the `||` short-circuit
  operator is PowerShell 7+ only; GitHub Actions Windows runners use PowerShell 5.1.

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
    uses: dorny/test-reporter@a43b3a5f7366b97d083190328d2c652e1a8b6aa2  # v3.0.0 — pin to SHA
    if: always()  # publish even on test failure
    continue-on-error: true  # Linux container jobs only — see rationale above
    with:
      name: ${{ inputs.reporter_name }}  # caller-supplied via reusable workflow input
      path: test_results/*.xml
      reporter: java-junit  # GTest XML is JUnit-compatible
      fail-on-error: false  # annotation failures must not mask test failures
  ```

  For hardcoded reporter names (non-reusable workflows): `_coverage-linux.yml` uses
  `name: 'Linux Coverage Tests'`; `_build-windows.yml` uses `name: 'Windows Unit Tests'` and
  omits `continue-on-error: true` (Windows job, not container). For `_test-linux.yml` the
  `name:` is supplied via the `inputs.reporter_name` reusable workflow input (e.g.,
  `'Linux Unit Tests'`).

  The `if: always()` is required so results are published even when tests fail. `fail-on-error: false` prevents a `dorny/test-reporter` error (e.g., no XML when tests crash early) from overwriting the real ctest exit code. **Implementation note**: The SHA `a43b3a5f7366b97d083190328d2c652e1a8b6aa2` has been verified against the `v3.0.0` tag. Re-verify before any baseline update using `gh release view v3.0.0 --repo dorny/test-reporter --json tagName,targetCommitish`.

**`continue-on-error: true` on Linux container jobs only**: Linux container jobs (`_test-linux.yml` and `_coverage-linux.yml`) must include `continue-on-error: true` on the `dorny/test-reporter` step. Rationale: inside the GHCR container the checkout UID differs from the runner process UID, which can occasionally cause dorny to produce a non-zero exit code even when `fail-on-error: false` is set — `continue-on-error: true` prevents that exit code from masking the real test result. The Windows job (`_build-windows.yml`) does NOT include `continue-on-error: true` — it does not run in a container and this scenario does not occur.

  **Before `dorny/test-reporter`**, Linux jobs and the Windows job each include a verification step
  that fails the job if no test XML was produced. The Linux form uses `shell: bash`; the Windows
  form uses `shell: pwsh` (step name "Verify test XML output" — no "exists" suffix):

  **Linux form (`_test-linux.yml`, `_coverage-linux.yml`)**:

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

  **Windows form (`_build-windows.yml`)**:

  ```yaml
  - name: Verify test XML output
    if: always()
    shell: pwsh
    run: |
      $count = (Get-ChildItem -Path test_results -Filter '*.xml' -ErrorAction SilentlyContinue | Measure-Object).Count
      if ($count -eq 0) { Write-Error "No test XML files found in test_results/"; exit 1 }
      Write-Host "Found $count test result file(s)."
  ```

  Note: `_coverage-linux.yml` does NOT include a "Verify test XML output exists" step — the
  coverage job goes directly from ctest execution to the git safe.directory fix and dorny reporter
  without an XML pre-check. The XML verification requirement applies to `_test-linux.yml` and
  `_build-windows.yml` only.

  **"Fix git safe.directory for test reporter"** (required in ALL Linux container jobs before
  dorny): both `_test-linux.yml` and `_coverage-linux.yml` must include:

  ```yaml
  - name: Fix git safe.directory for test reporter
    if: always()
    run: git config --global --add safe.directory "$GITHUB_WORKSPACE"
  ```

  This step is required because inside the GHCR container the checkout directory is owned by a
  different UID than the container runner process, which causes `dorny/test-reporter` to produce a
  non-zero exit code. The `git config` step must be placed immediately before the dorny step with
  `if: always()`. The Windows job does not run in a container and does not need this step.

- **Artifact upload steps** (must be explicitly included in workflow YAML — not specifying these
  means nothing is uploaded despite retention policy requirements):

  **Anti-wildcard rule**: `path:` values should list explicit paths where possible. Wildcards that
  might silently match nothing are forbidden. The following are the three **approved exceptions**
  where wildcards are required (because CPack or the release job generates version-stamped
  filenames at runtime that cannot be hard-coded at authoring time):
  - `build/*.dll` — acceptable in the Windows staging artifact upload because the DLL
    verification hard-fail step immediately preceding it guarantees at least one DLL is present.
  - `build/aitown-*.deb` — CPack generates the full `.deb` filename at packaging time.
  - `build/aitown-*.exe` — CPack generates the full `.exe` installer filename at packaging time.
  - `release-assets/**` — release artifact filenames contain the computed version string.

  **Artifact name uniqueness requirement**: All `upload-artifact` `name:` values MUST include `${{ github.sha }}` as a suffix — with one exception: `compile-commands-linux-${{ github.run_id }}` uses `run_id` instead of `sha` because `sonarcloud.yml` downloads it via the `workflow_run` event context where `github.event.workflow_run.id` (not `head_sha`) is the stable lookup key. Without a unique suffix, concurrent workflow runs (e.g., two PRs merging in rapid succession) upload artifacts with identical names — GitHub Actions silently overwrites the first with the second, destroying post-mortem data for the earlier run.

  ```yaml
  # After tests (build-linux job):
  # IMPORTANT: artifact names must be job-specific — runner.os returns "Linux" on both
  # build-linux and coverage-linux, causing a name collision. Use the job name explicitly.
  # ALL names MUST include ${{ github.sha }} for uniqueness across concurrent builds:
  #   build-linux job:    name: test-results-build-linux-${{ github.sha }}
  #   coverage-linux job: name: test-results-coverage-linux-${{ github.sha }}
  #   build-windows job:  name: test-results-build-windows-${{ github.sha }}
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: test-results-build-linux-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After tests (coverage-linux job) — retention-days: 14 is REQUIRED here explicitly:
  # coverage-linux runs under a separate job context; the upload step must carry its own
  # retention-days value and must NOT rely on the build-linux step definition above.
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: test-results-coverage-linux-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After tests (build-windows job):
  - name: Upload test results
    if: always()
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: test-results-build-windows-${{ github.sha }}
      path: test_results/
      retention-days: 14
  # After lcov (coverage-linux job) — all four coverage artifacts use if: always() and 14-day retention:
  # 1. Sonar Generic Coverage XML (consumed by sonarcloud.yml):
  - name: Upload Sonar coverage XML
    if: always()
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: coverage-sonar-linux-${{ github.sha }}
      path: coverage.xml
      retention-days: 14
  # 2. Coverage HTML report:
  - name: Upload coverage HTML report
    if: always()  # Upload even on gate failure — the report is most valuable when coverage is below threshold
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: coverage-report-linux-${{ github.sha }}
      path: coverage_html/
      retention-days: 14
  # 3. Filtered lcov info (for post-mortem analysis):
  - name: Upload coverage info
    if: always()
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: coverage-info-linux-${{ github.sha }}
      path: coverage_filtered.info
      retention-days: 14
  # After DLL verification (Windows job, on push to main or develop):
  # Uploads staging artifact used by package-windows (no rebuild in packaging).
  # Ninja single-config: executable is at build/aitown.exe (not build/Release/aitown.exe).
  - name: Upload Windows staging artifact
    if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: aitown-staging-windows-${{ github.sha }}
      path: |
        build/aitown.exe
        build/*.dll
        build/default.mhr
      retention-days: 30
  ```

  Linux build job must also upload a staging artifact for use by `package-linux-deb`.
  The artifact contains both the binary and the HRTF data file:

  ```yaml
  # After test run (build-linux job, on push to main or develop):
  - name: Upload Linux staging artifact
    if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')
    uses: actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f  # v7.0.0
    with:
      name: aitown-staging-linux-${{ github.sha }}
      path: |
        build/aitown
        build/default.mhr
      retention-days: 30
  ```

- **Artifact retention**: test XML retained 14 days (all three jobs: `build-linux`, `coverage-linux`, `build-windows`); coverage HTML report retained 14 days (same as test XML — both are diagnostic artifacts consumed during the CI review window); staging artifacts (`aitown-staging-windows-*`, `aitown-staging-linux-*`) and release installers retained 30 days. Every `upload-artifact` step MUST carry an explicit `retention-days:` value — never rely on the GitHub Actions default (90 days) or assume another job's step definition applies.
- **`coverage-linux` is a separate, self-contained job** — it performs its own configure+build+test+lcov sequence with `-DENABLE_COVERAGE=ON`. It does NOT depend on artifacts from `build-linux` (which would require large artifact transfers). This means `coverage-linux` re-runs the full build, but with coverage instrumentation enabled; `build-linux` can run a faster non-coverage build for binary verification. Both jobs run in parallel. The `all-checks-pass` gate references both. **Naming note**: the job can be renamed `build-test-coverage-linux` for clarity, as long as the name matches in the `needs:` list.

  **Note**: Do NOT add a "Verify shader assets" step to this job. Shader file existence checks are source-tree checks that belong in `validate-assets` (see Phase 11i phasing note below). This keeps build jobs focused on compilation and test execution.

  - **`coverage-linux` must include three explicit, separately named YAML steps for ctest** (unit tests, integration tests without display, and OpenGL tests under xvfb) **before the lcov capture step**. A single combined `ctest` step cannot use both `-LE` and `-L` flags simultaneously; three named steps make coverage tracing explicit. The three ctest steps in `coverage-linux` must mirror the three ctest steps in `build-linux` exactly (same label filters `-LE "integration|requires-opengl"`, `-L "^integration$"`, `-L "^requires-opengl$"`) to ensure coverage data is collected for all test categories.

  **`coverage-linux` runs in the pre-baked GHCR container** (`container: image:
  ghcr.io/m0wa/aitown-ci-linux:vcpkg-b2f068f@sha256:...`, `options: --user root`). There is no
  `apt-get install` step, no `actions/cache` step for vcpkg, and no `lukka/run-vcpkg` step — all
  system packages and vcpkg packages are pre-installed in the image.

  **`coverage-linux` does NOT include label-routing verification steps** (no "Verify unit test
  routing", "Verify integration test routing", or "Verify requires-opengl test routing"). Routing
  is verified by the parallel `test-linux` job (called by `build-linux` via `_test-linux.yml`).
  The `coverage-linux` job goes directly from build to `mkdir -p test_results` and ctest execution.

  The deployed step ordering for `coverage-linux` is:

  1. Checkout
  2. Detect compiler version (write `COMPILER_VERSION` to `$GITHUB_ENV`)
  3. `hendrikmuhs/ccache-action` — set up ccache with `-coverage` key suffix (reads `COMPILER_VERSION`)
  4. CMake configure (`cmake --preset ci-linux-coverage`)
  5. CMake build (`cmake --build build`)
  6. Create test results directory (`mkdir -p test_results`)
  7. Run unit tests ctest step
  8. Run integration tests ctest step
  9. Run OpenGL tests ctest step (xvfb)
  10. Fix git safe.directory for test reporter (`if: always()`)
  11. Publish test results (dorny/test-reporter, `if: always()`, `continue-on-error: true`)
  12. Upload test results (`if: always()`)
  13. Generate coverage report (lcov capture + filter + list + genhtml)
  14. Generate Sonar Generic Coverage XML (gcovr)
  15. Preflight src/simulation/ coverage entries (warning-only, deferred exit 1 until Phase 6)
  16. Enforce src/simulation/ 85% per-file floor (Phase 11, hard-fail)
  17. Enforce 95% total line coverage gate (hard-fail, Phase 6 applied)
  18. Enforce src/ui/ 25% worst-file coverage gate (hard-fail, Phase 4 applied)
  19. Check src/ui/ coverage completeness (`if: always()`)
  20. Upload Sonar coverage XML (`if: always()`)
  21. Upload coverage HTML (`if: always()`)
  22. Upload coverage info (`if: always()`)

  ```yaml
  - name: Verify unit test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -cE 'Test +#' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^unit$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Unit test routing verified: $count test(s) discovered."

  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -cE 'Test +#' || true)
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^integration$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."

  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -cE 'Test +#' || true)
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

    - name: Generate coverage report
      run: |
        BUILD_DIR=build
        # NOTE: In container jobs, use --base-directory . (NOT ${{ github.workspace }}).
        # ${{ github.workspace }} resolves to the HOST path (/__w/repo/repo) which differs
        # from the container working directory, causing path mismatches in coverage source
        # attribution. Use '.' (current directory) instead.
        lcov --capture --directory "${BUILD_DIR}" --base-directory . \
             --gcov-tool gcov-13 \
             --ignore-errors mismatch,inconsistent \
             --rc check_data_consistency=0 \
             --output-file coverage.info
        # Additional exclude patterns vs Phase 0 spec:
        # /opt/vcpkg_installed/* — container pre-baked vcpkg headers
        # $(pwd)/vcpkg_installed/* — workspace vcpkg fallback
        # $(pwd)/build/vcpkg_installed/* — build-dir vcpkg headers
        # */src/simulation/*.h, */src/ui/*.h, */src/interfaces/*.h — exclude inline headers
        #   from double-counting (the .cpp files are measured; .h files are defense-in-depth)
        # $(pwd)/.fetchcontent_cache/* — absolute form preferred over glob in containers
        # Note: "${BUILD_DIR}/_deps/*" intentionally absent — build/_deps/ never exists
        # with FETCHCONTENT_BASE_DIR=.fetchcontent_cache.
        lcov --remove coverage.info \
          --ignore-errors unused,inconsistent \
          --rc check_data_consistency=0 \
          '/usr/*' \
          '/opt/vcpkg_installed/*' \
          "$(pwd)/vcpkg_installed/*" \
          "$(pwd)/build/vcpkg_installed/*" \
          "$(pwd)/.fetchcontent_cache/*" \
          '*/tests/*' \
          '*/mock_*.h' '*/mock_*.cpp' \
          '*/manual_*.h' '*/manual_*.cpp' \
          '*/Mock*.h' '*/Mock*.cpp' \
          '*/Manual*.h' '*/Manual*.cpp' \
          '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
          '*/src/simulation/*.h' '*/src/ui/*.h' '*/src/interfaces/*.h' \
          --output-file coverage_filtered.info
        lcov --list coverage_filtered.info \
          --ignore-errors inconsistent \
          --rc check_data_consistency=0
        genhtml coverage_filtered.info --output-directory coverage_html/ \
          --ignore-errors inconsistent \
          --rc check_data_consistency=0

    - name: Generate Sonar Generic Coverage XML
      run: |
        gcovr --sonarqube coverage.xml \
          --gcov-executable gcov-13 \
          --exclude 'src/rendering/.*' \
          --exclude 'src/audio/.*' \
          --exclude 'src/platform/.*' \
          --exclude 'src/simulation/.*\.h' \
          --exclude 'src/ui/.*\.h' \
          --exclude 'src/interfaces/.*\.h'
        # NOTE: gcovr --exclude patterns must mirror lcov --remove patterns exactly.
        # Any new lcov --remove pattern must also be added to gcovr --exclude and vice versa.

    - name: Preflight src/simulation/ coverage entries
      run: |
        # WARNING-ONLY: fails if no src/simulation/ SF entries are found.
        # After Phase 6 implementation, change this step to exit 1 on missing simulation entries.
        if ! grep -q "SF:.*src/simulation/" coverage_filtered.info; then
          echo "WARNING: No src/simulation/ SF entries found in coverage_filtered.info — simulation coverage may be absent"
        fi

    - name: Enforce src/simulation/ 85% per-file floor (Phase 11)
      run: |
        # Parses coverage_filtered.info directly via SF/LH/LF records — version-agnostic.
        # Restricted to *.cpp$ (defense-in-depth: headers already excluded by lcov --remove).
        awk '
          /^SF:.*src\/simulation\/.*\.cpp$/ { file=$0; lh=0; lf=0; in_sim=1; next }
          in_sim && /^LH:/ { lh=substr($0,4)+0 }
          in_sim && /^LF:/ { lf=substr($0,4)+0 }
          in_sim && /^end_of_record/ {
            if (lf>0) { pct=lh/lf*100; if (min_pct=="" || pct<min_pct+0) { min_pct=pct; min_file=file } }
            in_sim=0
          }
          END {
            if (min_pct=="") { print "ERROR: no src/simulation/ .cpp entries found"; exit 1 }
            if (min_pct+0 < 85.0) {
              printf "FAIL: worst src/simulation/ file coverage %.1f%% < 85%% Phase 11 per-file floor\n", min_pct+0; exit 1
            }
            printf "PASS: src/simulation/ per-file floor %.1f%% >= 85%%\n", min_pct+0
          }
        ' coverage_filtered.info

    - name: Enforce 95% total line coverage gate
      run: |
        pct=$(lcov --summary coverage_filtered.info \
          --ignore-errors inconsistent --rc check_data_consistency=0 2>&1 \
          | grep 'lines' | grep -oP '[0-9]+\.[0-9]+(?=%)' | head -1)
        if [[ -z "$pct" ]]; then
          echo "ERROR: could not parse total line coverage from lcov --summary"; exit 1
        fi
        result=$(echo "$pct 95" | awk '{if ($1+0 < $2+0) print "FAIL"; else print "PASS"}')
        if [[ "$result" == "FAIL" ]]; then
          echo "ERROR: Total line coverage ${pct}% below 95% gate"; exit 1
        fi
        echo "PASS: Total line coverage ${pct}% >= 95%"

    - name: Enforce src/ui/ 25% worst-file coverage gate
      run: |
        # Direct SF/LH/LF parse — version-agnostic (lcov --list column format changed in 2.0).
        if ! grep -q "SF:.*src/ui/" coverage_filtered.info; then
          echo "ERROR: No src/ui/ coverage data found"; exit 1
        fi
        min_pct=$(awk '
          /^SF:.*src\/ui\// { in_ui=1; lh=0; lf=0; next }
          in_ui && /^LH:/ { lh=substr($0,4)+0 }
          in_ui && /^LF:/ { lf=substr($0,4)+0 }
          in_ui && /^end_of_record/ { if (lf>0) print (lh/lf)*100; in_ui=0 }
        ' coverage_filtered.info | sort -n | head -1)
        result=$(echo "$min_pct 25" | awk '{if ($1+0 < $2+0) print "FAIL"; else print "PASS"}')
        if [[ "$result" == "FAIL" ]]; then
          echo "ERROR: src/ui/ worst-file coverage ${min_pct}% below 25% Phase 4 gate"; exit 1
        fi
        echo "PASS: src/ui/ worst-file coverage ${min_pct}% >= 25%"
    ```

  **Note on coverage gate step separation**: The `_coverage-linux.yml` source implements each
  gate as a separate named YAML step (not inside a single opaque `run:` block). Separate steps
  give individual failure messages in the GitHub Actions UI — a critical operational advantage.
  The above YAML shows the deployed separate-step structure.

  **Phase 6 note on 95% gate**: The Phase 5 80% gate described in earlier spec revisions has
  been superseded. The deployed step is named "Enforce 95% total line coverage gate" and uses
  `if (pct+0 < 95.0)`. Phase 6 applied the threshold increase — the gate is now at 95%.

  **`coverage-linux` step ordering** (deployed, no routing checks — routing is verified by
  the parallel `test-linux` job): Checkout → Detect GCC version → Set up ccache →
  CMake configure → CMake build → Create test results directory →
  Run unit tests → Run integration tests → Run OpenGL tests (xvfb) →
  Fix git safe.directory → Publish test results (dorny) → Upload test results →
  Generate coverage report → Generate Sonar Generic Coverage XML →
  Preflight src/simulation/ → 85% per-file floor → 95% total gate → 25% UI gate →
  Check src/ui/ zero-hit files (`if: always()`) →
  Upload Sonar coverage XML → Upload coverage HTML → Upload coverage info.

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
      uses: dorny/test-reporter@a43b3a5f7366b97d083190328d2c652e1a8b6aa2  # v3.0.0 — pin to SHA
      if: always()
      continue-on-error: true  # Linux container job — see rationale in test-reporter section above
      with:
        name: 'Linux Coverage Tests'
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
      uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd  # v6.0.2

    - name: Run markdownlint
      run: npx markdownlint-cli@0.47.0 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

**Key properties**:

- `runs-on: ubuntu-latest` — Node.js/npm/npx are pre-installed; no additional setup required.
- `timeout-minutes: 5` — linting is fast (seconds); a 5-minute cap prevents runaway npm installs from consuming runner minutes.
- `permissions: contents: read` — the job only checks out and reads files; no artifact upload, no check annotations, no write access needed.
- The `npx markdownlint-cli@0.47.0` invocation pins the version inline — no prior `npm install -g` step is needed. The version pin is expressed inline in the `npx` command. `npx` is the correct invocation — the bare `markdownlint` command is NOT installed globally. Current pin: `@0.47.0`.
- The glob patterns cover all spec and documentation files: `architecture/**/*.md`, `implementation/*.md`, and `CLAUDE.md`. The shell expands these globs on `ubuntu-latest`. If the `implementation/` directory does not yet exist the glob silently matches nothing and the step passes — this is correct behavior for an empty phase.
- Exit code 1 on any violation — the job fails and blocks `all-checks-pass`.
- No caching step needed — `npx` for a single small package takes under 10 seconds.
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
        uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd  # v6.0.2 — verified
  ```

  The next step sets up Python 3 using `actions/setup-python`. The resolved SHA is
  `a309ff8b426b58ec0e2a45f0f869d46889d02405` (v6.2.0). All Python pip dependencies must be
  pinned to exact versions for reproducibility.

  **`python-version` MUST be set to a specific minor version (e.g., `'3.12'`) — NOT a floating major version (`'3'`).** Floating major versions break supply-chain reproducibility because `python-version: '3'` resolves to different patch versions on different runner instances — GitHub-hosted runners update their pre-installed Python over time, meaning the same workflow YAML can silently execute against `3.12.x` today and `3.13.x` next month.

  ```yaml
      - name: Set up Python 3
        uses: actions/setup-python@a309ff8b426b58ec0e2a45f0f869d46889d02405  # v6.2.0
        with:
          python-version: '3.12'  # pin to specific minor version — never use '3' (floating major breaks reproducibility)
  ```

  **Phase 5 Python dependencies**: the `validate-assets` job must install pinned Python packages
  and `ffmpeg` before running `validate_assets.py`. Add a pip install step immediately after the
  Python setup step:

  ```yaml
      - name: Install Python dependencies
        run: |
          pip install "mutagen==1.47.0" "Pillow==10.4.0"
          sudo apt-get install -y --no-install-recommends ffmpeg
  ```

  `mutagen==1.47.0` is required by checks #16–#19 for OGG/WAV duration and format inspection.
  `Pillow==10.4.0` is required by image-dimension and atlas checks. `ffmpeg` (via apt-get) is
  required for check_21 OGG decode. Pin all Python dependencies to exact versions for
  reproducibility — do NOT use `pip install mutagen` without a version pin.

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

  - Phase 11q12: Checks #1, #2, #3, #4, #4b, #6, #8, #10, #11, #15, and #32 (check #5
    skips PLY files per UV1 exemption) are **extended** to discover and validate `.ply`
    files alongside `.b3d`. The PLY-first mesh loading path introduced in Phase 11q12
    requires the asset validator to accept `_lodN.ply` filenames, read PLY geometry for
    triangle-count / UV / pivot validation, and treat a model set as complete when
    LOD0/1/2 are present in either format. Check #32 is extended (vehicle triangle budget
    extended for PLY path discovery). `check_32` is added to the numbered guard loop in
    `_validate-assets.yml` (after `check_31`); the header comment is updated from
    `check_31` to `check_32`. The checkout step in `_validate-assets.yml` must
    **not** use blanket `lfs: true` (that downloads ALL LFS objects including
    Tripo3D source zips/FBXs, bloating the job). Instead, add a selective LFS
    fetch step **after** checkout: `git lfs pull -I "assets/3d/**/*.ply"` — this
    fetches only the PLY geometry files tracked by Git LFS (`.gitattributes`:
    `assets/3d/**/*.ply filter=lfs ...`) as real geometry data. Without this
    step, content-parsing checks (3, 4, 4b, 6, 8, 32) receive ~130-byte LFS
    pointer stubs instead of actual PLY geometry. The same selective-fetch
    pattern applies to `_package-linux-deb.yml` and `_package-windows.yml` —
    use `git lfs pull -I "assets/3d/**/*.ply"` after checkout instead of
    blanket `lfs: true`. No change to `all-checks-pass` wiring.

    Selective LFS fetch step (add to `validate-assets` job immediately after the
    `actions/checkout` step and before the Python/pip install step):

    ```yaml
        - name: Fetch PLY geometry files from Git LFS
          run: git lfs pull -I "assets/3d/**/*.ply"
    ```

    Guard steps (add to `validate-assets` job after the existing check_31 guard, before
    `Run asset validation`):

    ```yaml
        - name: Verify check_32 present in validate_assets.py
          # check_32: vehicle per-class triangle budget (LOD0/LOD1) — extended in Phase 11q12 for PLY path discovery.
          # A missing check_32 allows over-budget vehicle meshes to pass CI silently.
          run: |
            grep -q "check_32" tools/validate_assets.py || \
              (echo "FAIL: check_32 not found in validate_assets.py — Phase 11q12 vehicle triangle budget gate missing" && exit 1)
            echo "PASS: check_32 present"

        - name: Verify PLY validation present in validate_assets.py
          # Phase 11q12: checks 1-4, 4b, 6, 8, 10, 11, 15, 32 extended for PLY format discovery (check 5 skips PLY per UV1 exemption).
          # A missing PLY path allows .ply assets to bypass validation silently.
          run: |
            grep -q '\.ply' tools/validate_assets.py || \
              (echo "FAIL: PLY validation not found in validate_assets.py — Phase 11q12 PLY discovery gate missing" && exit 1)
            echo "PASS: PLY validation present"
    ```

### PHASE 0 FORM (validate-assets not yet introduced)

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  # Phase 0 form — validate-assets job does not exist yet; add it in Phase 1.
  # compute-version intentionally excluded — skipped on PRs and workflow_dispatch.
  needs:
    - prepare
    - supply-chain-lint
    - build-linux
    - build-windows
    - coverage-linux
    - markdown-lint
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
          "${{ needs.prepare.result }}"
          "${{ needs.supply-chain-lint.result }}"
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

When the `validate-assets` job is added in Phase 1, update `all-checks-pass` to include it
simultaneously. The job runs `tools/validate_assets.py`, which is a stub that always exits 0
at Phase 1. Wiring it in now means the `needs:` list requires no further changes in Phase 5
(real checks), Phase 9 (Check \#15 full implementation and Check \#20), or Phase 10 (Check
\#21 zone loop silence-floor) — only the script content changes, not the CI wiring.

**`compute-version` is deliberately excluded from `needs:`**: `compute-version` is skipped
on `workflow_dispatch` and pull-request triggers (its `if:` condition only matches `push`
to `main`/`develop`). If `compute-version` were listed in `needs:`, a manual re-trigger or
a PR run would produce a `skipped` result that the bash gate array treats as non-`success`,
causing the gate to fail on every PR. Excluding `compute-version` from `needs:` keeps the
gate green on all non-push events while still blocking merges when build/test jobs fail.

**`prepare` is included in `needs:`**: `prepare` is a lightweight (~2 s) job that exports
`VCPKG_COMMIT_ID` as a job output so reusable-workflow `with:` blocks can reference it via
the `needs` context. It runs on every trigger including `workflow_dispatch`. Including it in
`all-checks-pass` ensures the export step itself is healthy.

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  # compute-version intentionally excluded — skipped on PRs and workflow_dispatch,
  # which would cause the gate to fail on those triggers.
  needs:
    - prepare
    - supply-chain-lint
    - validate-assets
    - build-linux
    - build-windows
    - coverage-linux
    - markdown-lint
    - asan-linux
  steps:
    - name: Verify all platform builds passed
      shell: bash   # REQUIRED: Bash arrays are used; default shell on ubuntu-latest is bash but must be explicit
      run: |
        # Explicit check: only "success" is acceptable.
        # "skipped" and "cancelled" are treated as failures — GitHub branch protection
        # considers a "skipped" gate job as passing unless this gate explicitly exits 1.
        # NOTE: This step uses a Bash array — it MUST run on ubuntu-latest with shell: bash.
        # Do NOT run the all-checks-pass job on windows-latest; Bash arrays are not compatible
        # with PowerShell (the Windows default shell). The job is ubuntu-latest by design.
        results=(
          "${{ needs.prepare.result }}"
          "${{ needs.supply-chain-lint.result }}"
          "${{ needs.validate-assets.result }}"
          "${{ needs.build-linux.result }}"
          "${{ needs.build-windows.result }}"
          "${{ needs.coverage-linux.result }}"
          "${{ needs.markdown-lint.result }}"
          "${{ needs.asan-linux.result }}"
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

## `prepare` Job

The `prepare` job is a lightweight (~2 s) job that exports `VCPKG_COMMIT_ID` as a job output so
reusable-workflow `with:` blocks can reference it via the `needs` context. The `env` context is
unavailable in `jobs.<id>.with` blocks for reusable workflows — values must be passed explicitly
via job outputs.

- **Runner**: `ubuntu-latest`
- **Timeout**: `timeout-minutes: 2`
- **Permissions**: `contents: none`
- **Runs on all triggers** including `workflow_dispatch`

Pattern:

```yaml
prepare:
  runs-on: ubuntu-latest
  timeout-minutes: 2
  permissions:
    contents: none
  outputs:
    vcpkg_commit_id: ${{ steps.export.outputs.vcpkg_commit_id }}
  steps:
    - name: Export vcpkg_commit_id
      id: export
      run: echo "vcpkg_commit_id=${{ env.VCPKG_COMMIT_ID }}" >> $GITHUB_OUTPUT
```

Downstream jobs consume this via `${{ needs.prepare.outputs.vcpkg_commit_id }}` in `with:` blocks.
Including `prepare` in `all-checks-pass` ensures the export step itself is healthy.

## Versioning

Version is computed from git tags only — no source file is modified by the pipeline.

### `compute-version` job

A single `compute-version` job handles both `main` and `develop` pushes, replacing the old
`bump-version` + `develop-version` two-job design. Using a single job eliminates the
skipped-job propagation problem: when two branch-conditional jobs are both listed in
`needs:`, a skipped job (because its `if:` condition did not match) propagates `skipped`
status to dependents, which propagates to `all-checks-pass`. A single job with internal
branch logic avoids this entirely.

- **Trigger**: `if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`
  - Skipped on all pull requests and on `workflow_dispatch` (manual re-trigger) — this is
    intentional. `all-checks-pass` does NOT include `compute-version` in its `needs:` list
    so that `workflow_dispatch` runs (where `compute-version` is skipped) still pass the gate.
- **Runner**: `ubuntu-latest`
- **Timeout**: `timeout-minutes: 5`
- **Permissions**: `contents: write` (required for tag creation in the `release` job; the
  permission is declared here because the job outputs the version used by `release`)
- **Outputs**: `version` — the version string used by downstream packaging and release jobs

#### On `main`

1. Checkout with `fetch-depth: 0` (full history required for `git describe`)
2. Read latest `v[0-9]*` tag via `git describe --tags --abbrev=0`; increment patch. Default
   fallback env vars: `DEFAULT_MAIN: v0.0.21` and `DEFAULT_DEVELOP: v0.0.0` are declared as
   named env vars (not magic literals) — update these values whenever the pipeline is reset after
   a history rewrite.
3. **Floor comparison (main only)**: if `DEFAULT_MAIN` is higher than what `git describe` found
   (e.g., after a deliberate history rewrite that removed old tags), `CURRENT` is replaced with
   `DEFAULT_MAIN`. This prevents version regression and is the key mechanism that ensures CI does
   not fail after a tag history rewrite.
   → outputs e.g. `0.0.22`

No tag is created here. Tag creation happens in the `release` job (main only) via
`gh release create --target`.

#### On `develop`

1. Checkout with `fetch-depth: 0`
2. Read latest `v[0-9]*` tag and append `-develop` suffix; default fallback `DEFAULT_DEVELOP: v0.0.0`
   (not `v0.0.1`). No floor comparison on develop. → outputs e.g. `0.1.1-develop`

No tag is created; no files are modified. The base is always the latest released tag
shared across `main` and `develop`.

#### Versioning contract

- Semantic versioning `MAJOR.MINOR.PATCH`. Only patch is auto-incremented by the pipeline.
- Git tags (`v<MAJOR>.<MINOR>.<PATCH>`) are the sole authoritative release markers.
  `CMakeLists.txt` is NOT modified and does NOT carry the live version.
- `develop` packages are pre-release artifacts identified by the `-develop` suffix.
  No GitHub release is created for `develop` pushes.
- To increment MAJOR or MINOR, manually push a tag (e.g. `git tag v1.0.0 && git push
  origin v1.0.0`) before the next merge; the pipeline increments patch from that base.
- The version is passed to packaging jobs via `needs.compute-version.outputs.version`.

---

## `package-windows` Job

Produces an NSIS-based `.exe` installer via CPack. Runs only on push to `main` or `develop`
(`if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`).
Does NOT run on pull requests. Does NOT block `all-checks-pass`.

- **Runner**: `windows-latest`
- **Timeout**: `timeout-minutes: 60`
- **Permissions**: `contents: read`
- **Dependencies**: `needs: [compute-version, build-windows]` — version from
  `needs.compute-version.outputs.version`

### Package version injection

The version is passed to CMake/CPack via `-DCPACK_PACKAGE_VERSION=<version>` at configure
time — do NOT modify `CMakeLists.txt`. The version flag MUST be quoted on the command line
to prevent CMake from misinterpreting the hyphen in `0.0.0-develop` as a source path:

```yaml
"-DCPACK_PACKAGE_VERSION=${{ needs.compute-version.outputs.version }}"
```

`CMakeLists.txt` MUST guard the variable with `if(NOT CPACK_PACKAGE_VERSION)` so the CI
override takes effect:

```cmake
if(NOT CPACK_PACKAGE_VERSION)
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
endif()
```

### Secure input handling in reusable workflows

`workflow_call` inputs interpolated directly into `run:` shell blocks are a script-injection
vector (SonarCloud `githubactions:S7630`). The caller controls input values; malicious or
malformed strings can inject arbitrary shell commands.

**Rule**: never use `${{ inputs.<name> }}` directly inside a `run:` block. Instead, assign the
input to a step-level `env:` variable and reference it from the shell.

**Bash pattern** (Linux steps in `_package-linux-deb.yml`):

```yaml
- name: Set up vcpkg
  env:
    VCPKG_COMMIT_ID: ${{ inputs.vcpkg_commit_id }}
  run: |
    git -C /opt/vcpkg fetch --depth=1 origin "$VCPKG_COMMIT_ID"

- name: Resolve package version
  id: pkgver
  env:
    PKG_VERSION: ${{ inputs.pkg_version }}
  run: echo "version=$PKG_VERSION" >> "$GITHUB_OUTPUT"
```

**PowerShell pattern** (Windows steps in `_package-windows.yml`):

```yaml
- name: Resolve package version
  id: pkgver
  shell: pwsh
  env:
    PKG_VERSION: ${{ inputs.pkg_version }}
  run: echo "version=$env:PKG_VERSION" >> $env:GITHUB_OUTPUT
```

This pattern applies to **every** `workflow_call` input used in a `run:` block across all
reusable packaging workflows. `${{ inputs.* }}` expressions ARE safe in non-`run:` fields
(e.g., `with:`, `cache key`, `container:`) because those fields do not invoke a shell interpreter.

### No-rebuild principle

`package-windows` **must not recompile** — it downloads the pre-built staging artifact
produced by `build-windows` and passes it directly to CPack. cmake configure is still
required (generates `CPackConfig.cmake` and the install rules); only `cmake --build` is
omitted.

`build-windows` must upload a staging artifact (`aitown-staging-windows-${{ github.sha }}`)
on every push to `main` **or** `develop` (not only `main`) containing:

- `build/aitown.exe`
- all `build/*.dll` files (GLEW, soft_oal, vcpkg runtime DLLs)
- `build/default.mhr`

### Step sequence

1. Checkout (`actions/checkout` SHA-pinned)
2. Install NSIS via Chocolatey (`choco install nsis --no-progress -y`)
3. `ilammy/msvc-dev-cmd` (SHA-pinned) — vcvarsall for CMake configure
4. `lukka/run-vcpkg` (SHA-pinned) — restore vcpkg packages (needed for cmake configure)
5. Resolve package version (echo `inputs.pkg_version` to `$GITHUB_OUTPUT`)
6. CMake configure with `-DAITOWN_ASSETS_DIR=assets -DBUILD_TESTING=OFF "-DCPACK_PACKAGE_VERSION=<version>"`
   — generates `CPackConfig.cmake` and install rules; **no build step follows**; version flag
   MUST be quoted (step 5 resolution; see "Package version injection" above)
7. Append `build\vcpkg_installed\x64-windows\bin` to `$env:GITHUB_PATH`
8. Download staging artifact `aitown-staging-windows-${{ github.sha }}` into `build/`
   — restores `aitown.exe`, DLLs, and `default.mhr` produced by `build-windows`
9. CPack — `cpack -G NSIS -C Release` (run inside `build/`)
10. Upload installer artifact (`name: aitown-installer-windows-<sha>`, `retention-days: 30`)

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
Version injection follows the same pattern as `package-windows` — `-DCPACK_PACKAGE_VERSION=<version>`
at CMake configure time, using `needs.compute-version.outputs.version` (passed via the
`pkg_version` reusable-workflow input).

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
- **Timeout**: `timeout-minutes: 90` (apt installs in bare containers are slower than
  on GitHub-hosted runners; vcpkg-tool build-from-source adds ~15 min on first run)
- **Permissions**: `contents: read`
- **Dependencies**: called by `ci.yml` via `uses: ./.github/workflows/_package-linux-deb.yml`
  with `vcpkg_commit_id` and `pkg_version` inputs; the caller sets
  `needs: [build-linux, prepare, compute-version]`

### No-rebuild principle

`package-linux-deb` **must not recompile** — it downloads the pre-built binary produced by
`build-linux` and passes it to CPack after cmake configure. cmake configure is still required
(generates `CPackConfig.cmake` and the install rules); only `cmake --build` is omitted.

`build-linux` must upload a staging artifact (`aitown-staging-linux-${{ github.sha }}`) on
every push to `main` **or** `develop` (not only main) containing:

- `build/aitown` (the compiled binary)
- `build/default.mhr` (HRTF data file — required at runtime by the OpenAL Soft audio system)

The same binary is used for all four distro matrix legs (bookworm, trixie, jammy, noble).
This is acceptable because the binary is linked against vcpkg-managed libraries and the
system-lib dependency set is stable across supported distros.

### Step sequence

1. Install system build dependencies (`apt-get`: `build-essential cmake ninja-build git
   curl zip unzip tar pkg-config autoconf automake libtool libgl1-mesa-dev libglu1-mesa-dev
   libx11-dev libxrandr-dev libxinerama-dev libxxf86vm-dev libopenal-dev libvorbis-dev
   python3 dpkg-dev fakeroot`)
2. Checkout (`actions/checkout` SHA-pinned)
3. Set up vcpkg — clone + `git checkout $VCPKG_COMMIT_ID` + **build vcpkg-tool from source**
   (prebuilt vcpkg-tool binaries from GitHub Releases return 404 in bare containers because
   the CDN glibc asset is unavailable; `bootstrap-vcpkg.sh` has no fallback and calls
   `exit 1` on `curl` failure; building from source via the `vcpkg-tool` CMake project is
   the only reliable path in distro containers without the standard runner preinstalls)
4. Install vcpkg packages: `vcpkg install --triplet x64-linux --overlay-ports=vcpkg-overlays`
5. Resolve package version (echo `inputs.pkg_version` to `$GITHUB_OUTPUT`)
6. CMake configure with `-G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_COVERAGE=OFF
   -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr
   -DAITOWN_ASSETS_DIR=/usr/share/aitown/assets -DCMAKE_TOOLCHAIN_FILE=...
   -DVCPKG_OVERLAY_PORTS=vcpkg-overlays -DVCPKG_MANIFEST_FEATURES=""
   -DVCPKG_INSTALLED_DIR=$VCPKG_ROOT/installed
   -DCPACK_PACKAGE_VERSION=<version>
   "-DCPACK_PACKAGE_FILE_NAME=aitown-<version>-<codename>"`
   — generates `CPackConfig.cmake` and install rules; **no build step follows**;
   `DCPACK_PACKAGE_FILE_NAME` embeds the distro codename so all four matrix legs produce
   differently named `.deb` files (avoids overwriting each other in the release)
7. Download staging artifact `aitown-staging-linux-${{ github.sha }}` into `build/`
8. Set executable bit: `chmod +x build/aitown`
9. CPack DEB: `cpack -G DEB` (run inside `build/`)
10. Upload `.deb` artifact (`name: aitown-deb-<distro>-<sha>`, `retention-days: 30`)

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

## `bump-version` and `develop-version` Jobs (deprecated)

These two jobs have been replaced by the single `compute-version` job. See the
`## Versioning` section above for the current design. The old `bump-version` job pushed
tags via `git push origin`, which fails with a GH013 pre-receive hook violation on this
repository; the replacement uses a REST API call in the `release` job instead.

---

## `release` Job

Creates a GitHub release with attached installer and `.deb` packages. **Only runs on `main`
— develop pushes never produce a GitHub release.** `develop` builds produce packages (with
`-develop` version suffix) but no release is published.

- **Trigger**: `if: github.event_name == 'push' && github.ref == 'refs/heads/main'`
- **Runner**: `ubuntu-latest`
- **Timeout**: `timeout-minutes: 10`
- **Permissions**: `contents: write` (required to create releases and push release tags)
- **Dependencies**: `needs: [compute-version, package-windows, package-linux-deb]`

### Step sequence

1. Checkout with `fetch-depth: 0` — required for `git log` changelog generation
2. Download Windows installer: `aitown-installer-windows-${{ github.sha }}` → `./release-assets/`
3. Download Linux .deb (bookworm): `aitown-deb-debian-bookworm-${{ github.sha }}` → `./release-assets/`
4. Download Linux .deb (trixie): `aitown-deb-debian-trixie-${{ github.sha }}` → `./release-assets/`
5. Download Linux .deb (jammy): `aitown-deb-ubuntu-jammy-${{ github.sha }}` → `./release-assets/`
6. Download Linux .deb (noble): `aitown-deb-ubuntu-noble-${{ github.sha }}` → `./release-assets/`
7. Generate changelog: an `extract()` shell function calls
   `git log "$RANGE" --pretty=format:"%s"` with conventional-commit prefix filtering (`feat`,
   `fix`, `ci`, `docs`, `test`, `chore`) to group commits into Markdown sections (Features,
   Bug Fixes, CI/CD, Tests, Documentation, Chores). Results are written to `/tmp/changelog.md`.
   Falls back to `"Initial release."` when no prior tag exists (`PREV_TAG` empty). The step
   outputs `prev_tag` to `$GITHUB_OUTPUT`.
8. Create GitHub release using the GitHub CLI `gh release create` in a 10-iteration idempotent
   retry loop. On each iteration: look up the existing release by tag; delete it and its orphan
   tag if found; then call:

   ```bash
   gh release create "${TAG}" \
     --title "AI Town ${TAG}" \
     --notes-file /tmp/changelog.md \
     --target "${{ github.sha }}" \
     release-assets/**
   ```

   The `--target "${{ github.sha }}"` flag pins the release to the exact commit, preventing a
   tag/commit mismatch if the tag was pre-created. The `release-assets/**` glob covers all
   downloaded package artifacts (Windows installer + four `.deb` files) whose filenames contain
   the computed version string and cannot be hard-coded at authoring time — this is one of the
   three approved wildcard exceptions to the anti-wildcard artifact rule (see artifact upload
   section). When an immutable-release conflict is detected (error message contains
   `tag_name was used by an immutable release`), the loop increments the patch version component
   and retries with the new tag.

   `softprops/action-gh-release` is NOT used. Both `git push` tag creation and
   `softprops/action-gh-release` fail with GH013 pre-receive hook violations on this repository;
   the `gh release create` CLI call creates the git tag and GitHub Release atomically in a single
   API call and bypasses the pre-receive hook.

   **`compute-version` does NOT create or push a git tag.** The job produces only a version
   string output (`$GITHUB_OUTPUT`). Tag creation is deferred to the `release` job via
   `gh release create --target` (atomic tag + GitHub Release). Develop pushes produce a
   `-develop`-suffixed version with no corresponding tag at all.

### Tag-creation design notes

- Tag is NOT created in `compute-version` — `compute-version` runs on both `main` and
  `develop`, but a release tag should only be created on `main`. Creating the tag in
  `release` (main only) keeps the responsibility co-located with the release step.
- Idempotent retry loop: up to 10 iterations; deletes the existing release and tag before
  recreating to handle transient failures without leaving orphaned releases.

### Gate status

`compute-version`, `package-windows`, `package-linux-deb`, and `release` are NOT in
`all-checks-pass` `needs:`. Failures in these jobs must not block PR merges — investigate
before the next merge to `main`.

---

## `docker-ci-image.yml` — CI Image Build Workflow

Manages the `ghcr.io/m0wa/aitown-ci-linux` container image lifecycle.

**Triggers**:

- Push to `main`/`develop` touching `docker/ci-linux/Dockerfile`, `vcpkg.json`, or `vcpkg-overlays/**`
- `workflow_dispatch` with `force_rebuild` boolean input (manual rebuild without a code change)
- Monthly schedule: `cron: '0 2 1 * *'` (1st of month, 02:00 UTC)

**Permissions**: `packages: write`, `contents: read`

**Timeout**: `timeout-minutes: 120` — a cold vcpkg build in Docker takes 30+ minutes on cache
miss; 120 min provides sufficient headroom.

**Key steps**:

1. Checkout
2. Extract `VCPKG_COMMIT_ID` from `ci.yml` env block and write to `$GITHUB_ENV` (separate step
   — `$GITHUB_ENV` writes are NOT visible within the same step)
3. Validate that `ARG VCPKG_COMMIT` exists in `docker/ci-linux/Dockerfile` WITHOUT a default
   value (the ARG must be explicit so the digest is always specified at build time)
4. Run an inline supply-chain lint (validates SHAs in ci.yml before building the image)
5. Compute `VCPKG_SHORT_SHA` (first 7 chars of `VCPKG_COMMIT_ID`) and write to `$GITHUB_ENV`
   (must be a SEPARATE step — demonstrates the `$GITHUB_ENV` step-ordering visibility rule)
6. `docker/login-action@4907a6ddec9925e35a0a9e82d7399ccc52663121` (v4.1.0) — GHCR login
7. `docker/setup-buildx-action@4d04d5d9486b7bd6fa91e7baf45bbb4f8b9deedd` (v4.0.0) — BuildKit
8. `docker/build-push-action@d08e5c354a6adb9ed34480a06d141179aa583294` (v7.0.0) — build and
   push with tag `vcpkg-${VCPKG_SHORT_SHA}` and GHA layer cache:
   `cache-from: type=gha`, `cache-to: type=gha,mode=max`
   (`mode=max` caches ALL intermediate layers — required because the expensive vcpkg compilation
   layer is deep in the image; `mode=min` would miss it, producing cold-cache build times on every run)
9. Print image digest and update instructions — the `steps.docker_build.outputs.digest` value
   (sha256) must be pinned in: (1) `.github/workflows/_build-linux.yml`, (2) `_test-linux.yml`,
   (3) `_coverage-linux.yml`, (4) `_asan-linux.yml`, and (5) `.devcontainer/Dockerfile`. The
   digest output is the authoritative source for the pin values.

**Image tag format**: `ghcr.io/m0wa/aitown-ci-linux:vcpkg-${VCPKG_SHORT_SHA}`

**Known issue — stale echo in "Print image digest" step**: the step currently references
`test-container-xvfb` job (which no longer exists). The correct jobs to update after an image
rebuild are: `_build-linux.yml`, `_test-linux.yml`, `_coverage-linux.yml`,
`_asan-linux.yml`, and `.devcontainer/Dockerfile`.

**Secure download policy**: All file downloads in `docker/ci-linux/Dockerfile` and
`.devcontainer/Dockerfile` must use `curl` (not `wget`) with `--proto '=https' --tlsv1.2`.
`wget --https-only` is **not** a valid substitute — it applies only to recursive crawls and
does not enforce HTTPS in non-recursive mode. Use `curl --proto '=https' --tlsv1.2 --fail
--location` for all artifact downloads. `npm install -g` calls must include `--ignore-scripts`
to prevent execution of lifecycle scripts from transitive dependencies.

---

## `sonarcloud.yml` — SonarCloud Analysis Workflow

Performs static analysis with coverage data for SonarCloud.

**Triggers**:

- `workflow_run` on CI completion — `if: ${{ github.event_name == 'workflow_dispatch' || github.event.workflow_run.conclusion == 'success' }}`. The `Analysis` job is entirely SKIPPED when the triggering CI run failed (no coverage XML artifact exists). `workflow_dispatch` bypasses this filter.
- `workflow_dispatch` with one required input: `ci_run_id` (CI run ID to pull artifacts from —
  used as both the `run-id:` lookup and the artifact name suffix)

**Security**: `workflow_run` executes with base-branch secrets. NEVER check out the PR HEAD
(untrusted contributor code). Check out base branch only; coverage data comes from the
already-vetted CI artifact. Adding a PR HEAD checkout to this workflow is a security vulnerability
that would expose `SONAR_TOKEN` to contributor code.

**Permissions**: `pull-requests: read`

**Top-level env vars**:

- `SONAR_SCANNER_VERSION: 7.0.2.4839`
- `SONAR_SCANNER_BINARIES_URL: https://binaries.sonarsource.com/Distribution/sonar-scanner-cli`

**Key steps**:

1. Checkout base branch
2. Download `coverage-sonar-linux-<run-id>` artifact (Sonar Generic Coverage XML from `_coverage-linux.yml`)
3. Download `compile-commands-linux-<run-id>` artifact (normalized `compile_commands.json` from `_build-linux.yml`)
4. "Debug compile_commands.json paths" — permanent inline Python diagnostic step (NOT
   debug-mode-only) that prints `GITHUB_WORKSPACE`, first entry's fields, and missing-file count
5. Cache SonarScanner CLI at `${{ runner.temp }}/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-Linux-X64`
6. Install SonarScanner CLI on cache miss: downloads from `${SONAR_SCANNER_BINARIES_URL}/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-linux-x64.zip` (lowercase `linux-x64`), then `mv` to capitalize → `Linux-X64`. The rename is required so the path matches the `actions/cache` key — a mismatch silently breaks cache hits.
7. Inline Python remap of `/__w/<owner>/<repo>` container paths to `${{ github.workspace }}` in the downloaded `compile_commands.json`
8. Run SonarScanner with:
   - `sonar.sources=src,.github/workflows,docker,.devcontainer`
   - `sonar.tests=tests`
   - `sonar.exclusions="assets/**,build/**,tools/**"`
   - `sonar.cpd.exclusions="tests/**"`
   - `sonar.cfamily.compile-commands=compile_commands.json`

**Two-stage path normalization**: (1) `normalize_compile_commands.py` called from `_build-linux.yml`
strips container-internal path prefixes during build; (2) `sonarcloud.yml` applies a second
`/__w/` → `${{ github.workspace }}` inline Python remap on the downloaded artifact. Both stages
must be updated if the container workspace path changes.

---

## `sonarcloud-debug.yml` — SonarCloud Diagnostic Workflow

`workflow_dispatch`-only diagnostic workflow. Same input as `sonarcloud.yml`
(`ci_run_id`). Downloads only `compile-commands-linux-<run-id>` (no coverage XML),
prints path counts before/after the `/__w/` → `$GITHUB_WORKSPACE` remap (identical Python
logic to `sonarcloud.yml`), reports missing-file counts. Does NOT run SonarScanner and requires
no `SONAR_TOKEN`.

**Permissions**: `pull-requests: read`

**No `timeout-minutes`** — gap: recommend adding 10 minutes.

**Uses**: `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd` (v6.0.2) and
`actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131` (v7.0.0).

**Intended use**: triggered manually when `sonarcloud.yml` fails path resolution to diagnose
whether the container-path remap is producing valid workspace-relative paths.

---

## `flawfinder.yml` — Security Analysis Workflow

Static security analysis via `david-a-wheeler/flawfinder`. Uploads SARIF to the GitHub
Security tab. Runs independently of the main CI pipeline (NOT in `all-checks-pass`).

**Triggers**: push/PR to `main` ONLY (not `develop` — scans only release-track code).
Weekly schedule: `cron: '26 9 * * 6'` (Saturdays, 09:26 UTC).

**Job-level permissions**: `security-events: write` (required for SARIF Security tab upload),
`contents: read`, `actions: read`.

**No `timeout-minutes`** — gap: recommend adding a value (e.g., 15 minutes).

**SHA-pinned actions**:

- `david-a-wheeler/flawfinder@8e4a779ad59dbfaee5da586aa9210853b701959c` — arguments: `'--sarif ./'` (scans entire repo root recursively)
- `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1)

---

## `msvc.yml` — MSVC Static Analysis Workflow

Full MSVC code analysis workflow using `microsoft/msvc-code-analysis-action`. Uploads SARIF
to the GitHub Security tab. Runs independently of the main CI pipeline (NOT in `all-checks-pass`).

**Triggers**: push/PR to `main` and weekly schedule: `cron: "33 13 * * 0"` (Sundays 13:33 UTC).
The two security-analysis workflows run on different days to stagger runner usage.

**Independent `VCPKG_COMMIT_ID`**: `msvc.yml` declares `VCPKG_COMMIT_ID` in its own `env:` block
(top-level workflow, not a reusable workflow). This is the correct pattern — top-level workflows
use `${{ env.VCPKG_COMMIT_ID }}` directly. **MAINTENANCE RISK**: `msvc.yml`'s `VCPKG_COMMIT_ID`
does NOT inherit from `prepare` and must be updated MANUALLY on every vcpkg baseline bump. This
is an additional item in the vcpkg baseline atomicity checklist (see `dependency-management.md`).

**SHA-pinned actions**:

- `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99`
- `github/codeql-action/upload-sarif@0e9f55954318745b37b7933c693bc093f7336125` (v4.35.1)
- `actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f` (v7.0.0) — "Upload SARIF as
  an Artifact" step: artifact name `sarif-file`, no explicit `retention-days` (gap: recommend
  7–14 days for a debug artifact).

**Version inconsistency**: `flawfinder.yml` uses `codeql-action/upload-sarif` v3.35.1
(`5c8a8a64...`) while `msvc.yml` uses v4.35.1 (`0e9f5595...`). Recommend standardising on v4
(v4.35.1) in a future maintenance pass.

**SARIF multi-run filter**: `msvc.yml` filters SARIF output to a single run (required by
`codeql-action/upload-sarif` which rejects multi-run SARIF).

**Job-level permissions**: `security-events: write`, `actions: read`, `contents: read`.

---

## `_package-linux-deb.yml` — Linux DEB Packaging Workflow

Produces `.deb` packages for four Debian/Ubuntu distros. The matrix strategy is defined INSIDE
the reusable workflow (not in the `ci.yml` caller).

**Checkout placement**: this is the ONLY workflow where checkout is NOT the first step. Bare
Debian/Ubuntu containers do not have `git` pre-installed. The `apt-get install` step (which
installs `git`) must run BEFORE `actions/checkout`. The checkout step also uses the bare `uses:`
form (no `name:` field) — recommend adding `name: Checkout code` for consistency.

**vcpkg bootstrap from source**: CDN prebuilt binaries return 404 in bare containers (`bootstrap-vcpkg.sh`
calls `exit 1` on curl failure). The `vcpkg-tool` CMake project is built from source. The tool
version is determined from `scripts/vcpkg-tool-metadata.txt` (self-consistent — always matches
the fetched vcpkg commit). Uses `if [ ! -d /opt/vcpkg/.git ]` pattern before `git init && git fetch
--depth=1 origin $VCPKG_COMMIT_ID && git checkout FETCH_HEAD` — handles `actions/cache` restoring
`/opt/vcpkg/packages/` before the setup step pre-creates the parent directory (causing plain
`git clone` to fail on a non-empty directory).

**vcpkg cache**: `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830` (v4.3.0) —
path: `/opt/vcpkg/packages`, key includes `${{ matrix.codename }}` for distro-level ABI isolation.

**Step sequence** (after apt-get):

1. (apt-get install — git + build deps + `git-lfs` from Phase 11q12 onward:
   bare Debian/Ubuntu containers do not ship `git-lfs`; it must be added to the
   same `apt-get install` step so that the selective LFS fetch in step 2a works)
2. Checkout
2a. (Phase 11q12) Selective LFS fetch: `git lfs pull -I "assets/3d/**/*.ply"` —
    fetches only PLY geometry files; blanket `lfs: true` must NOT be used because
    it downloads all LFS objects (including Tripo3D source zips/FBXs), which would
    be packaged into the `.deb` via the `install(DIRECTORY assets/ ...)` CPack rule
3. Cache vcpkg packages
4. vcpkg bootstrap from source + install (`--overlay-ports=vcpkg-overlays` for openal-soft 1.23.1 pin)
5. CMake configure (for CPack metadata only — no build step); uses `ci-linux` preset
6. Download `aitown-staging-linux-${{ github.sha }}` into `build/`
7. Set executable bit: `chmod +x build/aitown` (required — `actions/upload-artifact` strips permissions)
8. CPack DEB: `cpack -G DEB`
9. Upload `.deb` artifacts (four per matrix leg, `retention-days: 30`)

**Artifacts**: `aitown-deb-debian-bookworm-${{ github.sha }}`, `aitown-deb-debian-trixie-${{ github.sha }}`, `aitown-deb-ubuntu-jammy-${{ github.sha }}`, `aitown-deb-ubuntu-noble-${{ github.sha }}`

**`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`**: NOT set in this workflow — known inconsistency with
`_build-windows.yml`. Recommend adding in a maintenance pass.

---

## `_package-windows.yml` — Windows NSIS Packaging Workflow

Produces an NSIS `.exe` installer via CPack.

**Configure-only CMake**: runs `cmake --preset ci-windows -DAITOWN_ASSETS_DIR=assets -DBUILD_TESTING=OFF`
for metadata only — does NOT build binaries. Purpose: produce `CMakeCache.txt` so CPack can
locate install rules and NSIS script templates.

**PATH append**: uses `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append` (NOT bare `>>`
which writes UTF-16 LE with BOM in PowerShell 5.1, corrupting `$GITHUB_PATH`).

**Step sequence**:

1. Checkout
1a. (Phase 11q12) Selective LFS fetch: `git lfs pull -I "assets/3d/**/*.ply"` —
    fetches only PLY geometry files; blanket `lfs: true` must NOT be used because
    it downloads all LFS objects (including Tripo3D source zips/FBXs), which would
    be packaged into the NSIS installer via the `install(DIRECTORY assets/ ...)`
    CPack rule. No `git-lfs` install step is needed — `windows-latest` runners
    ship git-lfs pre-installed (unlike bare Debian/Ubuntu containers)
2. `choco install nsis --no-progress -y` (only workflow using Chocolatey)
3. `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` — vcvarsall
4. `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1` — restore vcpkg
5. Resolve package version
6. CMake configure (metadata only)
7. Append vcpkg bin to `GITHUB_PATH` via `Out-File -Encoding utf8 -Append`
8. Download `aitown-staging-windows-${{ github.sha }}` into `build/`
9. "Verify aitown.exe in staging artifact" (`Test-Path "build\aitown.exe"` hard-fail)
10. `cpack -G NSIS -C Release`
11. Upload installer: `name: aitown-installer-windows-${{ github.sha }}`, `path: build/aitown-*.exe`, `retention-days: 30`

**`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`**: NOT set — oversight (contrast with `_build-windows.yml`).
Recommend adding in a maintenance pass.

**No `actions/cache` step for vcpkg** — relies on `lukka/run-vcpkg` internal caching only.
Cold-cache packaging runs may approach the 60-minute timeout ceiling.
