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
    "fmt"
  ]
}
```
**`fmt` is a required explicit dependency**: the `openal-soft` vcpkg portfile applies a `devendor-fmt.diff` patch that replaces OpenAL Soft's bundled copy of `{fmt}` with the external vcpkg `fmt` package. This means `libopenal.a` contains object files that reference `fmt::v12::report_error` and other `fmt` symbols. Any CMake target that links (directly or transitively) against `OpenAL::OpenAL` must also link `fmt::fmt`, or the linker will fail with `undefined reference to fmt::v12::report_error`. Adding `fmt` to `vcpkg.json` ensures the vcpkg `fmt` package is installed; `fmt::fmt` must then be linked PRIVATE to `aitown_audio` in `CMakeLists.txt`.
- CMake configured with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`
- CI uses `lukka/run-vcpkg@<ACTION-SHA-REQUIRED> # v11.5` with a pinned vcpkg commit hash stored as `env.VCPKG_COMMIT_ID`. **`VCPKG_COMMIT_ID` must be declared at the workflow level** (in the top-level `env:` block of the CI YAML file, not at the job or step level) so it is available to all jobs and steps. Declaring it at the job level would make it unavailable to the baseline validation step if that step runs in a different job. Example declaration at workflow level:
  ```yaml
  env:
    VCPKG_COMMIT_ID: "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"  # placeholder — replace with real vcpkg commit SHA matching vcpkg.json builtin-baseline
  ``` **Implementation note**: The `<ACTION-SHA-REQUIRED>` token above is an angle-bracket placeholder that the SHA lint step (in `github-actions-workflow.md`) will catch if left unresolved. Before committing any CI YAML, replace it with the verified 40-character SHA for the intended `lukka/run-vcpkg` release tag by running `gh release view v11.5 --repo lukka/run-vcpkg --json tagName,targetCommitish`. The SHA `5e0cab206a5ea620130caf672fce3e4a6b5a793` used in earlier drafts is only 39 characters and is therefore invalid — a truncated SHA is NOT a valid supply-chain trust anchor. The action SHA pin must be updated alongside `VCPKG_COMMIT_ID` intentionally — they are both supply-chain trust anchors for the vcpkg install step.
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

### Baseline Staleness Risk

**Old vcpkg baselines break Windows CI via MSYS2 mirror 404s.** Confirmed failure mode (encountered in Phase 0): the `zlib` portfile at baseline `f7423ee` called `vcpkg_fixup_pkgconfig`, which attempted to download `msys2-runtime-3.5.3-3` from MSYS2 mirrors to obtain `pkgconf`. All six MSYS2 mirrors returned HTTP 404 — the package had been superseded and removed. The build failed with `error: Failed to download file with error: 1`.

**Rule**: If Windows CI fails with vcpkg download errors referencing MSYS2 packages, the baseline is too old. Update `builtin-baseline` in `vcpkg.json` AND `VCPKG_COMMIT_ID` in `ci.yml` to the current vcpkg HEAD (or a recent commit). Always verify that the `irrlicht` port still exists at the new baseline before committing:
```
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
# Phase 0: route OpenAL, fmt, and Vorbis to aitown_audio stub library (NOT to 'aitown' — that
# executable does not exist at Phase 0). The 'aitown' executable is the final game binary added
# in a later phase.
target_link_libraries(aitown_audio PRIVATE OpenAL::OpenAL fmt::fmt Vorbis::vorbisfile)
# Irrlicht links to aitown_render (PRIVATE — Irrlicht headers must NOT propagate to test targets).
target_link_libraries(aitown_render PRIVATE Irrlicht)
```
**IMPORTANT**: `target_link_libraries(aitown ...)` in the original snippet was incorrect — the `aitown` executable target does not exist at Phase 0. An implementer following the original snippet verbatim would get a CMake configure error: "Cannot specify link libraries for target aitown which is not built by this project."

**Irrlicht find_package — find-module (RESOLVED)**: The irrlicht vcpkg port at the pinned baseline does not ship a config-mode package (`irrlichtConfig.cmake` is absent). `find_package(Irrlicht REQUIRED)` therefore falls back to CMake's module mode. A minimal `cmake/FindIrrlicht.cmake` module is committed to the repo; it locates the Irrlicht headers and library from the vcpkg install tree and creates an `IMPORTED` target named `Irrlicht` (bare, no namespace). The CMake module path is set via `list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")` before the `find_package` call. **Target name is bare `Irrlicht`** — verified via adrido/irrlicht-vcpkg CMakeLists.txt: `install(TARGETS Irrlicht EXPORT Irrlicht ...)` with no `NAMESPACE` argument. Use `target_link_libraries(aitown_render PRIVATE Irrlicht)`.

On Windows: `soft_oal.dll` **and** `default.mhr` (HRTF data) copied to output via post-build commands. `libvorbisfile` is a static library in vcpkg's default triplet — no DLL copy is needed for it.

### Linux default.mhr Packaging

On Linux, `default.mhr` is installed by vcpkg alongside the OpenAL Soft library. After `cmake --build build` completes in the `build-linux` job, a CI verification step must confirm that `default.mhr` is accessible at the vcpkg-installed OpenAL Soft HRTF search path. Without this file, `AudioSystem` HRTF initialization fails at runtime even though the binary compiles and links successfully.

Add the following step immediately after `cmake --build build` in the `build-linux` job:

**PHASING**: At Phase 0, `default.mhr` is not yet copied to the binary output directory — the CMake post-build copy rule is a Phase 4 deliverable. At Phase 0, this step must be a **warning-only no-op** (see `implementation/phase-0.md` for the exact placeholder text: `step may be a documented no-op at Phase 0 with a TODO comment referencing Phase 4 where the full HRTF check is wired`). Implement the warning-only form at Phase 0 (e.g., log a message and exit 0 regardless). The hard-fail form below is the **Phase 4+ version** — replace the placeholder with this hard-fail step at Phase 4 delivery. This is the same pattern as the Windows DLL verification step (see the phasing note at the DLL verification block above).

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

### FetchContent vs vcpkg Resolution
The spec mandates vcpkg as the "mandatory dependency manager on all platforms." However, `googletest` and `RapidCheck` are fetched via CMake `FetchContent`. These two approaches coexist as follows:
- **googletest and RapidCheck remain FetchContent** (they are build-time testing tools, not runtime library dependencies, and pinning them via FetchContent SHA is well-established practice)
- **FetchContent source downloads must be cached in CI** via `actions/cache` keyed on the pinned SHA values. **FetchContent base dir must be outside the build tree** — set `FETCHCONTENT_BASE_DIR` in the CMake configure step to `.fetchcontent_cache` (a sibling of `build/`, not inside it). This prevents cache invalidation every time the build directory is cleared, and avoids the `build/_deps` path being path-dependent (CMake embeds absolute build-dir paths in `_deps`):
  ```yaml
  # CMake configure step — set FETCHCONTENT_BASE_DIR outside build tree:
  - name: Configure CMake
    run: cmake -B build -S . -DFETCHCONTENT_BASE_DIR=${{ github.workspace }}/.fetchcontent_cache ...

  # Cache step (runs BEFORE configure; restore-then-save on cache miss):
  - name: Cache FetchContent downloads
    uses: actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830  # v4.3.0 — pin to SHA in production
    with:
      path: .fetchcontent_cache
      key: fetchcontent-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}
  ```
  The cache key includes `cmake/**` to invalidate the cache when googletest or RapidCheck SHA pins are updated in CMake include files. All FetchContent SHA pins (googletest `v1.14.0` tag, RapidCheck SHA `ff6af6fc...`) must reside in files covered by this glob — either in the root `CMakeLists.txt` or under `cmake/`. **`COMPILER_VERSION` must be included** — a FetchContent dependency pinned by SHA can produce ABI-incompatible binaries if the compiler version changes; a stale FetchContent cache from a prior compiler causes link errors. `COMPILER_VERSION` must be detected and written to `$GITHUB_ENV` in a **separate step that runs before the `actions/cache` step** — see Caching spec for the compiler-detect step ordering requirement. Consistent key format with caching.md is required: both files must use `fetchcontent-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}`.
- This prevents re-cloning googletest and RapidCheck from GitHub on every CI run (adds 30–120 s and introduces GitHub availability as a failure mode)
- The vcpkg mandate applies to all runtime dependencies (Irrlicht, OpenAL Soft). The `vcpkg.json` must NOT list `googletest` or `rapidcheck` — those remain FetchContent exclusively. **DLL verification step required before artifact upload**: the Windows CI job must verify that `soft_oal.dll` and `default.mhr` are present in the output directory before uploading artifacts — a missing DLL produces a binary that crashes on launch and would waste the 30-day artifact retention window. **PHASING**: The `soft_oal.dll` and `default.mhr` post-build CMake copy commands are not delivered until Phase 4. At Phase 0, the DLL verification step MUST be a **warning-only placeholder that always exits 0** (see `implementation/phase-0.md` for the exact YAML). This hard-fail form below is the **Phase 4+ version** — it must replace the Phase 0 placeholder at Phase 4 delivery. Committing the hard-fail form at Phase 0 breaks the Windows CI job because the files do not yet exist. Add the Phase 4+ hard-fail step:
```yaml
- name: Verify required DLLs are present
  shell: pwsh
  run: |
    # Use explicit if-block syntax — "Test-Path ... || exit 1" is PowerShell 7+ only;
    # GitHub Actions Windows runners default to PowerShell 5.1 where || is not supported.
    if (-not (Test-Path "build/Release/soft_oal.dll")) {
      Write-Error "soft_oal.dll not found"
      exit 1
    }
    if (-not (Test-Path "build/Release/default.mhr")) {
      Write-Error "default.mhr not found"
      exit 1
    }
```
**Note**: Both `soft_oal.dll` AND `default.mhr` must be verified. The Phase 0 placeholder (see `implementation/phase-0.md`) is a warning-only check for both files.

### Irrlicht DLL on Windows (Phase 1+)

**RESOLVED — triplet: `x64-windows` (default dynamic).** No `VCPKG_DEFAULT_TRIPLET` override is set in CI; vcpkg defaults to `x64-windows` on Windows runners. This produces `Irrlicht.dll` as a dynamic library that must be copied to the output directory alongside `soft_oal.dll` when `aitown_render` is first linked against Irrlicht in Phase 1.

**Phase 1 action required**: Add a CMake post-build copy rule for `Irrlicht.dll` (parallel to the `soft_oal.dll` copy rule added in Phase 4) and add `Irrlicht.dll` to the Windows DLL verification step in CI alongside `soft_oal.dll`.
