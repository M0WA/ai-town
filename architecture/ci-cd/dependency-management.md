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
    "nlohmann-json",
    "gtest",
    "rapidcheck"
  ]
}
```

**`fmt` is a required explicit dependency**: the `openal-soft` vcpkg portfile applies a `devendor-fmt.diff` patch that replaces OpenAL Soft's bundled copy of `{fmt}` with the external vcpkg `fmt` package. This means `libopenal.a` contains object files that reference `fmt::v12::report_error` and other `fmt` symbols. Any CMake target that links (directly or transitively) against `OpenAL::OpenAL` must also link `fmt::fmt`, or the linker will fail with `undefined reference to fmt::v12::report_error`. Adding `fmt` to `vcpkg.json` ensures the vcpkg `fmt` package is installed; `fmt::fmt` must then be linked PRIVATE to `aitown_audio` in `CMakeLists.txt`.

**`nlohmann-json` provides JSON serialization/deserialization**: header-only, MIT licence, used by `CitySimulation.cpp` for save-file serialization (`serializeToJson`) and deserialization (`deserializeFromJson`). The vcpkg port is `nlohmann-json`; CMake target is `nlohmann_json::nlohmann_json` (config-mode). Link it to the target that compiles `CitySimulation.cpp` (i.e., `aitown_sim`). Being header-only, it requires no DLL copy rules on Windows and no additional system packages on Linux.

- CMake configured with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
- CI uses `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1 # v11.5` with a pinned vcpkg commit hash stored as `env.VCPKG_COMMIT_ID`. `lukka/run-vcpkg` is used only in the Windows job. Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and read from `/opt/vcpkg_installed` which is baked into the CI image at image-build time. **`VCPKG_COMMIT_ID` must be declared at the workflow level** (in the top-level `env:` block of the CI YAML file, not at the job or step level) so it is available to all jobs and steps. Declaring it at the job level would make it unavailable to the baseline validation step if that step runs in a different job. Example declaration at workflow level:

  ```yaml
  env:
    VCPKG_COMMIT_ID: "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"  # placeholder — replace with real vcpkg commit SHA matching vcpkg.json builtin-baseline
  ```

  **Implementation note**: The `lukka/run-vcpkg` action is pinned to SHA `5e0cab206a5ea620130caf672fce3e4a6b5666a1` (v11.5). This was verified via `gh release view v11.5 --repo lukka/run-vcpkg --json tagName,targetCommitish`. The SHA `5e0cab206a5ea620130caf672fce3e4a6b5a793` used in earlier drafts was only 39 characters and therefore invalid — a truncated SHA is NOT a valid supply-chain trust anchor. The action SHA pin must be updated alongside `VCPKG_COMMIT_ID` intentionally — they are both supply-chain trust anchors for the vcpkg install step.
- **Baseline enforcement**: A CI step must validate that `vcpkg.json`'s `builtin-baseline` matches `env.VCPKG_COMMIT_ID` before the vcpkg install step runs:

  ```yaml
  - name: Validate vcpkg baseline consistency
    shell: bash
    env:
      EXPECTED_BASELINE: ${{ inputs.vcpkg_commit_id }}
    run: |
      MANIFEST_BASELINE=$(jq -r '."builtin-baseline"' vcpkg.json)
      if [[ "$MANIFEST_BASELINE" != "$EXPECTED_BASELINE" ]]; then
        echo "ERROR: vcpkg.json builtin-baseline does not match vcpkg_commit_id input"
        exit 1
      fi
  ```

  **Location**: This step resides in `_validate-assets.yml` (not in the build jobs). It receives the `vcpkg_commit_id` value via the `inputs.` context (populated by the `prepare` job output and passed through `ci.yml`'s `with:` block) — the `env.VCPKG_COMMIT_ID` form is unavailable in reusable workflow `with:` contexts.

## Linux System Package Requirements

Linux CI jobs (`_build-linux.yml`, `_test-linux.yml`, `_coverage-linux.yml`) run in a pre-baked GHCR container image: `ghcr.io/m0wa/aitown-ci-linux:vcpkg-<short-sha>@sha256:<digest>`. All system packages are installed into this image at image-build time via `docker/ci-linux/Dockerfile`. CI jobs use `container: image:` to pull this image — **no runtime `apt-get install` step is needed or present in any Linux CI job**.

The `docker-ci-image.yml` workflow manages building and pushing the container image (see `## docker-ci-image.yml` section in `github-actions-workflow.md`).

**Required packages (must remain in `docker/ci-linux/Dockerfile`)**:

- `xvfb` — X Virtual Frame Buffer; required to run `requires-opengl` tests on headless CI runners via `xvfb-run`
- `libgl1-mesa-dev` — Mesa OpenGL development headers and stub libraries; required for CMake to find OpenGL during configuration and for linking against the Mesa software renderer
- `mesa-utils` — Mesa GL utilities (`glxinfo`, `glxgears`); used to verify the xvfb display is operational in diagnostics
- `libglew-dev` — GLEW development headers; required by the sRGB raw GL upload path (`glCompressedTexImage2D` and related calls); **also installed via vcpkg** (`glew` port), but the system package is needed for CMake's `find_package(GLEW)` fallback and for headers available during configuration before vcpkg runs
- `libxxf86vm-dev` — required by Irrlicht (`-lXxf86vm`); omitting this causes a linker error during Irrlicht build
- `lcov` — required by the `coverage-linux` job for the `lcov --capture`, `lcov --remove`, `lcov --list`, and `genhtml` commands that generate and filter the coverage report

**This package list is a Dockerfile requirement, not a CI-job-step requirement.** Whenever a new system package is needed by a CI job, it must be added to `docker/ci-linux/Dockerfile` and the image must be rebuilt via `docker-ci-image.yml`. The CI jobs read from the pre-baked image — they do not install packages at runtime.

Additionally, the Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and pass `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed` to CMake. The vcpkg packages (Irrlicht, OpenAL Soft, GLEW, etc.) are also pre-installed in the container image at `/opt/vcpkg_installed`, consistent with the same vcpkg commit pinned in `ci.yml`.

## Baseline Staleness Risk

**Old vcpkg baselines break Windows CI via MSYS2 mirror 404s.** Confirmed failure mode (encountered in Phase 0): the `zlib` portfile at baseline `f7423ee` called `vcpkg_fixup_pkgconfig`, which attempted to download `msys2-runtime-3.5.3-3` from MSYS2 mirrors to obtain `pkgconf`. All six MSYS2 mirrors returned HTTP 404 — the package had been superseded and removed. The build failed with `error: Failed to download file with error: 1`.

**Rule**: If Windows CI fails with vcpkg download errors referencing MSYS2 packages, the baseline is too old. Update `builtin-baseline` in `vcpkg.json` AND `VCPKG_COMMIT_ID` in BOTH `ci.yml` AND `msvc.yml` to the current vcpkg HEAD (or a recent commit). Always verify that the `irrlicht` port still exists at the new baseline before committing:

```bash
curl -s https://api.github.com/repos/microsoft/vcpkg/contents/ports/irrlicht?ref=<NEW_SHA>
```

A 200 response confirms the port exists. A 404 means the port was removed — try a slightly older commit.

**`msvc.yml` baseline atomicity**: `msvc.yml` declares its own independent `VCPKG_COMMIT_ID` env var — it does NOT inherit the value from the `prepare` job or `ci.yml`. A vcpkg baseline bump in `ci.yml` does NOT automatically update `msvc.yml`. Failing to update `msvc.yml` in the same PR as `ci.yml` causes MSVC static analysis to run against a different vcpkg tree than the main CI build, producing inconsistent results. The complete baseline atomicity checklist (from `CLAUDE.md`) is:

1. `vcpkg.json` — `builtin-baseline` updated
2. `ci.yml` — `VCPKG_COMMIT_ID` env var updated to the same commit
3. `msvc.yml` — `VCPKG_COMMIT_ID` env var updated to the same commit
4. `docker/ci-linux/Dockerfile` — `ARG VCPKG_COMMIT` build-arg updated to the same value
5. `.devcontainer/Dockerfile` — `FROM` line updated with new image tag and sha256 digest
6. `ci.yml` AND `.devcontainer/Dockerfile` — image digest pin updated to the `sha256:` output of the `docker-ci-image.yml` push step

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
# nlohmann-json: header-only JSON library used by CitySimulation for save-file serialization.
find_package(nlohmann_json CONFIG REQUIRED)
# Phase 0: route OpenAL, fmt, and Vorbis to aitown_audio stub library (NOT to 'aitown' — that
# executable does not exist at Phase 0). The 'aitown' executable is the final game binary added
# in a later phase.
target_link_libraries(aitown_audio PRIVATE OpenAL::OpenAL fmt::fmt Vorbis::vorbisfile)
# Irrlicht and GLEW link to aitown_render (PRIVATE — headers must NOT propagate to test targets).
# GLEW::GLEW must appear before Irrlicht — required for GLEW symbol deduplication per irrlicht-device-lifecycle.md mitigation-2.
target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht)
# nlohmann_json links to the target compiling CitySimulation.cpp (header-only — no binary artifact).
target_link_libraries(aitown_sim PRIVATE nlohmann_json::nlohmann_json)
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

**OpenAL DLL naming**: vcpkg installs OpenAL Soft on Windows as `OpenAL32.dll` (not `soft_oal.dll`). The CMake post-build rule copies `OpenAL32.dll` from the vcpkg bin directory and places it in the output directory renamed as `soft_oal.dll`. The CI verification step checks for `build/soft_oal.dll`, which is correctly produced by this rename-copy. `$<TARGET_FILE:OpenAL::OpenAL>` resolves to `OpenAL32.lib` (the import library) on Windows — do not use it as the DLL copy source; use the explicit vcpkg bin path `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll` instead.

### Linux default.mhr Packaging

On Linux, `default.mhr` is installed by vcpkg alongside the OpenAL Soft library. After `cmake --build build` completes in the `build-linux` job, a CI verification step must confirm that `default.mhr` is accessible at the vcpkg-installed OpenAL Soft HRTF search path. Without this file, `AudioSystem` HRTF initialization fails at runtime even though the binary compiles and links successfully.

Add the following step immediately after `cmake --build build` in the `build-linux` job:

**PHASING**: At Phase 0, `default.mhr` is not yet copied to the binary output directory — the CMake post-build copy rule is a Phase 7 deliverable. At Phase 0, this step must be a **warning-only no-op** (see `implementation/phase-0.md` for the exact placeholder text: `step may be a documented no-op at Phase 0 with a TODO comment referencing Phase 7 where the full HRTF check is wired`). Implement the warning-only form at Phase 0 (e.g., log a message and exit 0 regardless). The hard-fail form below is the **Phase 7+ version** — replace the placeholder with this hard-fail step at Phase 7 delivery. This is the same pattern as the Windows DLL verification step (see the phasing note at the DLL verification block above).

```yaml
- name: Verify default.mhr HRTF data
  shell: bash
  run: |
    if ! test -f build/default.mhr; then
      echo "ERROR: default.mhr not found — HRTF initialization will fail at runtime"
      exit 1
    fi
```

Relative paths (`build/default.mhr`) are preferred over `${{ github.workspace }}` in container jobs — the GitHub expression resolves to the host path (`/__w/repo/repo`) which may differ from the container working directory, causing a `find` across the entire workspace to silently find nothing even when the file exists at the expected relative path.

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
- name: Verify required DLLs and HRTF data (all hard-fail)
  shell: pwsh
  run: |
    # Use explicit if-block syntax — "Test-Path ... || exit 1" is PowerShell 7+ only;
    # GitHub Actions Windows runners default to PowerShell 5.1 where || is not supported.
    # Ninja single-config: DLLs are at build/ (not build/Release/).
    if (-not (Test-Path "build\Irrlicht.dll")) {
      Write-Error "Irrlicht.dll not found"; exit 1
    }
    if (-not (Test-Path "build\GLEW32.dll")) {
      Write-Error "GLEW32.dll not found"; exit 1
    }
    if (-not (Test-Path "build\soft_oal.dll")) {
      Write-Error "soft_oal.dll not found"; exit 1
    }
    if (-not (Test-Path "build\default.mhr")) {
      Write-Error "default.mhr not found"; exit 1
    }
```

**All four files — `Irrlicht.dll`, `GLEW32.dll`, `soft_oal.dll`, and `default.mhr` — must be verified.** `Irrlicht.dll` and `GLEW32.dll` are copied by post-build rules in Phase 1 CMakeLists.txt; their absence causes all test binaries to fail with a DLL load error. The Phase 0 placeholder (see `implementation/phase-0.md`) is a warning-only check for `soft_oal.dll` and `default.mhr` only; the hard-fail form for all four files is the Phase 7+ version.

### Windows GLEW vcpkg Verification

A single CI step ("Verify GLEW vcpkg artifacts (header and import lib)") verifies the GLEW vcpkg installation on Windows. This step runs **after** the CMake configure step (between Configure and Build) and checks both the GLEW header and the compiled import library in a single PowerShell block.

The step uses a dual-path pattern — manifest-mode path first, classic-mode fallback — because vcpkg installs differ by invocation context:

- **Manifest mode** (default when `vcpkg.json` is present): packages install into `build/vcpkg_installed/<triplet>/`
- **Classic mode** (global vcpkg install, no `vcpkg.json` in scope): packages install into `$VCPKG_ROOT/installed/<triplet>/`

```yaml
- name: Verify GLEW vcpkg artifacts (header and import lib)
  shell: pwsh
  run: |
    # Use explicit if-block syntax — "Test-Path ... || exit 1" is PowerShell 7+ only;
    # GitHub Actions Windows runners default to PowerShell 5.1 where || is not supported.
    $manifestHdr = "build\vcpkg_installed\x64-windows\include\GL\glew.h"
    $classicHdr  = Join-Path $env:VCPKG_ROOT "installed\x64-windows\include\GL\glew.h"
    if (-not (Test-Path $manifestHdr) -and -not (Test-Path $classicHdr)) {
      Write-Error "ERROR: GLEW not installed — GL/glew.h not found in manifest or classic path."
      exit 1
    }
    $manifestLib = "build\vcpkg_installed\x64-windows\lib\glew32.lib"
    $classicLib  = Join-Path $env:VCPKG_ROOT "installed\x64-windows\lib\glew32.lib"
    if (-not (Test-Path $manifestLib) -and -not (Test-Path $classicLib)) {
      Write-Error "ERROR: GLEW not installed — glew32.lib not found in manifest or classic path."
      exit 1
    }
    Write-Host "GLEW vcpkg artifacts verified."
```

**Note**: On Windows, the vcpkg GLEW portfile uses a libname override that installs `glew32.lib` rather than `glew.lib`. Both paths must be checked (manifest-mode and classic-mode) to avoid false negatives.

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
6. `build-windows` CI steps: add BOTH a "Verify glew vcpkg port" step (dual-path `GL/glew.h` header check — manifest path `build\vcpkg_installed\x64-windows\include\GL\glew.h` with fallback to `$VCPKG_ROOT\installed\x64-windows\include\GL\glew.h`) AND a "Verify GLEW vcpkg install" step (dual-path `glew32.lib` library check — manifest path `build/vcpkg_installed/x64-windows/lib/glew32.lib` with fallback to `$VCPKG_ROOT\installed\x64-windows\lib\glew32.lib`). Both use PS 5.1-compatible `if (-not (Test-Path ...) -and -not (Test-Path ...)) { exit 1 }` syntax. The header check must run before the library check. See `### Windows GLEW vcpkg Verification` above for the canonical YAML for both steps. Owner: `cicd-dev-github`.

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

**Step name**: `Verify required DLLs and HRTF data (all hard-fail)` (`.github/workflows/_build-windows.yml`, `build-windows` job)

**Evidence — all four hard-fail checks**:

```powershell
if (-not (Test-Path "build\Irrlicht.dll")) {
  Write-Error "Irrlicht.dll not found"; exit 1
}
if (-not (Test-Path "build\GLEW32.dll")) {
  Write-Error "GLEW32.dll not found"; exit 1
}
if (-not (Test-Path "build\soft_oal.dll")) {
  Write-Error "soft_oal.dll not found"; exit 1
}
if (-not (Test-Path "build\default.mhr")) {
  Write-Error "default.mhr not found"; exit 1
}
```

All checks use `if (-not (Test-Path ...)) { exit 1 }` syntax — the `||` short-circuit operator is PowerShell 7+ only; GitHub Actions Windows runners use PowerShell 5.1. `Irrlicht.dll` and `GLEW32.dll` are Phase 1 deliverables; `soft_oal.dll` and `default.mhr` are Phase 7 deliverables.

### Music Stem JSON Sidecar Enforcement (Linux)

Phase 7 adds a temporary sidecar enforcement step in the `build-linux` CI job.

**Step name**: `Verify music stem JSON sidecars` (`.github/workflows/ci.yml`, `build-linux` job, placed immediately after the `Build` step and before the first `ctest` invocation)

The step scans `assets/audio/music_*.ogg` and exits non-zero if any `.ogg` file lacks a companion `.json` sidecar. When no `music_*.ogg` files exist (glob expands to literal string), the loop body does not execute and the step exits 0 — correct behavior for a codebase with no music stems yet.

**This step applies to BOTH `build-linux` AND `coverage-linux`** — both jobs build and test the same codebase and can expose the sidecar absence at different stages. The `coverage-linux` job must include an identical music stem sidecar enforcement step placed immediately after its `Build` step and before the first `ctest` invocation, using the same YAML as `build-linux`.

**This step is temporary**: it must be removed when Phase 9 delivers Check #14 in `tools/validate_assets.py`, which provides permanent sidecar enforcement via the `validate-assets` CI job.
This removal is a Phase 9 CI deliverable — see `implementation/phase-9.md` for the corresponding task.

### `default.mhr` Verification (Linux)

The `build-linux` job already contains a hard-fail verification step added in Phase 4:

**Step name**: `Verify default.mhr HRTF data` (`.github/workflows/_build-linux.yml`, `build-linux` job)

This step uses `test -f build/default.mhr` to confirm the HRTF data file is present at the expected relative path and exits non-zero if not found. No change required in Phase 7 — the Phase 4 hard-fail form is already in place.

### Risk Mitigation

**Missing HRTF data risk**: `default.mhr` is the OpenAL Soft HRTF dataset used by `AudioSystem` for 3D spatial audio initialization (`ALC_HRTF_SOFT=ALC_TRUE`). If the file is absent at runtime, OpenAL Soft silently falls back to non-HRTF panning — all spatial audio cues (distance attenuation, directional cues, occlusion) degrade or disappear without any error message to the player. The hard-fail CI step ensures that a binary missing `default.mhr` is caught at build time rather than discovered by a player experiencing degraded audio. This prevents shipping a binary with silent spatial audio fallback.

**Missing OpenAL runtime risk**: `soft_oal.dll` is the OpenAL Soft runtime DLL required on Windows. Without it, the `aitown.exe` binary fails to start entirely (DLL load failure). The hard-fail CI step catches this class of packaging error before any artifact is uploaded and retained for 30 days.

**CMake copy rule (corrected in Phase 7 fix commit)**: vcpkg installs OpenAL Soft as `OpenAL32.dll`, not `soft_oal.dll`. The post-build copy command must use `OpenAL32.dll` as the source and rename it to `soft_oal.dll` at the destination. Attaching the copy to `aitown_audio` (a static library) is wrong — static library output dirs differ from the executable output dir. Attach only to the `aitown` executable target:

```cmake
add_custom_command(TARGET aitown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll"
        "$<TARGET_FILE_DIR:aitown>/soft_oal.dll"
    COMMENT "Copying OpenAL32.dll (OpenAL Soft runtime) to aitown output as soft_oal.dll"
)
```

**Music stem sidecar risk**: `AudioSystem` throws `std::runtime_error` at music stem load time when a sidecar `.json` file is absent. The temporary CI enforcement step prevents a committed `music_*.ogg` from reaching CI without its required sidecar, catching the error before the audio thread is ever launched.
