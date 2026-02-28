# Dependency Management

- **vcpkg** is the mandatory dependency manager on all platforms
- `vcpkg.json` manifest in repo root — `builtin-baseline` **must** match the `vcpkgGitCommitId` in the CI workflow:

```json
{
  "name": "aitown",
  "version": "0.1.0",
  "builtin-baseline": "<full-sha1-matching-vcpkgGitCommitId>",
  "dependencies": [
    "irrlicht",
    "openal-soft",
    "libvorbis",
    "fmt",
    "glew",
    "gtest",
    "rapidcheck"
  ]
}
```

**`fmt` is a required explicit dependency**: the `openal-soft` vcpkg portfile applies a `devendor-fmt.diff` patch that replaces OpenAL Soft's bundled copy of `{fmt}` with the external vcpkg `fmt` package. This means `libopenal.a` contains object files that reference `fmt::v12::report_error` and other `fmt` symbols. Any CMake target that links (directly or transitively) against `OpenAL::OpenAL` must also link `fmt::fmt`, or the linker will fail with `undefined reference to fmt::v12::report_error`. Adding `fmt` to `vcpkg.json` ensures the vcpkg `fmt` package is installed; `fmt::fmt` must then be linked PRIVATE to `aitown_audio` in `CMakeLists.txt`.

- CMake configured with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
- CI uses `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1 # v11.5` with a pinned vcpkg commit hash stored as `env.VCPKG_COMMIT_ID`. **`VCPKG_COMMIT_ID` must be declared at the workflow level** (in the top-level `env:` block of the CI YAML file, not at the job or step level) so it is available to all jobs and steps. Declaring it at the job level would make it unavailable to the baseline validation step if that step runs in a different job. Example declaration at workflow level:

  ```yaml
  env:
    VCPKG_COMMIT_ID: "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"  # placeholder — replace with real vcpkg commit SHA matching vcpkg.json builtin-baseline
  ``` **Implementation note**: The `lukka/run-vcpkg` action is pinned to SHA `5e0cab206a5ea620130caf672fce3e4a6b5666a1` (v11.5). This was verified via `gh release view v11.5 --repo lukka/run-vcpkg --json tagName,targetCommitish`. The SHA `5e0cab206a5ea620130caf672fce3e4a6b5a793` used in earlier drafts was only 39 characters and therefore invalid — a truncated SHA is NOT a valid supply-chain trust anchor. The action SHA pin must be updated alongside `VCPKG_COMMIT_ID` intentionally — they are both supply-chain trust anchors for the vcpkg install step.
- **Baseline enforcement**: A CI step must validate that `vcpkg.json`'s `builtin-baseline` matches `env.VCPKG_COMMIT_ID` before the vcpkg install step runs:

  ```yaml
  - name: Validate vcpkg baseline consistency
    shell: bash  # Required for Bash syntax on Windows runners (available via Git Bash)
    run: |
      MANIFEST_BASELINE=$(jq -r '."builtin-baseline"' vcpkg.json)
      if [[ "$MANIFEST_BASELINE" != "${{ env.VCPKG_COMMIT_ID }}" ]]; then
        echo "ERROR: vcpkg.json builtin-baseline does not match VCPKG_COMMIT_ID"
        exit 1
      fi
      # Note: jq is pre-installed on ubuntu-latest and windows-latest GitHub Actions runners
      # as of mid-2024 runner images. If jq availability is uncertain, add an install step.
  ```

## Linux System Package Requirements

The following apt-get packages must be installed on BOTH `build-linux` AND `coverage-linux` before CMake configuration runs. These packages provide the OpenGL development headers and virtual display support required by Irrlicht, GLEW, and xvfb-based OpenGL testing.

**Required packages (both `build-linux` and `coverage-linux`)**:

- `xvfb` — X Virtual Frame Buffer; required to run `requires-opengl` tests on headless CI runners via `xvfb-run`
- `libgl1-mesa-dev` — Mesa OpenGL development headers and stub libraries; required for CMake to find OpenGL during configuration and for linking against the Mesa software renderer
- `mesa-utils` — Mesa GL utilities (`glxinfo`, `glxgears`); used to verify the xvfb display is operational in diagnostics
- `libglew-dev` — GLEW development headers; required by the sRGB raw GL upload path (`glCompressedTexImage2D` and related calls); **also installed via vcpkg** (`glew` port), but the system package is needed for CMake's `find_package(GLEW)` fallback and for headers available during configuration before vcpkg runs

**Additional required package (`coverage-linux` ONLY)**:

- `lcov` — required by the `coverage-linux` job for the `lcov --capture`, `lcov --remove`, `lcov --list`, `lcov --summary`, and `genhtml` commands that generate and filter the coverage report. This package is NOT required by `build-linux` — that job uses `-DENABLE_COVERAGE=OFF` and never invokes lcov.

**THIS LIST MUST BE KEPT IN SYNC BETWEEN `build-linux` AND `coverage-linux` — they are fully independent jobs and each must install all required packages before CMake configuration. `coverage-linux` installs the base set PLUS `lcov`.**

Install step for `build-linux` (place before the CMake configure step):

```yaml
- name: Install system dependencies
  run: sudo apt-get update && sudo apt-get install -y xvfb libgl1-mesa-dev mesa-utils libglew-dev
```

Install step for `coverage-linux` (place before the CMake configure step — includes `lcov`):

```yaml
- name: Install system dependencies
  run: sudo apt-get update && sudo apt-get install -y xvfb libgl1-mesa-dev mesa-utils libglew-dev lcov
```

**Why both jobs need the base list**: `build-linux` and `coverage-linux` run on independent `ubuntu-latest` runner instances. There is no shared pre-install state between them. A package installed in one job's runner has no effect on the other. Omitting any package from either job causes a CMake configuration failure or a runtime failure during the `xvfb-run` test step in that job specifically.

**Why `lcov` is `coverage-linux`-only**: `build-linux` configures with `-DENABLE_COVERAGE=OFF` and never produces `.gcda` instrumentation data. Installing `lcov` in `build-linux` would be dead weight with no functional effect. `coverage-linux` configures with `-DENABLE_COVERAGE=ON`, runs all three ctest categories, and then invokes `lcov --capture` to collect `.gcda` output — without `lcov` installed, the capture step exits with "command not found" and the entire coverage job fails.

## Baseline Staleness Risk

**Old vcpkg baselines break Windows CI via MSYS2 mirror 404s.** Confirmed failure mode (encountered in Phase 0): the `zlib` portfile at baseline `f7423ee` called `vcpkg_fixup_pkgconfig`, which attempted to download `msys2-runtime-3.5.3-3` from MSYS2 mirrors to obtain `pkgconf`. All six MSYS2 mirrors returned HTTP 404 — the package had been superseded and removed. The build failed with `error: Failed to download file with error: 1`.

**Rule**: If Windows CI fails with vcpkg download errors referencing MSYS2 packages, the baseline is too old. Update `builtin-baseline` in `vcpkg.json` AND `VCPKG_COMMIT_ID` in `ci.yml` to the current vcpkg HEAD (or a recent commit). Always verify that the `irrlicht` port still exists at the new baseline before committing:

```bash
curl -s https://api.github.com/repos/microsoft/vcpkg/contents/ports/irrlicht?ref=<NEW_SHA>
```

A 200 response confirms the port exists. A 404 means the port was removed — try a slightly older commit.

### CMake Dependencies

```cmake
find_package(OpenAL REQUIRED)
# fmt is required because openal-soft's devendor-fmt vcpkg patch causes libopenal.a to reference
# fmt symbols. Any target linking OpenAL::OpenAL must also link fmt::fmt to satisfy those symbols.
find_package(fmt CONFIG REQUIRED)
find_package(Vorbis REQUIRED)
# Irrlicht: resolved via cmake/FindIrrlicht.cmake (find-module, not config-mode — see note below).
# SPIKE RESOLVED: target name is bare Irrlicht (no namespace).
find_package(Irrlicht REQUIRED)
# GLEW: required for the sRGB raw GL upload path (glCompressedTexImage2D and related calls).
find_package(GLEW REQUIRED)
# Phase 0: route OpenAL, fmt, and Vorbis to aitown_audio stub library (NOT to 'aitown' — that
# executable does not exist at Phase 0). The 'aitown' executable is the final game binary added
# in a later phase.
target_link_libraries(aitown_audio PRIVATE OpenAL::OpenAL fmt::fmt Vorbis::vorbisfile)
# Irrlicht and GLEW link to aitown_render (PRIVATE — headers must NOT propagate to test targets).
# GLEW::GLEW must appear before Irrlicht — required for GLEW symbol deduplication per irrlicht-device-lifecycle.md mitigation-2.
target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht)
```

**GLEW — Windows triplet note**: On Windows with the `x64-windows` triplet active, vcpkg resolves `glew` to `glew:x64-windows` automatically — no platform-specific triplet override in `vcpkg.json` is needed. Before committing a baseline update that adds GLEW, verify the `glew` port exists at the pinned baseline using `gh api /repos/microsoft/vcpkg/contents/ports/glew`. A 200 response confirms the port is present at that baseline; a 404 means the port was removed or renamed — try an adjacent vcpkg commit. This verification is mandatory: a missing port at the pinned baseline causes a hard build failure on all platforms.

**PHASE 1 EXIT CRITERION — GLEW port verification (BLOCKING)**: Before merging the Phase 1 **six-item atomicity commit**, the developer MUST verify that the `glew` vcpkg port exists at the exact baseline pinned in `vcpkg.json`. This verification must occur BEFORE the Phase 1 atomicity commit merges — it must not be deferred to Phase 2. Run the following command and confirm a 200 HTTP status is returned:

```bash
gh api /repos/microsoft/vcpkg/contents/ports/glew?ref=$(jq -r '."builtin-baseline"' vcpkg.json)
```

A 404 response means the `glew` port does not exist at the pinned baseline — the Phase 1 atomicity commit MUST NOT be merged until a vcpkg baseline is found where both `irrlicht` and `glew` ports are present. This is a hard blocking exit criterion. Do not rely on a local `vcpkg install` succeeding as a proxy — local installs may use a different baseline or a cached binary. The GitHub API check against the exact pinned SHA is the authoritative verification method.

**Important implementation notes**:

- **(a) Developer-side manual gate only**: This is a **developer-side manual verification step**, NOT a CI pipeline step. There is no automated CI job that runs the `gh api` port-check command — the developer must run it manually from a terminal before pushing the Phase 1 atomicity commit. Windows CI and Linux CI do not execute this `gh api` check.
- **(b) Bash shell required**: The `$(jq -r ...)` substitution syntax is **bash-specific**. This command MUST be run in a `bash` shell. On Windows, use Git Bash or WSL — do NOT run in PowerShell or cmd.exe, where `$(...)` is not supported and the command will fail silently or produce incorrect output.
- **(c) `gh` CLI authentication required**: The `gh` CLI must be authenticated before running (`gh auth status` must show an active account). Unauthenticated GitHub API calls are subject to a 60 requests/hour rate limit and may return HTTP 403 instead of the expected 200 or 404, producing a false pass.
- **(d) Two complementary gates — not alternatives**: The developer-side `gh api` port-check (notes a–c above) and the CI-side `Test-Path` hard-fail in item 6 of the atomicity commit (see below) are **complementary gates that must both be present**. They are not alternatives to each other. The `gh api` check runs before the commit is pushed, confirming that the `glew` vcpkg port exists at the pinned baseline in the GitHub-hosted vcpkg registry. The CI-side `Test-Path` check runs during `build-windows` execution, confirming that vcpkg actually installed the GLEW library artifact into the expected directory on the Windows runner. A passing `gh api` check does not substitute for the CI `Test-Path` check (the port could exist but fail to install due to a toolchain mismatch), and a passing CI `Test-Path` check does not substitute for the developer-side `gh api` check (a developer who skips the pre-push verification may push a broken baseline that causes every CI run to fail before the `Test-Path` step is even reached). Both gates must be implemented as part of the Phase 1 atomicity commit.

**IMPORTANT**: `target_link_libraries(aitown ...)` in the original snippet was incorrect — the `aitown` executable target does not exist at Phase 0. An implementer following the original snippet verbatim would get a CMake configure error: "Cannot specify link libraries for target aitown which is not built by this project."

**Irrlicht find_package — find-module (RESOLVED)**: The irrlicht vcpkg port at the pinned baseline does not ship a config-mode package (`irrlichtConfig.cmake` is absent). `find_package(Irrlicht REQUIRED)` therefore falls back to CMake's module mode. A minimal `cmake/FindIrrlicht.cmake` module is committed to the repo; it locates the Irrlicht headers and library from the vcpkg install tree and creates an `IMPORTED` target named `Irrlicht` (bare, no namespace). The CMake module path is set via `list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")` before the `find_package` call. **Target name is bare `Irrlicht`** — verified via adrido/irrlicht-vcpkg CMakeLists.txt: `install(TARGETS Irrlicht EXPORT Irrlicht ...)` with no `NAMESPACE` argument. Use `target_link_libraries(aitown_render PRIVATE Irrlicht)`.

On Windows: `soft_oal.dll` **and** `default.mhr` (HRTF data) copied to output via post-build commands. `libvorbisfile` is a static library in vcpkg's default triplet — no DLL copy is needed for it.

**OpenAL DLL naming**: vcpkg installs OpenAL Soft on Windows as `OpenAL32.dll` (not `soft_oal.dll`). The CMake post-build rule copies `OpenAL32.dll` from the vcpkg bin directory and places it in the output directory renamed as `soft_oal.dll`. The CI verification step checks for `build/soft_oal.dll`, which is correctly produced by this rename-copy. `$<TARGET_FILE:OpenAL::OpenAL>` resolves to `OpenAL32.lib` (the import library) on Windows — do not use it as the DLL copy source; use the explicit vcpkg bin path `${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll` instead.

### Linux default.mhr Packaging

On Linux, `default.mhr` is installed by vcpkg alongside the OpenAL Soft library. After `cmake --build build` completes in the `build-linux` job, a CI verification step must confirm that `default.mhr` is accessible at the vcpkg-installed OpenAL Soft HRTF search path. Without this file, `AudioSystem` HRTF initialization fails at runtime even though the binary compiles and links successfully.

Add the following step immediately after `cmake --build build` in the `build-linux` job:

**PHASING**: At Phase 0, `default.mhr` is not yet copied to the binary output directory — the CMake post-build copy rule is a Phase 7 deliverable. At Phase 0, this step must be a **warning-only no-op** (see `implementation/phase-0.md` for the exact placeholder text: `step may be a documented no-op at Phase 0 with a TODO comment referencing Phase 7 where the full HRTF check is wired`). Implement the warning-only form at Phase 0 (e.g., log a message and exit 0 regardless). The hard-fail form below is the **Phase 7+ version** — replace the placeholder with this hard-fail step at Phase 7 delivery. This is the same pattern as the Windows DLL verification step (see the phasing note at the DLL verification block above).

```yaml
- name: Verify default.mhr is present (Linux)
  shell: bash
  run: |
    if ! find "${{ github.workspace }}" -name "default.mhr" | grep -q .; then
      echo "ERROR: default.mhr not found — HRTF initialization will fail at runtime"
      exit 1
    fi
```

A CMake install rule may also be needed to copy `default.mhr` from the vcpkg install tree into the binary directory on Linux builds, so that runtime lookups by the OpenAL Soft HRTF loader succeed relative to the executable path. This rule is parallel to the Windows post-build copy command already specified. The CI verification step catches the absence of this rule before a broken binary is promoted to integration tests.

### Testing Dependency Management

`googletest` and `rapidcheck` are managed via vcpkg (same as all other C++ dependencies):

- `vcpkg.json` lists `"gtest"` and `"rapidcheck"` as dependencies.
- `CMakeLists.txt` uses `find_package(GTest CONFIG REQUIRED)` and `find_package(rapidcheck CONFIG REQUIRED)`.
- CMake targets: `GTest::gtest_main`, `GTest::gmock` (namespaced — standard googletest export); `rapidcheck`, `rapidcheck_gtest` (bare names — rapidcheck upstream uses no NAMESPACE in install(EXPORT)).
- `include(GoogleTest)` is retained — it is a CMake built-in module providing `gtest_discover_tests()`, independent of how googletest is obtained.

**Why vcpkg (not FetchContent)**: Using vcpkg for all C++ dependencies — including test dependencies — eliminates git clone overhead at CMake configure time (FetchContent clones are sequential and add 30–60 s per CI job on cold cache). vcpkg binary caching extracts pre-built packages in seconds. All dependency versions are governed by the `builtin-baseline`, keeping the supply chain in one place.

**rapidcheck_gtest target**: The vcpkg `rapidcheck` port installs `rapidcheck_gtest` as an INTERFACE library (headers only, no compiled code). It provides the `rapidcheck/gtest.h` integration header. The target does not declare a dependency on GTest — test targets that use `rapidcheck_gtest` must also link `GTest::gtest_main` or `GTest::gmock` explicitly (already the case in all CMakeLists.txt `target_link_libraries` calls).

**DLL verification step required before artifact upload**: the Windows CI job must verify that `soft_oal.dll` and `default.mhr` are present in the output directory before uploading artifacts — a missing DLL produces a binary that crashes on launch and would waste the 30-day artifact retention window. **PHASING**: The `soft_oal.dll` and `default.mhr` post-build CMake copy commands are not delivered until Phase 7. At Phase 0, the DLL verification step MUST be a **warning-only placeholder that always exits 0** (see `implementation/phase-0.md` for the exact YAML). This hard-fail form below is the **Phase 7+ version** — it must replace the Phase 0 placeholder at Phase 7 delivery. Committing the hard-fail form at Phase 0 breaks the Windows CI job because the files do not yet exist. Add the Phase 7+ hard-fail step:

```yaml
- name: Verify required DLLs are present
  shell: pwsh
  run: |
    # Use explicit if-block syntax — "Test-Path ... || exit 1" is PowerShell 7+ only;
    # GitHub Actions Windows runners default to PowerShell 5.1 where || is not supported.
    # Ninja single-config: DLLs are at build/ (not build/Release/).
    if (-not (Test-Path "build/soft_oal.dll")) {
      Write-Error "soft_oal.dll not found"
      exit 1
    }
    if (-not (Test-Path "build/default.mhr")) {
      Write-Error "default.mhr not found"
      exit 1
    }
```

**Note**: Both `soft_oal.dll` AND `default.mhr` must be verified. The Phase 0 placeholder (see `implementation/phase-0.md`) is a warning-only check for both files.

### Windows GLEW vcpkg Verification

Two sequential CI steps verify the GLEW vcpkg installation on Windows before the build step runs: a **header check** (step "Verify glew vcpkg port") and a **library check** (step "Verify GLEW vcpkg install"). Both use the same dual-path pattern — manifest-mode path first, classic-mode fallback — because vcpkg installs differ by invocation context:

- **Manifest mode** (default when `vcpkg.json` is present): packages install into `build/vcpkg_installed/<triplet>/`
- **Classic mode** (global vcpkg install, no `vcpkg.json` in scope): packages install into `$VCPKG_ROOT/installed/<triplet>/`

Checking only one path causes false negatives in the other mode. Both steps are mandatory and must use PS 5.1-compatible `if (-not (...)) { exit 1 }` syntax — the `||` short-circuit for process exit codes is PowerShell 7+ only.

#### Step A — Header check ("Verify glew vcpkg port")

Runs **before** the CMake configure step. Confirms the GLEW development headers are present so CMake's `find_package(GLEW REQUIRED)` can locate them. If headers are absent, the configure step fails with an opaque CMake error rather than a clear diagnostic.

```yaml
- name: Verify glew vcpkg port
  shell: pwsh
  run: |
    # Manifest mode installs to build/vcpkg_installed/; classic mode to $VCPKG_ROOT/installed/.
    $manifestHdr = "build\vcpkg_installed\x64-windows\include\GL\glew.h"
    $classicHdr  = Join-Path $env:VCPKG_ROOT "installed\x64-windows\include\GL\glew.h"
    if (-not (Test-Path $manifestHdr) -and -not (Test-Path $classicHdr)) {
      Write-Error "ERROR: GLEW not installed — GL/glew.h not found in manifest path ($manifestHdr) or classic path ($classicHdr)."
      exit 1
    }
    Write-Host "GLEW installed artifact verified."
```

#### Step B — Library check ("Verify GLEW vcpkg install")

Runs after the vcpkg install step and **before** the CMake configure step. Confirms the compiled `glew.lib` artifact is present so the linker can find it. A header-only check would not catch a scenario where headers were installed but the compiled library was not.

```yaml
- name: Verify GLEW vcpkg install (Windows)
  shell: pwsh
  run: |
    # Use explicit if-block syntax — "Test-Path ... || exit 1" is PowerShell 7+ only;
    # GitHub Actions Windows runners default to PowerShell 5.1 where || is not supported.
    if (-not (Test-Path "build/vcpkg_installed/x64-windows/lib/glew.lib")) {
      if (-not (Test-Path "$env:VCPKG_ROOT\installed\x64-windows\lib\glew.lib")) {
        Write-Error "GLEW not found in either manifest-mode or classic-mode install"; exit 1
      }
    }
```

**PowerShell 5.1 compatibility**: GitHub Actions Windows runners ship PowerShell 5.1 as the default shell. Use the explicit `if (-not (...)) { ... }` form as shown in both steps above.

### Irrlicht DLL on Windows (Phase 1+)

**RESOLVED — triplet: `x64-windows` (default dynamic).** No `VCPKG_DEFAULT_TRIPLET` override is set in CI; vcpkg defaults to `x64-windows` on Windows runners. This produces `Irrlicht.dll` as a dynamic library that must be copied to the output directory alongside `soft_oal.dll` when `aitown_render` is first linked against Irrlicht in Phase 1.

**Phase 1 action required**: Add a CMake post-build copy rule for `Irrlicht.dll` (parallel to the `soft_oal.dll` copy rule added in Phase 7) and add `Irrlicht.dll` to the Windows DLL verification step in CI alongside `soft_oal.dll`.

### GLEW32.dll on Windows (Phase 1+)

GLEW is linked to `aitown_render` via `GLEW::GLEW`. On Windows with the `x64-windows` triplet, vcpkg installs GLEW as a dynamic library, producing `glew32.dll` in the vcpkg bin tree. This DLL must be copied to the build output directory so that the `aitown` executable can locate it at runtime. The CI workflow already contains a hard-fail check for `build/GLEW32.dll` (Ninja single-config output path) — if no CMake copy rule is present, the Windows CI job fails immediately when Phase 1 GLEW linkage is merged.

Add the following post-build copy command to `CMakeLists.txt`, alongside the `Irrlicht.dll` copy rule:

```cmake
# Windows-only: copy GLEW32.dll to build output alongside Irrlicht.dll
if(WIN32)
  add_custom_command(TARGET aitown_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE_DIR:GLEW::GLEW>/../bin/glew32.dll
      $<TARGET_FILE_DIR:aitown_render>
    COMMENT "Copying GLEW32.dll to output directory"
  )
endif()
```

**Phase 1 atomicity requirement**: The GLEW32.dll post-build copy command is a mandatory part of the Phase 1 atomicity commit. All **six** of the following changes must land in a single atomic commit — merging any subset breaks Windows CI:

1. Add `glew` to `vcpkg.json` dependencies
2. Add `find_package(GLEW REQUIRED)` to `CMakeLists.txt`
3. Add `target_link_libraries(aitown_render PRIVATE ... GLEW::GLEW)` to `CMakeLists.txt`
4. Add the `Irrlicht.dll` CMake post-build copy command to `CMakeLists.txt`
5. Add the `GLEW32.dll` CMake post-build copy command to `CMakeLists.txt` (this section)
6. `build-windows` CI steps: add BOTH a "Verify glew vcpkg port" step (dual-path `GL/glew.h` header check — manifest path `build\vcpkg_installed\x64-windows\include\GL\glew.h` with fallback to `$VCPKG_ROOT\installed\x64-windows\include\GL\glew.h`) AND a "Verify GLEW vcpkg install" step (dual-path `glew.lib` library check — manifest path `build/vcpkg_installed/x64-windows/lib/glew.lib` with fallback to `$VCPKG_ROOT\installed\x64-windows\lib\glew.lib`). Both use PS 5.1-compatible `if (-not (Test-Path ...) -and -not (Test-Path ...)) { exit 1 }` syntax. The header check must run before the library check. See `### Windows GLEW vcpkg Verification` above for the canonical YAML for both steps. Owner: `cicd-dev-github`.

Omitting item 5 causes the existing `build/GLEW32.dll` hard-fail CI check to trigger on every Windows build from Phase 1 onward. Omitting item 6 leaves the Windows GLEW vcpkg installation unverified — a missing or misconfigured GLEW port would silently produce a broken binary rather than failing the CI job immediately.

### DLL Verification CI YAML Co-Landing Requirement

**CMakeLists.txt DLL copy rules and the CI YAML DLL verification step MUST land in the same commit.** This is the same atomicity principle as the **six-item GLEW commit** above, applied to the relationship between CMake and CI YAML.

Specifically:

- The CMakeLists.txt `add_custom_command` post-build copy rules for `Irrlicht.dll` and `GLEW32.dll` produce the DLL files in the output directory.
- The CI YAML hard-fail verification step (`if (-not (Test-Path "build/Irrlicht.dll")) { exit 1 }` and the equivalent for `GLEW32.dll`) checks that those files exist before artifact upload (Ninja single-config: DLLs land in `build/`, not `build/Release/`).

A partial commit breaks CI in one of two ways:

1. **CMake rules without CI YAML**: DLLs are copied correctly but no verification step exists — a regression that removes the copy rule goes undetected until a user downloads the artifact and encounters a launch crash.
2. **CI YAML without CMake rules**: The verification step hard-fails immediately on every Windows build because the DLLs are never copied — the entire Windows CI job is broken for every PR until the CMake rules are added.

Both partial states leave the repository in a broken or unverified condition. The co-landing requirement ensures CI is green immediately after the commit merges and remains green on every subsequent build.

**This applies to ALL DLL copy rule and CI verification step pairs**, not only the Phase 1 GLEW/Irrlicht pair. Any future DLL that is added via a CMake post-build copy command (e.g., `soft_oal.dll` in Phase 7) must have its corresponding CI verification step added in the same commit as the CMake copy rule. The Phase 7 `soft_oal.dll` / `default.mhr` copy rules and the hard-fail CI step replacing the Phase 0 placeholder must co-land atomically.

## Phase 7 CI Deliverables Sign-Off

### Hard-Fail DLL and HRTF Data Verification (Windows)

Phase 7 hardens the `build-windows` CI job's DLL verification step from the Phase 0 warning-only placeholder to a full hard-fail for both `soft_oal.dll` and `default.mhr`.

**Step name**: `Verify required DLLs and HRTF data (all hard-fail)` (`.github/workflows/ci.yml`, `build-windows` job, Step 18)

**Evidence — `soft_oal.dll` hard-fail**:

```powershell
if (-not (Test-Path "build/soft_oal.dll")) {
  Write-Error "soft_oal.dll not found"
  exit 1
}
```

**Evidence — `default.mhr` hard-fail**:

```powershell
if (-not (Test-Path "build/default.mhr")) {
  Write-Error "default.mhr not found"
  exit 1
}
```

Both checks use `if (-not (Test-Path ...)) { exit 1 }` syntax — the `||` short-circuit operator is PowerShell 7+ only; GitHub Actions Windows runners use PowerShell 5.1.

### Music Stem JSON Sidecar Enforcement (Linux)

Phase 7 adds a temporary sidecar enforcement step in the `build-linux` CI job.

**Step name**: `Verify music stem JSON sidecars` (`.github/workflows/ci.yml`, `build-linux` job, placed immediately after the `Build` step and before the first `ctest` invocation)

The step scans `assets/audio/music_*.ogg` and exits non-zero if any `.ogg` file lacks a companion `.json` sidecar. When no `music_*.ogg` files exist (glob expands to literal string), the loop body does not execute and the step exits 0 — correct behavior for a codebase with no music stems yet.

**This step applies to BOTH `build-linux` AND `coverage-linux`** — both jobs build and test the same codebase and can expose the sidecar absence at different stages. The `coverage-linux` job must include an identical music stem sidecar enforcement step placed immediately after its `Build` step and before the first `ctest` invocation, using the same YAML as `build-linux`.

**This step is temporary**: it must be removed when Phase 9 delivers Check #14 in `tools/validate_assets.py`, which provides permanent sidecar enforcement via the `validate-assets` CI job.
This removal is a Phase 9 CI deliverable — see `implementation/phase-9.md` for the corresponding task.

### `default.mhr` Verification (Linux)

The `build-linux` job already contains a hard-fail verification step added in Phase 4:

**Step name**: `Verify default.mhr HRTF data` (`.github/workflows/ci.yml`, `build-linux` job, Step 13)

This step uses `find "${{ github.workspace }}" -name "default.mhr"` to locate the HRTF data file anywhere in the workspace (including the vcpkg install tree) and exits non-zero if not found. No change required in Phase 7 — the Phase 4 hard-fail form is already in place.

### Risk Mitigation

**Missing HRTF data risk**: `default.mhr` is the OpenAL Soft HRTF dataset used by `AudioSystem` for 3D spatial audio initialization (`ALC_HRTF_SOFT=ALC_TRUE`). If the file is absent at runtime, OpenAL Soft silently falls back to non-HRTF panning — all spatial audio cues (distance attenuation, directional cues, occlusion) degrade or disappear without any error message to the player. The hard-fail CI step ensures that a binary missing `default.mhr` is caught at build time rather than discovered by a player experiencing degraded audio. This prevents shipping a binary with silent spatial audio fallback.

**Missing OpenAL runtime risk**: `soft_oal.dll` is the OpenAL Soft runtime DLL required on Windows. Without it, the `aitown.exe` binary fails to start entirely (DLL load failure). The hard-fail CI step catches this class of packaging error before any artifact is uploaded and retained for 30 days.

**CMake copy rule (corrected in Phase 7 fix commit)**: vcpkg installs OpenAL Soft as `OpenAL32.dll`, not `soft_oal.dll`. The post-build copy command must use `OpenAL32.dll` as the source and rename it to `soft_oal.dll` at the destination. Attaching the copy to `aitown_audio` (a static library) is wrong — static library output dirs differ from the executable output dir. Attach only to the `aitown` executable target:

```cmake
add_custom_command(TARGET aitown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll"
        "$<TARGET_FILE_DIR:aitown>/soft_oal.dll"
    COMMENT "Copying OpenAL32.dll (OpenAL Soft runtime) to aitown output as soft_oal.dll"
)
```

**Music stem sidecar risk**: `AudioSystem` throws `std::runtime_error` at music stem load time when a sidecar `.json` file is absent. The temporary CI enforcement step prevents a committed `music_*.ogg` from reaching CI without its required sidecar, catching the error before the audio thread is ever launched.
