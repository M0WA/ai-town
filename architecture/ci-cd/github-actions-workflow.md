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
- **Job timeout requirements**: All jobs must include `timeout-minutes` to prevent runaway builds from consuming runner minutes indefinitely. Recommended values: `build-linux: 30`, `build-windows: 40`, `coverage-linux: 45` (longer due to full build + instrumented tests + lcov), `all-checks-pass: 5`. Without these limits, a hung MSVC linker or stuck xvfb process can block the runner for the GitHub Actions default 6-hour maximum, wasting all allocated minutes on the repo for that billing period.

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

- **`build-linux` job** (`ubuntu-latest`): install xvfb + Mesa + libgl1-mesa-dev + vcpkg; CMake configure with **`-DENABLE_COVERAGE=OFF`** (coverage instrumentation disabled — this is the fast binary-verification build, not the coverage build); build; then, before running tests, verify that label routing is non-zero; then run tests in three explicitly named steps.

  **Integration test routing verification (mandatory post-build step)**: After the build step and before any test execution step, add a CI step that queries the number of tests discovered under the `integration` label. If zero tests are discovered, the step exits non-zero and the job fails immediately. This prevents the false-green scenario where `gtest_discover_tests()` with a misconfigured `LABEL` silently produces zero tests and `ctest -L '^integration$'` exits 0 — a zero-test discovery does NOT constitute a passing verification:

  ```yaml
  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^integration$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."
  ```

  **The same pattern applies to the `requires-opengl` label.** Add an analogous verification step after the integration routing check and before the `xvfb-run` step:

  ```yaml
  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^requires-opengl$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Requires-opengl test routing verified: $count test(s) discovered."
  ```

  Both checks must be placed **after the CMake build step and before any ctest execution step** so that a label misconfiguration fails the job before any false-passing `ctest -L` invocation can run. Neither step requires a display or audio device — they only invoke `ctest -N` (list mode, no test execution).

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
- **`build-linux` ccache setup**: Include `hendrikmuhs/ccache-action` before the CMake configure step (Linux job only — `if: runner.os == 'Linux'`; ccache does not support `cl.exe` on Windows). Add `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` to the CMake configure step. See `caching.md` for the authoritative platform-specific caching rules, ccache key format, and action SHA.

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

- **Windows job** (`windows-latest`): build step; then create the test results directory and run tests — use **explicit label filtering** to skip `requires-opengl` tests (no display available on Windows runners). The `requires-opengl` label is Linux-only (`xvfb-run`); Windows integration tests run under `AITOWN_HEADLESS=1`:

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

  `AITOWN_HEADLESS=1` suppresses both `AudioSystem` initialization and `IrrlichtDevice` window creation on headless runners. The Windows job runs both unit tests and integration tests but excludes `requires-opengl` tests (no xvfb available on Windows). The two-step structure mirrors the Linux job naming convention for clarity — a single combined `ctest -LE "requires-opengl"` step is equivalent but hides the unit/integration distinction in CI logs. The `New-Item` step is mandatory — `GTEST_OUTPUT=xml:test_results/` silently writes nothing if the directory does not exist. **No coverage steps**. `dorny/test-reporter` glob: `test_results/*.xml`.
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
  # After tests (all jobs, always):
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
      name: test-results-build-linux-${{ github.sha }}   # <-- replace "build-linux" with actual job name per job
      path: test_results/
      retention-days: 14
  # After lcov (coverage-linux job):
  - name: Upload coverage report
    if: always()  # Upload even on gate failure — the report is most valuable when coverage is below 80%
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: coverage-report-${{ github.sha }}
      path: coverage_html/
      retention-days: 14
  # After DLL verification (Windows job, on push to main only):
  - name: Upload Windows binary
    if: github.ref == 'refs/heads/main'
    uses: actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08  # v4.6.0
    with:
      name: aitown-windows-${{ github.sha }}
      path: build/Release/
      retention-days: 30
  ```

- **Artifact retention**: test XML retained 14 days; coverage HTML report retained 14 days; release binaries (Windows, on push to `main` only) retained 30 days
- **`coverage-linux` is a separate, self-contained job** — it performs its own configure+build+test+lcov sequence with `-DENABLE_COVERAGE=ON`. It does NOT depend on artifacts from `build-linux` (which would require large artifact transfers). This means `coverage-linux` re-runs the full build, but with coverage instrumentation enabled; `build-linux` can run a faster non-coverage build for binary verification. Both jobs run in parallel. The `all-checks-pass` gate references both. **Naming note**: the job can be renamed `build-test-coverage-linux` for clarity, as long as the name matches in the `needs:` list.
  - **`coverage-linux` must include three explicit, separately named YAML steps for ctest** (unit tests, integration tests without display, and OpenGL tests under xvfb) **before the lcov capture step**. A single combined `ctest` step cannot use both `-LE` and `-L` flags simultaneously; three named steps make coverage tracing explicit. The three ctest steps in `coverage-linux` must mirror the three ctest steps in `build-linux` exactly (same label filters `-LE "integration|requires-opengl"`, `-L "^integration$"`, `-L "^requires-opengl$"`) to ensure coverage data is collected for all test categories.

  **Label-routing verification in `coverage-linux` (mandatory)**: The `coverage-linux` job MUST include the same two label-routing non-zero discovery verification steps that `build-linux` includes — one for the `integration` label and one for the `requires-opengl` label. The exact step order within `coverage-linux` is:

  1. Install system dependencies (`apt-get`)
  2. Detect compiler version (write `COMPILER_VERSION` to `$GITHUB_ENV`)
  3. `actions/cache` for vcpkg and FetchContent (reads `COMPILER_VERSION` from step 2)
  4. `lukka/run-vcpkg` — install vcpkg packages
  5. `hendrikmuhs/ccache-action` — set up ccache with `-coverage` key suffix
  6. CMake configure (`cmake -B build ... -DENABLE_COVERAGE=ON`)
  7. CMake build (`cmake --build build`)
  8. **Verify integration test routing (non-zero discovery)** — after build, before any ctest
  9. **Verify requires-opengl test routing (non-zero discovery)** — after build, before any ctest
  10. Run unit tests ctest step
  11. Run integration tests ctest step
  12. Run OpenGL tests ctest step (xvfb)
  13. Verify test XML output exists
  14. Publish test results (dorny/test-reporter)
  15. Capture and gate lcov coverage
  16. Upload coverage artifact

  Steps 8 and 9 (the two label-routing verification steps) are placed **after CMake build step (7) and before the first ctest execution step (10)**. A label misconfiguration that produces zero-test discovery in `build-linux` will equally affect `coverage-linux`; without these checks, a zero-discovery run silently under-reports coverage and exits 0.

  ### coverage-linux: label-routing verification YAML

  These steps are IDENTICAL to the `build-linux` forms — copy them exactly. They are reproduced here verbatim so that an implementer building `coverage-linux` from this spec alone can derive the exact YAML without referring back to the `build-linux` documentation.

  ```yaml
  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^integration$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."

  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '\''^requires-opengl$'\'' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Requires-opengl test routing verified: $count test(s) discovered."
  ```

  Both checks must be placed **after the CMake build step and before the first ctest execution step** so that a label misconfiguration fails the job before any false-passing `ctest -L` invocation can run. Neither step requires a display or audio device — they only invoke `ctest -N` (list mode, no test execution).

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
          '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
          --output-file coverage_filtered.info
        lcov --list coverage_filtered.info
        genhtml coverage_filtered.info --output-directory coverage_html/
        # PHASING NOTE: No hard coverage gate at Phase 0.
        # lcov --fail-under-percent does NOT exist in lcov 2.0 (ubuntu-latest ships 2.0;
        # the flag was added in lcov 2.1). Using it exits 1 with "Unknown option".
        # At Phase 0 the gate would be 0% anyway. Use --summary for informational output.
        # Phase 2 TODO: implement 80% gate via bash awk check or after confirming lcov 2.1+:
        #   lcov --summary coverage_filtered.info | awk '/lines/ {if ($2+0 < 80) exit 1}'
        lcov --summary coverage_filtered.info
        # Phase 1 src/ui/ coverage gate (BLOCKING): enforce a 25% floor on src/ui/ files.
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
  - **`coverage-linux` test reporting steps (required)**: After all three ctest steps and **before the lcov capture step**, `coverage-linux` must include the XML verification and `dorny/test-reporter` steps. Placing these **before lcov** is intentional: if a test fails and ctest exits non-zero, the XML files may still be present; reporting them before the lcov step ensures test annotations reach the PR even when lcov subsequently fails or is skipped. Without these steps, test failures in the coverage build produce no PR annotations, silently hiding coverage-run failures from reviewers. **Step order in `coverage-linux`**: (1) unit tests ctest, (2) integration tests ctest, (3) OpenGL tests ctest under xvfb, (4) Verify test XML, (5) Publish test results via `dorny/test-reporter`, (6) lcov capture + filter + gate, (7) Upload coverage artifact. The `coverage-linux` YAML must include (after all ctest steps, before lcov):

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
      run: npm install -g markdownlint-cli

    - name: Run markdownlint
      run: markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

**Key properties**:

- `runs-on: ubuntu-latest` — Node.js/npm are pre-installed; no additional setup required.
- `timeout-minutes: 5` — linting is fast (seconds); a 5-minute cap prevents runaway npm installs from consuming runner minutes.
- `permissions: contents: read` — the job only checks out and reads files; no artifact upload, no check annotations, no write access needed.
- The `npm install -g markdownlint-cli` step installs `markdownlint-cli` at the version available from npm registry. To pin to a specific version use `npm install -g markdownlint-cli@0.47.0` — pin the version explicitly if the repo requires reproducible linting behavior.
- The `markdownlint` command runs with the glob patterns that cover all spec and documentation files: `architecture/**/*.md`, `implementation/*.md`, and `CLAUDE.md`. The shell expands these globs on `ubuntu-latest` (bash, globstar not needed for single-level `**`). If the `implementation/` directory does not yet exist the glob silently matches nothing and the step passes — this is correct behavior for an empty phase.
- Exit code 1 on any violation — the job fails and blocks `all-checks-pass`.
- No caching step needed — `npm install -g` for a single small package takes under 10 seconds and adds no meaningful cache key complexity.
- No `dorny/test-reporter` step — `markdownlint` produces plain text output, not JUnit XML. CI log output is sufficient for diagnosis.
- No artifact upload step — no binary or report output is produced.

- **`validate-assets` job** — validates asset files using the Python validation script. Must run on every push and PR alongside the build jobs so asset errors are caught before any binary is produced. Runs on `ubuntu-latest` with a 10-minute timeout.

  **Phasing**: This job is introduced in Phase 1 running `tools/validate_assets.py` as a stub that always exits 0. It is wired into `all-checks-pass` at Phase 1 creation — not deferred to a later phase. This means the stub always passes, keeping the gate green while the real check logic is absent. In Phase 2 the script gains 13 real checks; in Phase 6 a 14th sidecar check is added. The job definition and `all-checks-pass` wiring remain unchanged across all phases.

  Job definition:

**IMPORTANT**: Do NOT copy this SHA — you MUST resolve it live at implementation time using:
`gh release view --repo actions/setup-python --json tagName,url`
or equivalently via the GitHub API. The SHA shown here is a placeholder only.

  ```yaml
  validate-assets:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    permissions:
      contents: read  # checkout only — no check annotations or artifact writes needed
    steps:
      - name: Checkout
        uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11  # v4.1.1 — verified

      - name: Set up Python 3
        uses: actions/setup-python@<RESOLVE_AT_IMPLEMENTATION_TIME>  # resolve SHA live — see note above
        with:
          python-version: '3'

      - name: Run asset validation
        run: python tools/validate_assets.py
  ```

  The `python tools/validate_assets.py` command must exit non-zero on any validation error — the script is responsible for printing a human-readable error message before exiting so CI logs identify the offending asset. The `validate-assets` job must be added to the `all-checks-pass` needs list simultaneously with its creation; omitting it from `needs:` means a failing asset-validation run does not block merges.

- **`all-checks-pass` gate job** — MUST include `if: always()`. **Any new CI job must be added to the `needs` list below.** See `branch-protection.md` for the full rationale and `if: always()` requirement.

  **IMPORTANT — phased implementation warning**: The `all-checks-pass` job evolves across phases. Do NOT reference a job in `needs:` before that job exists — it causes the entire workflow YAML to fail to parse before any job runs. Follow the phased forms below exactly.

  **Phasing summary for `validate-assets`**:
  - Phase 0: `validate-assets` job does not exist yet; omit from `needs:`.
  - Phase 1: `validate-assets` job is introduced running `tools/validate_assets.py` as a stub that always exits 0. Wire it into `all-checks-pass` immediately. Adding the job (even as a stub) now means the `all-checks-pass` dependency list never needs to change in later phases — only the script gains real checks.
  - Phase 2: the stub script gains 13 real asset checks; the job definition and `all-checks-pass` wiring are unchanged.
  - Phase 6: a 14th sidecar check is added to the script; again no change to the job definition or wiring.

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

When the `validate-assets` job is added in Phase 1, update `all-checks-pass` to include it simultaneously. The job runs `tools/validate_assets.py`, which is a stub that always exits 0 at Phase 1. Wiring it in now means the `needs:` list requires no further changes in Phase 2 (real checks) or Phase 6 (sidecar check) — only the script content changes, not the CI wiring.

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
