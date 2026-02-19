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

### Supply-Chain SHA Lint Step

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

**Cross-reference**: The `validate-assets` job definition (later in this file) uses `actions/checkout@<SHA>` and `actions/setup-python@<SHA>` placeholder tokens — these MUST be replaced with verified 40-character SHAs before the workflow is committed. This lint step will catch any unresolved placeholders at CI time.

- **`build-linux` job** (`ubuntu-latest`): install xvfb + Mesa + libgl1-mesa-dev + vcpkg; CMake configure with **`-DENABLE_COVERAGE=OFF`** (coverage instrumentation disabled — this is the fast binary-verification build, not the coverage build); build; then run tests in three explicitly named steps:
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
  ```yaml
  - name: Set up ccache
    if: runner.os == 'Linux'
    uses: hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9  # v1.2.14 — pin to SHA
    with:
      key: ${{ runner.os }}-ccache-${{ env.COMPILER_VERSION }}
  ```
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
  The `if: always()` is required so results are published even when tests fail. `fail-on-error: false` prevents a `dorny/test-reporter` error (e.g., no XML when tests crash early) from overwriting the real ctest exit code. **Implementation note**: The SHA `31a54ee7ebcacc03a09ea97a7e5465a47b84aea5` is a placeholder SHA in the correct 40-character format. Before committing any CI YAML, verify it matches the actual commit for the `v1.9.1` tag by running `gh release view v1.9.1 --repo dorny/test-reporter --json tagName,targetCommitish` and substituting the returned SHA. Using an unverified SHA defeats supply-chain pinning.

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
  ```yaml
  # After tests (all jobs, always):
  # IMPORTANT: artifact names must be job-specific — runner.os returns "Linux" on both
  # build-linux and coverage-linux, causing a name collision. Use the job name explicitly:
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
  - **`coverage-linux` must include three explicit, separately named YAML steps for ctest** (unit tests, integration tests without display, and OpenGL tests under xvfb) **before the lcov capture step**. A single combined `ctest` step cannot use both `-LE` and `-L` flags simultaneously; three named steps make coverage tracing explicit. The three ctest steps in `coverage-linux` must mirror the three ctest steps in `build-linux` exactly (same label filters `-LE "integration|requires-opengl"`, `-L "^integration$"`, `-L "^requires-opengl$"`) to ensure coverage data is collected for all test categories:
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
- **`validate-assets` job** — validates asset files using the Python validation script. Must run on every push and PR alongside the build jobs so asset errors are caught before any binary is produced. Runs on `ubuntu-latest` with a 10-minute timeout. Job definition:
  ```yaml
  validate-assets:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - name: Checkout
        uses: actions/checkout@<SHA>  # verify SHA at implementation time

      - name: Set up Python 3
        uses: actions/setup-python@<SHA>  # verify SHA at implementation time
        with:
          python-version: '3'

      - name: Run asset validation
        run: python tools/validate_assets.py
  ```
  The `python tools/validate_assets.py` command must exit non-zero on any validation error — the script is responsible for printing a human-readable error message before exiting so CI logs identify the offending asset. The `validate-assets` job must be added to the `all-checks-pass` needs list simultaneously with its creation; omitting it from `needs:` means a failing asset-validation run does not block merges.
- **`all-checks-pass` gate job** — MUST include `if: always()`. **Any new CI job must be added to the `needs` list below.** See `branch-protection.md` for the full rationale and `if: always()` requirement.

  **IMPORTANT — phased implementation warning**: The `all-checks-pass` job has two forms depending on the delivery phase. Do NOT copy the Phase 6+ final form verbatim at Phase 0 — referencing a non-existent `validate-assets` job in `needs:` causes the entire workflow YAML to fail to parse before any job runs. Implement the Phase 0 form first; add `validate-assets` to `needs:` only when that job is introduced in Phase 6.

#### PHASE 0 FORM (validate-assets added in Phase 6):

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  # Phase 0 form (validate-assets added in Phase 6):
  needs: [build-linux, build-windows, coverage-linux]
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

#### PHASE 6+ FINAL FORM (after validate-assets job is introduced):

```yaml
all-checks-pass:
  runs-on: ubuntu-latest
  if: always()
  needs: [build-linux, build-windows, coverage-linux, validate-assets]  # ADD new jobs here
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
