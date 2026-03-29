# CI/CD Review: AI Town Pipeline

**Reviewed files:**
- `/workspace/architecture/ci-cd/github-actions-workflow.md`
- `/workspace/architecture/ci-cd/dependency-management.md`
- `/workspace/architecture/ci-cd/caching.md`
- `/workspace/architecture/ci-cd/branch-protection.md`
- `/workspace/architecture/testing/headless-ci-testing.md`
- `/workspace/architecture/testing/coverage.md`
- `/workspace/.github/workflows/ci.yml`
- `/workspace/.github/workflows/_build-linux.yml`
- `/workspace/.github/workflows/_build-windows.yml`
- `/workspace/.github/workflows/_coverage-linux.yml`
- `/workspace/.github/workflows/_supply-chain-lint.yml`
- `/workspace/.github/workflows/_validate-assets.yml`
- `/workspace/.github/workflows/_markdown-lint.yml`
- `/workspace/.github/workflows/_package-windows.yml`
- `/workspace/.github/workflows/_package-linux-deb.yml`
- `/workspace/.github/workflows/docker-ci-image.yml`

---

## CRITICAL Issues

---

### ISSUE-1 [INCONSISTENCY] [CRITICAL] — `_build-linux.yml` and `_coverage-linux.yml` skip the vcpkg `actions/cache` step entirely

**Severity:** CRITICAL

**Description:**
`caching.md` (line 3–27) and `dependency-management.md` (line 26–31) both mandate an explicit `actions/cache` step for vcpkg packages in every build job, with a four-component cache key (`runner.os`, `COMPILER_VERSION`, `hashFiles('vcpkg.json')`, `vcpkg_commit_id`). The spec also explicitly states this step must come after the compiler-detect step.

`_build-linux.yml` and `_coverage-linux.yml` have no `actions/cache` step at all. Both configure CMake with `-DVCPKG_MANIFEST_INSTALL=OFF` and `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed`, delegating the entire vcpkg install to the pre-baked Docker image. There is also no `lukka/run-vcpkg` step in either file.

This means:
- The `actions/cache` architecture described in `caching.md` is completely absent from the two Linux jobs.
- The FetchContent cache key format specified in `caching.md` (line 28) is also never used.
- `caching.md` step 3 in the mandated order reads `COMPILER_VERSION` from step 2 — but on Linux the only purpose of the detect step in the actual workflow is to key the ccache action. The spec's four-component vcpkg key is irrelevant to the actual workflow, yet the spec documents it as required.

**Root cause:** The spec was written assuming `lukka/run-vcpkg` + `actions/cache` for Linux, but the implementation switched to a containerized GHCR image with pre-installed vcpkg packages (`/opt/vcpkg_installed`). The spec was never updated to reflect this architectural shift.

**Proposed resolution:** Update `caching.md` and `dependency-management.md` to document the containerized approach: when `container: image: ghcr.io/...` is in use with `VCPKG_MANIFEST_INSTALL=OFF`, no `actions/cache` vcpkg step and no `lukka/run-vcpkg` step are used in `build-linux` / `coverage-linux`. The cache strategy shifts to the Docker layer cache in `docker-ci-image.yml` (via `cache-from/cache-to: type=gha`). Add a note that `caching.md`'s four-component vcpkg key applies to the Windows job only. Consolidate this in a "Platform-specific caching summary" table.

---

### ISSUE-2 [INCONSISTENCY] [CRITICAL] — `dependency-management.md` mandates `lukka/run-vcpkg` for Linux; actual workflow uses none

**Severity:** CRITICAL

**Description:**
`dependency-management.md` line 26 states: "CI uses `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1` with a pinned vcpkg commit hash stored as `env.VCPKG_COMMIT_ID`", and this is presented as applying to all CI jobs. The spec further states `VCPKG_COMMIT_ID` must be declared at the workflow level "so it is available to all jobs and steps."

`_build-linux.yml` has zero references to `lukka/run-vcpkg`. The vcpkg commit ID is passed as an input (`vcpkg_commit_id`) but is only used in the `ci.yml` orchestration layer; `_build-linux.yml` and `_coverage-linux.yml` never consume it. The baseline consistency check in `_validate-assets.yml` does consume it correctly, but `build-linux` and `coverage-linux` bypass the entire install step via the pre-baked image.

This creates a dangerous gap: if the `docker-ci-image.yml` image is rebuilt at a different vcpkg baseline than `VCPKG_COMMIT_ID`, the installed packages in the image will silently mismatch the validated baseline. The `validate-assets` job's baseline consistency check verifies `vcpkg.json`'s baseline matches `VCPKG_COMMIT_ID`, but never verifies that the image was built with the same commit.

**Proposed resolution:** The spec must document that Linux builds derive their vcpkg installation from the Docker image rather than a live `lukka/run-vcpkg` invocation, and explain the atomicity contract: image rebuild (`docker-ci-image.yml`) must be triggered and completed before the updated baseline can be used in `build-linux`. Add a CI step or check (in `docker-ci-image.yml` or as a new `validate-baseline-image` step) that verifies the image's embedded vcpkg commit matches the workflow-level `VCPKG_COMMIT_ID`.

---

### ISSUE-3 [INCONSISTENCY] [CRITICAL] — `coverage.md` specifies `--ignore-errors mismatch,inconsistent,version` but actual workflow omits `version`

**Severity:** CRITICAL

**Description:**
`coverage.md` lines 12–19 show the canonical `lcov --capture` invocation with `--ignore-errors mismatch,inconsistent,version`. The `version` error suppresses "GCC/gcov version-string mismatch when build and capture gcov versions differ." The `CLAUDE.md` notes section also specifies `mismatch,inconsistent,version` (comma-separated single flag).

`_coverage-linux.yml` line 201 uses `--ignore-errors mismatch,inconsistent` — omitting `version`. Since the Dockerfile installs gcc-13 but the comment in `_coverage-linux.yml` itself (lines 186–188) documents that "gcov-14 cannot read .gcda files produced by GCC 13," this is a concern: although the `--gcov-tool gcov-13` flag routes to the correct gcov binary, a version-string mismatch warning from an lcov internal consistency check can still cause a non-zero exit when `version` is absent. Under lcov 2.x this may not be a hard failure in the specific scenario with `--gcov-tool gcov-13`, but the spec and the implementation disagree.

**Proposed resolution:** Add `version` to the `--ignore-errors` flag in `_coverage-linux.yml`'s `lcov --capture` invocation to match `coverage.md` exactly. Alternatively, if the implementation team has confirmed `version` is not needed with `--gcov-tool gcov-13`, update `coverage.md` and `CLAUDE.md` to remove `version` from the documented set.

---

### ISSUE-4 [GAP] [CRITICAL] — `_build-linux.yml` missing `actions/cache` step for vcpkg, but spec step ordering comment is orphaned

**Severity:** CRITICAL

**Description:**
`_build-linux.yml` step numbering comments reference steps that do not exist in the file. The file comments say "Step 1: checkout", "Step 4: Detect GCC version", "Step 8: Set up ccache", "Step 9: Configure CMake". Steps 2, 3, 5, 6, 7 are not present and are not explained. The gaps correspond to the removed `actions/cache` (vcpkg), `lukka/run-vcpkg`, and possibly system-dependency install steps. The same issue is present in `_coverage-linux.yml` which uses "Step 4", "Step 8", "Step 9" with identical gaps.

This creates maintenance confusion: a developer following the spec's step ordering (which explicitly numbers steps 1–17+ in `github-actions-workflow.md` §coverage-linux) cannot reconcile the spec's step list with the file's step comments.

**Proposed resolution:** Renumber the step comments in `_build-linux.yml` and `_coverage-linux.yml` to be sequential (1, 2, 3 ...) reflecting only steps that actually exist. Alternatively, retain the spec numbering but add an explanatory comment block at the top of each file stating which spec steps are handled by the Docker image and therefore absent.

---

### ISSUE-5 [PROBLEM] [CRITICAL] — `_coverage-linux.yml` `Preflight src/simulation/ coverage entries` step exits 0 on failure (warning-only) but spec mandates hard-fail at Phase 6

**Severity:** CRITICAL

**Description:**
`coverage.md` §Phase 6 (lines 183–192) specifies that the `src/simulation/` SF preflight step must `exit 1` if no `src/simulation/` SF entries are present in `coverage_filtered.info`. The step in `github-actions-workflow.md` (step 16b) also states it is a "Phase 6 deliverable" that runs before the 95% gate.

`_coverage-linux.yml` lines 248–256 implement this step as warning-only: it prints a `WARNING:` message and does NOT exit non-zero when simulation entries are absent. The comment says "change this step to exit 1 on missing simulation entries" — indicating Phase 6 work was completed but this step was never hardened. The project is well past Phase 6 (the Phase 11 per-file 85% floor step at line 258 is already implemented as hard-fail).

This means a broken `simulation_tests` registration would cause the entire 95% gate to silently compute coverage across only `src/terrain/` and `src/ui/`, potentially showing green while simulation code is 0% covered.

**Proposed resolution:** Remove the warning-only logic and replace with the hard-fail form from `coverage.md` §Phase 6:
```bash
if ! grep -q "SF:.*src/simulation/" coverage_filtered.info; then
  echo "PREFLIGHT FAIL: No src/simulation/ SF entries in coverage_filtered.info."
  exit 1
fi
```

---

## HIGH Issues

---

### ISSUE-6 [INCONSISTENCY] [HIGH] — `dependency-management.md` specifies `glew.lib` library name but actual workflow verifies `glew32.lib`

**Severity:** HIGH

**Description:**
`dependency-management.md` §Step B "Verify GLEW vcpkg install" (lines 230–239) shows the canonical YAML checking for `build/vcpkg_installed/x64-windows/lib/glew.lib`. However, `_build-windows.yml` step 10 (line 105) correctly checks `glew32.lib` and includes a comment: "On Windows, vcpkg GLEW port installs glew32.lib (not glew.lib) per portfile libname override."

The spec document is wrong: it specifies `glew.lib` in the canonical YAML. Any developer implementing from the spec will write a check that never fails (glew.lib does not exist) rather than actually verifying the real file `glew32.lib`.

**Proposed resolution:** Update `dependency-management.md` Step B canonical YAML to reference `glew32.lib` not `glew.lib`, and add a note explaining the Windows portfile libname override.

---

### ISSUE-7 [INCONSISTENCY] [HIGH] — `caching.md` FetchContent cache section references `.fetchcontent_cache` which is unused in actual Linux jobs

**Severity:** HIGH

**Description:**
`caching.md` lines 28–29 mandate caching `.fetchcontent_cache` with a key including `COMPILER_VERSION` and `hashFiles('CMakeLists.txt', 'cmake/**')`. This key format is documented as "MUST be identical between dependency-management.md and this file."

Neither `_build-linux.yml` nor `_coverage-linux.yml` have a FetchContent cache step. Because vcpkg manages all test dependencies (gtest, rapidcheck), FetchContent is not used in the Linux container build path. The `.fetchcontent_cache` directory never exists.

`dependency-management.md` line 172 explicitly states: "Why vcpkg (not FetchContent): Using vcpkg for all C++ dependencies — including test dependencies — eliminates git clone overhead at CMake configure time." This directly contradicts the need for the FetchContent caching section in `caching.md`.

**Proposed resolution:** Mark the FetchContent caching section in `caching.md` as "applicable only if FetchContent is used" with a note that the current implementation uses vcpkg for all dependencies. Alternatively, remove it entirely since the current and planned architecture uses vcpkg exclusively. The "MUST be identical" cross-reference to `dependency-management.md` should also be removed or qualified.

---

### ISSUE-8 [INCONSISTENCY] [HIGH] — `dependency-management.md` lists `libxxf86vm-dev` as a required system package but it is absent from `github-actions-workflow.md` and both actual workflows

**Severity:** HIGH

**Description:**
`headless-ci-testing.md` line 5–6 lists `libxxf86vm-dev` as a required system package: "provides the `xf86vmode` library required by Irrlicht's X11 display mode enumeration." The note explicitly says without it, Irrlicht fails to build with `cannot find -lXxf86vm`.

`dependency-management.md` lines 53–58 list required apt packages for `build-linux` and `coverage-linux` but do NOT include `libxxf86vm-dev`. `github-actions-workflow.md` also does not include it in the system dependency install step.

`_build-linux.yml` and `_coverage-linux.yml` have no apt-get install step at all (dependencies are in the Docker image). However, the spec documents an explicit `apt-get install` step with a list that is missing `libxxf86vm-dev`.

**Proposed resolution:** Add `libxxf86vm-dev` to the apt package list in `dependency-management.md`. Verify the Docker image includes it (check `docker/ci-linux/Dockerfile`). Also align `headless-ci-testing.md`'s package list with `dependency-management.md` since they describe the same requirement.

---

### ISSUE-9 [GAP] [HIGH] — No spec coverage for `bump-version` and `release` jobs

**Severity:** HIGH

**Description:**
`github-actions-workflow.md` contains a `## 'bump-version' Job` section (referenced at line 969+) and a `## 'release' Job` section, and both jobs are implemented in `ci.yml` (lines 163–253). However, neither `branch-protection.md` nor `caching.md` mention these jobs, and neither section describes:

- What permissions `bump-version` needs to push to `main` and the implications for branch protection rules that require PRs. The job uses `contents: write` and force-pushes a tag — bypassing branch protection for the commit.
- What `release` retention policy applies for GitHub release assets (as opposed to artifact retention).
- Whether `bump-version` should be gated on `all-checks-pass` (currently it is not in `all-checks-pass.needs`).
- The race condition: `bump-version` runs on every push to `main` in parallel with `release`. If `bump-version` fails, the tag is not pushed, but `release` may still attempt to create a release with the wrong version.

The `bump-version` job's `git push --follow-tags` will fail on protected branches because it is a direct push (not a PR). The job sets `contents: write` but branch protection rules require PRs with approvals. This is a latent failure mode.

**Proposed resolution:** Add a `## Auto-versioning` section to `github-actions-workflow.md` explaining the `bump-version` / `release` lifecycle, including: (1) why `bump-version` must use a branch protection bypass token or `permissions: contents: write` with an allowed bypass actor; (2) the sequencing dependency between `bump-version` and `release`; (3) an explicit note that `release` is outside the `all-checks-pass` gate by design; (4) retention policy for GitHub release assets (separate from artifact retention).

---

### ISSUE-10 [PROBLEM] [HIGH] — `_package-windows.yml` re-runs vcpkg install without MSVC environment setup for the cache step

**Severity:** HIGH

**Description:**
`_package-windows.yml` runs `ilammy/msvc-dev-cmd` and `lukka/run-vcpkg` but does NOT include the MSVC version detect step (`vswhere.exe`) or an `actions/cache` step for vcpkg packages before running vcpkg. This means:
1. On a cold cache the package job rebuilds all vcpkg dependencies from scratch — adding 30–40 minutes to every `main`/`develop` push.
2. The `caching.md` four-component key requirement is not implemented here.

Additionally, `_package-windows.yml` does not add `build\vcpkg_installed\x64-windows\bin` to PATH before running CPack (only appends it via `Out-File -FilePath $env:GITHUB_PATH` which takes effect for subsequent steps — this is correct, but no step actually uses it in the package job since tests are disabled). This is not a bug but is undocumented inconsistency.

**Proposed resolution:** Add a vcpkg cache step to `_package-windows.yml` mirroring `_build-windows.yml` steps 3–4, or add a note to `github-actions-workflow.md` explicitly acknowledging that packaging jobs accept the cold-cache rebuild cost and explaining why (packaging is release-only and infrequent).

---

### ISSUE-11 [GAP] [HIGH] — `_package-linux-deb.yml` installs vcpkg from source on every run with no caching; containers are not digest-pinned

**Severity:** HIGH

**Description:**
`_package-linux-deb.yml` runs on raw distro container images (`debian:bookworm`, `ubuntu:22.04`, etc.) and clones the entire vcpkg repo, builds the vcpkg tool from source, and runs `vcpkg install` on every single run. This is:
1. Extremely slow (15–30+ minutes per matrix leg), and
2. Uses floating tags (`debian:bookworm`, `ubuntu:22.04`) with no digest pin, which the supply-chain lint (`_supply-chain-lint.yml` lines 49–55) would flag if those lines were `container: image:` entries in a CI workflow. They are matrix-injected values so the lint regex (`^\s+image:\s+`) would match the final resolved `container: ${{ matrix.container }}` line.

**Check:** `_supply-chain-lint.yml` checks `container: image:` lines but does NOT check `container: ${{ matrix.container }}` because the value is a context variable, not a literal. The lint regex at line 50 matches literal image lines. This means the four matrix containers bypass the digest-pin lint entirely.

This creates a supply-chain gap: a tag like `debian:bookworm` can be updated silently to point to a different image digest, and no CI check would catch it.

**Proposed resolution:**
1. Add a note to `dependency-management.md` documenting that packaging containers use floating tags by design (cross-distro compatibility testing requires tracking moving distro images), but acknowledge the supply-chain trade-off.
2. Add a caching strategy for the vcpkg build in `_package-linux-deb.yml` (cache `/opt/vcpkg/installed` keyed on `inputs.vcpkg_commit_id`).
3. Alternatively, build distro-specific CI images similar to the main `ghcr.io/m0wa/aitown-ci-linux` image.

---

### ISSUE-12 [INCONSISTENCY] [HIGH] — `github-actions-workflow.md` documents `markdown-lint` step with unpinned `npm install -g markdownlint-cli` but actual workflow pins `@0.47.0`

**Severity:** HIGH

**Description:**
`github-actions-workflow.md` §markdown-lint job (line 583) shows: `run: npm install -g markdownlint-cli` (no version pin). The spec note at line 594 says "To pin to a specific version use `npm install -g markdownlint-cli@0.47.0`" as an option, not a requirement.

`_markdown-lint.yml` line 26 correctly pins `npm install -g markdownlint-cli@0.47.0`.

The spec presents pinning as optional ("to pin... use...") while the actual implementation correctly enforces it. A future developer reading the spec may implement an unpinned version, causing non-reproducible lint results.

**Proposed resolution:** Update `github-actions-workflow.md` §markdown-lint to change the job definition snippet to use `markdownlint-cli@0.47.0` and reword the note from "to pin... use" to "MUST pin to a specific version; current pin: `@0.47.0`."

---

### ISSUE-13 [INCONSISTENCY] [HIGH] — `coverage.md` Phase 4 src/ui/ gate uses `lcov --list` parsing but `_coverage-linux.yml` uses direct `.info` file parsing

**Severity:** HIGH

**Description:**
`coverage.md` §Phase 4 src/ui/ Coverage Baseline (lines 244–276) documents a gate using `lcov --list coverage_filtered.info | grep -E "src/ui/" | awk -F'|' '{print $NF+0}'` to extract coverage percentages from column-delimited `--list` output. The same section includes a preflight check for the `|` column delimiter.

`_coverage-linux.yml` step 20 (lines 309–334) uses a completely different approach: it parses `coverage_filtered.info` directly via SF/LH/LF records in a single awk pass. This approach is described in `coverage.md` as the Phase 6 method and labeled "version-agnostic; does NOT use `lcov --list` output."

Both approaches produce the same result when correctly implemented, but the spec and implementation disagree about which method to use for the Phase 4 gate. Future maintainers reading the spec will implement the `lcov --list` parsing approach and get a different implementation from what is deployed.

**Proposed resolution:** Update `coverage.md` §Phase 4 to replace the `lcov --list` parsing approach with the direct `.info` parsing approach (awk SF/LH/LF), since that is what is actually deployed and is acknowledged as superior (version-agnostic). Remove the lcov 2.x `|` delimiter preflight check from the spec since it is no longer needed.

---

### ISSUE-14 [PROBLEM] [HIGH] — `_build-linux.yml` step comments reference out-of-order step numbers that are never resolved

**Severity:** HIGH

**Description:**
`_build-linux.yml` step comments use non-sequential numbering (Steps 1, 4, 8, 9, 10, 10b, 11, 12, 12b, 12c, 13, 14, 15, 16, 17, 18, 19). Steps 2, 3, 5, 6, 7 are absent with no explanation. The same pattern appears in `_coverage-linux.yml` (Steps 1, 4, 8, 9, 10, 11, 12, 12b, 12c, 13–21).

This numbering originates from `github-actions-workflow.md` §coverage-linux ordered step list (lines 317–336), where some steps correspond to operations now embedded in the Docker image. However, the spec's step list (1–17) in that section also skips the system-dependency install step that was removed from the container-based workflow.

The skipped step numbers cause confusion when cross-referencing spec to implementation. Anyone adding a new step using the spec's numbering will produce a step number collision or further gaps.

**Proposed resolution:** Either (a) renumber steps in both workflow files to be fully sequential, or (b) add a comment at the top of each workflow file listing which spec steps are handled by the Docker image ("Steps 2, 3, 5, 6, 7 are handled by the GHCR container image and are not present as explicit steps"). The spec's ordered step list in `github-actions-workflow.md` §coverage-linux should be updated to mark container-handled steps.

---

## MEDIUM Issues

---

### ISSUE-15 [DUPLICATE] [MEDIUM] — Supply-chain SHA lint logic is duplicated between `_supply-chain-lint.yml` and `docker-ci-image.yml`

**Severity:** MEDIUM

**Description:**
The SHA lint logic (checking for angle-bracket placeholders and short SHAs) appears in three places:
1. `_supply-chain-lint.yml` lines 27–47 (canonical, checks all workflow files)
2. `docker-ci-image.yml` lines 83–101 (self-check, scoped to `docker-ci-image.yml` only)
3. `github-actions-workflow.md` lines 33–56 (documented as a step inside `build-linux`)

The spec (`github-actions-workflow.md` line 27–58) describes this as "the first named step in `build-linux`", but the actual implementation delegates it to `_supply-chain-lint.yml` as a separate reusable workflow. `build-linux` no longer contains the lint step inline. The spec is outdated.

**Proposed resolution:** Update `github-actions-workflow.md` to describe the supply-chain lint as a dedicated reusable workflow job (`_supply-chain-lint.yml`) rather than an inline step in `build-linux`. Note that `docker-ci-image.yml` carries its own self-check for defense-in-depth. Keep one authoritative source for the lint logic description.

---

### ISSUE-16 [GAP] [MEDIUM] — `branch-protection.md` does not address `bump-version` direct-push exception

**Severity:** MEDIUM

**Description:**
`branch-protection.md` §5 states: "Do not allow bypass by administrators" and "all contributors including admins must merge through PRs." The `bump-version` job in `ci.yml` (lines 176–188) directly pushes a commit and tag to `main` via `git push --follow-tags` using the `GITHUB_TOKEN` with `contents: write`. This is a bot-originated direct push that bypasses the PR requirement.

GitHub branch protection rules can be configured to allow `github-actions[bot]` as a bypass actor even when administrator bypass is disabled. The spec does not document this exception, leaving implementers with no guidance on how to make the `bump-version` job work without disabling the branch protection rule that the spec mandates.

**Proposed resolution:** Add a sub-section to `branch-protection.md` titled "Bot push bypass for auto-versioning" that:
1. Explains that `github-actions[bot]` must be added to the "Allow specified actors to bypass required pull requests" list for both `main` and `develop`.
2. Notes this is a narrowly scoped exception for the `bump-version` bot commit only.
3. Clarifies that human contributors and administrator accounts remain subject to the full PR requirement.

---

### ISSUE-17 [GAP] [MEDIUM] — No spec for `actions/download-artifact` SHA pin in `release` job

**Severity:** MEDIUM

**Description:**
`ci.yml` `release` job (lines 217–244) uses `actions/download-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08` — the same SHA as `actions/upload-artifact`. This is correct by coincidence (both are v4.6.0), but `caching.md` only lists `actions/upload-artifact` in its SHA registry (line 35). `actions/download-artifact` is not listed in `caching.md` even though it requires a SHA pin equally.

`_supply-chain-lint.yml` would catch a missing pin at CI time, but the spec has a gap: maintainers updating the SHA registry in `caching.md` may not update `actions/download-artifact` since it is not listed.

**Proposed resolution:** Add `actions/download-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08 # v4.6.0` to the SHA registry in `caching.md`. Note that upload and download share the same v4.6.0 SHA and must be updated together.

---

### ISSUE-18 [GAP] [MEDIUM] — `docker-ci-image.yml` digest update is manual; no automated reminder or enforcement

**Severity:** MEDIUM

**Description:**
After `docker-ci-image.yml` builds and pushes a new image, step 9 (lines 140–159) prints instructions to manually update `ci.yml` and `.devcontainer/Dockerfile` with the new digest. This is a purely manual step. There is no CI check that verifies the digest in `_build-linux.yml` / `_coverage-linux.yml` matches the most recently pushed image digest.

The five-item vcpkg baseline atomicity contract (`CLAUDE.md` §vcpkg Baseline Atomicity) documents this requirement, but nothing in the CI pipeline enforces that an image rebuild is followed by a digest pin update in `ci.yml`. A developer could rebuild the image, forget to update the digest, and CI would keep using the old image silently.

**Proposed resolution:** Add a step to `docker-ci-image.yml` that uses `gh` CLI to create a draft PR (or open an issue) with the exact diff needed for `ci.yml` and `.devcontainer/Dockerfile`. Alternatively, document a `validate-image-digest` step in `_build-linux.yml` or `_supply-chain-lint.yml` that queries the GHCR image manifest and verifies the pinned digest matches the tag used in `container: image:`.

---

### ISSUE-19 [INCONSISTENCY] [MEDIUM] — `github-actions-workflow.md` `lcov --capture` uses `${{ github.workspace }}` for `--base-directory` but `_coverage-linux.yml` uses `.` (dot)

**Severity:** MEDIUM

**Description:**
`github-actions-workflow.md` lines 423–431 explain at length why `${{ github.workspace }}` is preferred over `.` for the `--base-directory` argument: "it works correctly on self-hosted runners where the runner's working directory convention may differ from `$GITHUB_WORKSPACE`."

`_coverage-linux.yml` line 199 uses `--base-directory .` (dot), contradicting the spec's preferred form.

This is not a bug in the standard GitHub-hosted runner case because the shell CWD is always `$GITHUB_WORKSPACE` at step start. However, the spec explicitly documents the reason for preferring the absolute path form, and the implementation silently ignores it.

**Proposed resolution:** Update `_coverage-linux.yml` line 199 to use `--base-directory "${GITHUB_WORKSPACE}"` (environment variable form, safe inside a container where `${{ github.workspace }}` may resolve to the host path while the shell CWD is the container path `/__w/...`). Note: inside the container, `$GITHUB_WORKSPACE` resolves to the container path `/__w/ai-town/ai-town`, making `"${GITHUB_WORKSPACE}"` more correct than `${{ github.workspace }}` which resolves to the host path at YAML evaluation time. Update the spec to acknowledge this container-path distinction.

---

### ISSUE-20 [MISSING] [MEDIUM] — No spec for Dependabot or automated dependency update automation

**Severity:** MEDIUM

**Description:**
No spec file in `architecture/ci-cd/` addresses automated dependency update tooling. The pipeline currently has:
- Pinned action SHAs that go stale silently (no automated PR when a new version is released)
- A pinned vcpkg baseline that must be manually updated
- A pinned `markdownlint-cli@0.47.0` that goes stale silently
- Pinned `mutagen` without version pin (contradicting the reproducibility goals stated in the spec)

`dependency-management.md` says "Do NOT pin `mutagen` to a specific version" (line 658) which is inconsistent with the reproducibility principles applied to all other pinned dependencies.

**Proposed resolution:** Add a `## Automated Dependency Updates` section to `dependency-management.md` addressing: (1) whether Dependabot is configured for GitHub Actions; (2) the process for updating pinned action SHAs; (3) an explicit policy on Python pip dependency pinning (either pin all pip packages with a `requirements-validate-assets.txt` or acknowledge the non-reproducibility trade-off with a rationale).

---

### ISSUE-21 [GAP] [MEDIUM] — Windows `_package-windows.yml` does not run tests before packaging; spec has no test gate on packaging

**Severity:** MEDIUM

**Description:**
`_package-windows.yml` configures CMake with `-DBUILD_TESTING=OFF` and never runs CTest. The CI wiring in `ci.yml` does gate `package-windows` on `needs: [build-windows]` (line 141), meaning the quality gate is enforced. However:

1. `github-actions-workflow.md` has no spec section for the packaging jobs at all (the spec ends at the `release` job with minimal description).
2. The `needs: [build-windows]` dependency means a successful `build-windows` (which does run tests) gates the packaging job. But `package-windows` rebuilds from source independently — it does not reuse `build-windows` artifacts. A fluke in the second build that produces a different binary than the tested one is theoretically possible.

**Proposed resolution:** Add a `## Packaging Jobs` section to `github-actions-workflow.md` explaining: (1) why packaging rebuilds from source rather than reusing `build-windows` artifacts (artifact transfer size, cache alignment); (2) how the `needs: [build-windows]` dependency provides the quality gate; (3) why `BUILD_TESTING=OFF` is correct for packaging builds; (4) what retention policy applies to packaging artifacts.

---

### ISSUE-22 [INCONSISTENCY] [MEDIUM] — `caching.md` mentions `softprops/action-gh-release` with a placeholder SHA but `ci.yml` uses a real SHA that is undocumented in the spec

**Severity:** MEDIUM

**Description:**
`caching.md` line 37 lists: `softprops/action-gh-release@<40-CHAR-SHA>  # resolve at implementation time`. The implementation in `ci.yml` line 247 uses `softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564  # v2.1.0`.

The `<40-CHAR-SHA>` placeholder in `caching.md` was never updated to the real resolved SHA. The supply-chain lint in `_supply-chain-lint.yml` does NOT check spec markdown files — only `.github/workflows/*.yml` files. So the placeholder in the spec goes undetected.

**Proposed resolution:** Update `caching.md` line 37 to replace the placeholder with `softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564 # v2.1.0`.

---

### ISSUE-23 [PROBLEM] [MEDIUM] — `_coverage-linux.yml` lcov `--ignore-errors` on `--remove` step uses `unused,inconsistent` but spec specifies only `unused`

**Severity:** MEDIUM

**Description:**
`coverage.md` lines 30–32 specify `--ignore-errors unused` for the `lcov --remove` step (not `unused,inconsistent`). `_coverage-linux.yml` line 216 uses `--ignore-errors unused,inconsistent` on the `--remove` step. The additional `inconsistent` suppressor is not documented in `coverage.md` and its rationale is not captured anywhere in the spec.

This is a low-risk discrepancy (suppressing additional warnings is not harmful) but it means the spec does not explain why `inconsistent` is needed on the `--remove` step in addition to the `--capture` step. If someone follows the spec and omits `inconsistent` from `--remove`, they may encounter unexpected non-zero exits.

**Proposed resolution:** Add `inconsistent` to the `--ignore-errors` on the `lcov --remove` step in `coverage.md` and add a comment explaining: "lcov 2.x may also emit inconsistent data errors during --remove when processing coverage data with lambda inlining; include inconsistent here for the same reason as --capture."

---

### ISSUE-24 [GAP] [MEDIUM] — `branch-protection.md` does not address `develop` branch protection for `package-linux-deb` and `package-windows` jobs

**Severity:** MEDIUM

**Description:**
`ci.yml` lines 141–155 trigger `package-windows` and `package-linux-deb` on both `main` and `develop` pushes (`github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop'`). `branch-protection.md` requires protected `develop` to pass `all-checks-pass`, which is correct. But there is no discussion of the packaging jobs' relationship to branch protection: these jobs are not in `all-checks-pass.needs`, so a failing packaging job does not block `develop` merges.

This means a broken CPack configuration or NSIS packaging script can merge to `develop` silently through PRs (PRs only run CI, not packaging — packaging only triggers on push). The first signal of a broken installer is when the package job runs post-merge.

**Proposed resolution:** Add a note to `branch-protection.md` explaining that packaging jobs run post-merge and are intentionally outside the PR gate, along with the rationale (packaging is release infrastructure, not build quality). Document the remediation path when a packaging job fails on `develop` (immediate follow-up PR to fix, not a rollback).

---

## LOW Issues

---

### ISSUE-25 [DUPLICATE] [LOW] — vcpkg baseline atomicity requirements are documented in three separate places with slight wording differences

**Severity:** LOW

**Description:**
The five-item vcpkg baseline atomicity commit requirement is documented in:
1. `dependency-management.md` lines 3–4 (brief mention)
2. `CLAUDE.md` §Build & Toolchain "vcpkg Baseline Atomicity" (five numbered items)
3. `MEMORY.md` §vcpkg Baseline Staleness (separate memory note with different framing)

The three descriptions have slightly different item counts and wording. `CLAUDE.md` lists five items. `dependency-management.md` only mentions `builtin-baseline` and `VCPKG_COMMIT_ID` without the full five-item list. A developer reading only `dependency-management.md` would miss items 3–5 (Dockerfile, devcontainer, digest pin).

**Proposed resolution:** Add the full five-item atomicity list to `dependency-management.md` as the canonical location, and change `CLAUDE.md` to reference `dependency-management.md` for the authoritative list rather than duplicating it.

---

### ISSUE-26 [MISSING] [LOW] — No spec for what happens when `docker-ci-image.yml` monthly schedule triggers but the image digest is already current

**Severity:** LOW

**Description:**
`docker-ci-image.yml` has a monthly schedule trigger (`cron: '0 2 1 * *'`). On a scheduled run, if no files changed, Docker BuildKit will produce the same image layers (assuming the base OS packages are unchanged). But it will push a new image tag with the same digest, and the digest in `ci.yml` / `.devcontainer/Dockerfile` may not need updating.

There is no documented policy on:
- Whether the monthly rebuild always produces a new digest (OS security updates usually do)
- Whether the digest update PR should be created automatically or manually reviewed
- What to do if the scheduled rebuild fails (no notification mechanism is documented)

**Proposed resolution:** Add a brief spec note to `github-actions-workflow.md` in the `docker-ci-image.yml` section explaining: (1) the monthly rebuild purpose (pick up OS security patches from the base image); (2) that a digest change from a scheduled rebuild requires the same atomicity commit as a vcpkg baseline update (items 4 and 5 of the five-item contract); (3) recommended alerting if the scheduled rebuild fails (GitHub Actions notification to repo admins).

---

### ISSUE-27 [GAP] [LOW] — `caching.md` does not specify artifact retention for `_package-linux-deb.yml` or `_package-windows.yml` packaging artifacts

**Severity:** LOW

**Description:**
`caching.md` (and `github-actions-workflow.md` §Artifact retention) specify retention for test XML (14 days), coverage HTML (14 days), and Windows binary (30 days). The packaging artifacts in `_package-windows.yml` (line 58: `retention-days: 30`) and `_package-linux-deb.yml` (line 108: `retention-days: 30`) both use 30 days — matching the Windows binary.

This is consistent, but the 30-day retention for packaging artifacts is not explicitly stated in the spec. A future change to artifact retention policy might update the spec without updating the packaging workflows.

**Proposed resolution:** Add packaging artifact retention (30 days, same as release binaries) to the retention policy table in `github-actions-workflow.md`.

---

### ISSUE-28 [PROBLEM] [LOW] — `_package-windows.yml` uses `Out-File -FilePath $env:GITHUB_PATH` PATH append idiom but `_build-windows.yml` uses `>> $env:GITHUB_PATH`

**Severity:** LOW

**Description:**
`_build-windows.yml` line 125 appends to `GITHUB_PATH` with `"..." >> $env:GITHUB_PATH`. `_package-windows.yml` line 47 uses `echo "$vcpkgBin" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append`. Both accomplish the same result but inconsistently.

Neither `caching.md` nor `dependency-management.md` document the canonical PS 5.1 GITHUB_PATH append idiom. `caching.md` only documents the `$GITHUB_ENV` append idiom for compiler version detection.

**Proposed resolution:** Add the canonical PS 5.1 `GITHUB_PATH` append idiom to `caching.md` alongside the `GITHUB_ENV` idiom. Standardize both workflows to use `"$vcpkgBin" >> $env:GITHUB_PATH` form (the simpler form already used in `_build-windows.yml`).

---

### ISSUE-29 [MISSING] [LOW] — No spec for `ilammy/msvc-dev-cmd` SHA in the SHA registry

**Severity:** LOW

**Description:**
`caching.md` lines 30–37 document the SHA registry for all pinned actions, but `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` (v1.13.0) is not included in the `caching.md` SHA list. It appears only in `github-actions-workflow.md` §Windows job (line 180) and in the actual workflow files.

If the SHA registry in `caching.md` is intended to be the authoritative list for supply-chain management, this omission is a gap.

**Proposed resolution:** Add `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c # v1.13.0` and `docker/login-action@c94ce9fb468520275223c153574b00df6fe4bcc9 # v3`, `docker/setup-buildx-action@8d2750c68a42422c14e847fe6c8ac0403b4cbd6f # v3`, and `docker/build-push-action@10e90e3645eae34f1e60eeb005ba3a3d33f178e8 # v6` to the SHA registry in `caching.md`.

---

### ISSUE-30 [GAP] [LOW] — `headless-ci-testing.md` §Containerised CI section lacks spec for ccache key behavior inside containers

**Severity:** LOW

**Description:**
`headless-ci-testing.md` lines 14–29 describe the Phase 11b containerized CI transition but do not mention how ccache functions inside the container. `caching.md` documents ccache for native runners but does not address the container case: when the job runs inside a container, `hendrikmuhs/ccache-action` uses the GitHub Actions cache API which works via the `ACTIONS_CACHE_URL` environment variable injected into the container by the runner. This works correctly but is undocumented in the spec.

**Proposed resolution:** Add a note to `caching.md` §compiler output caching confirming that `hendrikmuhs/ccache-action` is compatible with container jobs on GitHub-hosted runners (the action uses the Actions cache service API, not the local filesystem, so it functions identically inside and outside containers).

---

## Summary Table

| Issue | Category | Severity | Spec File(s) | Workflow File(s) |
|-------|----------|----------|--------------|-----------------|
| ISSUE-1 | INCONSISTENCY | CRITICAL | `caching.md`, `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-2 | INCONSISTENCY | CRITICAL | `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-3 | INCONSISTENCY | CRITICAL | `coverage.md`, `CLAUDE.md` | `_coverage-linux.yml` |
| ISSUE-4 | GAP | CRITICAL | `github-actions-workflow.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-5 | PROBLEM | CRITICAL | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-6 | INCONSISTENCY | HIGH | `dependency-management.md` | `_build-windows.yml` |
| ISSUE-7 | INCONSISTENCY | HIGH | `caching.md`, `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-8 | INCONSISTENCY | HIGH | `dependency-management.md`, `headless-ci-testing.md` | `_build-linux.yml` |
| ISSUE-9 | GAP | HIGH | `github-actions-workflow.md`, `branch-protection.md` | `ci.yml` |
| ISSUE-10 | PROBLEM | HIGH | `caching.md` | `_package-windows.yml` |
| ISSUE-11 | GAP | HIGH | `dependency-management.md` | `_package-linux-deb.yml` |
| ISSUE-12 | INCONSISTENCY | HIGH | `github-actions-workflow.md` | `_markdown-lint.yml` |
| ISSUE-13 | INCONSISTENCY | HIGH | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-14 | PROBLEM | HIGH | `github-actions-workflow.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-15 | DUPLICATE | MEDIUM | `github-actions-workflow.md` | `_supply-chain-lint.yml`, `docker-ci-image.yml` |
| ISSUE-16 | GAP | MEDIUM | `branch-protection.md` | `ci.yml` |
| ISSUE-17 | GAP | MEDIUM | `caching.md` | `ci.yml` |
| ISSUE-18 | GAP | MEDIUM | `github-actions-workflow.md` | `docker-ci-image.yml` |
| ISSUE-19 | INCONSISTENCY | MEDIUM | `github-actions-workflow.md` | `_coverage-linux.yml` |
| ISSUE-20 | MISSING | MEDIUM | `dependency-management.md` | — |
| ISSUE-21 | GAP | MEDIUM | `github-actions-workflow.md` | `_package-windows.yml` |
| ISSUE-22 | INCONSISTENCY | MEDIUM | `caching.md` | `ci.yml` |
| ISSUE-23 | PROBLEM | MEDIUM | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-24 | GAP | MEDIUM | `branch-protection.md` | `ci.yml` |
| ISSUE-25 | DUPLICATE | LOW | `dependency-management.md`, `CLAUDE.md` | — |
| ISSUE-26 | MISSING | LOW | `github-actions-workflow.md` | `docker-ci-image.yml` |
| ISSUE-27 | GAP | LOW | `caching.md`, `github-actions-workflow.md` | `_package-linux-deb.yml`, `_package-windows.yml` |
| ISSUE-28 | PROBLEM | LOW | `caching.md` | `_build-windows.yml`, `_package-windows.yml` |
| ISSUE-29 | MISSING | LOW | `caching.md` | `_build-windows.yml`, `_package-windows.yml`, `docker-ci-image.yml` |
| ISSUE-30 | GAP | LOW | `headless-ci-testing.md`, `caching.md` | `_build-linux.yml`, `_coverage-linux.yml` |

**Counts:** 5 CRITICAL, 9 HIGH, 10 MEDIUM, 6 LOW = **30 total issues**
