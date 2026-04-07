# CI/CD Spec Fix Plan

## Summary

The four CI/CD spec files (`github-actions-workflow.md`, `caching.md`, `dependency-management.md`,
`branch-protection.md`) were written incrementally as the pipeline evolved through Phases 0–11.
The deployed pipeline has diverged significantly: the monolithic `ci.yml` described in the specs
has been refactored into multiple reusable workflow files (`_build-linux.yml`, `_test-linux.yml`,
`_coverage-linux.yml`, `_build-windows.yml`, `_package-linux-deb.yml`, `_package-windows.yml`,
`_supply-chain-lint.yml`, `_validate-assets.yml`, `_markdown-lint.yml`); several new jobs exist
(`prepare`, `compute-version`, `release`, `package-linux-deb`, `package-windows`); action SHA pins
have been updated throughout; the `dorny/test-reporter` version has advanced from v1.9.1 to
v3.0.0; `actions/checkout` is pinned to v6.0.2 (not v4.1.1); `actions/upload-artifact` is at
v7.0.0 (not v4.6.0); Linux jobs now run in a pre-baked GHCR container rather than on bare
`ubuntu-latest` with apt-get installs; the `build-linux` job uploads a build artifact consumed
by a separate `test-linux` job (via `_test-linux.yml`); `coverage-linux` has additional coverage
gates (95% total, 85% per-file simulation floor, 25% UI worst-file, src/simulation/ preflight,
gcovr Sonar XML output) not described in the spec; and `markdown-lint` uses `npx` rather than a
global `npm install -g` step. Most spec text remains accurate in intent but refers to the wrong
file names, wrong action SHAs, incorrect structural assumptions (monolithic vs. reusable), and
stale step sequences.

---

## Discrepancies

### [github-actions-workflow.md: Supply-Chain SHA Lint Step] — Lint now in reusable workflow, not inline in build-linux

**Spec says:** The supply-chain SHA lint step is placed as the first named step inside the
`build-linux` job in `ci.yml`, immediately after checkout and before compiler detection. It
greps only `ci.yml` for placeholder SHAs and short SHAs.

**Source says:** The lint lives in a dedicated reusable workflow
`.github/workflows/_supply-chain-lint.yml` called by `ci.yml` as a separate `supply-chain-lint`
job. It greps ALL `.github/workflows/*.yml` files (not just `ci.yml`) and additionally checks
`container: image:` lines for full sha256 digest pins. The `build-linux` job has no inline lint
step.

**Fix:** Update the spec to describe the `_supply-chain-lint.yml` reusable workflow. State that
the lint runs as a standalone `supply-chain-lint` job called via `uses:
./.github/workflows/_supply-chain-lint.yml`, that it targets all `*.yml` files under
`.github/workflows/`, and that it validates container image digest pins in addition to action
SHAs. Remove the prose describing the lint as an inline step inside `build-linux`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-linux job] — Job now delegates to reusable workflow and runs in container

**Spec says:** `build-linux` is a monolithic job in `ci.yml` that: installs system dependencies
with `apt-get`, detects GCC version, sets up ccache, configures CMake, builds, runs routing
checks, runs all three ctest suites, publishes test results with dorny/test-reporter, and uploads
artifacts — all as inline steps.

**Source says:** `build-linux` is defined in `.github/workflows/_build-linux.yml` as a reusable
workflow. It has TWO jobs internally: `build-linux` (compile only, runs in a pre-baked GHCR
container `ghcr.io/m0wa/aitown-ci-linux:vcpkg-b2f068f@sha256:...`) and `test-linux` (delegates
to `_test-linux.yml` via `uses:`). There is no `apt-get` install step — the container has all
dependencies pre-installed. The `build-linux` job does NOT run ctest; instead it uploads the
build directory as an artifact, and `test-linux` downloads it and runs all ctest suites.
Additional steps present in source but not described in spec: "Verify libGLEW.a artifact (CI-1)",
"Verify default.mhr HRTF data", "Normalize compile_commands.json for SonarCloud", and "Upload
compile_commands.json" (for SonarCloud integration).

**Fix:** Rewrite the `build-linux` section to: (a) describe the two-job split (build job +
test-linux reusable workflow delegation); (b) document the pre-baked GHCR container image
approach and remove all references to `apt-get` install steps inside `build-linux`; (c) document
the SonarCloud-related steps (normalize + upload compile_commands.json); (d) document the
`_test-linux.yml` reusable workflow (inputs: `build_artifact_name`, `build_dir`,
`reporter_name`, `test_artifact_name`). The spec should not describe routing checks and ctest
execution as part of the compile job.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: dependency-management.md: Linux system dependencies] — apt-get installs removed from CI jobs

**Spec (dependency-management.md, "Linux System Package Requirements") says:** Both `build-linux`
and `coverage-linux` must include an explicit `apt-get` install step before CMake configure,
installing `xvfb`, `libgl1-mesa-dev`, `mesa-utils`, `libglew-dev`, `libxxf86vm-dev` (and `lcov`
for coverage-linux). The spec provides exact YAML for both install steps.

**Source says:** Neither `_build-linux.yml` nor `_coverage-linux.yml` contains any `apt-get`
install step. All system packages are pre-installed in the GHCR container image
(`ghcr.io/m0wa/aitown-ci-linux:vcpkg-b2f068f@sha256:...`). The `docker/ci-linux/Dockerfile`
and the `docker-ci-image.yml` workflow manage the image build. The CI jobs set
`VCPKG_MANIFEST_INSTALL=OFF` and pass `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed` to CMake,
indicating vcpkg packages are also pre-installed in the image.

**Fix:** In `dependency-management.md`, replace the `apt-get` install step YAML with a
description of the containerized approach: packages are baked into
`ghcr.io/m0wa/aitown-ci-linux` via `docker/ci-linux/Dockerfile`; CI jobs use
`container: image:` to pull this image; no install step is needed at runtime. Reference the
`docker-ci-image.yml` workflow for how the image is built and pushed. Keep the package list as a
documentation requirement (these packages must remain in the Dockerfile), but remove the YAML
blocks that show `sudo apt-get` steps in the CI workflow.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: action SHA pins] — Multiple SHA pins are stale

**Spec says (various locations):**
- `actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11` (v4.1.1)
- `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830` (v4.3.0) — cited in `caching.md`
- `actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08` (v4.6.0)
- `hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9` (v1.2.14)
- `dorny/test-reporter@31a54ee7ebcacc03a09ea97a7e5465a47b84aea5` (v1.9.1)
- `softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564` (v2.1.0) — cited in `caching.md`

**Source says (deployed workflow files):**
- `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd` (v6.0.2) — used in all workflow files
- `actions/cache@668228422ae6a00e4ad889ee87cd7109ec5666a7` (v5.0.4) — used in `_build-windows.yml`; `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830` (v4.3.0) used in `sonarcloud.yml` and `_package-linux-deb.yml`
- `actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f` (v7.0.0) — used in all workflow files
- `actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131` (v7.0.0) — used in multiple workflow files (not mentioned in spec at all)
- `hendrikmuhs/ccache-action@33522472633dbd32578e909b315f5ee43ba878ce` (v1.2.22) — used in `_build-linux.yml` and `_coverage-linux.yml`
- `dorny/test-reporter@a43b3a5f7366b97d083190328d2c652e1a8b6aa2` (v3.0.0) — used in `_test-linux.yml`, `_coverage-linux.yml`, `_build-windows.yml`
- `softprops/action-gh-release` is not used; the release job uses `gh release create` (GitHub CLI) directly

**Fix:** Update all SHA pin references across `github-actions-workflow.md` and `caching.md` to
the currently deployed values. Remove the `softprops/action-gh-release` SHA reference entirely
and replace it with a note that the `release` job uses the `gh` CLI (`gh release create`)
directly. Add `actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131` (v7.0.0) to
the pinned-actions table in `caching.md`. Note that two different `actions/cache` SHAs are
intentionally in use: the Windows build job uses v5.0.4
(`668228422ae6a00e4ad889ee87cd7109ec5666a7`) while `sonarcloud.yml` and `_package-linux-deb.yml`
continue to use v4.3.0 (`0057852bfaa89a56745cba8c7296529d2fc39830`) — the spec should document
both usages and recommend v5.0.4 (`668228422ae6a00e4ad889ee87cd7109ec5666a7`) as the canonical
value for new workflow additions; the v4.3.0 pin in existing workflows should be updated in a
future baseline bump.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: dorny/test-reporter — version and name format] — v3.0.0 SHA and reporter_name input

**Spec says:** Use `dorny/test-reporter@31a54ee7ebcacc03a09ea97a7e5465a47b84aea5` (v1.9.1).
The `name:` field uses `Test Results (${{ github.job }})` format. `continue-on-error: true` is
not mentioned in the spec.

**Source says:** All three jobs use `dorny/test-reporter@a43b3a5f7366b97d083190328d2c652e1a8b6aa2`
(v3.0.0). In `_test-linux.yml` the `name:` is `${{ inputs.reporter_name }}` (caller-provided
via reusable workflow input, e.g. `'Linux Unit Tests'`). In `_coverage-linux.yml` the `name:`
is hardcoded `'Linux Coverage Tests'`. In `_build-windows.yml` the `name:` is hardcoded
`'Windows Unit Tests'`. Only the two Linux container jobs (`_test-linux.yml` and
`_coverage-linux.yml`) include `continue-on-error: true`; `_build-windows.yml` does not.
The spec does not mention `continue-on-error: true` at all.

**Fix:** Update the SHA and version comment to v3.0.0
(`@a43b3a5f7366b97d083190328d2c652e1a8b6aa2`). Update the `name:` field examples to reflect
the reusable-workflow input pattern for `_test-linux.yml` and the hardcoded strings for the
other jobs. Document `continue-on-error: true` as required on the Linux container jobs only
(not on the Windows job), alongside `fail-on-error: false`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: markdown-lint job] — Uses npx, not global npm install

**Spec says:** The `markdown-lint` job installs `markdownlint-cli` globally with `npm install -g
markdownlint-cli@0.47.0`, then invokes `markdownlint` (the globally installed binary) with the
glob patterns.

**Source says:** `_markdown-lint.yml` uses a single step: `npx markdownlint-cli@0.47.0
'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`. There is no `npm install -g` step.
The `actions/checkout` SHA is `de0fac2e4500dabe0009e67214ff5f5447ce83dd` (v6.0.2), not
`b4ffde65f46336ab88eb53be808477a3936bae11` (v4.1.1).

**Fix:** Replace the two-step (install + run) pattern in the `markdown-lint` job definition with
the single `npx markdownlint-cli@0.47.0 ...` invocation. Update the `actions/checkout` SHA.
Note that `npx` requires no prior `npm install -g` and that the version pin is expressed inline
in the `npx` command.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: ci.yml top-level structure] — Multiple new jobs not described

**Spec says:** `ci.yml` contains five jobs: `build-linux`, `build-windows`, `coverage-linux`,
`markdown-lint`, `all-checks-pass`. The spec also mentions `validate-assets` as introduced in
Phase 1.

**Source says:** `ci.yml` contains twelve jobs: `prepare`, `compute-version`, `supply-chain-lint`,
`validate-assets`, `build-linux`, `build-windows`, `coverage-linux`, `markdown-lint`,
`all-checks-pass`, `package-windows`, `package-linux-deb`, `release` (twelve total). The spec
does not describe: the `prepare` job (exports `VCPKG_COMMIT_ID` as a job output so reusable
workflow `with:` blocks can reference it — env context is unavailable in `jobs.<id>.with`);
the `compute-version` job (auto-increments the patch version on main pushes and sets a
`-develop` suffix on develop pushes); `package-windows` and `package-linux-deb` jobs (trigger
only on push to main/develop, consume staging artifacts from build jobs); and the `release` job
(creates GitHub releases using `gh release create` and downloads four `.deb` artifacts plus one
Windows installer artifact). The `all-checks-pass` job now lists `prepare` and `supply-chain-lint`
in `needs:` in addition to the jobs mentioned in the spec.

**Fix:** Document all twelve `ci.yml` jobs. Add sections for: `prepare` (purpose, why env
context cannot be used in `with:`, job output pattern); `compute-version` (tag increment logic,
`DEFAULT_MAIN` fallback, develop suffix); `package-windows` and `package-linux-deb` (push-only
trigger, staging artifact download, CPack invocation, `_package-windows.yml` and
`_package-linux-deb.yml` reusable workflow delegation); `release` (multi-artifact download,
`gh release create --target`, immutable-release retry loop). Update the `all-checks-pass`
`needs:` list to include `prepare` and `supply-chain-lint`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: permissions block] — Missing `packages: read`

**Spec says:** The workflow-level permissions block has two entries:
```yaml
permissions:
  checks: write
  contents: read
```

**Source says:** `ci.yml` has three entries at workflow level:
```yaml
permissions:
  checks: write
  contents: read
  packages: read    # required by build-linux and coverage-linux to pull GHCR image
```
Individual jobs also override permissions: `compute-version` uses `contents: write`; `prepare`
uses `contents: none`; `release` uses `contents: write`.

**Fix:** Add `packages: read` to the workflow-level permissions block in the spec. Document that
individual jobs may override permissions (e.g., `contents: write` for `compute-version` and
`release`, `contents: none` for `prepare`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-linux — routing checks and simulation_tests verification] — Additional checks not in spec

**Spec says:** Three routing checks are required in `build-linux` (unit, integration,
requires-opengl), each using `grep -c 'Test #'`.

**Source says:** `_test-linux.yml` contains four routing checks, not three. In addition to the
three label checks, there is a fourth step: "Verify simulation_tests registration" that uses
`ctest -N -L '^unit$' | grep -i 'simulation' | grep -cE 'Test +#'`. The grep patterns are
NOT uniform: only the unit check and simulation_tests check use `grep -cE 'Test +#'` (extended
regex, handles CTest alignment padding); the integration check and requires-opengl check use
`grep -c 'Test #'` (literal match). The spec shows `grep -c 'Test #'` uniformly without `-E`.
Also, the ordering in `_test-linux.yml` is: integration check, then requires-opengl check, then
unit check — whereas the spec orders them unit first. There is also a "Restore execute
permissions on test binaries" step (`find $BUILD_DIR -maxdepth 2 -type f -name '*_tests' -exec
chmod +x {} \;`) that the spec does not describe, required because `actions/upload-artifact`
strips file permissions.

**Fix:** Add the "Verify simulation_tests registration" step to the spec, after the unit
routing check. Update the `grep` pattern to `grep -cE 'Test +#'` for ALL four routing checks
for consistency and future-proofing (any label could exceed 9 tests, where CTest emits two
spaces before `#`). Document the "Restore execute permissions" step as a required step in
the test job immediately after downloading the build artifact. Reconcile the step ordering.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — additional lcov flags and gates] — Spec describes Phase 0 lcov invocation; source reflects Phase 11

**Spec says:** The `lcov --capture` call uses:
```
lcov --capture --directory build --base-directory ${{ github.workspace }} \
     --ignore-errors mismatch \
     --output-file coverage.info
```
The `lcov --remove` call uses `--ignore-errors unused` with patterns including
`"${{ github.workspace }}/.fetchcontent_cache/*"`. The spec mentions a Phase 0 informational
`lcov --summary` with no hard gate, and describes Phase 4 and Phase 5 coverage gates as future
deliverables.

**Source says:** `_coverage-linux.yml` uses:
```
lcov --capture --directory build --base-directory . \
     --gcov-tool gcov-13 \
     --ignore-errors mismatch,inconsistent \
     --rc check_data_consistency=0 \
     --output-file coverage.info
```
The `lcov --remove` call uses `--ignore-errors unused,inconsistent --rc
check_data_consistency=0` and includes additional exclude patterns:
`'/opt/vcpkg_installed/*'`, `"$(pwd)/vcpkg_installed/*"`, `"$(pwd)/build/vcpkg_installed/*"`,
`'*/src/simulation/*.h'`, `'*/src/ui/*.h'`, `'*/src/interfaces/*.h'`. The `$(pwd)` idiom is
used instead of `${{ github.workspace }}` (to avoid the container path mismatch). The `lcov
--list` and `genhtml` calls also include `--ignore-errors inconsistent --rc
check_data_consistency=0`. There is no `lcov --summary` call. Four steps run in this order: (1) `src/simulation/` SF
preflight (warning-only); (2) an 85% per-file `src/simulation/` floor awk gate; (3) a 95%
total line coverage awk gate; (4) a 25% worst-file `src/ui/` gate (implemented as a direct
SF/LH/LF parse, not the `lcov --list | grep | awk` method described in the spec). Additionally, a
`gcovr --sonarqube` step generates `coverage.xml` for SonarCloud, and the "Check src/ui/ zero-hit
files" step uses a Python `os.path.exists` guard for the case where
`coverage_filtered.info` does not exist.

**Fix:** Update the spec's lcov invocation to include `--gcov-tool gcov-13`,
`--ignore-errors mismatch,inconsistent` (combined, not separate), and `--rc
check_data_consistency=0`. Replace `${{ github.workspace }}` with `$(pwd)` in the
`--base-directory` and exclude path arguments, and document why (container path mismatch). Add
the additional exclude patterns to the `lcov --remove` call. Remove the `lcov --summary` step.
Document all four active coverage gates (95% total, 85% per-file simulation, 25% UI, SF
preflight) as implemented rather than as phase-future work. Add the `gcovr --sonarqube` step
to the spec. Update the "Check src/ui/ zero-hit files" Python snippet to include the
`os.path.exists('coverage_filtered.info')` guard.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — label routing checks absent] — Routing checks not present in coverage-linux

**Spec says:** The `coverage-linux` job must include the same three label-routing non-zero
discovery verification steps that `build-linux` includes (steps 8, 9, 10 in the specified
ordering). The spec describes this as a mandatory requirement.

**Source says:** `_coverage-linux.yml` does NOT contain any routing check steps (no "Verify unit
test routing", "Verify integration test routing", or "Verify requires-opengl test routing" steps).
The coverage job goes directly from build to `mkdir -p test_results` and ctest execution.

**Fix:** This is a discrepancy where the deployed source DOES NOT have something the spec
requires. However, since the instruction is to bring the spec into alignment with the source
(not vice versa), update the spec to reflect that routing checks are ONLY in the shared
`_test-linux.yml` reusable workflow (used by `build-linux`'s `test-linux` delegate job) and are
NOT duplicated in `coverage-linux`. Remove the "coverage-linux: label-routing verification" YAML
blocks from the spec and update the prose to state that `coverage-linux` runs ctest directly
without routing pre-checks, as the routing is verified by `test-linux` in the parallel
`build-linux` pipeline.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — step ordering for test reporting] — Reporting BEFORE lcov, upload AFTER

**Spec says (step 17 ordering description):** "After all three ctest steps and before the lcov
capture step, `coverage-linux` must include the XML verification and `dorny/test-reporter`
steps." The spec ordering is: ctest steps → XML verify → dorny/test-reporter → lcov →
zero-hit check → upload coverage artifact.

**Source says:** `_coverage-linux.yml` ordering is: ctest steps → git safe.directory fix →
dorny/test-reporter (both with `if: always()`) → Upload test results → lcov capture → gcovr
Sonar XML → src/simulation/ SF preflight → 85% per-file floor → 95% total gate → 25% UI
worst-file gate → zero-hit coverage completeness check → Upload Sonar coverage XML → Upload
coverage HTML → Upload coverage info (lcov). There is no standalone "Verify test XML output
exists" step before dorny in `coverage-linux` (unlike in `_test-linux.yml` which does have
one). The "Fix git safe.directory for test reporter" step (runs before dorny with `if:
always()`) is not mentioned in the spec.

**Fix:** Update the step ordering in the `coverage-linux` section to match the deployed
ordering. Document the "Fix git safe.directory for test reporter" step (`git config --global
--add safe.directory "$GITHUB_WORKSPACE"`) as a required step before dorny in container jobs,
explaining the UID mismatch rationale. Clarify that in `coverage-linux`, the XML verification
step is omitted (unlike in `_test-linux.yml`) and dorny runs directly after the git safe.directory
fix. Update the upload artifact list to include three separate uploads: Sonar coverage XML,
coverage HTML, and coverage filtered info.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-windows — DLL verification ordering] — DLL check runs AFTER tests, not before

**Spec says:** The DLL verification step ("Verify Phase 7 DLLs and HRTF data present") runs
BEFORE the test steps, immediately after "Add vcpkg bin to PATH". The spec shows soft_oal.dll
and default.mhr verification before the `ctest` invocations.

**Source says:** In `_build-windows.yml`, the DLL verification step ("Verify required DLLs and
HRTF data (all hard-fail)") runs AFTER both test steps and after the test XML verification step
(Step 18 per the comment). The order is: Build → Add vcpkg bin to PATH → Run unit tests → Run
integration tests → Verify test XML output → Publish test results → Upload test results →
"Verify required DLLs and HRTF data (all hard-fail)" → Upload Windows staging artifact.

**Fix:** Update the spec to reflect that DLL verification runs after test execution and after
test result publication, immediately before staging artifact upload. Update the step number
reference from "Step 18" (which still appears in a spec comment) to align with the actual
deployed position.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-windows — GLEW verification is combined into one step] — Two steps vs one

**Spec (dependency-management.md) says:** GLEW verification uses TWO separate steps: "Verify
glew vcpkg port" (header check, runs before CMake configure) and "Verify GLEW vcpkg install"
(library check, runs before CMake configure). Each is a separate YAML step.

**Source says:** `_build-windows.yml` uses ONE combined step "Verify GLEW vcpkg artifacts
(header and import lib)" that checks both `GL/glew.h` and `glew32.lib` in a single PowerShell
block. This step runs AFTER CMake configure (between the Configure and Build steps).

**Fix:** In `dependency-management.md`, update the Windows GLEW verification section to describe
a single combined step that checks both header and library, and update its placement to after
CMake configure (not before configure as the spec currently states).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: validate-assets job] — actions/setup-python SHA and pip install differ

**Spec says:** The `actions/setup-python` step must use a placeholder `@<40-CHAR-SHA>` that the
implementer resolves at commit time. The Python dependencies step installs `mutagen` without a
version pin (`pip install mutagen`).

**Source says:** `_validate-assets.yml` uses
`actions/setup-python@a309ff8b426b58ec0e2a45f0f869d46889d02405` (v6.2.0) with
`python-version: '3.12'`. The pip install step installs pinned versions:
`pip install "mutagen==1.47.0" "Pillow==10.4.0"` plus `sudo apt-get install -y
--no-install-recommends ffmpeg` (for check_21 OGG decode). The spec does not mention `Pillow`
or `ffmpeg` as required dependencies.

**Fix:** Replace the `@<40-CHAR-SHA>` placeholder with the resolved SHA
`a309ff8b426b58ec0e2a45f0f869d46889d02405` (v6.2.0). Update the pip install step to pin
`mutagen==1.47.0` and `Pillow==10.4.0`. Add `ffmpeg` (installed via apt-get) as a required
dependency for check_21. Add explicit version pin requirements for pip dependencies — the spec
currently gives no pinning guidance, but pinned versions are the deployed reality and required
for reproducibility.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: validate-assets job — additional steps not described] — Source has many more steps

**Spec says:** The `validate-assets` job runs: checkout, setup-python, pip install, (Phase 11i)
shader verify, run validate_assets.py.

**Source says:** `_validate-assets.yml` has many additional steps not described in the spec:
- "Verify required checks present in validate_assets.py" (loops over check_20 through check_31)
- "Verify hud_sprites_ui.dds not git-tracked"
- "Verify hud_sprites_ui_layout.json not git-tracked"
- "Verify shader assets" (Phase 11i step)
- "Validate vcpkg baseline consistency" (reads `inputs.vcpkg_commit_id` via env var)
- "Verify clouds.png present"
- "Verify font assets present" (hud_font.xml, hud_mono_font.xml)
- "Verify Phase 10 audio assets present" (seven audio files)
- "Verify terrain assets present" (DDS textures, splat PNG, terrain shaders)

**Fix:** Document all the deployed steps in the spec's `validate-assets` section. For the "Verify
required checks present" step, note that it currently covers check_20 through check_31. Document
that the vcpkg baseline consistency check is performed in `validate-assets` (not inline in
`build-linux` or `build-windows` as the older spec describes). Document the asset presence
verification steps (clouds, fonts, audio, terrain).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: validate-assets — vcpkg baseline check location] — Check moved to validate-assets

**Spec (dependency-management.md) says:** The vcpkg baseline consistency step runs inline in the
build job (implied — the YAML shows `shell: bash` with `${{ env.VCPKG_COMMIT_ID }}`):
```yaml
- name: Validate vcpkg baseline consistency
  shell: bash
  run: |
    MANIFEST_BASELINE=$(jq -r '."builtin-baseline"' vcpkg.json)
    if [[ "$MANIFEST_BASELINE" != "${{ env.VCPKG_COMMIT_ID }}" ]]; then ...
```

**Source says:** The baseline check is in `_validate-assets.yml` as a step that receives
`EXPECTED_BASELINE: ${{ inputs.vcpkg_commit_id }}` via env (using the `inputs.` context, not
`env.VCPKG_COMMIT_ID` directly, because env context is unavailable in reusable workflow
`with:` blocks — the `prepare` job pattern propagates the value).

**Fix:** Update `dependency-management.md` to state that the vcpkg baseline consistency check
is located in the `validate-assets` job (`_validate-assets.yml`), not in the build jobs. Update
the YAML example to use `EXPECTED_BASELINE: ${{ inputs.vcpkg_commit_id }}` and read from that
env var rather than `${{ env.VCPKG_COMMIT_ID }}`.


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: FetchContent caching] — FetchContent not used in deployed pipeline

**Spec says:** There is a FetchContent cache requirement: "When `FETCHCONTENT_BASE_DIR` is set
to `.fetchcontent_cache` (outside the build tree), this directory must be explicitly cached by
`actions/cache` in the `coverage-linux` job." The spec provides a full key format:
`fetchcontent-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('CMakeLists.txt',
'cmake/**') }}`.

**Source says:** Neither `_coverage-linux.yml` nor any other workflow file contains an
`actions/cache` step for `.fetchcontent_cache`. This is consistent with the project using vcpkg
for ALL dependencies (including googletest and rapidcheck) — FetchContent is not used. The note
in `dependency-management.md` explains this explicitly ("Why vcpkg (not FetchContent)").
`FETCHCONTENT_BASE_DIR` is not set in any workflow.

**Fix:** Remove the FetchContent caching section from `caching.md` entirely, or replace it with
a one-line note stating that FetchContent is not used in this project — all dependencies
including test libraries are vcpkg-managed, so no `.fetchcontent_cache` directory exists and no
`actions/cache` step for it is needed.


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: Linux vcpkg caching — no actions/cache step for Linux] — Spec mentions ccache only; source has no vcpkg cache step on Linux

**Spec says:** "Linux builds use a pre-baked Docker CI image that includes `/opt/vcpkg_installed`
... Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and read from `/opt/vcpkg_installed` — no
`actions/cache` step for vcpkg is required or used in Linux jobs."

**Source says:** This is correct — no `actions/cache` step for vcpkg appears in `_build-linux.yml`
or `_coverage-linux.yml`. However, `caching.md` also says "BOTH `build-linux` AND
`coverage-linux` must include their own independent compiler-detect step" so that `env.COMPILER_VERSION`
is set for the FetchContent cache key. The FetchContent key is spurious (FetchContent not used),
so the only real purpose of the GCC detect step is the ccache key. The spec's compiler-detect
rationale should be simplified to only mention the ccache key purpose.

**Fix:** Remove all FetchContent-related rationale from the compiler-detect step description in
`caching.md`. Keep the description of the compiler-detect step but simplify its purpose to:
setting `COMPILER_VERSION` for the ccache key (ccache for both `build-linux` and
`coverage-linux`) and the Windows vcpkg cache key.


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: actions/cache SHA pin for Linux] — No actions/cache used in Linux CI jobs

**Spec says:** `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830` (v4.3.0) is listed as
a required pinned action for vcpkg caching in Linux jobs.

**Source says:** `_build-linux.yml` and `_coverage-linux.yml` do not contain any `actions/cache`
step. The `actions/cache` step appears directly in four workflows: `_build-windows.yml`
(`@668228422ae6a00e4ad889ee87cd7109ec5666a7`, v5.0.4), `sonarcloud.yml`
(`@0057852bfaa89a56745cba8c7296529d2fc39830`, v4.3.0), `_package-linux-deb.yml`
(`@0057852bfaa89a56745cba8c7296529d2fc39830`, v4.3.0), and `msvc.yml`
(`@0057852bfaa89a56745cba8c7296529d2fc39830`, v4.3.0). The ccache on Linux is handled by
`hendrikmuhs/ccache-action` (which internally uses `actions/cache`).

**Fix:** In `caching.md`, clarify that `actions/cache` is used directly only in the Windows
build job (v5.0.4) and in the package/Sonar workflows. Update the pinned SHA table to show
v5.0.4 (`668228422ae6a00e4ad889ee87cd7109ec5666a7`) as the canonical Windows build job pin.
Note that Linux ccache caching goes through `hendrikmuhs/ccache-action`, which manages its own
`actions/cache` invocation internally.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: all-checks-pass needs list] — compute-version not in needs

**Spec says:** The `all-checks-pass` job's `needs:` list covers: `build-linux`, `build-windows`,
`coverage-linux`, `markdown-lint`, and (Phase 1+) `validate-assets`.

**Source says:** The deployed `all-checks-pass` `needs:` list is: `prepare`,
`supply-chain-lint`, `validate-assets`, `build-linux`, `build-windows`, `coverage-linux`,
`markdown-lint`. `compute-version` is NOT in `all-checks-pass` (it is only a dependency for the
packaging and release jobs, which are outside the gate). The spec needs to add `prepare` and
`supply-chain-lint`.

**Fix:** Update the `all-checks-pass` `needs:` list in the spec to include `prepare` and
`supply-chain-lint`. Confirm that `compute-version` is intentionally excluded from
`all-checks-pass` (it is a push-only job and would make `all-checks-pass` fail on PRs where
`compute-version` is skipped).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: supply-chain lint — scope and location] — Spec describes inline build-linux step; source uses separate job scanning all workflows

**Spec says:** The lint step in `build-linux` checks only `.github/workflows/ci.yml`. The regex
patterns are: `<[A-Z_][A-Z0-9_-]*>` for placeholders and `@[0-9a-f]{1,39}\b` for short SHAs.

**Source says:** The `_supply-chain-lint.yml` standalone job checks ALL `*.yml` files under
`.github/workflows/`. It also adds a third check: container `image:` lines that lack a full
`@sha256:[0-9a-f]{64}` digest pin. The short-SHA grep is scoped to `uses:` lines only
(`grep -P 'uses:.*@[0-9a-f]{1,39}\b'`), whereas the spec's pattern matches any `@<short-sha>`
anywhere in the file.

**Fix:** Update the spec to describe the `_supply-chain-lint.yml` reusable workflow. State
that it checks all workflow files (not just `ci.yml`), and document the third check (container
image digest pins). Clarify that the short-SHA grep is restricted to `uses:` lines.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: Windows job — FORCE_JAVASCRIPT_ACTIONS_TO_NODE24] — Not described in spec

**Spec says:** No mention of `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`.

**Source says:** `_build-windows.yml` sets `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` at the
job `env:` level to suppress node20-deprecation warnings from `ilammy/msvc-dev-cmd` and
`lukka/run-vcpkg` (which have no node24 release). This is a job-level env var, not a step-level
env var.

**Fix:** Add a note to the Windows job section of `github-actions-workflow.md` stating that
`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` must be set at the job `env:` level to suppress
node20-deprecation warnings for actions that have no node24 release (specifically
`ilammy/msvc-dev-cmd` and `lukka/run-vcpkg`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: Windows staging artifact contents] — build/*.dll wildcard

**Spec says:** The Windows staging artifact upload uses:
```yaml
path: |
  build/aitown.exe
  build/*.dll
  build/default.mhr
```

**Source says:** `_build-windows.yml` uses exactly this pattern. This matches the spec.

**Note:** No discrepancy for this item — both agree on `build/*.dll` wildcard. However,
the spec's general guidance says "Do not use wildcard globs that might silently match nothing"
— this is contradicted by using `build/*.dll` as a wildcard. The spec should either acknowledge
this as an acceptable exception (DLL verification step guarantees at least one DLL exists before
this upload) or update the general anti-wildcard guidance to clarify the exception.

**Fix:** Add a clarifying note to the anti-wildcard guidance explaining that `build/*.dll` is
acceptable in the staging upload because the DLL verification hard-fail step immediately
preceding it guarantees at least one DLL is present.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-linux — "Verify shader assets" step] — Step removed from build-linux, now only in validate-assets

**Spec says (Phase 11i note):** "Do NOT add a 'Verify shader assets' step to this job. Shader
file existence checks are source-tree checks that belong in `validate-assets`."

**Source says:** `_build-linux.yml` has NO shader verification step. `_validate-assets.yml` HAS
a "Verify shader assets" step. This is consistent with the spec's note.

**Note:** No discrepancy. However, the spec's note ("Do NOT add a 'Verify shader assets' step")
is phrased as a future warning rather than a present description. The spec should update this
to past tense ("This step was consolidated into `validate-assets` in Phase 11i") to reflect
current deployed state.

**Fix:** Update the Phase 11i note in the `build-linux` section from a prohibition ("Do NOT
add") to a statement of fact ("This step is in `validate-assets`, not in `build-linux`").


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: job timeout-minutes — prepare and compute-version not mentioned]

**Spec says:** "Recommended values: `build-linux: 30`, `build-windows: 40`, `coverage-linux:
60`, `all-checks-pass: 5`."

**Source says:** Additional jobs have timeout values: `prepare: 2`, `compute-version: 5`,
`all-checks-pass: 5`, `release: 10`, `supply-chain-lint` (no explicit timeout — not in spec).
Package jobs: `package-linux-deb: 90`, `package-windows: 60`. The `all-checks-pass` job does
have `timeout-minutes: 5` in the deployed `ci.yml`, matching the spec's recommendation.

**Fix:** Extend the timeout table in the spec to cover all jobs: add `prepare: 2`,
`compute-version: 5`, `all-checks-pass: 5` (already deployed), `release: 10`,
`package-linux-deb: 90`, `package-windows: 60`. Note that `supply-chain-lint` has no explicit
`timeout-minutes` and one should be added.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: actions/checkout v6.0.2 SHA] — New version not documented anywhere

**Spec says:** `actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11` (v4.1.1) — cited
in the markdown-lint job definition, the validate-assets job definition, and the cross-reference
note about validate-assets using v4.1.1.

**Source says:** ALL workflow files (`_build-linux.yml`, `_coverage-linux.yml`,
`_build-windows.yml`, `_test-linux.yml`, `_validate-assets.yml`, `_markdown-lint.yml`,
`_package-linux-deb.yml`, `_package-windows.yml`, `sonarcloud.yml`, `docker-ci-image.yml`,
`flawfinder.yml`) use `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd` (v6.0.2).
The v4.1.1 SHA `b4ffde65f...` appears nowhere in any deployed workflow.

**Fix:** Update every SHA reference for `actions/checkout` across all four spec files from
`b4ffde65f46336ab88eb53be808477a3936bae11` (v4.1.1) to
`de0fac2e4500dabe0009e67214ff5f5447ce83dd` (v6.0.2). This is the highest-priority SHA fix
since the spec's v4.1.1 pin is used as an illustrative example in multiple locations.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: sonarcloud.yml] — Sonar workflow not described anywhere in specs

**Spec says:** No mention of a SonarCloud integration workflow in any of the four CI/CD spec
files.

**Source says:** `.github/workflows/sonarcloud.yml` exists and is a complete workflow that:
triggers on `workflow_run` (after CI completes) and `workflow_dispatch`; downloads the
`coverage-sonar-linux-<sha>` artifact (Sonar Generic Coverage XML produced by `gcovr
--sonarqube` in `_coverage-linux.yml`) and the `compile-commands-linux-<sha>` artifact
(normalized `compile_commands.json` produced by `_build-linux.yml`); caches the SonarScanner
CLI binary; remaps paths in `compile_commands.json` from the container workspace prefix to the
runner workspace; and runs SonarScanner with `sonar.cfamily.compile-commands`. The workflow has
`permissions: pull-requests: read`. There is also `sonarcloud-debug.yml` in the deployed
workflows.

**Fix:** Add a new section to `github-actions-workflow.md` describing the SonarCloud integration:
the `sonarcloud.yml` workflow trigger pattern (`workflow_run` on CI completion), the two artifact
downloads (coverage XML and compile_commands), the path remapping requirement, the SonarScanner
CLI caching step, and the `sonar.cfamily.compile-commands` scan parameter. Document that
`_build-linux.yml` produces `compile-commands-linux-<sha>` (normalized via
`.github/scripts/normalize_compile_commands.py`) and `_coverage-linux.yml` produces
`coverage-sonar-linux-<sha>` (via `gcovr --sonarqube`) as SonarCloud input artifacts.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: flawfinder.yml and msvc.yml] — Additional workflows not described

**Spec says:** No mention of `flawfinder.yml` or `msvc.yml`.

**Source says:** Both `flawfinder.yml` (static security analysis via david-a-wheeler/flawfinder,
uploads SARIF to GitHub Security tab) and `msvc.yml` exist in the deployed workflow directory.
Neither is called by `ci.yml` or referenced in `all-checks-pass`.

**Fix:** Add brief documentation of `flawfinder.yml` in `github-actions-workflow.md` as a
supplementary security-analysis workflow that runs independently of the main CI pipeline
(not in `all-checks-pass`). If `msvc.yml` is a legacy or experimental workflow, note its status.


**Status: DONE** — Applied in TASK 1.

---

### [branch-protection.md: all-checks-pass needs list references] — Out of sync with deployed needs list

**Spec says (branch-protection.md):** References the Phase 0 form of `all-checks-pass` as
having `build-linux`, `build-windows`, `coverage-linux`, `markdown-lint` in `needs:`, and Phase
1 adding `validate-assets`. States "The `needs:` list evolves in phases."

**Source says:** The deployed `all-checks-pass` `needs:` list is: `prepare`,
`supply-chain-lint`, `validate-assets`, `build-linux`, `build-windows`, `coverage-linux`,
`markdown-lint`. The "Phase 1 re-registration events" note mentions five upstream jobs
(`build-linux`, `build-windows`, `coverage-linux`, `markdown-lint`, `validate-assets`), which
is now stale (supply-chain-lint and prepare are also upstream).

**Fix:** Update the "Phase 1 — `validate-assets` addition" re-registration note to list all
seven current upstream jobs. Update the registration procedure to note that `supply-chain-lint`
and `prepare` are also upstream of `all-checks-pass` and must have run at least once on the
target branch before they appear in GitHub's branch protection UI autocomplete.


**Status: DONE** — Applied in TASK 1.

---

### [dependency-management.md: Linux default.mhr verification — find command uses github.workspace]

**Spec says:** The Linux `default.mhr` verification step uses:
```bash
if ! find "${{ github.workspace }}" -name "default.mhr" | grep -q .; then
```

**Source says:** `_build-linux.yml` uses a simpler direct path check:
```bash
if ! test -f build/default.mhr; then
```
This uses a relative path (`build/default.mhr`) instead of a `find` across the entire workspace,
which is faster and avoids the `${{ github.workspace }}` vs. container `/__w/` path mismatch.

**Fix:** Update the verification step YAML in `dependency-management.md` to use `test -f
build/default.mhr` (the deployed form). Add a note that relative paths are preferred in container
jobs to avoid the `${{ github.workspace }}` vs `/__w/` mismatch.


**Status: DONE** — Applied in TASK 1.

---

### [dependency-management.md: "Phase 7 CI Deliverables" step name and location] — Step in _build-windows.yml, not ci.yml build-windows

**Spec says:** The DLL verification step is called "Verify required DLLs and HRTF data (all
hard-fail)" and is described as being in `.github/workflows/ci.yml`, `build-windows` job,
Step 18.

**Source says:** The step exists in `.github/workflows/_build-windows.yml` (not `ci.yml`), in
the `build-windows` job, and it is indeed named "Verify required DLLs and HRTF data (all
hard-fail)". There is no longer a monolithic `ci.yml` build-windows job.

**Fix:** Update all references in `dependency-management.md` from `.github/workflows/ci.yml`
to `.github/workflows/_build-windows.yml` for the DLL verification step. Remove the "Step 18"
numbering reference (step numbers in split reusable workflows are not canonical in the spec).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: release job — retry loop and immutable-release conflict handling not described]

**Spec says:** The `release` job is briefly described as using `gh release create` (see SHA-pins discrepancy entry). No detail about the retry loop, immutable-release conflict detection, or `--target` flag is provided.

**Source says:** `ci.yml` `release` job implements a 10-iteration idempotent retry loop: on each iteration it looks up the existing release by tag, deletes it and its orphan tag if found, then calls `gh release create "${TAG}" --title "AI Town ${TAG}" --notes-file /tmp/changelog.md --target "${{ github.sha }}" release-assets/**`. When an immutable-release conflict is detected (error message contains `tag_name was used by an immutable release`), it increments the patch version component and retries with the new tag. The artifact glob is `release-assets/**` (wildcard over the download-artifact output directory). The `--target github.sha` flag pins the release to the exact commit, not just the tag.

**Fix:** Document the `release` job mechanics: the 10-iteration retry loop, the idempotent delete-before-create pattern, the immutable-release conflict escalation (patch version increment), the `--target "${{ github.sha }}"` flag rationale (prevents a tag/commit mismatch if the tag was pre-created), and the `release-assets/**` glob over the multi-artifact download directory. This is purely additive — the release job is not described anywhere in the current spec beyond the SHA-pins entry.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: compute-version — DEFAULT_MAIN fallback value and floor comparison not described]

**Spec says:** The `compute-version` job is described only as "auto-increments the patch version on main pushes and sets a `-develop` suffix on develop pushes." No fallback values or floor comparison logic are described.

**Source says:** The deployed `compute-version` step sets `DEFAULT_MAIN: v0.0.21` and `DEFAULT_DEVELOP: v0.0.0` as named env vars. On main pushes, if `DEFAULT_MAIN` is higher than what `git describe --tags` found (e.g., after a history rewrite that removed old tags), `CURRENT` is replaced with `DEFAULT_MAIN` — a floor that prevents version regression. The develop fallback is `DEFAULT_DEVELOP: v0.0.0` (not `v0.0.1`). The floor comparison is the key mechanism that prevents a CI failure after a deliberate tag history rewrite.

**Fix:** Add detail to the `compute-version` section: document `DEFAULT_MAIN` and `DEFAULT_DEVELOP` as named env vars (not magic literals); document the floor comparison logic for main pushes; note the current values (`v0.0.21` / `v0.0.0`) and that these are updated whenever the pipeline is reset after a history rewrite. Cross-reference this with the `CLAUDE.md` note about `DEFAULT_MAIN` fallback after history rewrites.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-linux — libGLEW.a and default.mhr verification steps not described in detail]

**Spec says (CI_CD_SPECS_FIX.md item 2):** The existing gap entry mentions these steps exist but provides no detail on their implementation or purpose.

**Source says:** `_build-linux.yml` has two verification steps after Build:
- "Verify libGLEW.a artifact (CI-1)": `find /opt/vcpkg_installed -name "libGLEW.a" | grep -q "libGLEW.a"` — targets the container's pre-baked vcpkg path at `/opt/vcpkg_installed` (not `build/vcpkg_installed/`), uses a glob to be triplet-agnostic, and guards against `glew` being removed from `vcpkg.json`.
- "Verify default.mhr HRTF data": `test -f build/default.mhr` — simpler relative-path check (already partly documented in the `dependency-management.md` discrepancy entry).

**Fix:** Document the "Verify libGLEW.a artifact (CI-1)" step: specify the `/opt/vcpkg_installed` target path, the triplet-agnostic glob, and the rationale (container-resident vcpkg install vs workspace vcpkg install). Distinguish from the Windows GLEW check which targets `build/vcpkg_installed/x64-windows/`. This supplements the existing CI_CD_SPECS_FIX.md item 2 fix with specific implementation details.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-linux — intermediate build artifact retention-days: 1 not documented]

**Spec says:** The artifact retention section describes: test XML at 14 days, coverage HTML at 14 days, staging artifacts at 30 days. No intermediate pipeline artifact is mentioned.

**Source says:** `_build-linux.yml` uploads THREE artifacts: (1) `compile-commands-linux-${{ github.sha }}` (path: `compile_commands.json`, `retention-days: 14`, unconditional — produced by the normalize step for SonarCloud); (2) `build-linux-${{ github.sha }}` (full `build/` directory, `retention-days: 1`) — consumed by the `test-linux` job via `actions/download-artifact`; (3) `aitown-staging-linux-${{ github.sha }}` (only `build/aitown` + `build/default.mhr`, `retention-days: 30`, conditional on push to main/develop). The intermediate build artifact with 1-day retention is a pipeline-internal artifact not intended for post-run diagnostic use.

**Fix:** Document all three artifacts. Add the `compile-commands-linux-${{ github.sha }}` artifact (path: `compile_commands.json`, `retention-days: 14`, unconditional) alongside the intermediate build artifact. Document the intermediate build artifact (`build-linux-${{ github.sha }}`, `path: build/`, `retention-days: 1`) as a required pipeline-internal upload step. Explain that `retention-days: 1` is intentional — the artifact is consumed during the same CI run by the `test-linux` job and has no diagnostic value beyond that window. Distinguish it from the staging artifact (30-day retention, packaging use).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — artifact names differ and two additional artifacts not documented]

**Spec says:** The coverage HTML artifact is named `coverage-report-${{ github.sha }}` (no `-linux-` infix). No other coverage artifacts are described.

**Source says:** `_coverage-linux.yml` uploads four artifacts, all with `if: always()` and `retention-days: 14`:
1. `test-results-coverage-linux-${{ github.sha }}` (path: `test_results/` — GTest XML, uploaded after dorny step)
2. `coverage-sonar-linux-${{ github.sha }}` (path: `coverage.xml` — Sonar Generic Coverage XML from `gcovr --sonarqube`)
3. `coverage-report-linux-${{ github.sha }}` (path: `coverage_html/` — HTML report)
4. `coverage-info-linux-${{ github.sha }}` (path: `coverage_filtered.info` — filtered lcov info for post-mortem analysis)

None of the coverage artifact names match the spec's `coverage-report-${{ github.sha }}`.

**Fix:** Update the coverage HTML artifact name to `coverage-report-linux-${{ github.sha }}`. Document all four artifacts produced by `_coverage-linux.yml`: the test results upload (`test-results-coverage-linux-*`), the Sonar XML (`coverage-sonar-linux-*`, consumed by `sonarcloud.yml`), the HTML report (`coverage-report-linux-*`), and the filtered info file (`coverage-info-linux-*`). All four use `if: always()` so they upload even when a coverage gate fails.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-windows — DLL verification checks Irrlicht.dll and GLEW32.dll; spec only mentions soft_oal.dll and default.mhr]

**Spec says:** The Windows DLL verification step checks only `soft_oal.dll` and `default.mhr`. Both are described as hard-fails. The `dependency-management.md` Phase 7 sign-off section also states "Both `soft_oal.dll` AND `default.mhr` must be verified."

**Source says:** `_build-windows.yml` step "Verify required DLLs and HRTF data (all hard-fail)" checks FOUR files: `build\Irrlicht.dll`, `build\GLEW32.dll`, `build\soft_oal.dll`, and `build\default.mhr`. All four are hard-fails. `Irrlicht.dll` and `GLEW32.dll` are copied by post-build rules in Phase 1 CMakeLists.txt and are equally critical for test execution — their absence would cause all test binaries to fail DLL load silently.

**Fix:** Update the Windows DLL verification step YAML in both `github-actions-workflow.md` and `dependency-management.md` to include all four checks: `Irrlicht.dll`, `GLEW32.dll`, `soft_oal.dll`, and `default.mhr`. Update the `dependency-management.md` Phase 7 sign-off sentence to enumerate all four files instead of only two.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-windows — test artifact name is `test-results-build-windows-*`, not `test-results-windows-*`]

**Spec says:** The Windows test results artifact is named `test-results-windows-${{ github.sha }}`.

**Source says:** `_build-windows.yml` uploads with `name: test-results-build-windows-${{ github.sha }}` (with `-build-` infix, matching the job name convention used by Linux builds). The upload step is named "Upload test results (build-windows)".

**Fix:** Update the Windows test results artifact name in the spec from `test-results-windows-${{ github.sha }}` to `test-results-build-windows-${{ github.sha }}`. Update the step name to "Upload test results (build-windows)" to match the deployed name.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: build-windows — "Verify test XML output" step uses PowerShell, not bash]

**Spec says:** The "Verify test XML output exists" step in all three jobs must use `shell: bash` — described as "safe on both Linux and Windows runners via Git Bash."

**Source says:** `_build-windows.yml` implements its "Verify test XML output" step with `shell: pwsh` and `Get-ChildItem -Path test_results -Filter '*.xml' | Measure-Object`:
```yaml
- name: Verify test XML output
  if: always()
  shell: pwsh
  run: |
    $count = (Get-ChildItem -Path test_results -Filter '*.xml' -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($count -eq 0) { Write-Error "..."; exit 1 }
```
The Linux jobs (`_test-linux.yml`) do use `shell: bash` as the spec requires. The step name is "Verify test XML output" (not "Verify test XML output exists").

**Fix:** Update the spec to reflect that the Windows XML verification step uses `shell: pwsh` (not `shell: bash`). The statement that `shell: bash` is "safe on both Linux and Windows via Git Bash" was written before the reusable workflow split; `_build-windows.yml` consistently uses PowerShell throughout. Update the step name to "Verify test XML output" for the Windows variant.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: dorny/test-reporter — continue-on-error: true rationale for Linux-only]

**Spec says:** The spec does not mention `continue-on-error: true` on any dorny step.

**Source says:** `_build-windows.yml`'s dorny step does NOT include `continue-on-error: true`. Only the Linux container jobs (`_test-linux.yml` and `_coverage-linux.yml`) include it. The Windows dorny step has `if: always()` and `fail-on-error: false` but no `continue-on-error: true`. This is a platform-specific difference not described in the spec.

**Fix:** The spec should document that `continue-on-error: true` is a Linux-container-only field on dorny steps. The rationale: inside the GHCR container the checkout UID differs from the runner process UID, which can occasionally cause dorny to produce a non-zero exit code even when `fail-on-error: false` is set — `continue-on-error: true` prevents that exit code from masking the real test result. On the Windows job (which does not run in a container) this scenario does not occur and the field is omitted.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _test-linux.yml — "Fix git safe.directory" step required in test-linux, not only coverage-linux]

**Spec (CI_CD_SPECS_FIX.md item 12):** The existing gap entry documents the "Fix git safe.directory for test reporter" step only as a `coverage-linux` requirement.

**Source says:** `_test-linux.yml` also contains the "Fix git safe.directory for test reporter" step (`git config --global --add safe.directory "$GITHUB_WORKSPACE"` with `if: always()`), placed immediately before the dorny step. Both container jobs (`_test-linux.yml` and `_coverage-linux.yml`) require it — both run inside the GHCR container where the checkout directory is owned by a different UID than the container runner process.

**Fix:** Extend the git safe.directory rule in the spec to apply to ALL Linux container jobs that invoke `dorny/test-reporter`: both `_test-linux.yml` and `_coverage-linux.yml`. The rule should be stated as: any reusable workflow running `dorny/test-reporter` inside a container job must include this `git config` step with `if: always()` immediately before the reporter step.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _test-linux.yml — artifact name is caller-supplied input, not hardcoded]

**Spec says:** The test results artifact upload step is shown with a hardcoded name: `name: test-results-build-linux-${{ github.sha }}`.

**Source says:** In `_test-linux.yml`, the artifact name is `${{ inputs.test_artifact_name }}` — a required reusable workflow input (default: `'test-results-linux'`). The caller (`_build-linux.yml`) passes `test_artifact_name: test-results-build-linux-${{ github.sha }}`. The sha suffix is injected at the call site, not inside the reusable workflow.

**Fix:** Document that `_test-linux.yml` uses a caller-supplied `test_artifact_name` input for the artifact upload. The sha uniqueness requirement applies at the call site: callers MUST pass `test_artifact_name: test-results-<job>-${{ github.sha }}`. The spec's uniqueness requirement section should clarify this indirection — the sha suffix is enforced by convention at the caller, not by the reusable workflow itself.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — "Create test results directory" is a separate named step, not inline mkdir in each ctest step]

**Spec says:** Each ctest `run:` block in `coverage-linux` (and `build-linux`) includes inline `mkdir -p test_results` before the ctest command, e.g.:
```yaml
run: |
  mkdir -p test_results
  ctest --test-dir build -LE ...
```

**Source says:** Both `_coverage-linux.yml` and `_test-linux.yml` have a single dedicated step "Create test results directory" (`run: mkdir -p test_results`) placed before all three ctest steps. Each ctest step's `run:` contains only the `ctest` invocation — no inline `mkdir -p`. The Windows job (`_build-windows.yml`) uses `New-Item -Force` inline in each test step (matching the spec's per-step pattern for that job only).

**Fix:** Update the `coverage-linux` and `_test-linux.yml` YAML examples in the spec to show a single "Create test results directory" step before the three ctest steps. The inline `mkdir -p` form should only appear in Windows YAML examples. Note that both patterns are functionally equivalent but the Linux container jobs use the single pre-test step for clarity.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — lcov --list and genhtml missing --ignore-errors inconsistent --rc check_data_consistency=0]

**Spec says (and CI_CD_SPECS_FIX.md item 10):** Item 10 documents the additional flags needed on `lcov --capture` and `lcov --remove` but does not address `lcov --list` or `genhtml`.

**Source says:** `_coverage-linux.yml` calls both `lcov --list` and `genhtml` with the same consistency flags:
```bash
lcov --list coverage_filtered.info \
  --ignore-errors inconsistent \
  --rc check_data_consistency=0

genhtml coverage_filtered.info --output-directory coverage_html/ \
  --ignore-errors inconsistent \
  --rc check_data_consistency=0
```
Without these flags, `lcov --list` and `genhtml` emit "inconsistent" errors from GCC 13 inline-function data that exit non-zero and abort coverage reporting even after a successful `lcov --capture`.

**Fix:** Add `--ignore-errors inconsistent --rc check_data_consistency=0` to both the `lcov --list` and `genhtml` invocations in the spec. Document that these flags must appear on all four lcov-related invocations (`--capture`, `--remove`, `--list`, `genhtml`) — omitting them from any one invocation causes the GCC 13 inline-function noise to re-surface as a fatal error at that step.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — src/ui/ 25% gate uses direct SF/LH/LF parse, not lcov --list pipeline]

**Spec says:** The `src/ui/` 25% worst-file gate uses an `lcov --list | grep -E "src/ui/" | grep -v "^Total" | awk -F'|' ...` pipeline that depends on `lcov --list` output format.

**Source says:** `_coverage-linux.yml` implements the gate by directly parsing `coverage_filtered.info` using `awk` over SF/LH/LF records — version-agnostic, not dependent on `lcov --list` column format (which changed between lcov 1.x and 2.x). It also includes a preflight guard (`grep -q "SF:.*src/ui/"`) before the awk block. The error message references "Phase 4 gate".

**Fix:** Replace the `lcov --list | grep | awk` pipeline example in the spec with the direct SF/LH/LF parse approach. Note that the `lcov --list` pipeline is format-fragile (column delimiter changed in lcov 2.0) and has been superseded in the deployed source. The `coverage.md` Phase 4 section already documents the correct approach — the spec's `github-actions-workflow.md` example should match it.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: ci.yml — job dependency graph: all build jobs depend on validate-assets]

**Spec says:** The spec does not document the full job dependency graph for `ci.yml` beyond mentioning `needs: [supply-chain-lint, prepare]` in passing.

**Source says:** In `ci.yml`:
- `validate-assets` has `needs: [supply-chain-lint, prepare]`
- `build-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `build-windows` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `coverage-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`
- `compute-version` has NO `needs:` key — it runs independently, gated only by an `if:` condition restricting it to push events on main/develop
- `package-*` and `release` depend on compute-version and the respective build jobs

All three build jobs wait for `validate-assets` before starting. A failing `validate-assets` blocks ALL build jobs — not just `all-checks-pass`.

**Fix:** Add a job dependency graph section to the spec documenting the full `needs:` chain for all jobs. State explicitly that `build-linux`, `build-windows`, and `coverage-linux` all have `needs: [supply-chain-lint, validate-assets, prepare]`. Document the rationale: asset validation gates the build to avoid wasting build minutes compiling code with missing or malformed assets.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: docker-ci-image.yml — CI image build workflow not described anywhere in specs]

**Spec says:** No mention of `docker-ci-image.yml` in any of the four CI/CD spec files.

**Source says:** `.github/workflows/docker-ci-image.yml` manages the `ghcr.io/m0wa/aitown-ci-linux` container image lifecycle. Triggers: push to main/develop touching `docker/ci-linux/Dockerfile`, `vcpkg.json`, or `vcpkg-overlays/**`; `workflow_dispatch` with `force_rebuild` boolean input; monthly schedule (1st of month, 02:00 UTC). Key steps: extract `VCPKG_COMMIT_ID` from `ci.yml` env (a `$GITHUB_ENV` step that must precede the `actions/cache` step — demonstrating the step-ordering visibility rule); validate that `ARG VCPKG_COMMIT` exists in the Dockerfile without a default; run an inline supply-chain lint; build and push with tag `vcpkg-<short-sha>` + `sha256:` digest. Permissions: `packages: write, contents: read`.

**Fix:** Add a `docker-ci-image.yml` section to `github-actions-workflow.md`. Cover: three trigger types, `force_rebuild` input, `VCPKG_COMMIT_ID` extraction step and its ordering requirement, Dockerfile ARG validation, inline supply-chain lint, image tag format (`vcpkg-<short-sha>`), and `packages: write` permission. Cross-reference from `dependency-management.md`'s containerized-approach section.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: msvc.yml — structured MSVC static analysis workflow, not legacy stub]

**Spec (CI_CD_SPECS_FIX.md item 28):** The existing fix entry says only "if `msvc.yml` is a legacy or experimental workflow, note its status."

**Source says:** `msvc.yml` is a full structured workflow: triggers on push/PR to `main` and weekly (Sunday 13:33 UTC); uses `microsoft/msvc-code-analysis-action`; filters SARIF output to a single run (required by `codeql-action/upload-sarif` which rejects multi-run SARIF); uploads results to the GitHub Security tab. Includes vcpkg caching, `ilammy/msvc-dev-cmd`, and `lukka/run-vcpkg`. Has its own independent `VCPKG_COMMIT_ID` env var — NOT derived from `prepare`. This is a maintenance risk: vcpkg baseline updates in `ci.yml` do not automatically propagate to `msvc.yml`.

**Fix:** Document `msvc.yml` properly: trigger schedule, purpose (MSVC static analysis, Security tab upload), SARIF multi-run filter requirement, and the independent `VCPKG_COMMIT_ID` maintenance concern (must be updated manually when the main `ci.yml` baseline is bumped — add this to the vcpkg baseline atomicity checklist in `dependency-management.md`).


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: Windows vcpkg cache key uses inputs.vcpkg_commit_id, not env.VCPKG_COMMIT_ID; restore-keys not documented]

**Spec says:** The Windows vcpkg cache key is `vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('vcpkg.json') }}-${{ env.VCPKG_COMMIT_ID }}`. No `restore-keys:` fallback tiers are described.

**Source says:** `_build-windows.yml` uses `inputs.vcpkg_commit_id` (not `env.VCPKG_COMMIT_ID`) because `env` context is unavailable in reusable workflow jobs. The full key: `vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('vcpkg.json') }}-${{ inputs.vcpkg_commit_id }}`. Two `restore-keys:` fallback tiers: (1) `vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ inputs.vcpkg_commit_id }}-` (same baseline, any vcpkg.json hash); (2) `vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-` (any prior entry for this OS+compiler). Cache path: `C:\Users\runneradmin\AppData\Local\vcpkg\archives`.

**Fix:** Update the Windows vcpkg cache key in `caching.md` to use `inputs.vcpkg_commit_id`. Add the two-tier `restore-keys:` fallback. Document the cache path (`C:\Users\runneradmin\AppData\Local\vcpkg\archives`). Add a note explaining the `inputs.` vs `env.` distinction: in a reusable workflow called via `uses:`, the `env` context is not populated from the caller's `env:` block — values must be passed explicitly via `inputs`.


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md: lcov --capture — --gcov-tool gcov-13 and --rc check_data_consistency=0 not in canonical command]

**Spec says (`coverage.md`):** The canonical `lcov --capture` command uses:
```
lcov --capture --directory build --base-directory $(pwd) \
     --ignore-errors mismatch,inconsistent,version \
     --output-file coverage.info
```
No `--gcov-tool` flag and no `--rc check_data_consistency=0`.

**Source says:** `_coverage-linux.yml` uses:
```bash
lcov --capture --directory "${BUILD_DIR}" --base-directory . \
     --gcov-tool gcov-13 \
     --ignore-errors mismatch,inconsistent \
     --rc check_data_consistency=0 \
     --output-file coverage.info
```
Three differences from `coverage.md`: (1) `--gcov-tool gcov-13` absent from spec; (2) `--rc check_data_consistency=0` absent from spec; (3) spec adds `version` to `--ignore-errors` but source uses only `mismatch,inconsistent`.

**Fix:** In `coverage.md`, add `--gcov-tool gcov-13` and `--rc check_data_consistency=0` to the canonical `lcov --capture` command. Reconcile `--ignore-errors`: align `coverage.md` to drop `version` (matching deployed source), or note that `version` is a locally recommended addition not present in deployed CI. Use `--base-directory .` instead of `$(pwd)` in `coverage.md` to match the source and avoid the container path mismatch.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: lcov --remove — six additional exclude patterns, FetchContent path change, and --rc check_data_consistency=0 not present]

**Spec says (`coverage.md`):** The `lcov --remove` command excludes: `/usr/*`, `"*/.fetchcontent_cache/*"`, `*/tests/*`, mock/manual file patterns, `*/src/rendering/*`, `*/src/audio/*`, `*/src/platform/*`. Uses `--ignore-errors unused,inconsistent`.

**Source says:** `_coverage-linux.yml` adds six additional exclude patterns not in `coverage.md`:
- `/opt/vcpkg_installed/*` — container pre-baked vcpkg headers
- `$(pwd)/vcpkg_installed/*` — workspace vcpkg fallback
- `$(pwd)/build/vcpkg_installed/*` — build-dir vcpkg headers
- `*/src/simulation/*.h` — interface/inline headers excluded to avoid double-counting
- `*/src/ui/*.h` — same rationale
- `*/src/interfaces/*.h` — same rationale

Additionally, the FetchContent pattern changes from `"*/.fetchcontent_cache/*"` (glob, in `coverage.md`) to `"$(pwd)/.fetchcontent_cache/*"` (absolute via `$(pwd)`, in source) — avoiding the glob form which can match unexpected paths inside the container.

Also adds `--rc check_data_consistency=0` to `lcov --remove` (required on all four lcov invocations).

**Fix:** Add all six additional patterns to `lcov --remove` in `coverage.md`. Update the FetchContent exclude pattern from `"*/.fetchcontent_cache/*"` to `"$(pwd)/.fetchcontent_cache/*"`. Document rationale for each group: vcpkg patterns exclude pre-installed container headers; header-only patterns exclude inline headers from double-counting; `$(pwd)` form is preferred over glob inside containers. Add `--rc check_data_consistency=0` to the `lcov --remove` invocation. Note that this flag must appear on all four lcov invocations — `--capture`, `--remove`, `--list`, and `genhtml`.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md: top-level apt-get install language conflicts with containerized CI reality]

**Spec says (`headless-ci-testing.md`):** The top-level section states: "Linux CI runner requires virtual display only for requires-opengl tests: install `xvfb` + `libgl1-mesa-dev` + `libxxf86vm-dev`." The phrasing implies CI runners perform runtime `apt-get install` for these packages.

**Source says:** The "Containerised CI (Phase 11b)" section in the same file correctly states packages are pre-installed in the CI base image with no `apt-get install` step. The deployed `_build-linux.yml`, `_coverage-linux.yml`, and `_test-linux.yml` contain no `apt-get install` step. The top-level description is accurate only for pre-Phase-11b history.

**Fix:** Restructure `headless-ci-testing.md` to move the package installation list into a Dockerfile requirement note rather than a CI-job-step requirement. The top-level description should reflect the current deployed state (packages pre-installed in `ghcr.io/m0wa/aitown-ci-linux` via `docker/ci-linux/Dockerfile`; no runtime `apt-get` step in CI jobs). Keep the package list as a record of what must remain in the Dockerfile.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: routing check grep patterns — rationale for recommending uniform -cE 'Test +#']

**Spec says:** The spec uses `grep -c 'Test #'` uniformly for all routing checks, with no `-E` flag.

**Source says:** The deployed `_test-linux.yml` uses non-uniform patterns: unit and simulation_tests checks use `grep -cE 'Test +#'`; integration and requires-opengl checks use `grep -c 'Test #'` (literal). The literal pattern works in practice for integration and requires-opengl because their test counts are currently in single digits (CTest emits `Test #1:` with one space), but it would silently undercount if any check ever reached double-digit tests (CTest emits `Test  #1:` with two leading spaces for alignment, which breaks `'Test #'` literal matching).

**Fix:** Update the spec to recommend `grep -cE 'Test +#'` for ALL four routing checks (unit, integration, requires-opengl, simulation_tests). The `-E 'Test +#+'` pattern is future-proof: `Test +#` matches both the single-digit form (`Test #1:`, one space) and the multi-digit alignment form (`Test  #1:`, two spaces). Apply uniformly for consistency — the spec should not prescribe different patterns for different labels.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: release job — full softprops YAML block still present in spec body]

**Spec says:** The `## release Job` section (beyond the SHA-pins table) contains a full YAML step block showing `uses: softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564` with `fail_on_unmatched_files: true`, a separate "Create version tag via REST API" prerequisite step, and "Tag-creation design notes" describing the idempotent REST `gh api .../git/refs` pattern.

**Source says:** The deployed `ci.yml` release job uses ONLY `gh release create` (GitHub CLI). There is no `softprops/action-gh-release` step anywhere and no separate REST tag-creation step. The SHA-pins discrepancy (CI_CD_SPECS_FIX.md item 4) already notes this, but the spec body still contains the full stale YAML block and design-note prose describing the softprops pattern.

**Fix:** Remove the entire `softprops/action-gh-release` YAML step block, the "Create version tag via REST API" step, and the associated "Tag-creation design notes" from the `release Job` section. Replace with the `gh release create` loop pattern and the immutable-release conflict handling already described in CI_CD_SPECS_FIX.md's release job mechanics entry. Remove `fail_on_unmatched_files: true` (it is a softprops input, not a `gh` flag).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: docker-ci-image.yml — Docker action SHA pins not documented; timeout-minutes: 120 and cron expression missing]

**Spec says:** The CI_CD_SPECS_FIX.md entry for `docker-ci-image.yml` prescribes documenting triggers, `force_rebuild` input, VCPKG extraction, ARG validation, supply-chain lint, image tag format, and `packages: write` permission. It does not mention action SHAs, job timeout, or the cron expression.

**Source says:** `docker-ci-image.yml` uses three SHA-pinned Docker actions not listed in any spec:
- `docker/login-action@4907a6ddec9925e35a0a9e82d7399ccc52663121` (v4.1.0)
- `docker/setup-buildx-action@4d04d5d9486b7bd6fa91e7baf45bbb4f8b9deedd` (v4.0.0)
- `docker/build-push-action@d08e5c354a6adb9ed34480a06d141179aa583294` (v7.0.0)

The build job has `timeout-minutes: 120` (image builds with vcpkg compilation from source can exceed an hour on cache miss). The monthly schedule cron is `'0 2 1 * *'` (1st of month, 02:00 UTC).

**Fix:** When adding the `docker-ci-image.yml` section (as the existing fix entry prescribes), include the three Docker action SHA pins in the pinned-actions table in `caching.md`. Document `timeout-minutes: 120` with its rationale (vcpkg source compilation on cache miss). Document the monthly cron `'0 2 1 * *'` explicitly.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: flawfinder.yml — trigger branches, cron, and action SHA pins not documented]

**Spec says:** The CI_CD_SPECS_FIX.md entry for `flawfinder.yml` says only to add "brief documentation" noting it runs independently. It does not describe trigger branches, cron schedule, or SHA pins.

**Source says:** `flawfinder.yml` triggers on push/PR to `main` ONLY (not `develop`). Weekly cron: `'26 9 * * 6'` (Saturday 09:26 UTC). SHA-pinned actions not listed in any spec:
- `david-a-wheeler/flawfinder@8e4a779ad59dbfaee5da586aa9210853b701959c`
- `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1)

No `timeout-minutes` on the job.

**Fix:** Document `flawfinder.yml` fully: triggers on push/PR to `main` only (not `develop`), weekly Saturday cron, no `timeout-minutes` (flag as a gap to address). Add `david-a-wheeler/flawfinder` and `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1) SHA pins to `caching.md`'s pinned-actions table.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: msvc.yml — codeql-action/upload-sarif version inconsistency with flawfinder.yml; microsoft/msvc-code-analysis-action SHA undocumented]

**Spec says:** No SHA pins documented for `msvc.yml` in any spec file.

**Source says:** `msvc.yml` uses `github/codeql-action/upload-sarif@0e9f55954318745b37b7933c693bc093f7336125` (v4.35.1) while `flawfinder.yml` uses `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1) — two different major versions of the same action in deployed workflows. Additionally, `msvc.yml` uses `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99` (undocumented). Only `msvc.yml` has its own independent `VCPKG_COMMIT_ID` env var (not derived from `prepare`); `flawfinder.yml` performs no vcpkg work and has no `VCPKG_COMMIT_ID` variable.

**Fix:** Add `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99` and `github/codeql-action/upload-sarif@0e9f55954318745b37b7933c693bc093f7336125` (v4) to the spec's pinned-actions table. Flag the v3 vs v4 version inconsistency between `flawfinder.yml` and `msvc.yml` as a maintenance concern — recommend standardising on v4 (v4.35.1) in a follow-up PR. Document that `msvc.yml` has its own independent `VCPKG_COMMIT_ID` env var that must be updated manually on every baseline bump (extend the vcpkg baseline atomicity checklist to include `msvc.yml`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _package-linux-deb.yml — vcpkg cache path, key format, and codename component not documented]

**Spec says:** The `package-linux-deb` section does not describe any `actions/cache` step for vcpkg.

**Source says:** `_package-linux-deb.yml` uses `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830` (v4.3.0) with:
- `path: /opt/vcpkg/packages`
- `key: vcpkg-deb-${{ matrix.codename }}-${{ hashFiles('vcpkg.json') }}-${{ inputs.vcpkg_commit_id }}`
- Two-tier `restore-keys:` with codename-scoped fallbacks

The `matrix.codename` component is mandatory — different distros (bookworm, trixie, jammy, noble) have different glibc versions producing ABI-incompatible vcpkg archives that cannot be shared across codenames.

**Fix:** Document the `_package-linux-deb.yml` vcpkg cache in `caching.md`: path `/opt/vcpkg/packages`, key format including `matrix.codename`, two-tier restore-keys, and the ABI-isolation rationale. Note this job uses `actions/cache` v4.3.0 (same as `sonarcloud.yml`) — adding this as a fourth location where the v4.3.0 pin appears.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _package-windows.yml — "Verify aitown.exe" step missing; no actions/cache step for vcpkg]

**Spec says:** The `package-windows` step sequence lists: checkout → NSIS install → msvc-dev-cmd → run-vcpkg → resolve version → CMake configure → append vcpkg bin → download staging → CPack → upload installer. No verification step between download and CPack.

**Source says:** `_package-windows.yml` inserts "Verify aitown.exe in staging artifact" (`Test-Path "build\aitown.exe"` hard-fail) between download and CPack. Additionally, unlike `_build-windows.yml`, `_package-windows.yml` has NO `actions/cache` step for vcpkg — it relies entirely on `lukka/run-vcpkg`'s internal binary caching with no GitHub-hosted cache persistence between runs, making cold-cache packaging runs potentially exhaust the 60-minute timeout.

**Fix:** Add "Verify aitown.exe in staging artifact" as a step between "Download staging artifact" and "CPack" in the `package-windows` spec. Document that `_package-windows.yml` has no explicit `actions/cache` step for vcpkg and flag this as a known performance gap (cold-cache runs may approach the 60-minute timeout ceiling).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _package-linux-deb.yml — git init/fetch pattern for non-empty vcpkg dir not described]

**Spec says:** Step 3 of `package-linux-deb` describes: "Set up vcpkg — clone + `git checkout $VCPKG_COMMIT_ID`" as a simple clone.

**Source says:** `_package-linux-deb.yml` uses `if [ ! -d /opt/vcpkg/.git ]` before `git init && git remote add origin ... && git fetch --depth=1 origin $VCPKG_COMMIT_ID && git checkout FETCH_HEAD`. This handles the case where `actions/cache` restores `/opt/vcpkg/packages/` into a directory that already exists (making plain `git clone` fail on a non-empty directory). Also, `VCPKG_TOOL_TAG` is read from `scripts/vcpkg-tool-metadata.txt` (not hardcoded) for self-describing version extraction.

**Fix:** Replace the "clone + checkout" description with the `init`/`fetch`/`checkout FETCH_HEAD` pattern. Document why: `actions/cache` restores package archives into `/opt/vcpkg/packages/` before the vcpkg setup step, pre-creating the parent directory and causing `git clone` to fail. Note that `VCPKG_TOOL_TAG` is read from `scripts/vcpkg-tool-metadata.txt`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: sonarcloud.yml — workflow_dispatch inputs, debug step, and full sonar-scanner parameters not documented]

**Spec says:** CI_CD_SPECS_FIX.md's existing entry for `sonarcloud.yml` prescribes documenting: trigger pattern, two artifact downloads, path remapping, SonarScanner CLI caching, and `sonar.cfamily.compile-commands`. It does not mention `workflow_dispatch` inputs or the diagnostic step.

**Source says:** `sonarcloud.yml` has `workflow_dispatch` with two required inputs: `ci_run_id` (CI run ID to pull artifacts from) and `head_sha` (HEAD SHA of that run — used to construct artifact names). A permanent diagnostic step "Debug compile_commands.json paths" (inline Python) prints path information and lists missing files. The `sonar-scanner` invocation includes non-obvious parameters: `sonar.sources=src,.github/workflows,docker,.devcontainer` (not just `src`), `sonar.tests=tests`, `sonar.exclusions="assets/**,build/**,tools/**"`, `sonar.cpd.exclusions="tests/**"`.

**Fix:** When documenting `sonarcloud.yml`, add: the `workflow_dispatch` inputs (`ci_run_id` for artifact retrieval, `head_sha` for artifact name construction); the permanent "Debug compile_commands.json paths" diagnostic step; the full `sonar-scanner` parameter set including `sonar.sources` covering `.github/workflows`, `docker`, and `.devcontainer` — and note why those non-source directories are included (static analysis of workflow YAML and Dockerfile logic is intentional).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: _test-linux.yml — timeout-minutes: 30 for test-linux job not in timeout table]

**Spec says:** The timeout table lists `build-linux: 30`. This refers to the compile job. No separate timeout is documented for the `test-linux` job.

**Source says:** `_test-linux.yml` job `test-linux` has `timeout-minutes: 30`. Since `_build-linux.yml` calls `_test-linux.yml` via `uses:`, the `build-linux` pipeline path consists of two sequential jobs each with their own 30-minute window — up to 60 minutes total for compile + test.

**Fix:** Add `test-linux: 30` to the timeout table and document that the `build-linux` pipeline path has two sequential jobs (compile: 30 min, test: 30 min), for a maximum path length of 60 minutes.


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md: lcov --capture — base-directory uses ${{ github.workspace }} in github-actions-workflow.md spec YAML with prose rationale recommending it over `.`]

**Spec says (`github-actions-workflow.md` lcov capture YAML block):** The spec uses `--base-directory ${{ github.workspace }}` with prose rationale: "is preferred because: (1) it is explicit and self-documenting; (2) `$(pwd)` varies when lcov is invoked from a script."

**Source says:** `_coverage-linux.yml` uses `--base-directory .`. Inside a container job, `${{ github.workspace }}` resolves to the host path (`/__w/repo/repo`) which differs from the container working directory, causing path mismatches in coverage report source attribution. `$(pwd)` and `.` both work correctly inside the container. The spec's prose recommendation of `${{ github.workspace }}` over `.` is actively incorrect for container jobs and would cause a coverage reporting failure if followed.

**Fix:** Replace `--base-directory ${{ github.workspace }}` with `--base-directory .` in the `github-actions-workflow.md` lcov capture YAML block. Remove the prose rationale recommending `${{ github.workspace }}`. Add a note: in container jobs, `${{ github.workspace }}` resolves to the host path which differs from the container working directory; use `.` (current directory) instead. This is distinct from CI_CD_SPECS_FIX.md item 35 which addresses `coverage.md` only.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: ccache-action SHA in YAML code blocks is v1.2.14; source uses v1.2.22]

**Spec says (inline YAML code blocks in build-linux and coverage-linux ccache setup sections):** Both code blocks show `hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9 # v1.2.14`.

**Source says:** Both `_build-linux.yml` and `_coverage-linux.yml` use `hendrikmuhs/ccache-action@33522472633dbd32578e909b315f5ee43ba878ce # v1.2.22`. The SHA-pins table discrepancy is already documented (CI_CD_SPECS_FIX.md item 4), but the inline YAML code blocks in the spec sections still contain the stale SHA. A developer following the inline spec YAML to implement `build-linux` would embed the wrong SHA.

**Fix:** Update both ccache YAML code blocks in `github-actions-workflow.md` (build-linux and coverage-linux ccache setup sections) from `@ed74d11c0b343532753ecead8a951bb09bb34bc9 # v1.2.14` to `@33522472633dbd32578e909b315f5ee43ba878ce # v1.2.22`.


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md: src/simulation/ 85% per-file awk pattern — source restricts to .cpp$ but spec matches all files under src/simulation/]

**Spec says (`coverage.md` Phase 11 awk):** The awk pattern matches `SF:.*src\/simulation\/` — any file path under `src/simulation/` including headers.

**Source says:** `_coverage-linux.yml` uses `SF:.*src\/simulation\/.*\.cpp$` — explicitly restricted to `.cpp` files only. The `lcov --remove` step already excludes `*/src/simulation/*.h`, so no headers should appear in `coverage_filtered.info`. However, the awk restriction to `\.cpp$` is defense-in-depth against header slip-through and makes the intent explicit.

**Fix:** Update the `src/simulation/` 85% per-file floor awk in `coverage.md` § Phase 11 to use `SF:.*src\/simulation\/.*\.cpp$`. Add a note explaining the `\.cpp$` restriction is defense-in-depth — headers are already excluded by `lcov --remove` but the awk pattern makes the `.cpp`-only intent explicit.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: src/ui/ 25% worst-file gate awk — source uses sort -n | head -1 pipeline; spec uses awk END block for minimum]

**Spec says (`coverage.md` Phase 4 awk):** The minimum per-file coverage is tracked inside `in_ui && /^end_of_record/` handler: `if (min_pct == "" || pct < min_pct+0) { min_pct=pct; min_file=fname }`. The `END` block performs the gate check: `if (min_pct+0 < 25.0) { ... exit 1 } else { ... printf "PASS..." }`. This is the spec's awk-internal accumulator form.

**Source says:** `_coverage-linux.yml` computes the minimum differently — it uses `print (lh/lf)*100` inside the awk body (per-file output) and pipes the results to `sort -n | head -1` in the shell to extract the minimum. The minimum is selected outside awk via shell pipeline rather than tracked inside awk state.

**Fix:** Update the `coverage.md` Phase 4 `src/ui/` awk to use the `sort -n | head -1` pipeline form matching the deployed source. Note that both forms produce identical results; the pipeline form is used in deployment.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md: "Phase 11b" future-work framing for containerized CI is stale; containerization is now deployed]

**Spec says (`headless-ci-testing.md`):** The section header "Containerised CI (Phase 11b)" frames containerization as future work with language like "When `build-linux` and `coverage-linux` switch to `container: image:` mode (Phase 11b)." A reference to a "temporary `test-container-xvfb` spike PR job (Model A)" implies the transition is in progress.

**Source says:** All three Linux jobs (`_build-linux.yml`, `_test-linux.yml`, `_coverage-linux.yml`) already run in `container: image: ghcr.io/m0wa/aitown-ci-linux:...`. Container mode is the current deployed state. No `test-container-xvfb` job exists in `ci.yml`.

**Fix:** Rewrite the "Containerised CI (Phase 11b)" section to use past tense and describe current state. Change the header to "Containerised CI (deployed)". Replace "when they switch" with "these jobs run in". Remove the `test-container-xvfb` spike PR reference — it was a transition artifact and is not deployed. This supplements CI_CD_SPECS_FIX.md item 38 which addresses only the top-level `apt-get install` language.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: validate-assets — "Do NOT pin mutagen" instruction still present in spec body]

**Spec says (lines ~694–695):** "Do NOT pin `mutagen` to a specific version — use `pip install mutagen` without version pinning to always use the latest compatible release."

**Source says:** `_validate-assets.yml` pins `mutagen==1.47.0` and `Pillow==10.4.0`. The "Do NOT pin" instruction in the spec is an active incorrect directive that would cause future contributors to deliberately unpin dependencies, breaking reproducibility.

**Fix:** Remove the "Do NOT pin `mutagen` to a specific version" sentence from the spec. Replace with: "Pin all Python dependencies to exact versions for reproducibility. Current pins: `mutagen==1.47.0`, `Pillow==10.4.0`." This is distinct from CI_CD_SPECS_FIX.md item 20 which addresses the pip install step content — this entry addresses the explicit anti-pinning instruction that must be removed.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — step-numbered ordering list still references apt-get, actions/cache, lukka/run-vcpkg as steps 1–4]

**Spec says:** The `coverage-linux` mandatory step ordering list starts: "(1) Install system dependencies (apt-get)" / "(3) actions/cache for vcpkg" / "(4) lukka/run-vcpkg — install vcpkg packages" / "(5) Set up ccache."

**Source says:** `_coverage-linux.yml` runs in a pre-baked GHCR container. Steps 1 (apt-get), 3 (actions/cache vcpkg), and 4 (lukka/run-vcpkg) are entirely absent from the deployed job. The actual first steps are: Checkout → Detect GCC version → Set up ccache → Configure CMake → Build.

**Fix:** Replace the entire numbered step list for `coverage-linux` with the deployed ordering. Remove all references to apt-get, actions/cache for vcpkg, and lukka/run-vcpkg from the `coverage-linux` step sequence. The step count shrinks significantly because the containerized job does not perform runtime package installation.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: coverage-linux — gates described as inside single `run:` block; source uses separate named steps]

**Spec says:** The spec describes steps 16 and 16b–16c as being "inside step 16 (the lcov capture-and-gate `run:` block)" — all of lcov capture, filter, list, genhtml, awk gates, and gcovr inside a single named step.

**Source says:** `_coverage-linux.yml` implements each gate as a separate named YAML step:
1. "Generate coverage report" (lcov capture + filter + list + genhtml)
2. "Generate Sonar Generic Coverage XML" (gcovr)
3. "Preflight src/simulation/ coverage entries" (warning-only)
4. "Enforce src/simulation/ 85% per-file floor (Phase 11)" (hard-fail)
5. "Enforce 95% total line coverage gate" (hard-fail)
6. "Enforce src/ui/ 25% worst-file coverage gate" (hard-fail)
7. "Check src/ui/ coverage completeness" (`if: always()`)

Separate steps give individual failure messages in the GitHub Actions UI — a critical operational difference from a single opaque run block.

**Fix:** Update the `coverage-linux` section to document seven distinct named steps (matching their deployed names). Remove "inside step 16" language. Show each gate as its own YAML step with its specific step name.


**Status: DONE** — Applied in TASK 1.

---

### [framework.md: simulation_tests DISCOVERY_TIMEOUT — "apply to ALL test targets" prose can be misread as all using 30s]

**Spec says (`framework.md`):** Line ~33: "Without `DISCOVERY_TIMEOUT 30`, CMake test discovery times out... Apply a `DISCOVERY_TIMEOUT` to ALL test targets." Later the spec shows `DISCOVERY_TIMEOUT 60` for simulation_tests and terrain_tests with a comment explaining the 60s override.

**Source says:** The spec is internally correct (30s default for most, 60s override for simulation/terrain) but the "apply to ALL test targets" phrasing can be misread as prescribing 30s for ALL targets — contradicting the 60s override shown in the code examples.

**Fix:** Clarify `framework.md` line ~33 to: "Apply `DISCOVERY_TIMEOUT` to ALL test targets — use 30s for most targets, 60s for `simulation_tests` and `terrain_tests` (see override table). Without any `DISCOVERY_TIMEOUT`, CMake defaults to 5s which is insufficient for coverage-instrumented binaries on loaded CI runners."


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: package-windows — `needs:` list missing `prepare`]

**Spec says:** The spec describes `package-windows` as depending on `build-windows` and `compute-version`, and shows a `needs: [build-windows, compute-version]` example.

**Source says:** `ci.yml` line 192 shows `package-windows` with `needs: [build-windows, prepare, compute-version]` — `prepare` is required because the job consumes the `vcpkg_commit_id` output from `prepare` to construct artifact names.

**Fix:** Update the `package-windows` entry in `github-actions-workflow.md` to list `needs: [build-windows, prepare, compute-version]` (three-way dependency). Explain that `prepare` is needed for its `vcpkg_commit_id` output which flows into artifact name construction.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: timeout-minutes table — `supply-chain-lint` value missing]

**Spec says:** The `github-actions-workflow.md` timeout table documents several job timeouts but does not include an entry for `supply-chain-lint`.

**Source says:** The `supply-chain-lint` job in `ci.yml` has NO `timeout-minutes` entry. The four jobs with explicit timeouts are: `prepare` (2 min), `compute-version` (5 min), `all-checks-pass` (5 min), and `release` (10 min). `supply-chain-lint` relies on GitHub Actions' default runner timeout (6 hours), which is excessive for a linting job.

**Fix:** Add `supply-chain-lint` to the timeout table in `github-actions-workflow.md` noting that it currently has no explicit `timeout-minutes` set (differs from the other bounded jobs). Recommend adding a 5-minute cap to prevent runaway lint steps from blocking the pipeline.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: container jobs — `options: --user root` not documented]

**Spec says:** The spec describes container jobs using a GHCR image but does not mention the `options:` key on any container job.

**Source says:** `_build-linux.yml`, `_test-linux.yml`, and `_coverage-linux.yml` all specify `options: --user root` on their `container:` block. This is required because the pre-baked GHCR image's default user is non-root, which causes permission failures when writing to `$GITHUB_WORKSPACE` and `/home/runner`.

**Fix:** Add an `options: --user root` note to the container job documentation. Explain that this is mandatory for all three Linux container jobs — without it, file-ownership mismatches block artifact upload and workspace writes.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-windows.yml` — PATH append method not documented]

**Spec says:** The spec does not document the method used to append the DLL directory to PATH in `_package-windows.yml`.

**Source says:** `_package-windows.yml` uses `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append` to append the path. The `-Encoding utf8` flag is required to prevent PowerShell from writing a UTF-16 BOM that would corrupt `$GITHUB_PATH`; bare `>>` redirection in PowerShell 5.1 uses UTF-16 LE by default.

**Fix:** Document in `github-actions-workflow.md` that PATH appends on Windows CI must use `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append`, not `>> $env:GITHUB_PATH`. Explain the BOM corruption risk with bare `>>` in PowerShell 5.1.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_validate-assets.yml` — check identifier is `check_24_clouds_png`, not `check_24`]

**Spec says:** The spec's asset-validation section refers to cloud sky check as `check_24` without the `_clouds_png` suffix.

**Source says:** `_validate-assets.yml` uses the identifier `check_24_clouds_png` (full descriptive name, not a bare numbered alias).

**Fix:** Update all references in `github-actions-workflow.md` from `check_24` to `check_24_clouds_png` wherever the clouds-PNG asset validation check is named.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: artifact glob rules — `release-assets/**` exception to anti-wildcard rule not documented]

**Spec says:** The spec states that artifact upload `path:` values must not use bare wildcards and should list explicit paths.

**Source says:** The `release` job in `ci.yml` uses `path: release-assets/**` — a wildcard glob — because the release assets directory contains files with version-stamped names unknown at workflow-authoring time. This is a deliberate documented exception to the anti-wildcard rule.

**Fix:** Add a note in `github-actions-workflow.md` that `release-assets/**` is an approved exception to the anti-wildcard rule. Explain the rationale: release artifact filenames include the computed version string and cannot be hard-coded.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: SonarCloud job — `.github/scripts/normalize_compile_commands.py` script undocumented]

**Spec says:** The spec describes the SonarCloud job's path-remapping step at a high level ("remap container paths to workspace paths") but does not mention the helper script used to perform this remapping.

**Source says:** The path-remapping happens in two distinct stages across two files:
1. `_build-linux.yml` (step "Normalize compile_commands.json for SonarCloud") calls `python3 .github/scripts/normalize_compile_commands.py` to strip container-internal path prefixes before uploading the `compile-commands-linux-*` artifact.
2. `sonarcloud.yml` performs a second downstream remap via an **inline Python heredoc block** (lines ~59–79) using `re.compile(r"/__w/[^/]+/[^/]+")` — it does NOT call `normalize_compile_commands.py`; it rewrites paths in the downloaded artifact to match `${{ github.workspace }}`.

Neither the existence of the helper script nor the two-stage remapping architecture is described in the spec.

**Fix:** Add a subsection documenting the two-stage path normalization: (1) `normalize_compile_commands.py` called from `_build-linux.yml` during build to strip container paths before artifact upload; (2) `sonarcloud.yml` inline Python remap of `/__w/<owner>/<repo>` prefix to `${{ github.workspace }}` on the downloaded artifact. Document that both stages must be updated if the container workspace path changes.


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: `msvc.yml` — third deployment of `actions/cache` v4.3.0 (`@0057852`) not listed]

**Spec says:** The `caching.md` stale SHA table documents `actions/cache v4.3.0 (@0057852)` as used in `sonarcloud.yml` and `_package-linux-deb.yml` (two locations).

**Source says:** `msvc.yml` also uses `actions/cache@0057852` (v4.3.0) — making it three locations using the stale version, not two.

**Fix:** Update the stale-SHA table entry for `actions/cache v4.3.0` to list all three files: `sonarcloud.yml`, `_package-linux-deb.yml`, and `msvc.yml`.


**Status: DONE** — Applied in TASK 1.

---

### [headless-ci-testing.md / coverage.md: "Verify test XML output exists" step — absent from `_coverage-linux.yml`]

**Spec says:** `headless-ci-testing.md` and `coverage.md` are both silent on whether a "Verify test XML output exists" step is required for `_coverage-linux.yml`. Neither spec file mandates this verification step for the coverage job.

**Source says:** `_test-linux.yml` and `_build-windows.yml` both include a "Verify test XML output exists" step that checks `test_results/` is non-empty after the test run. `_coverage-linux.yml` does NOT have this step, even though it also uses `GTEST_OUTPUT=xml:test_results/` and uploads XML results to the dorny reporter. The coverage job's XML output is unverified.

**Fix:** Update `headless-ci-testing.md` to explicitly mandate a "Verify test XML output exists" step for ALL jobs that use `GTEST_OUTPUT=xml:test_results/` — including `_coverage-linux.yml`. The current spec is silent on this requirement for the coverage job, leaving a gap where silent `GTEST_OUTPUT` misconfiguration would go undetected.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md / coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: simulation preflight step — `# change to exit 1 after Phase 6` comment not documented]

**Spec says:** `coverage.md` describes the simulation preflight step without mentioning that it is currently warning-only pending Phase 6, and that the `exit 1` escalation is deferred.

**Source says:** `_coverage-linux.yml`'s "Preflight src/simulation/ coverage entries" step (lines ~183–184) contains: `"After Phase 6 implementation, change this step to exit 1 on missing simulation entries."` The step currently prints a warning and continues; it does NOT hard-fail. This is distinct from the 95% total line gate (lines 214–232), which IS a hard-fail (`exit 1`) as deployed.

**Fix:** Update `coverage.md` to distinguish between the two gate types: (1) the simulation preflight is currently warning-only with a deferred `exit 1` comment tied to Phase 6 completion; (2) the 95% total line gate is an unconditional hard-fail as deployed. The spec should document the staged activation of the simulation preflight so engineers understand why it does not block the pipeline yet.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: Makefile lcov invocations — four divergences from CI `_coverage-linux.yml`]

**Spec says:** `coverage.md` documents the `make test` lcov invocation as the canonical form for local coverage runs, showing flags consistent with the CI deployment.

**Source says:** The Makefile's lcov invocations diverge from `_coverage-linux.yml` in four ways:
1. `lcov --remove` and `genhtml` in the Makefile are missing `--rc check_data_consistency=0` (present in CI).
2. `genhtml` in the Makefile is missing `--ignore-errors inconsistent` (present in CI to suppress GCC 13 inline noise in HTML generation).
3. The Makefile includes extra `--ignore-errors version,empty` flags on `lcov --capture` that are intentional local-only additions (devcontainer GCC version mismatch) absent from CI.
4. The Makefile is missing three header-specific exclude patterns that CI adds to `lcov --remove`: `'*/src/simulation/*.h'`, `'*/src/ui/*.h'`, and `'*/src/interfaces/*.h'`. (Note: `'*/src/rendering/*'`, `'*/src/audio/*'`, and `'*/src/platform/*'` ARE present in both the Makefile and CI — those are not the missing ones.)

**Fix:** Add a table to `coverage.md` documenting the four intentional divergences between `make test` (local) and `_coverage-linux.yml` (CI), with a rationale column explaining why each divergence exists. This prevents future engineers from "fixing" intentional local-only flags.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md / headless-ci-testing.md: `_test-linux.yml` `BUILD_DIR` env var — spec hardcodes `build`]

**Spec says:** The spec's ctest command examples in `coverage.md` and `headless-ci-testing.md` hardcode `--test-dir build` in all ctest invocations.

**Source says:** `_test-linux.yml` declares a job-level `env: BUILD_DIR: ${{ inputs.build_dir }}` and uses `${BUILD_DIR}` throughout all routing checks and ctest invocations. This allows callers to override the build directory via the `build_dir` workflow input.

**Fix:** Update ctest examples in `coverage.md` and `headless-ci-testing.md` to use `${BUILD_DIR}` (referencing an env var) rather than hardcoding `build`, and document the `build_dir` input parameter and its default value.


**Status: SKIPPED** — Fix targets `coverage.md / headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [coverage.md: `_coverage-linux.yml` ctest steps — hardcode `--test-dir build`; `BUILD_DIR` is only shell-local in lcov block]

**Spec says:** The spec implies a consistent `BUILD_DIR` variable is used throughout `_coverage-linux.yml` steps.

**Source says:** In `_coverage-linux.yml`, the ctest invocation steps hardcode `--test-dir build` (they do NOT use `${BUILD_DIR}`). The `BUILD_DIR` variable is only defined as a shell-local variable inside the lcov `run:` block and is not a job-level env var. This is an asymmetry with `_test-linux.yml` (which uses job-level `BUILD_DIR`) and means the coverage job cannot vary its build directory via input.

**Fix:** Document in `coverage.md` that `_coverage-linux.yml` uses hardcoded `--test-dir build` in ctest steps (unlike `_test-linux.yml`'s parameterized `${BUILD_DIR}`), and that `BUILD_DIR` in the lcov block is a shell-local convenience alias, not a job-level input parameter.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md: ctest `--parallel` flag — intentionally absent; rationale undocumented]

**Spec says:** The spec does not mention the `--parallel` flag for ctest, leaving its omission unexplained.

**Source says:** All ctest invocations across `_test-linux.yml`, `_coverage-linux.yml`, and `_build-windows.yml` omit `--parallel`. This is intentional: parallel test execution causes non-deterministic interleaving of OpenAL Soft's global device context on the CI runner (only one null driver instance is available), producing spurious test failures in audio unit tests. Serial execution is required for correctness.

**Fix:** Add a note in `headless-ci-testing.md` explicitly stating that `--parallel` is intentionally omitted from all ctest invocations, with the rationale: parallel audio tests share the OpenAL null driver's global context, causing non-deterministic failures. Serial execution (`--parallel 1` is the default) is required.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: `_validate-assets.yml` — git-tracking check steps run before pip install; step ordering not described]

**Spec says:** The spec's `validate-assets` section implies the step order: Checkout → Set up Python → Install Python dependencies → verify steps. No "Verify hud_sprites_ui.dds not git-tracked" or "Verify hud_sprites_ui_layout.json not git-tracked" steps are described, and no ordering constraints between those checks and pip install are documented.

**Source says:** `_validate-assets.yml` deploys this actual order: Checkout → Set up Python → "Verify required checks present in validate_assets.py" → "Verify hud_sprites_ui.dds not git-tracked" → "Verify hud_sprites_ui_layout.json not git-tracked" → "Install Python dependencies" (pip install mutagen + Pillow + apt-get ffmpeg) → "Verify shader assets" → "Run asset validation" → "Validate vcpkg baseline consistency" → "Verify clouds.png present" → "Verify font assets present" → "Verify Phase 10 audio assets present" → "Verify terrain assets present". The three pre-pip steps use only `grep`/`bash`/`git ls-files` and have no Python dependency — they run before pip intentionally to fail fast on structural issues without waiting for package installation.

**Fix:** Update the `validate-assets` spec section to document the full deployed step ordering. Explain that "Verify required checks present", "Verify hud_sprites_ui.dds not git-tracked", and "Verify hud_sprites_ui_layout.json not git-tracked" run before pip install by design (fail-fast on no-Python-dependency checks). Document that the four asset-presence verification steps (clouds, fonts, audio, terrain) are placed after "Run asset validation".


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_validate-assets.yml` — "Verify required checks present" is a loop over check identifiers, not N individual steps]

**Spec says:** The spec (Phase 11d section) shows individual guard steps per check identifier — one YAML `run:` step per check, each calling `grep -q "$checkN"`.

**Source says:** `_validate-assets.yml` uses a single "Verify required checks present in validate_assets.py" step that loops over all check identifiers (`check_20` through `check_31`, including `check_24_clouds_png`) in a single `for check in ...` bash loop with a `failed` accumulator flag. All missing checks are reported before the step exits non-zero, so a single run reveals all absent identifiers at once. Adding a new check requires only one line in the loop list.

**Fix:** Replace per-check individual YAML step examples in the spec with the loop-based pattern. Document: single step iterates all identifiers; `failed` flag accumulates failures; step exits 1 after checking all so multiple missing checks surface together; new checks cost a single-line addition to the loop list; `check_24_clouds_png` uses the full function name (not `check_24`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-linux-deb.yml` — matrix strategy, bare-container approach, vcpkg-from-source bootstrap not described]

**Spec says:** The spec mentions `_package-linux-deb.yml` produces `.deb` artifacts but documents no internal mechanics.

**Source says:** `_package-linux-deb.yml` has significant undocumented design: (1) `strategy.matrix` over four distros (`debian-bookworm`, `debian-trixie`, `ubuntu-jammy`, `ubuntu-noble`) with `fail-fast: false` — the matrix is defined inside the reusable workflow, not the caller. (2) Each leg runs in a bare distro container (`debian:bookworm`, `debian:trixie`, `ubuntu:22.04`, `ubuntu:24.04`) with `apt-get` for build deps — NOT the pre-baked GHCR image. (3) vcpkg is bootstrapped from source (vcpkg-tool built with CMake) because the prebuilt vcpkg-tool binary CDN URL returns 404 in bare containers; `lukka/run-vcpkg` is NOT used. (4) The Linux staging artifact (`aitown-staging-linux-${{ github.sha }}`) is downloaded into `build/` after CMake configure, executable bit is restored, then CPack runs. (5) Produces four artifacts: `aitown-deb-debian-bookworm-${{ github.sha }}`, `aitown-deb-debian-trixie-${{ github.sha }}`, `aitown-deb-ubuntu-jammy-${{ github.sha }}`, `aitown-deb-ubuntu-noble-${{ github.sha }}`, each with `retention-days: 30`. (6) Uses `actions/cache` v4.3.0 (`@0057852bfaa89a56745cba8c7296529d2fc39830`), not v5.0.4. `timeout-minutes: 90` is set inside the reusable workflow.

**Fix:** Add a detailed `_package-linux-deb.yml` mechanics section: matrix strategy (4 distros, `fail-fast: false`); bare-container approach (no GHCR image); vcpkg-from-source bootstrap and CDN-404 rationale; staging artifact download-and-restore pattern with executable-bit step; all four artifact names and retention; absence of `lukka/run-vcpkg`; and `timeout-minutes: 90`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-windows.yml` — configure-only CMake, NSIS via Chocolatey, staging artifact pattern not described]

**Spec says:** The spec mentions `_package-windows.yml` but documents no internal mechanics.

**Source says:** `_package-windows.yml` has undocumented design: (1) Runs `cmake --preset ci-windows -DAITOWN_ASSETS_DIR=assets -DBUILD_TESTING=OFF` for metadata only — does NOT build binaries. Comment: "CMake configure only — does NOT build the binaries. Purpose: produce CMakeCache.txt so that cpack -C Release can locate install rules and NSIS script templates." (2) Installs NSIS via `choco install nsis --no-progress -y` — the only workflow using Chocolatey. (3) Uses `ilammy/msvc-dev-cmd` and `lukka/run-vcpkg` (same SHAs as `_build-windows.yml`). (4) DLL directory appended to PATH via `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append`. (5) Downloads the Windows staging artifact into `build/`; verifies `aitown.exe` is present before CPack. (6) Artifact name: `aitown-installer-windows-${{ github.sha }}` with `retention-days: 30`; path glob: `build/aitown-*.exe`. (7) `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` is NOT set in this workflow.

**Fix:** Add a `_package-windows.yml` mechanics section: configure-only CMake and rationale; Chocolatey NSIS install; `ilammy/msvc-dev-cmd` + `lukka/run-vcpkg` usage; `Out-File` PATH append form; staging artifact download and `aitown.exe` verification; artifact name/retention; absence of `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` (contrast with `_build-windows.yml`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `docker-ci-image.yml` — Docker action SHAs, monthly cron, 120-minute timeout, and digest output not documented]

**Spec says (existing CI_CD_SPECS_FIX.md item 27):** The existing entry says to document `docker-ci-image.yml`'s trigger pattern, `force_rebuild` input, `VCPKG_COMMIT_ID` extraction, Dockerfile ARG validation, inline supply-chain lint, image tag format, and `packages: write` permission. It does not cover the Docker action SHAs, monthly cron, timeout, or digest output mechanism.

**Source says:** `docker-ci-image.yml` additionally uses: `docker/login-action@4907a6ddec9925e35a0a9e82d7399ccc52663121` (v4.1.0), `docker/setup-buildx-action@4d04d5d9486b7bd6fa91e7baf45bbb4f8b9deedd` (v4.0.0), `docker/build-push-action@d08e5c354a6adb9ed34480a06d141179aa583294` (v7.0.0). Monthly schedule: `cron: '0 2 1 * *'` (1st of month, 02:00 UTC). `timeout-minutes: 120` — the longest in the repo; required because a cold vcpkg build in Docker takes 30+ minutes. `steps.docker_build.outputs.digest` is the mechanism to obtain the sha256 digest that must be pinned in `ci.yml` and `.devcontainer/Dockerfile` after each image push.

**Fix:** Extend the `docker-ci-image.yml` documentation to include: the three Docker action SHAs and their version tags; the monthly schedule cron value and UTC time; the 120-minute timeout and its rationale; `steps.docker_build.outputs.digest` as the digest source for pinning. Add all three Docker action SHAs to the pinned-actions table in `caching.md`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `sonarcloud.yml` — `workflow_dispatch` inputs, "Debug" step, and `SONAR_SCANNER_VERSION` undocumented]

**Spec says (existing CI_CD_SPECS_FIX.md item 27):** The existing entry lists what the SonarCloud section must document but does not mention the `workflow_dispatch` inputs, the debug diagnostic step, or the `SONAR_SCANNER_VERSION` env var.

**Source says:** `sonarcloud.yml` has three undocumented elements: (1) `workflow_dispatch` inputs: `ci_run_id` (required — the CI run ID to pull artifacts from) and `head_sha` (required — the HEAD SHA of that CI run for artifact names). Without these, manual dispatch cannot target a specific CI run's artifacts. (2) "Debug compile_commands.json paths" step runs on every execution (not gated by `if: runner.debug`) — prints `GITHUB_WORKSPACE`, first entry's `file`/`directory`/`command` fields, and missing-file count via inline Python. (3) `SONAR_SCANNER_VERSION: 7.0.2.4839` is declared as a top-level env var; the SonarScanner CLI is cached at `${{ runner.temp }}/sonar-scanner-cli-${{ env.SONAR_SCANNER_VERSION }}-Linux-X64` (i.e., `${{ runner.temp }}/sonar-scanner-cli-7.0.2.4839-Linux-X64`).

**Fix:** Add documentation of: the two `workflow_dispatch` inputs and their purpose (allow manual re-runs against a specific CI artifact set); the "Debug compile_commands.json paths" step as a permanent diagnostic (not debug-mode-only); `SONAR_SCANNER_VERSION: 7.0.2.4839`; and `${{ runner.temp }}` as the SonarScanner CLI cache path.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_coverage-linux.yml` spec YAML block — shows "Verify test XML" before dorny; deployed has no such step]

**Spec says (`github-actions-workflow.md` lines ~572–595):** The `coverage-linux` reporting section contains a YAML block showing a "Verify test XML output exists" step before the `dorny/test-reporter` step, with prose: "After all three ctest steps and before the lcov capture step, `coverage-linux` must include the XML verification and `dorny/test-reporter` steps."

**Source says:** `_coverage-linux.yml` (lines 85–99) has this sequence: "Fix git safe.directory for test reporter" → "Publish test results" (dorny, directly) → "Upload test results". There is no "Verify test XML output exists" step anywhere in the file. The spec's YAML block and prose directive are directly contradicted by the deployed source.

**Fix:** Remove the "Verify test XML output exists" step from the `coverage-linux` YAML block in `github-actions-workflow.md`. Update the prose to reflect the actual deployed order: git safe.directory fix → dorny (no XML pre-check) → upload test results. (The separate question of whether this step *should* be added is covered by an existing gap entry.)


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `flawfinder.yml` — triggers main-only, Saturday cron, and action SHAs undocumented]

**Spec says (existing CI_CD_SPECS_FIX.md item 28):** The existing entry says to add brief documentation of `flawfinder.yml`. No trigger details or action SHAs are specified.

**Source says:** `flawfinder.yml` triggers on push/PR to `main` ONLY — it does NOT trigger on `develop`. Weekly schedule: `cron: '26 9 * * 6'` (Saturdays, 09:26 UTC). Uses `david-a-wheeler/flawfinder@8e4a779ad59dbfaee5da586aa9210853b701959c` with `arguments: '--sarif ./'` (scans entire repo root recursively). Uses `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1). No `VCPKG_COMMIT_ID` env var. `timeout-minutes` not set (inherits 6-hour default).

**Fix:** When documenting `flawfinder.yml`, specify: triggers are push/PR to `main` only (not `develop` — deliberate, scans only release-track code); Saturday weekly cron; `david-a-wheeler/flawfinder` SHA `8e4a779ad59dbfaee5da586aa9210853b701959c`; `github/codeql-action/upload-sarif` SHA `5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1); `--sarif ./` scan scope. Add both SHAs to the pinned-actions table in `caching.md`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `msvc.yml` vs `flawfinder.yml` — `codeql-action/upload-sarif` uses different major versions (v4 vs v3)]

**Spec says (existing CI_CD_SPECS_FIX.md item 29):** The existing entry describes `msvc.yml` but does not enumerate its action SHAs and does not note the version split with `flawfinder.yml`.

**Source says:** `msvc.yml` uses `github/codeql-action/upload-sarif@0e9f55954318745b37b7933c693bc093f7336125` (v4.35.1). `flawfinder.yml` uses `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699` (v3.35.1). These are different major versions of the same action — a maintenance hazard if v3 and v4 diverge in SARIF upload behavior. `msvc.yml` also uses `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99` (no version comment in workflow).

**Fix:** Document in `github-actions-workflow.md` that `msvc.yml` uses `codeql-action/upload-sarif` v4.35.1 (`0e9f55954318745b37b7933c693bc093f7336125`) while `flawfinder.yml` uses v3.35.1 (`5c8a8a642e79153f5d047b10ec1cba1d1cc65699`). Note the version split and recommend aligning both to the same major version in a future maintenance pass. Add `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99` to the pinned-actions table. Add a version comment for the `microsoft/msvc-code-analysis-action` SHA.


**Status: DONE** — Applied in TASK 1.

---

### [dependency-management.md: `msvc.yml` `VCPKG_COMMIT_ID` — not listed in vcpkg baseline atomicity checklist]

**Spec says:** The vcpkg baseline atomicity checklist in `dependency-management.md` and the "Baseline Staleness Risk" section document that a vcpkg baseline update must touch `vcpkg.json`, `ci.yml` (`VCPKG_COMMIT_ID`), and the Docker files. `msvc.yml` is not mentioned.

**Source says:** `msvc.yml` declares `VCPKG_COMMIT_ID: "b2f068faf45a3f04145bec0f52a66526ad590227"` independently in its own `env:` block — identical to `ci.yml`'s value. A vcpkg baseline bump in `ci.yml` does NOT automatically update `msvc.yml`, creating a silent desync risk where MSVC static analysis runs against a different vcpkg tree than the main CI build.

**Fix:** Add `msvc.yml`'s `VCPKG_COMMIT_ID` as an additional item in the vcpkg baseline atomicity checklist in `dependency-management.md`. Update the "Baseline Staleness Risk" rule to say: "update `VCPKG_COMMIT_ID` in BOTH `ci.yml` AND `msvc.yml`." Note that `msvc.yml` is not called by `ci.yml` and does not inherit the `prepare` job's `vcpkg_commit_id` output, making manual synchronization mandatory.


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md: `lcov --capture` `empty` error suppressor — divergence between spec, Makefile, and CI not documented]

**Spec says (`coverage.md`):** The `lcov --capture` `--ignore-errors` flag specifies `mismatch,inconsistent,version`. `CLAUDE.md` notes section also lists `mismatch,inconsistent,version`.

**Makefile says:** `--ignore-errors mismatch,inconsistent,version,empty` — adds `empty` as a fourth suppressor.

**Source says (`_coverage-linux.yml`):** Uses only `--ignore-errors mismatch,inconsistent` — neither `version` nor `empty` is present in CI.

**Fix:** Document the three-way divergence in `coverage.md`: (1) spec recommends `mismatch,inconsistent,version`; (2) Makefile uses `mismatch,inconsistent,version,empty` (local-developer additions for gcov version drift and unexecuted source files); (3) CI uses the minimal `mismatch,inconsistent`. Explain `empty`: lcov 2.x emits a "no data found" error when a source file is compiled but never executed (zero `.gcda`); `empty` suppresses this for local runs where not all code paths are exercised. Add a note that the canonical local recommendation should include `empty` for developer convenience, while CI keeps the minimal set.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: Makefile retains `lcov --summary` alongside awk gate — spec's "replaces" language overstates the substitution]

**Spec says (`coverage.md` line ~101):** "This step replaces the current `lcov --summary coverage_filtered.info` informational step." This implies that once the awk gate is in place, `lcov --summary` is no longer used.

**Makefile says:** Both `lcov --summary $(COVERAGE_FILTERED)` AND the awk gate are present simultaneously in the `make test` target. `lcov --summary` runs first (provides human-readable per-directory breakdown), then the awk gate enforces the numeric threshold.

**Fix:** Update `coverage.md`'s "replaces" language to clarify it applies only to the CI `_coverage-linux.yml` job. The Makefile local-developer target intentionally retains both: `lcov --summary` for diagnostic readability and the awk gate for threshold enforcement. Update the `coverage.md` local-developer command reference to show `lcov --summary` before the awk gate, matching the Makefile's deployed order.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md: `AITOWN_HEADLESS` and `ALSOFT_DRIVERS` env vars — not documented; `AITOWN_HEADLESS` must be absent from `requires-opengl` tests]

**Spec says (`headless-ci-testing.md` and `framework.md`):** Neither file mentions `AITOWN_HEADLESS` or `ALSOFT_DRIVERS` env vars. Ctest invocation examples are shown without any `env:` block.

**Source says:** All three deployed CI jobs set `AITOWN_HEADLESS: '1'` and `ALSOFT_DRIVERS: 'null'` on unit and integration ctest steps. Critically, `AITOWN_HEADLESS` is intentionally absent from the `requires-opengl` ctest step — it suppresses Irrlicht device initialization, which is required for OpenGL tests. `_build-windows.yml` also sets both vars on Windows test steps.

**Fix:** Add a section to `headless-ci-testing.md` documenting the required env vars per ctest suite:
- Unit and integration tests: `AITOWN_HEADLESS=1`, `ALSOFT_DRIVERS=null`
- OpenGL tests: `ALSOFT_DRIVERS=null` only — `AITOWN_HEADLESS` **must NOT** be set (it suppresses Irrlicht device init required for `requires-opengl` tests)

Explain the risk: omitting `AITOWN_HEADLESS=1` from unit/integration steps can cause audio or device initialization to be attempted on headless runners, masking test failures. The absence from `requires-opengl` is intentional and must not be "fixed."


**Status: SKIPPED** — Fix targets `headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [framework.md: test target table — `DISCOVERY_TIMEOUT` column missing; per-target override values not visible in table]

**Spec says (`framework.md`):** The "CMake Test Target Decomposition" table has columns: CMake target, Source directory, Label, CTest filter, Notes. `DISCOVERY_TIMEOUT` overrides are documented only in prose and inline code comments, not in the table.

**Source says (`CMakeLists.txt`):** `simulation_tests` and `terrain_tests` use `DISCOVERY_TIMEOUT 60`; all other targets (`ui_tests`, `audio_tests`, `integration_tests`, `opengl_tests`) use the default 30s set by `aitown_add_tests`. These overrides are critical for CI correctness on loaded runners.

**Fix:** Add a `DISCOVERY_TIMEOUT` column to the CMake Test Target Decomposition table in `framework.md`. Values: `simulation_tests: 60s`, `terrain_tests: 60s`, all others: `30s (default)`. This makes per-target timeout values immediately visible without requiring the reader to trace code comment examples.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [testability-architecture.md: `AITOWN_TESTING_ENABLED` — `simulation_tests` also uses it; only `ui_tests`/`aitown_ui` documented]

**Spec says (`testability-architecture.md`):** `AITOWN_TESTING_ENABLED=1` is documented as being set on `ui_tests` and `aitown_ui` to enable test seams for `UIManager`. `framework.md` does not mention `AITOWN_TESTING_ENABLED` at all.

**Source says (`CMakeLists.txt`):** `target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)` is also present — enabling `CitySimulation::testForceUnlockDensityTier()` which is used in density-tier upgrade tests. The define gates simulation-side test backdoors as well as UI-side seams.

**Fix:** Update `testability-architecture.md`'s `AITOWN_TESTING_ENABLED` documentation to list all three targets that use the define: `ui_tests`, `aitown_ui`, and `simulation_tests`. Document that `simulation_tests` uses it to enable `CitySimulation::testForceUnlockDensityTier()`. Clarify that the define is not UI-specific — it is a general test-backdoor gate applied to any target that needs compile-time-disabled test seams.


**Status: SKIPPED** — Fix targets `testability-architecture.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: job timeout table — `validate-assets: 10` and `markdown-lint: 5` missing]

**Spec says (CI_CD_SPECS_FIX.md item 23 fix prescription):** The timeout table additions listed are: `prepare: 2`, `compute-version: 5`, `all-checks-pass: 5`, `release: 10`, `package-linux-deb: 90`, `package-windows: 60`. This list is the complete set called out in item 23.

**Source says:** Two additional jobs have explicit `timeout-minutes` values not captured by item 23: `_validate-assets.yml` line 18 sets `timeout-minutes: 10`; `_markdown-lint.yml` line 15 sets `timeout-minutes: 5`. Neither is listed in the item 23 fix prescription or anywhere else in CI_CD_SPECS_FIX.md.

**Fix:** Extend the item 23 fix prescription to also add `validate-assets: 10` and `markdown-lint: 5` to the timeout table. The complete corrected timeout table for all jobs with explicit `timeout-minutes` values is: `prepare: 2`, `validate-assets: 10`, `build-linux: 30`, `test-linux: 30` (set inside `_test-linux.yml`), `build-windows: 40`, `coverage-linux: 60`, `markdown-lint: 5`, `all-checks-pass: 5`, `compute-version: 5`, `release: 10`, `package-linux-deb: 90`, `package-windows: 60`, `docker-ci-image build-and-push: 120`. (`supply-chain-lint` has no explicit timeout — documented in a separate entry.)


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-linux-deb.yml` step sequence — "Set executable bit on binary" step not documented]

**Spec says (CI_CD_SPECS_FIX.md item 43):** The `_package-linux-deb.yml` step sequence is described as: install deps → download staging artifact → CPack. No step between "Download Linux staging artifact" and "Create Debian package" is mentioned.

**Source says:** `_package-linux-deb.yml` has a "Set executable bit on binary" step (`run: chmod +x build/aitown`) placed between "Download Linux staging artifact" and "Create Debian package". This step is required because `actions/upload-artifact` strips executable permissions when archiving — without `chmod +x`, CPack's `install(TARGETS aitown ...)` copies a non-executable binary into the `.deb`, and the installed game cannot be launched.

**Fix:** Add "Set executable bit on binary" (`run: chmod +x build/aitown`) to the `_package-linux-deb.yml` step sequence, positioned after "Download Linux staging artifact" and before "Create Debian package". Document the rationale: `actions/upload-artifact` strips file permissions; the executable bit must be restored before CPack installs the binary. Cross-reference the analogous "Restore execute permissions on test binaries" step in `_test-linux.yml` which addresses the same root cause.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `docker-ci-image.yml` — "Print image digest" step contains stale `test-container-xvfb` job reference]

**Spec says (CI_CD_SPECS_FIX.md item 47):** The existing entry prescribes documenting `docker-ci-image.yml`'s digest output mechanism. Neither this entry nor any other addresses the content of the "Print image digest and update instructions" step's echo output.

**Source says:** `docker-ci-image.yml` lines 148–158, the "Print image digest and update instructions" step, echoes: `"1. ci.yml — in the test-container-xvfb job container: block:"`. The `test-container-xvfb` job no longer exists in `ci.yml`. The three actual jobs that use the container image are `build-linux` (`_build-linux.yml`), `test-linux` (`_test-linux.yml`), and `coverage-linux` (`_coverage-linux.yml`). An operator following these instructions after an image rebuild would look for a nonexistent job, leaving the real container jobs unpinned.

**Fix:** Document this stale reference in `github-actions-workflow.md`'s `docker-ci-image.yml` section as a workflow defect. The correct update instructions should list: (1) `.github/workflows/_build-linux.yml` — `container: image:` line; (2) `.github/workflows/_test-linux.yml` — `container: image:` line; (3) `.github/workflows/_coverage-linux.yml` — `container: image:` line; (4) `.devcontainer/Dockerfile` — `FROM` line. The echo output in the workflow file itself must be corrected to remove the `test-container-xvfb` reference.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: ccache-action `if: runner.os == 'Linux'` guard — spec prescribes it; deployed Linux-only workflows omit it]

**Spec says (`github-actions-workflow.md` lines ~155–176):** `if: runner.os == 'Linux'` is prescribed on every `hendrikmuhs/ccache-action` step. The YAML blocks for `build-linux` and `coverage-linux` both show this guard.

**Source says:** `_build-linux.yml` line 45 and `_coverage-linux.yml` line 42 use `hendrikmuhs/ccache-action` with NO `if:` condition. The guard is structurally unnecessary in these workflows because they are Linux-only container jobs that never run on Windows runners. The spec's `if: runner.os == 'Linux'` guidance was written for a monolithic `ci.yml` where Linux and Windows steps coexisted in the same job; it is obsolete for split reusable workflows.

**Fix:** Update the ccache-action guidance in `github-actions-workflow.md` to remove the `if: runner.os == 'Linux'` requirement for Linux-dedicated reusable workflows (`_build-linux.yml`, `_coverage-linux.yml`). Add a note: "In Linux-only reusable workflows (which always run in a Linux container), the `if: runner.os == 'Linux'` guard is unnecessary and is omitted in the deployed source. The guard is only required in workflows where jobs may run on multiple OS types."


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md: Phase 0 canonical command block — `lcov --list` and `genhtml` missing GCC 13 flags; `lcov --summary` present but absent from deployed source]

**Spec says (`coverage.md` lines 56–64):** Shows bare `lcov --list coverage_filtered.info` and `genhtml coverage_filtered.info --output-directory coverage_html/` with no `--ignore-errors inconsistent --rc check_data_consistency=0`. Line 64 shows `lcov --summary coverage_filtered.info` as part of the canonical local command block.

**Source says:** `_coverage-linux.yml` applies `--ignore-errors inconsistent --rc check_data_consistency=0` to both `lcov --list` (line 155) and `genhtml` (line 156). There is no `lcov --summary` call in `_coverage-linux.yml`. Existing CI_CD_SPECS_FIX.md item 36 documents adding these flags to `lcov --remove` in `coverage.md`, and item 38 documents adding them to `github-actions-workflow.md` YAML blocks — but neither item explicitly addresses `coverage.md` lines 56 (`lcov --list`) and 59 (`genhtml`).

**Fix:** In `coverage.md`, add `--ignore-errors inconsistent --rc check_data_consistency=0` to the `lcov --list` and `genhtml` calls in the Phase 0 canonical command block (lines 56 and 59). Remove the `lcov --summary coverage_filtered.info` call (line 64) — it is absent from both `_coverage-linux.yml` and as a primary step in the Makefile. Document the canonical ordering as `--capture → --remove → --list → genhtml → gate awk`; `--summary` is a diagnostic-only tool not part of the gate pipeline.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [coverage.md: Phase 5 and Phase 6 total-line gate — spec describes 80% (Phase 5) as current; deployed source enforces 95% (Phase 6 complete)]

**Spec says (`coverage.md` lines 79–101):** Phase 5 gate uses `if (pct+0 < 80.0)` with step name "Enforce 80% total line coverage gate". Phase 6 section (lines 155–173) instructs: "change the existing `80.0` threshold … to `95.0`" as a future CI deliverable written in future tense.

**Source says (`_coverage-linux.yml` lines 214–232):** The deployed step is named "Enforce 95% total line coverage gate" and uses `if (pct+0 < 95.0)`. The Phase 6 CI deliverable has already been applied. `coverage.md`'s Phase 6 section still reads as a future action item even though the transition is complete in the deployed source.

**Fix:** Update `coverage.md` to reflect the deployed state: (1) mark the Phase 5 80% gate description as historical (superseded); (2) change Phase 6 section language from future tense ("MUST be applied during Phase 6 implementation: change `80.0` to `95.0`") to past tense ("Applied in Phase 6; the active gate in `_coverage-linux.yml` enforces 95%"). The Phase 6 awk snippet showing `< 95.0` is correct but should be labeled "currently deployed."


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [framework.md: `simulation_tests` `DISCOVERY_TIMEOUT` comment — "8-file binary" count is stale]

**Spec says (`framework.md` line ~85):** `# 60s: 8-file binary with RapidCheck property tests under coverage instrumentation`

**Source says (`CMakeLists.txt`):** `simulation_tests` has an `add_executable` with 8 initial files plus approximately 15 additional files added via `target_sources` calls, totalling ~23 source files. The "8-file" count referenced in the spec comment is significantly stale.

**Fix:** Update the `# 60s: 8-file binary` comment in `framework.md` to remove the specific file count. Replace with a rationale that does not depend on file count: `# 60s: coverage-instrumented binary with RapidCheck property tests requires extended discovery beyond the default 30s on loaded CI runners`. The `DISCOVERY_TIMEOUT 60` value itself is correct and must be retained.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md / framework.md: Windows ctest invocations require `-C Release` — not documented in any testing spec]

**Spec says (`headless-ci-testing.md` and `framework.md`):** All ctest command examples are shown without `-C Release`. The CLAUDE.md "Running Tests — Windows" section shows the catch-all `ctest --test-dir build -C Release --output-on-failure` but the label-filtered commands omit it.

**Source says (`_build-windows.yml` lines 131, 143):**
```
ctest --test-dir build -C Release -LE "integration|requires-opengl" --output-on-failure
ctest --test-dir build -C Release -L "^integration$" --output-on-failure
```
Both Windows ctest steps include `-C Release`. Without it, CTest on Windows may fail to locate test binaries or discover 0 tests because the `ci-windows` Ninja preset does not set `CMAKE_BUILD_TYPE`, and CTest reads the configuration from the `-C` flag when locating built test binaries.

**Fix:** Add a Windows-specific note to `headless-ci-testing.md` (and the Windows CI section of `github-actions-workflow.md`) stating that all Windows ctest invocations require `-C Release`. Explain the rationale: the Ninja generator is single-config; `-C Release` is required for CTest to locate test binaries correctly. Show the Windows ctest command variants with `-C Release` alongside the Linux variants without it.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md / framework.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: `sonarcloud-debug.yml` — diagnostic workflow exists but is entirely undocumented]

**Spec says:** No spec file mentions `sonarcloud-debug.yml`. CI_CD_SPECS_FIX.md item 28 parenthetically acknowledges its existence but prescribes no documentation for it.

**Source says:** `.github/workflows/sonarcloud-debug.yml` is a `workflow_dispatch`-only diagnostic workflow. It accepts the same two inputs as `sonarcloud.yml` (`ci_run_id`, `head_sha`). It downloads only `compile-commands-linux-${{ inputs.head_sha }}` (no coverage XML), prints paths before remapping, applies the identical `/__w/` → `$GITHUB_WORKSPACE` inline Python remap, then prints paths after remapping and reports missing-file counts. It does NOT run SonarScanner and requires no `SONAR_TOKEN`. Uses `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd` and `actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131`. Has `permissions: pull-requests: read`. No `timeout-minutes`.

**Fix:** Add a `sonarcloud-debug.yml` subsection to `github-actions-workflow.md` describing it as a diagnostic-only workflow: `workflow_dispatch`-only, same two inputs as `sonarcloud.yml`, downloads only `compile-commands-linux-*`, prints path counts before/after remap without running a scan, no `SONAR_TOKEN` required. Note the absence of `timeout-minutes` as a gap (should add a short value, e.g., 10 min). Clarify its intended use: triggered manually when `sonarcloud.yml` fails path resolution to diagnose whether the container-path remap is producing valid workspace-relative paths.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `docker-ci-image.yml` — Docker BuildKit GHA layer cache (`cache-from`/`cache-to`) not documented]

**Spec says (CI_CD_SPECS_FIX.md items 27 and 47):** Existing entries prescribe documenting `docker-ci-image.yml`'s triggers, actions SHAs, monthly cron, 120-minute timeout, and digest output. Neither entry mentions the Docker layer cache configuration.

**Source says:** `.github/workflows/docker-ci-image.yml` configures `docker/build-push-action` with `cache-from: type=gha` and `cache-to: type=gha,mode=max`. This uses GitHub Actions' built-in Docker BuildKit layer cache to persist image layers across runs. `mode=max` stores all intermediate layers (not just the final image) — required because the vcpkg compilation layer is deep in the image and `mode=min` would only cache the final layer, missing the expensive intermediate build stages (~30 min cold).

**Fix:** Add the GHA layer cache configuration to the `docker-ci-image.yml` documentation: `cache-from: type=gha`, `cache-to: type=gha,mode=max`. Explain it caches Docker image layers (distinct from `actions/cache` which archives vcpkg binaries). Document that `mode=max` is required — not `mode=min` — because the expensive vcpkg intermediate layers must be cached to achieve sub-5-minute warm rebuilds.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `sonarcloud.yml` Analysis job — skips on failed CI run; `if:` condition not documented]

**Spec says:** The SonarCloud workflow is described as triggering "after CI completes" via `workflow_run`. No mention is made of the job-level `if:` condition.

**Source says:** `.github/workflows/sonarcloud.yml` line 33: `if: ${{ github.event_name == 'workflow_dispatch' || github.event.workflow_run.conclusion == 'success' }}`. The `Analysis` job is entirely skipped when the triggering CI run did not succeed. `workflow_dispatch` manual runs bypass this filter.

**Fix:** Document the `Analysis` job's `if:` condition: SonarCloud analysis is skipped when the triggering CI run failed (no coverage XML was produced). Explain the rationale: a failed CI run means `coverage-linux` did not complete, so no `coverage-sonar-linux-*` artifact exists for ingestion. The `workflow_dispatch` path allows re-scanning a specific CI run by ID regardless of its conclusion.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `msvc.yml` — SARIF artifact upload step and missing `retention-days` not documented]

**Spec says (CI_CD_SPECS_FIX.md items 29 and 66):** Existing entries describe `msvc.yml`'s purpose, SHA version split, and independent `VCPKG_COMMIT_ID`. No entry documents the SARIF artifact upload step.

**Source says:** `.github/workflows/msvc.yml` lines 96–100 contain "Upload SARIF as an Artifact" — uploads the SARIF file with `name: sarif-file` using `actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f` (v7.0.0). No `retention-days:` is specified, so it inherits the repository default (typically 90 days). This step is separate from the Security tab upload (`codeql-action/upload-sarif`) and allows post-run download of the raw SARIF for manual inspection.

**Fix:** Document the "Upload SARIF as an Artifact" step in the `msvc.yml` section: artifact name `sarif-file`, no explicit `retention-days` (flag as a gap — a short retention such as 7 or 14 days is appropriate for a debug artifact). Clarify its purpose: raw SARIF download for local inspection, separate from the GitHub Security tab integration.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_supply-chain-lint.yml` — SHA lint loop exits immediately on first failure, masking container image digest check]

**Spec says:** The spec describes the supply-chain lint as having two checks: SHA pin validation and container image digest validation. No ordering or failure-propagation interaction between the two is documented.

**Source says:** `.github/workflows/_supply-chain-lint.yml` lines 29–46: the SHA lint loop uses a `failed=1` accumulator and processes ALL `.github/workflows/*.yml` files before calling `exit 1` at the end — so all SHA pin failures across all files are reported in one pass. However, the `exit 1` at line 45 means the step terminates after the SHA loop completes, and lines 49–54 (the container image digest loop) never execute if any SHA failure was found. A workflow file with both bad SHA pins AND a missing container digest would report all SHA failures but silently skip the container digest check. The two lint phases are not independent.

**Fix:** Document the sequential early-exit behavior of the lint step in `github-actions-workflow.md`: a SHA lint failure (placeholder or short SHA) causes the step to exit before the container image digest check runs. Flag this as a known gap: container digest failures are only reported when the SHA lint passes. Recommend restructuring with a shared `failed=1` accumulator across both loops so all errors surface in a single execution regardless of which lint category fails.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-linux-deb.yml` — checkout step runs after apt-get, not first; bare containers lack `git`]

**Spec says (CI_CD_SPECS_FIX.md item 43):** The `_package-linux-deb.yml` step sequence is described but the checkout position is not explicitly noted. All other workflows document checkout as the first step.

**Source says:** `.github/workflows/_package-linux-deb.yml` line 57: `- uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd` runs after the "Install build dependencies" (`apt-get`) step (line 45) but BEFORE the "Cache vcpkg packages" step (line 62). The actual order is: apt-get install → checkout → actions/cache. This is the only workflow in the repo where checkout is not the first step. Reason: bare Debian/Ubuntu containers do not have `git` pre-installed; `git` is explicitly installed as a separate package in the `apt-get install` step (`build-essential` does NOT include `git`). That `apt-get` step must run first — without `git`, `actions/checkout` fails with "command not found: git". The checkout step also has no `name:` field (bare `uses:` form), unlike all other workflows which use `name: Checkout code`.

**Fix:** Document the checkout placement exception in `_package-linux-deb.yml`: checkout is intentionally NOT the first step — it follows `apt-get` because bare distro containers lack `git`. Note this is the only workflow with this pattern. Recommend adding `name: Checkout code` for consistency (cosmetic). Add this rationale to item 43's step sequence description.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `compute-version` job — does NOT push a git tag; tag creation is deferred to the `release` job]

**Spec says (CI_CD_SPECS_FIX.md item 25):** The existing entry documents `DEFAULT_MAIN`/`DEFAULT_DEVELOP` floor values and the version string computation. It does not state whether the job pushes a git tag.

**Source says:** The `compute-version` job (`.github/workflows/ci.yml` lines 61–96) only writes a version string to `$GITHUB_OUTPUT` — it performs no `git tag`, `git push`, or GitHub API call. Tag creation happens atomically in the `release` job via `gh release create "${TAG}" --target "${{ github.sha }}"`, which creates the git tag and GitHub Release in a single API call using `target_commitish`. Develop-branch pushes produce a `-develop`-suffixed version string with no corresponding tag at all.

**Fix:** Add to the `compute-version` documentation: the job produces a version string output only — it does NOT create or push a git tag. Document that tag creation is deferred to the `release` job via `gh release create --target` (atomic tag + GitHub Release). State that develop pushes produce a `-develop`-suffixed version with no tag. This prevents confusion with a hypothetical "bump-version-then-push-tag" pattern.


**Status: DONE** — Applied in TASK 1.

---

### [testability-architecture.md: `AITOWN_TESTING_ENABLED` on `aitown_sim` — spec prescribes applying it; deployed source explicitly prohibits it and uses local-fake pattern]

**Spec says (`testability-architecture.md` line ~1191):** "Pattern: apply to `aitown_sim` as well when simulation_tests needs non-inline test helpers (e.g. `testForceUnlockDensityTier()`). Added in Phase 11m." This prescribes `target_compile_definitions(aitown_sim PRIVATE AITOWN_TESTING_ENABLED=1)` when simulation test seams are needed.

**Source says (`CMakeLists.txt` lines ~610, ~824, ~826):** `AITOWN_TESTING_ENABLED=1` is applied to three targets: `simulation_tests` (line 610), `aitown_ui` (line 824), and `ui_tests` (line 826). It is NOT applied to `aitown_sim`. The test file uses a local `FakeCitySimulation` inline stub, so `testForceUnlockDensityTier` is defined inside the test — not on the real `aitown_sim` library. Applying it to `aitown_sim` would compile test-seam methods into the production static library linked by the `aitown` binary — a defect the deployed approach explicitly prevents.

**Fix:** Update `testability-architecture.md` line ~1191 to reflect the deployed pattern: "For non-inline simulation test helpers, use a local `FakeCitySimulation` test stub rather than calling methods on the real `CitySimulation` class. This avoids applying `AITOWN_TESTING_ENABLED=1` to the `aitown_sim` library. `AITOWN_TESTING_ENABLED=1` MUST NOT be applied to `aitown_sim` — that library is linked into the `aitown` game binary." Clarify that the deployed targets with this define are `simulation_tests`, `aitown_ui`, and `ui_tests` — `aitown_ui` legitimately needs it because `UIManager` test seams are non-inline and cannot be faked at the test level.


**Status: SKIPPED** — Fix targets `testability-architecture.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md: `AITOWN_HEADLESS=1` on integration tests — semantic conflict with "EDT_NULL Irrlicht device" language; unresolved by item 58]

**Spec says (`headless-ci-testing.md` line ~4):** "Integration tests: use EDT_NULL Irrlicht device and OpenAL Soft null backend" — implies integration tests CREATE an Irrlicht device (the null driver). The existing item 58 prescribes adding `AITOWN_HEADLESS=1` to the integration test env var table but does not define whether this flag suppresses only non-null (OpenGL) device creation or all device creation including EDT_NULL.

**Source says (`_test-linux.yml` lines 131–137, `_coverage-linux.yml` lines 70–76):** Both set `AITOWN_HEADLESS: '1'` on integration test ctest steps. `_coverage-linux.yml`'s step is even named "Run integration tests (no display, EDT_NULL)" — asserting EDT_NULL use while also setting the device-suppression flag. No comment in either workflow file explains how these are compatible.

**Fix:** Add a semantic definition to `headless-ci-testing.md` clarifying whether `AITOWN_HEADLESS=1` suppresses only non-null (OpenGL) device init or ALL device init. If it only blocks OpenGL: update the integration test description to explicitly state that EDT_NULL device creation is permitted under `AITOWN_HEADLESS=1`. If it blocks all device init: update the integration test description to remove the "use EDT_NULL Irrlicht device" claim (it is then aspirational, not current). This semantic precision is required before item 58's env-var table fix can be implemented correctly — the table must accurately describe what the flag does, not just that it is set.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md` (outside the four CI/CD spec files).

---

### [Makefile: `make test` ctest invocations omit `GTEST_OUTPUT`, `AITOWN_HEADLESS`, and `ALSOFT_DRIVERS` — undocumented divergence from CI]

**Spec says (`CLAUDE.md` "Running Tests", `framework.md` line ~35):** `GTEST_OUTPUT=xml:test_results/` is the required format for GTest XML output. Item 58 prescribes documenting `AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` as required on ctest steps. Neither spec states the Makefile is exempt.

**Source says (`Makefile` lines 68–75):** All three `ctest` invocations in the `test` target omit `GTEST_OUTPUT`, `AITOWN_HEADLESS`, and `ALSOFT_DRIVERS`. Running `make test` locally: (1) produces no JUnit XML — local test failures cannot be inspected with JUnit viewers; (2) may attempt real audio/display device initialization that CI suppresses — local machines with audio hardware or a running display server follow different code paths than CI.

**Fix:** Add a Makefile divergence note to `coverage.md` documenting that `make test` intentionally omits these env vars, with rationale: `GTEST_OUTPUT` is omitted because local developers do not use dorny; `AITOWN_HEADLESS`/`ALSOFT_DRIVERS` are omitted to allow local developer machines to exercise real hardware paths. If CI-equivalent local runs are needed, prefix ctest with `GTEST_OUTPUT='xml:test_results/' AITOWN_HEADLESS=1 ALSOFT_DRIVERS=null`. Also note that `make test` not producing XML means item 12's "Verify test XML output" step is inherently inapplicable to local runs.


**Status: SKIPPED** — Fix targets `Makefile` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: release job — "Generate changelog" step entirely undocumented]

**Spec says:** CI_CD_SPECS_FIX.md items 25 and 26 prescribe documenting the `release` job's retry loop, immutable-release handling, `--target` flag, and `release-assets/**` glob. Neither entry mentions a changelog step.

**Source says:** `ci.yml` lines 266–294 contain a "Generate changelog" step that runs before "Create GitHub release". It uses an `extract()` shell function calling `git log "$RANGE" --pretty=format:"%s"` with conventional-commit prefix filtering (`feat`, `fix`, `ci`, `docs`, `test`, `chore`) to group commits into Markdown sections (Features, Bug Fixes, CI/CD, Tests, Documentation, Chores). Results are written to `/tmp/changelog.md`. Falls back to `"Initial release.\n"` when no prior tag exists (`PREV_TAG` is empty). The step outputs `prev_tag` to `$GITHUB_OUTPUT`. The subsequent `gh release create` call passes `--notes-file /tmp/changelog.md`.

**Fix:** Add the "Generate changelog" step to the `release` job documentation: describe the `extract()` function, six conventional-commit categories, `/tmp/changelog.md` output path, `"Initial release."` fallback, `prev_tag` output, and `--notes-file` flag on `gh release create`. Without this, the spec's description of the `release` job is incomplete — readers would not know the release notes are auto-generated from commit history.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-linux-deb.yml` — `--overlay-ports=vcpkg-overlays` and non-standard cache path `/opt/vcpkg/packages` undocumented]

**Spec says (CI_CD_SPECS_FIX.md item 43):** Documents matrix strategy, bare-container approach, vcpkg-from-source bootstrap, staging artifact download, artifact names, `lukka/run-vcpkg` absence, and `timeout-minutes: 90`. Does not mention the `vcpkg-overlays` overlay flag or the cache path.

**Source says:** `_package-linux-deb.yml` line 104: `$VCPKG_ROOT/vcpkg install --triplet x64-linux --overlay-ports=vcpkg-overlays` — the repo's local overlay directory is applied, pinning `openal-soft` to 1.23.1 (gcc-12 fallback). The `actions/cache` step (line 63) caches `/opt/vcpkg/packages` with a key including `${{ matrix.codename }}` — a container-internal path distinct from the Windows job's `AppData\Local\vcpkg\archives`. The `matrix.codename` segment in the key prevents ABI-incompatible archives from being shared across distro legs.

**Fix:** Add to the `_package-linux-deb.yml` documentation: (a) `--overlay-ports=vcpkg-overlays` pins local port overrides (openal-soft 1.23.1) in packaging builds; (b) the vcpkg binary cache path is `/opt/vcpkg/packages` (container-local, not a runner AppData path); (c) the cache key includes `${{ matrix.codename }}` for distro-level isolation.


**Status: DONE** — Applied in TASK 1.

---

### [dependency-management.md: `_package-linux-deb.yml` vcpkg bootstrap — version determined from `vcpkg-tool-metadata.txt`; mechanism undocumented]

**Spec says (CI_CD_SPECS_FIX.md item 43):** Documents the CDN-404 rationale for building vcpkg-tool from source but does not describe how the correct tool version is selected.

**Source says:** `_package-linux-deb.yml` line 88: `VCPKG_TOOL_TAG=$(grep '^VCPKG_TOOL_RELEASE_TAG=' /opt/vcpkg/scripts/vcpkg-tool-metadata.txt | cut -d= -f2)`. The version tag is read from the vcpkg repo's own metadata file after the shallow fetch, ensuring the tool version is always consistent with the fetched vcpkg commit. The build uses `cmake -B build -DVCPKG_DEVELOPMENT_WARNINGS=OFF` with Ninja and Release config. After build, `VCPKG_ROOT=/opt/vcpkg` is written to `$GITHUB_ENV` for subsequent steps.

**Fix:** Add to the vcpkg bootstrap documentation in `dependency-management.md` and `github-actions-workflow.md`: the `vcpkg-tool-metadata.txt` version determination mechanism; CMake/Ninja build flags (`-DVCPKG_DEVELOPMENT_WARNINGS=OFF`, Release); `VCPKG_ROOT=/opt/vcpkg` export to `$GITHUB_ENV`. Note the self-consistency guarantee: the tool version always matches the vcpkg commit being fetched, with no manual version tracking required.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `msvc.yml` — weekly cron `"33 13 * * 0"` (Sundays 13:33 UTC) not documented in any entry]

**Spec says (CI_CD_SPECS_FIX.md items 28, 66, 67):** These entries describe `msvc.yml`'s purpose, SARIF handling, SHA version split, and `VCPKG_COMMIT_ID` concern. None specifies the cron expression.

**Source says:** `msvc.yml` line 17: `cron: "33 13 * * 0"` — Sundays at 13:33 UTC. `flawfinder.yml` runs Saturdays at 09:26 UTC (documented in item 62). The two security-analysis workflows run on different days to stagger runner usage.

**Fix:** Add `cron: "33 13 * * 0"` (Sundays 13:33 UTC) to the `msvc.yml` trigger documentation. Note the intentional day-stagger with `flawfinder.yml` (Saturdays 09:26 UTC).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `sonarcloud.yml` — `SONAR_SCANNER_BINARIES_URL` env var and download directory rename asymmetry undocumented]

**Spec says (CI_CD_SPECS_FIX.md items 27 and 64):** Prescribe documenting `SONAR_SCANNER_VERSION: 7.0.2.4839` and the `${{ runner.temp }}` cache path. Neither mentions `SONAR_SCANNER_BINARIES_URL` or the install step's directory rename.

**Source says:** `sonarcloud.yml` line 29: `SONAR_SCANNER_BINARIES_URL: https://binaries.sonarsource.com/Distribution/sonar-scanner-cli`. The "Install Sonar Scanner CLI" step (lines 114–126, runs on cache miss): downloads `sonar-scanner-cli-${SONAR_SCANNER_VERSION}-linux-x64.zip` (lowercase `linux-x64`), unzips to `sonar-scanner-${SONAR_SCANNER_VERSION}-linux-x64`, then `mv`s to `$RUNNER_TEMP/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-Linux-X64` (capitalized `Linux-X64`). `curl` uses `--user-agent sonarqube-scan-action`. The capitalization change (`linux-x64` → `Linux-X64`) is required for the path to match the `actions/cache` key — a mismatch would silently break cache hits on every run.

**Fix:** Add `SONAR_SCANNER_BINARIES_URL` to the SonarCloud documentation. Document the install step: URL construction, `--user-agent sonarqube-scan-action` flag, the lowercase-to-capitalized `mv` rename (`linux-x64` → `Linux-X64`), and that this rename must match the `actions/cache` path exactly. Flag the capitalization asymmetry as a maintenance hazard — if the URL format changes, the rename and cache path must be updated atomically.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_package-windows.yml` — `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` absent appears to be an oversight, not a deliberate design decision]

**Spec says (CI_CD_SPECS_FIX.md item 43):** Acknowledges that `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` is absent from `_package-windows.yml` (contrast with `_build-windows.yml`) but provides no rationale.

**Source says:** `_package-windows.yml` uses `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` and `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1` — the identical node20 actions as `_build-windows.yml`. No comment explains the absence of `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true`. `_build-windows.yml` sets it at the job `env:` level to suppress node20-deprecation warnings emitted by these actions. `_package-windows.yml` would emit those same warnings on every run. No design rationale justifies the asymmetry — it appears to be an omission when the workflow was authored.

**Fix:** Update item 43's fix prescription to explicitly state that `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` should be added to `_package-windows.yml`'s job `env:` block in a maintenance pass — the omission is an oversight, not a deliberate design difference. Document this in `github-actions-workflow.md` as a known inconsistency between the two Windows workflows.


**Status: DONE** — Applied in TASK 1.

---

### [coverage.md / CI_CD_SPECS_FIX.md item 64: Makefile `make test` omits `lcov --list` — fifth divergence from CI not in item 64's table]

**Spec says (CI_CD_SPECS_FIX.md item 64):** Lists four Makefile-vs-CI divergences. Item 72 notes that the canonical ordering is `--capture → --remove → --list → genhtml → gate awk` and prescribes removing `lcov --summary` from the spec's canonical command block.

**Source says (`Makefile` lines 80–103):** The Makefile has NO `lcov --list` call. The step sequence is: `lcov --capture` → `lcov --remove` → `genhtml` → `lcov --summary` → awk gate. `lcov --list` (which prints per-file line counts useful for diagnosing gate failures) is completely absent. `lcov --summary` (aggregate totals only) appears in a different position and is not a substitute. This is a fifth Makefile-vs-CI divergence not captured in item 64's four-entry table.

**Fix:** Extend item 64's divergence table with a fifth row: "Makefile omits `lcov --list coverage_filtered.info` (present in CI between `--remove` and `genhtml`) and uses `lcov --summary` after `genhtml` instead. `lcov --list` produces per-file line counts needed to diagnose which files are below a gate threshold; `lcov --summary` shows only aggregate totals — not equivalent." Recommend adding `lcov --list` to the Makefile before `genhtml` to match CI's diagnostic output.


**Status: SKIPPED** — Fix targets `coverage.md / CI_CD_SPECS_FIX.md item 64` (outside the four CI/CD spec files).

---

### [coverage.md: `make test` omits the 85% per-file simulation floor and 25% src/ui/ worst-file gates — undocumented CI-only enforcement]

**Spec says (`coverage.md` §Phase 11 and §Phase 4):** The 85% per-file simulation floor and the 25% worst-file `src/ui/` gate are described in their phase sections without explicitly stating they are CI-only. Line 13 implies parity: "`make test` enforces the 95% gate locally; CI `coverage-linux` job enforces the same threshold" — "the same threshold" is ambiguous about whether it covers only 95% or all gates.

**Source says (`Makefile` lines 95–103):** Only the 95% total line coverage awk gate is present. The `src/simulation/` SF preflight, the 85% per-file floor awk, and the `src/ui/` 25% worst-file gate awk are completely absent. A developer can merge code that passes `make test` but fails `coverage-linux` CI on the simulation or UI sub-gates.

**Fix:** Add an explicit note to `coverage.md` — in a "Makefile vs CI gate parity" subsection — stating: "`make test` enforces only the 95% total line coverage gate. The Phase 11 85% per-file `src/simulation/` floor and the Phase 4 25% worst-file `src/ui/` gate are CI-only enforcement steps and are not replicated in the Makefile. A green `make test` does NOT guarantee `coverage-linux` CI will pass all coverage gates." Either document this as an intentional divergence (CI is authoritative) or prescribe adding the sub-gates to the Makefile for local parity.


**Status: SKIPPED** — Fix targets `coverage.md` (outside the four CI/CD spec files).

---

### [github-actions-workflow.md: `_package-linux-deb.yml` step sequence — CMake configure step missing from item 43]

**Spec says (CI_CD_SPECS_FIX.md item 43):** The step sequence is described as: install deps → download staging artifact → chmod +x → CPack. No CMake configure step is mentioned.

**Source says:** `_package-linux-deb.yml` includes a CMake configure step between vcpkg install and the staging artifact download: `cmake --preset ci-linux -DBUILD_TESTING=OFF -DAITOWN_ASSETS_DIR=assets`. This produces `CMakeCache.txt` so CPack can locate install rules and the `CPACK_PACKAGE_FILE_NAME` variable. No `cmake --build` step exists — all binaries come from the staging artifact download. The step is analogous to the configure-only step documented for `_package-windows.yml` but was not added to item 43's sequence.

**Fix:** Extend item 43's step sequence to insert "CMake configure (for CPack metadata only — no build)" between "Install vcpkg packages" and "Download Linux staging artifact". Add the note: "configure only — produces CMakeCache.txt for CPack install rules; binaries come from the staging artifact."


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `package-linux-deb` job — `needs:` list not documented]

**Spec says:** No existing entry documents the `package-linux-deb` job's `needs:` list. Item 54 documents `package-windows`'s `needs: [build-windows, prepare, compute-version]` and the rationale for `prepare`, but no analogous entry exists for `package-linux-deb`.

**Source says:** `ci.yml` line 202: `package-linux-deb` has `needs: [build-linux, prepare, compute-version]` — three dependencies, mirroring `package-windows`. `prepare` is required for its `vcpkg_commit_id` output used in artifact name construction.

**Fix:** Add a `package-linux-deb` `needs:` entry documenting `needs: [build-linux, prepare, compute-version]`. Explain that `prepare` is required for its `vcpkg_commit_id` output (same rationale as item 54 for `package-windows`).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_build-windows.yml` line 122 — bare `>> $env:GITHUB_PATH` used instead of `Out-File -Encoding utf8`]

**Spec says (CI_CD_SPECS_FIX.md item 51):** Documents that `_package-windows.yml` correctly uses `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append` to prevent PowerShell 5.1 UTF-16 BOM corruption. Does not flag `_build-windows.yml` as using the risky form.

**Source says:** `_build-windows.yml` line 122: `"${{ github.workspace }}\build\vcpkg_installed\x64-windows\bin" >> $env:GITHUB_PATH` — bare `>>` redirection in PowerShell 5.1, which writes UTF-16 LE by default, potentially corrupting `$GITHUB_PATH` with a BOM. `_package-windows.yml` uses the correct `Out-File -Encoding utf8 -Append` form. The two Windows workflows are inconsistent on this point, and `_build-windows.yml` carries the same BOM risk that item 51 warns against.

**Fix:** Add an entry flagging `_build-windows.yml` line 122 as using the risky `>>` form. Recommend updating it to `Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append` for consistency with `_package-windows.yml` and to prevent BOM corruption. This is a deployed defect in the production build job.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_coverage-linux.yml` gcovr step — `--gcov-executable gcov-13` flag undocumented]

**Spec says (CI_CD_SPECS_FIX.md item 10):** Documents the `gcovr --sonarqube` step and notes gcovr filters mirror lcov patterns. Does not mention the `--gcov-executable` flag.

**Source says:** `_coverage-linux.yml` line 163: the `gcovr` invocation includes `--gcov-executable gcov-13`. Without this, gcovr uses the system default `gcov` which may differ from the `gcc-13` compiler used to build the project, causing gcovr to fail reading `.gcda` files. This is the gcovr equivalent of `lcov --capture`'s `--gcov-tool gcov-13` flag (documented in item 35).

**Fix:** Update the gcovr step documentation to include `--gcov-executable gcov-13`. Note the parallel with `--gcov-tool gcov-13` on `lcov --capture` — both flags must name the same GCC version matching the compiler that produced the `.gcda` files.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `_coverage-linux.yml` gcovr exclusions — must stay in sync with `lcov --remove`; synchronization requirement undocumented]

**Spec says (CI_CD_SPECS_FIX.md item 10):** Notes gcovr filters mirror lcov patterns. Does not state the synchronization requirement explicitly or enumerate the full gcovr `--exclude` list.

**Source says:** `_coverage-linux.yml` lines 164–173: gcovr uses `--exclude 'src/rendering/.*'`, `--exclude 'src/audio/.*'`, `--exclude 'src/platform/.*'`, `--exclude 'src/simulation/.*\.h'`, `--exclude 'src/ui/.*\.h'`, `--exclude 'src/interfaces/.*\.h'`. These mirror the `lcov --remove` patterns exactly. If a new `lcov --remove` pattern is added (e.g., a new exclusion directory), it must also be added to gcovr — failure produces inconsistent coverage between the lcov gate and the SonarCloud dashboard.

**Fix:** Add an explicit synchronization note to the gcovr step documentation: the `gcovr --exclude` list must mirror the `lcov --remove` exclusion set. Any change to either requires updating both. Document the current gcovr `--exclude` set with the rationale for each pattern.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `sonarcloud.yml` Analysis job — no `timeout-minutes`]

**Spec says (CI_CD_SPECS_FIX.md item 65):** Item 65 flags `sonarcloud-debug.yml`'s debug job as lacking `timeout-minutes`. No entry covers `sonarcloud.yml`'s `Analysis` job.

**Source says:** `sonarcloud.yml`'s `Analysis` job has no `timeout-minutes`. The job can take 10–20+ minutes (artifact downloads + path remap + full SonarScanner run). Without a timeout it inherits GitHub Actions' 6-hour default.

**Fix:** Add `sonarcloud.yml` `Analysis` job to the missing-timeout list. Recommend 30 minutes (10 for setup/artifacts + 20 for scan). Distinguish from item 65 which covers `sonarcloud-debug.yml` only.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `flawfinder.yml` — job-level permissions block (`security-events: write`, `actions: read`, `contents: read`) not documented]

**Spec says (CI_CD_SPECS_FIX.md item 62):** Documents `flawfinder.yml`'s triggers, cron, and action SHAs. Does not document the permissions block.

**Source says:** `flawfinder.yml` sets permissions at the JOB level (not workflow level): `permissions: actions: read, contents: read, security-events: write`. `security-events: write` is required by `github/codeql-action/upload-sarif` to write to the GitHub Security tab. `actions: read` is required to read workflow run data. These are job-level, consistent with `msvc.yml`'s `analyze` job.

**Fix:** Add the `flawfinder.yml` job-level permissions block to its documentation: `security-events: write` (SARIF Security tab upload), `contents: read` (checkout), `actions: read`. Note job-level scope (not workflow-level).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: artifact wildcard glob — `build/aitown-*.deb` and `build/aitown-*.exe` are additional approved exceptions not yet documented]

**Spec says (CI_CD_SPECS_FIX.md item 52):** Documents `release-assets/**` as the only approved exception to the anti-wildcard artifact upload rule.

**Source says:** `_package-linux-deb.yml` line 143: `path: build/aitown-*.deb`. `_package-windows.yml` line 90: `path: build/aitown-*.exe`. Both use wildcard globs because CPack generates filenames containing the computed version string and platform identifier at runtime — literal paths are impossible at authoring time. Neither is documented as an approved exception.

**Fix:** Extend item 52's anti-wildcard exception note to cover `build/aitown-*.deb` and `build/aitown-*.exe`. Shared rationale: CPack generates version-stamped filenames; a preceding CPack step guarantees at least one matching file. These are the three total approved wildcard exceptions: `release-assets/**`, `build/aitown-*.deb`, `build/aitown-*.exe`.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `docker-ci-image.yml` — `VCPKG_SHORT_SHA` env var and second `$GITHUB_ENV` write step undocumented]

**Spec says (CI_CD_SPECS_FIX.md items 27 and 47):** Document the `VCPKG_COMMIT_ID` extraction step. Do not mention `VCPKG_SHORT_SHA` or a second env write step.

**Source says:** `docker-ci-image.yml` lines 70–78: a separate "Compute VCPKG_SHORT_SHA" step takes the first 7 characters of `VCPKG_COMMIT_ID` (`${FULL:0:7}`) and writes `VCPKG_SHORT_SHA` to `$GITHUB_ENV`. This must be a separate step (not combined with step 2 or the build step) because `$GITHUB_ENV` writes are not visible within the same step. `VCPKG_SHORT_SHA` is used in the image tag: `ghcr.io/m0wa/aitown-ci-linux:vcpkg-${VCPKG_SHORT_SHA}`.

**Fix:** Add `VCPKG_SHORT_SHA` as a second env var to the `docker-ci-image.yml` documentation. Describe the two-step `$GITHUB_ENV` write pattern: step 2 writes `VCPKG_COMMIT_ID`, step 4 writes `VCPKG_SHORT_SHA` (first 7 chars). Note the image tag format uses `VCPKG_SHORT_SHA` for the human-readable component. Cross-reference the step ordering rule (item 12 of CLAUDE.md).


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `sonarcloud.yml` — `workflow_run` security rationale (never check out PR HEAD) undocumented]

**Spec says (CI_CD_SPECS_FIX.md items 27 and 64):** Prescribe documenting the trigger pattern and `workflow_dispatch` inputs. Neither mentions the security constraint on checkout behavior.

**Source says:** `sonarcloud.yml` lines 35–38 contain a SECURITY comment: "`workflow_run` executes with base-branch secrets. Never check out the PR HEAD (untrusted contributor code). Check out the base branch only; coverage data comes from the already-vetted CI artifact, not from re-running untrusted code." This constraint prevents `SONAR_TOKEN` exposure to contributor-controlled code.

**Fix:** Document the `workflow_run` security constraint: `workflow_run` triggers execute with repository secrets from the base branch; checking out the PR HEAD would expose `SONAR_TOKEN` to contributor code. Coverage and compile_commands come from the already-sandboxed CI artifact (via download), not re-execution. This constraint must not be relaxed. Add a warning: adding a PR HEAD checkout to this workflow is a security vulnerability.


**Status: DONE** — Applied in TASK 1.

---

### [caching.md: `msvc.yml` uses `env.VCPKG_COMMIT_ID` in cache key; spec only documents `inputs.vcpkg_commit_id` form for reusable workflows]

**Spec says (CI_CD_SPECS_FIX.md item 30):** Documents `_build-windows.yml` using `inputs.vcpkg_commit_id` in the cache key and explains why `env.` cannot be used in reusable workflow `jobs.<id>.with` blocks.

**Source says:** `msvc.yml` lines 52–55: cache key uses `${{ env.VCPKG_COMMIT_ID }}` (not `${{ inputs.vcpkg_commit_id }}`). `msvc.yml` is a top-level workflow (not a reusable workflow called via `uses:`), so it has direct access to the `env` context. This is the correct pattern for non-reusable workflows. The spec's caching documentation only covers the `inputs.` form.

**Fix:** Add a note to `caching.md` clarifying two valid patterns: (1) reusable workflows (`_build-windows.yml`) must use `inputs.vcpkg_commit_id` because `env` context is unavailable in `jobs.<id>.with`; (2) top-level workflows (`msvc.yml`) use `${{ env.VCPKG_COMMIT_ID }}` directly via the workflow-level `env:` block. Both produce identical cache key values.


**Status: DONE** — Applied in TASK 1.

---

### [github-actions-workflow.md: `supply-chain-lint` timeout must be set inside `_supply-chain-lint.yml`, not in `ci.yml` caller]

**Spec says (CI_CD_SPECS_FIX.md item 56):** Recommends adding `timeout-minutes: 5` but does not specify where — `ci.yml`'s caller entry or `_supply-chain-lint.yml`'s job definition.

**Source says:** For reusable workflows called via `uses:`, GitHub Actions does not support `timeout-minutes` on the caller's `uses:` job entry — it has no effect there. The timeout must be set on the job definition inside `_supply-chain-lint.yml` itself (on the `supply-chain-lint:` job at `jobs: supply-chain-lint:`). Adding it in `ci.yml` would be a no-op.

**Fix:** Clarify item 56's fix: `timeout-minutes: 5` must be added inside `_supply-chain-lint.yml`'s `supply-chain-lint` job definition, NOT in `ci.yml`'s `uses: ./.github/workflows/_supply-chain-lint.yml` caller entry. For reusable workflows, timeouts must be set in the called workflow's job block.


**Status: DONE** — Applied in TASK 1.

---

### [framework.md: `audio_tests` `add_executable` — spec shows 1 initial source file; deployed has 2]

**Spec says (`framework.md` line ~191):** `add_executable(audio_tests tests/audio/audio_smoke_test.cpp)` — one file, with the Phase 0 comment "initial target creation (smoke test only)."

**Source says (`CMakeLists.txt` lines 851–854):** `add_executable(audio_tests tests/audio/audio_smoke_test.cpp tests/audio/audio_crossfade_test.cpp)` — two files at initial declaration. `audio_crossfade_test.cpp` is present from the initial `add_executable` call.

**Fix:** Update `framework.md` line ~191 to show both initial source files. Update the "smoke test only" comment to reflect the two-file starting state.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [framework.md: `audio_tests` Phase 10+ source files undocumented — `volume_control_test.cpp`, `vehicle_audio_positioning_test.cpp`, `audio_system_transition_main_menu_test.cpp`, `audio_crossfade_property_test.cpp`]

**Spec says (`framework.md` lines ~201–206):** The Phase 7 and Phase 10 `target_sources` blocks are shown. No source files beyond those two blocks are documented.

**Source says (`CMakeLists.txt` lines 892–903):** Additional `target_sources` calls add `volume_control_test.cpp`, `vehicle_audio_positioning_test.cpp`, `audio_system_transition_main_menu_test.cpp`, and `audio_crossfade_property_test.cpp` — none mentioned in the spec. The Phase 10 block in the spec also lists 4 files but the deployed source has 5 in that group.

**Fix:** Update `framework.md`'s `audio_tests` source file inventory to include the post-Phase-10 additions. Correct the Phase 10 count from 4 to 5 files. Add the four unlisted files to the documented set.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [framework.md: `integration_tests` `add_executable` — spec shows 1 source file; deployed has 4; "sole non-device test" claim stale]

**Spec says (`framework.md` line ~264):** `add_executable(integration_tests tests/integration/irrlicht_ui_backend_compile_test.cpp)` with a note that this is "the sole non-device test in `integration_tests`."

**Source says (`CMakeLists.txt` lines 921–926):** `add_executable(integration_tests ...)` with 4 files: `irrlicht_ui_backend_compile_test.cpp`, `irrlicht_ui_backend_test.cpp`, `texture_cache_integration_test.cpp`, `camera_controller_live_test.cpp`. Additional files are added via `target_sources`. The "sole non-device test" claim is stale — multiple files now exist.

**Fix:** Update `framework.md` line ~264 to show the 4-file `add_executable`. Remove the "sole non-device test" claim and replace with the current expanded scope. Update the `integration_tests` table Notes column accordingly.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [framework.md: `opengl_tests` `add_executable` — spec shows 2 stub files; deployed has 6 real test files; stub names incorrect]

**Spec says (`framework.md` lines ~231–238):** `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp)` — two stub files.

**Source says (`CMakeLists.txt` lines 1001–1008):** `add_executable(opengl_tests ...)` with 6 files: `shader_loading_test.cpp`, `lod_swap_smoke_test.cpp`, `building_lod_swap_test.cpp`, `building_srgb_fallback_test.cpp`, `cloud_plane_test.cpp`, `texture_cache_atlas_test.cpp`. `stub_succeed.cpp` is absent (removed when real tests existed). `shader_stub_compile_test.cpp` was renamed to `shader_loading_test.cpp`. `building_lod_swap_test.cpp`, `building_srgb_fallback_test.cpp`, and `texture_cache_atlas_test.cpp` are entirely undocumented.

**Fix:** Update `framework.md` lines ~231–238 to show the current 6-file `add_executable`. Remove `stub_succeed.cpp` (deleted) and rename `shader_stub_compile_test.cpp` → `shader_loading_test.cpp`. Add the three undocumented files. Update the stub-lifecycle narrative to reflect that both stubs have been replaced by real test implementations.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).

---

### [coverage.md / CI_CD_SPECS_FIX.md item 64: Makefile `lcov --remove` missing `$(pwd)/vcpkg_installed/*` — sixth divergence not in item 64's table]

**Spec says (CI_CD_SPECS_FIX.md item 64):** Lists four Makefile-vs-CI divergences. Does not include the missing workspace-level vcpkg path.

**Source says (`Makefile` line 83, `_coverage-linux.yml` line 140):** CI excludes `$(pwd)/vcpkg_installed/*` (workspace-level vcpkg install directory) in addition to `/opt/vcpkg_installed/*` and `$(pwd)/build/vcpkg_installed/*`. The Makefile has the container path (`/opt/vcpkg_installed/*`) and the build-dir path (`*/build/vcpkg_installed/*`) but is missing the workspace-level form `$(pwd)/vcpkg_installed/*`.

**Fix:** Extend item 64's divergence table with a sixth row: Makefile `lcov --remove` is missing `$(pwd)/vcpkg_installed/*` (workspace-level vcpkg install, present in CI). Without it, coverage data from workspace-local vcpkg headers may not be properly excluded in local runs.


**Status: SKIPPED** — Fix targets `coverage.md / CI_CD_SPECS_FIX.md item 64` (outside the four CI/CD spec files).

---

### [coverage.md / CI_CD_SPECS_FIX.md item 64: Makefile `lcov --remove` uses `"*/.fetchcontent_cache/*"` (glob) vs CI's `"$(pwd)/.fetchcontent_cache/*"` — seventh divergence not in item 64's table]

**Spec says (CI_CD_SPECS_FIX.md items 36 and 64):** Item 36 documents changing the `coverage.md` canonical command from `"*/.fetchcontent_cache/*"` to `"$(pwd)/.fetchcontent_cache/*"`. Item 64 lists four Makefile-vs-CI divergences but does not include this path-form difference.

**Source says (`Makefile` line 85, `_coverage-linux.yml` line 141):** CI uses `"$(pwd)/.fetchcontent_cache/*"` (absolute via `$(pwd)`); Makefile uses `"*/.fetchcontent_cache/*"` (glob form). The glob form is safe on local machines but can over-match in container environments. The Makefile's glob form is intentionally local-developer-oriented.

**Fix:** Extend item 64's divergence table with a seventh row: Makefile uses `"*/.fetchcontent_cache/*"` (glob) while CI uses `"$(pwd)/.fetchcontent_cache/*"` (absolute `$(pwd)` form). Rationale for divergence: the glob form is acceptable locally; CI uses the absolute form for container-path precision.


**Status: SKIPPED** — Fix targets `coverage.md / CI_CD_SPECS_FIX.md item 64` (outside the four CI/CD spec files).

---

### [testability-architecture.md: `AITOWN_TESTING_ENABLED` on `aitown_ui` — link-error consequence undocumented; item 78 prescribes listing the targets but not the mechanistic rationale]

**Spec says (CI_CD_SPECS_FIX.md item 78):** Prescribes documenting three targets (`simulation_tests`, `aitown_ui`, `ui_tests`) but does not describe the link-error consequence of omitting the define from `aitown_ui`.

**Source says (`CMakeLists.txt` lines 819–826 comment):** The deployed comment reads: "`AITOWN_TESTING_ENABLED=1` MUST be on `aitown_ui PRIVATE` because `handleNewGameRequest` and `setGameSessionActiveForTest` are non-inline functions defined in `UIManager.cpp` — without this flag, these symbols are not compiled into the library, and `ui_tests` gets undefined-reference link errors." This is a mechanistic requirement (link error), not merely a test-seam gate. Item 78's fix prescription omits this.

**Fix:** Extend item 78's fix prescription to include the link-error rationale for `aitown_ui`: `handleNewGameRequest()` and `setGameSessionActiveForTest()` are non-inline functions in `UIManager.cpp` — omitting `AITOWN_TESTING_ENABLED=1` from `aitown_ui` causes undefined-reference link errors in `ui_tests`, not just test seam failures. This is mechanistically different from `simulation_tests` (where the flag gates an inline stub). Update `testability-architecture.md` to document this link-error consequence.


**Status: SKIPPED** — Fix targets `testability-architecture.md` (outside the four CI/CD spec files).

---

### [headless-ci-testing.md / framework.md: `_test-linux.yml` reusable workflow declares its own `packages: read` permission — callers cannot fully delegate container-pull permissions]

**Spec says:** No existing entry documents permission scoping for reusable workflows that pull GHCR container images.

**Source says (`_test-linux.yml` lines 42–45):** The `test-linux` job declares its own permissions block: `packages: read`, `checks: write`, `contents: read`. Reusable workflow jobs that pull container images from GHCR must declare `packages: read` at the job level INSIDE the reusable workflow — callers cannot delegate this by setting it only in their own permissions block.

**Fix:** Document in `headless-ci-testing.md` (or the reusable workflow spec section) that container-pulling reusable workflows must declare `packages: read` in their own job-level permissions block. Setting `packages: read` only in the caller does not grant the reusable workflow's job access to pull GHCR images.


**Status: SKIPPED** — Fix targets `headless-ci-testing.md / framework.md` (outside the four CI/CD spec files).

---

### [CI_CD_SPECS_FIX.md item 57 fix precision — prescribes `${BUILD_DIR}` in `headless-ci-testing.md` but `_coverage-linux.yml` hardcodes `--test-dir build`; single example cannot accurately represent both workflows]

**Spec says (CI_CD_SPECS_FIX.md item 57):** Prescribes updating ctest examples in `headless-ci-testing.md` to use `${BUILD_DIR}`. Does not note that `_coverage-linux.yml` hardcodes `--test-dir build` (as item 57 itself acknowledges), meaning a `${BUILD_DIR}` example would be inaccurate for the coverage job.

**Source says:** `_test-linux.yml` uses `${BUILD_DIR}` (job-level env var from `inputs.build_dir`). `_coverage-linux.yml` hardcodes `--test-dir build`. A single unified example using `${BUILD_DIR}` in `headless-ci-testing.md` would misrepresent the coverage job.

**Fix:** Clarify item 57's fix prescription: `headless-ci-testing.md` needs two separate ctest command examples — one for `_test-linux.yml` (parameterized `${BUILD_DIR}`, with a note it is set from `inputs.build_dir`) and one for `_coverage-linux.yml` (literal `build`, hardcoded). A single `${BUILD_DIR}` example would introduce a new inaccuracy for the coverage job.


**Status: SKIPPED** — Fix targets `CI_CD_SPECS_FIX.md item 57 fix precision — prescribes `${BUILD_DIR}` in `headless-ci-testing.md` but `_coverage-linux.yml` hardcodes `--test-dir build`; single example cannot accurately represent both workflows` (outside the four CI/CD spec files).

---

### [Makefile: `make test` conditionally skips `requires-opengl` tests when `xvfb-run` absent — undocumented; coverage implication unaddressed]

**Spec says (`headless-ci-testing.md` and `coverage.md`):** Neither file documents the conditional `xvfb-run` skip behavior in `make test`.

**Source says (`Makefile` lines 70–75):** The `requires-opengl` ctest step is wrapped in: `if which xvfb-run > /dev/null 2>&1; then ... else echo "xvfb-run not found — skipping requires-opengl tests"; fi`. On developer machines without `xvfb-run` (Windows, macOS, minimal Linux), OpenGL tests are silently skipped. CI always runs them (the container has `xvfb` pre-installed). Lines exercised only by `opengl_tests` will be uncovered in local coverage reports when xvfb is absent.

**Fix:** Document in `headless-ci-testing.md` and `coverage.md` that `make test` conditionally runs `requires-opengl` tests only when `xvfb-run` is present. State that CI always runs them. Add a coverage implication note: local `make test` coverage reports on machines without `xvfb-run` will have lower `requires-opengl`-only line coverage than the CI report.


**Status: SKIPPED** — Fix targets `Makefile` (outside the four CI/CD spec files).

---

### [framework.md: `terrain_tests` `target_include_directories` — spec lists `src/terrain/` and `${CMAKE_SOURCE_DIR}`; deployed source omits both]

**Spec says (`framework.md` line ~174):** `target_include_directories(terrain_tests PRIVATE tests/simulation/ tests/terrain/ src/terrain/ src/rendering/ src/interfaces/ ${CMAKE_SOURCE_DIR})`.

**Source says (`CMakeLists.txt` lines 643–649):** `target_include_directories(terrain_tests PRIVATE tests/simulation/ tests/terrain/ src/rendering/ src/interfaces/)` — `src/terrain/` and `${CMAKE_SOURCE_DIR}` are absent. `src/terrain/` is available via `aitown_terrain`'s PUBLIC include path. `${CMAKE_SOURCE_DIR}` access is provided via the `aitown_project_root_includes` interface library linked in `target_link_libraries`.

**Fix:** Update `framework.md`'s `terrain_tests` `target_include_directories` example to remove `src/terrain/` and `${CMAKE_SOURCE_DIR}`. Add a note that `src/terrain/` headers are transitively available via `aitown_terrain`'s `PUBLIC` include path, and that `${CMAKE_SOURCE_DIR}` access is provided by the `aitown_project_root_includes` interface library.


**Status: SKIPPED** — Fix targets `framework.md` (outside the four CI/CD spec files).