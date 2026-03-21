## Phase 11i: CI Packaging — Windows Installer, Debian/Ubuntu Packages & Asset Validation Consolidation

**Status: Planned**

### Goal

Three CI/CD improvements delivered as a single cohesive phase:

1. **Asset validation consolidation** — The shader-asset verification step is currently
   duplicated inside both `build-linux` and `coverage-linux`. Moving it into the dedicated
   `validate-assets` job eliminates the duplication and makes `validate-assets` the single
   source of truth for all source-tree asset checks. OS-specific build jobs must contain no
   asset-validation logic that does not require a compiled binary.

2. **Windows installer** — A new `package-windows` CI job produces an NSIS-based `.exe`
   installer via CPack. Runs on push to `main` and `develop` only (not PRs).

3. **Linux distribution packages** — A new `package-linux-deb` CI job produces `.deb`
   packages for four targets using a matrix of distro-specific containers:
   - Debian Bookworm (12, stable)
   - Debian Trixie (13, testing/current)
   - Ubuntu 22.04 LTS (Jammy Jellyfish)
   - Ubuntu 24.04 LTS (Noble Numbat)

   Runs on push to `main` and `develop` only (not PRs).

---

### Deliverables

> **Implementation note**: Deliverable **§1a** (shader verification consolidation spec
> updates — removing the "Verify shader assets" step from `build-linux`/`coverage-linux`
> documentation and adding it to the `validate-assets` job section, plus the general
> source-tree-check rule and ordering rationale) has been **pre-applied** to
> `architecture/ci-cd/github-actions-workflow.md` as part of a fix-implementation review.
> Verify that section is correct before starting §2.
>
> Deliverables **§1b** (`package-windows` job spec) and **§1c** (`package-linux-deb` job
> spec) are **pending** — add those sections to the spec as part of implementing this
> phase. Deliverables **§2** (CI YAML changes) and **§3** (verification) are also
> **pending**.

#### 1. Spec Updates — `architecture/ci-cd/github-actions-workflow.md`

##### 1a. Asset Validation Consolidation

- [x] Add a **Shader Asset Verification** subsection to the `validate-assets` job
  documentation. The "Verify shader assets" step (checking that
  `assets/shaders/ui_quad.vert` and `assets/shaders/ui_quad.frag` exist) is a pure
  source-tree file check — it requires no build artifacts, no C++ toolchain, and no
  OS-specific environment. It must be moved from `build-linux` and `coverage-linux` into
  the `validate-assets` job. Document the updated `validate-assets` step order:

  1. Checkout
  2. Set up Python 3
  3. Install Python dependencies (`pip install mutagen`)
  4. **Verify shader assets** (new step moved from `build-linux`/`coverage-linux`):

     ```yaml
     - name: Verify shader assets
       shell: bash
       run: |
         test -f assets/shaders/ui_quad.vert || { echo "Missing ui_quad.vert"; exit 1; }
         test -f assets/shaders/ui_quad.frag || { echo "Missing ui_quad.frag"; exit 1; }
     ```

  5. Run asset validation (`python tools/validate_assets.py`)
  6. (Phase 11d+) Verify check_N presence guard steps

  (`cicd-dev-github`)

- [x] Update the `build-linux` job documentation: remove the "Verify shader assets" step
  from the mandatory step sequence. Update the step documentation in the spec to reflect
  that this step no longer appears in `build-linux`. Document the rationale: source-tree
  checks belong in `validate-assets`; `build-linux` only validates build artifacts and
  test execution. (`cicd-dev-github`)

- [x] Update the `coverage-linux` job documentation: remove the "Verify shader assets"
  step from its mandatory step sequence (step 11 in the current ordered list). Renumber
  subsequent steps accordingly. (`cicd-dev-github`)

- [x] Add a general rule to the `validate-assets` job documentation:
  **Any CI step that checks source-tree file existence, file format, or file content and
  requires no compiled binary must be placed in `validate-assets`, not in
  `build-linux`, `build-windows`, or `coverage-linux`.** This rule prevents future
  duplication. (`cicd-dev-github`)

##### 1b. `package-windows` Job

- [ ] Add a **`package-windows` job** section documenting the following. Insert this section in `architecture/ci-cd/github-actions-workflow.md` immediately after the `all-checks-pass` job documentation section and before any trailing phasing-summary or appendix content.

  - **Trigger condition**: runs only on push to `main` or `develop`
    (`if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`).
    Does NOT run on pull requests. Does NOT block `all-checks-pass`.

  - **Runner**: `windows-latest`.

  - **Timeout**: `timeout-minutes: 60` (NSIS installer creation adds time on top of the
    full build).

  - **Permissions**: `contents: read`.

  - **Dependencies**: `needs: [build-windows]` — packaging runs only after the Windows
    build job passes. This is a **quality gate only** (not artifact sharing): the
    packaging job rebuilds from source rather than downloading `build-windows` artifacts.
    The rationale: `build-windows` verifies that the code compiles and all tests pass;
    `package-windows` then produces a clean, definitive release build. If `build-windows`
    fails, packaging is skipped entirely. The packaged binary is built with the same
    `ci-windows` preset, so the build is reproducible.

  - **Step sequence**:
    1. Checkout (`actions/checkout` SHA-pinned)
    2. Install NSIS via Chocolatey:

       ```yaml
       - name: Install NSIS
         shell: pwsh
         run: choco install nsis --no-progress -y
       ```

    3. `ilammy/msvc-dev-cmd` (SHA-pinned) — required for the CMake configure step
    4. `lukka/run-vcpkg` (SHA-pinned) — restore vcpkg packages
    5. CMake configure:

       ```yaml
       - name: CMake configure
         shell: pwsh
         run: cmake --preset ci-windows -DAITOWN_ASSETS_DIR=assets
       ```

       The `-DAITOWN_ASSETS_DIR=assets` flag sets the compile-time asset path to a relative
       path (`assets/`), resolved from the process working directory at runtime.
    6. CMake build: `cmake --build build --parallel`
    7. Append vcpkg DLL directory to `$env:GITHUB_PATH` (same step as `build-windows`)
    8. CPack — generate NSIS installer:

       ```yaml
       - name: Create Windows installer
         shell: pwsh
         working-directory: build
         run: cpack -G NSIS -C Release
       ```

    9. Upload installer artifact:

       ```yaml
       - name: Upload Windows installer
         uses: actions/upload-artifact@<SHA-PLACEHOLDER>  # v4.6.0 — resolve SHA via: gh release view v4.6.0 --repo actions/upload-artifact; NEVER copy from this document
         with:
           name: aitown-installer-windows-${{ github.sha }}
           path: build/aitown-*.exe
           retention-days: 30
       ```

  - **CPack NSIS requirements** (to be added to `CMakeLists.txt`):
    - `CPACK_PACKAGE_NAME`: `"AI Town"`
    - `CPACK_PACKAGE_VENDOR`: project maintainer name
    - `CPACK_PACKAGE_VERSION`: `${PROJECT_VERSION}`
    - `CPACK_NSIS_DISPLAY_NAME`: `"AI Town"`
    - `CPACK_NSIS_INSTALL_ROOT`: `"$PROGRAMFILES64"`
    - `CPACK_NSIS_CREATE_ICONS_EXTRA`: Start Menu and Desktop shortcut to `aitown.exe`
    - `CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL`: `ON`
    - `CPACK_INSTALL_CMAKE_PROJECTS`: include the main `aitown` target
    - All vcpkg runtime DLLs must be installed alongside `aitown.exe` via
      `install(FILES ...)` in `CMakeLists.txt` — the installer must be self-contained
      (no separate vcpkg installation required on the end user's machine).
    - `default.mhr` (HRTF data file required for 3D spatial audio):
      `install(FILES ${CMAKE_BINARY_DIR}/default.mhr DESTINATION .)` — this file is
      copied to `${CMAKE_BINARY_DIR}` by a CMake `POST_BUILD copy_if_different` rule
      sourcing from the vcpkg OpenAL Soft share directory
      (`${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr`).
      This POST_BUILD rule must exist in `CMakeLists.txt`; CPack runs after the build
      completes, so the file will be present when CPack executes. Without it the HRTF
      initialization step fails silently at runtime. **Windows HRTF search path**: On
      Windows, OpenAL Soft searches the application executable directory (alongside
      `aitown.exe` and `soft_oal.dll`) for `default.mhr`, so `DESTINATION .` is the
      correct install destination — no additional path configuration is required.
    - `soft_oal.dll` (OpenAL Soft renamed as `soft_oal.dll` per Windows naming
      convention): `install(FILES ${CMAKE_BINARY_DIR}/soft_oal.dll DESTINATION .)` —
      must be explicitly listed; it is NOT automatically included by the generic DLL
      glob and its absence causes `alGetError`/`alcOpenDevice` to fail on the end
      user's machine.
    - Data files (assets/): `install(DIRECTORY assets DESTINATION .)` so the `assets/`
      directory lands as a subdirectory alongside `aitown.exe` in the installer root
      (e.g., `<install dir>/assets/shaders/...`). The trailing slash MUST be omitted:
      with a trailing slash, CMake would install the *contents* of `assets/` directly
      to the root, breaking the `assets/` subdirectory that `AITOWN_ASSETS_DIR=assets`
      expects. This is the Windows equivalent of the Linux
      `install(DIRECTORY assets/ DESTINATION share/aitown/assets)` rule and is required
      so that DDS textures, audio assets, and shader files are included in the `.exe`
      installer.
    - The `CPACK_GENERATOR` default is NOT set in `CMakeLists.txt`; the generator is
      always specified explicitly on the `cpack` command line (`-G NSIS`, `-G DEB`).

  (`cicd-dev-github`)

##### 1c. `package-linux-deb` Job

- [ ] Add a **`package-linux-deb` job** section documenting the following. Insert this section in `architecture/ci-cd/github-actions-workflow.md` immediately after the `all-checks-pass` job documentation section and before any trailing phasing-summary or appendix content.

  - **Trigger condition**: runs only on push to `main` or `develop`
    (`if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`).
    Does NOT run on pull requests. Does NOT block `all-checks-pass`.

  - **Strategy**: `matrix` build over four distro targets:

    | `matrix.distro` | `matrix.container` | `matrix.codename` |
    |---|---|---|
    | `debian-bookworm` | `debian:bookworm` | `bookworm` |
    | `debian-trixie` | `debian:trixie` | `trixie` |
    | `ubuntu-jammy` | `ubuntu:22.04` | `jammy` |
    | `ubuntu-noble` | `ubuntu:24.04` | `noble` |

    `fail-fast: false` — a failure on one distro must not cancel the other three.

  - **Container**: `container: ${{ matrix.container }}`

  - **Runner**: `ubuntu-latest` (the GitHub Actions host; the container provides the
    distro environment).

  - **Timeout**: `timeout-minutes: 60`.

  - **Permissions**: `contents: read`.

  - **Dependencies**: none (`needs:` is omitted — packaging runs independently; the
    distro builds are independent and the packaging job does not reuse `build-linux`
    artifacts because each distro links against its own system libraries).

  - **Step sequence**:
    1. Install system build dependencies:

       ```yaml
       - name: Install build dependencies
         run: |
           apt-get update -y
           apt-get install -y --no-install-recommends \
             build-essential cmake ninja-build git curl zip unzip tar pkg-config \
             libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev \
             libopenal-dev libvorbis-dev \
             python3 \
             dpkg-dev fakeroot
       ```

       Notes:
       - `fakeroot` is required by CPack DEB to build the package archive without
         requiring root.
       - `dpkg-dev` provides `dpkg-architecture` (required by CPack DEB for `DEB_HOST_ARCH`
         resolution) and `dpkg-shlibdeps` (for automatic `Depends:` field population).
       - System packages (`libopenal-dev`, `libvorbis-dev`, `libgl1-mesa-dev`) are
         preferred over vcpkg for the `.deb` packaging build — the resulting package
         declares `Depends: libopenal1, libvorbis0a, libgl1` (runtime library packages)
         rather than bundling the libraries, keeping the `.deb` small and idiomatic.
       - Irrlicht is NOT available as a system package in these distros and is built via
         vcpkg (see step 4).

    2. Checkout (`actions/checkout` SHA-pinned — note: in a container job the checkout
       step must use `actions/checkout` as normal; GitHub Actions injects the token
       automatically).

    3. Install vcpkg:

       ```yaml
       - name: Set up vcpkg
         run: |
           git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
           git -C /opt/vcpkg checkout ${VCPKG_COMMIT_ID}
           /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
           echo "VCPKG_ROOT=/opt/vcpkg" >> $GITHUB_ENV
       ```

       **Pinning**: The vcpkg clone must use the same `VCPKG_COMMIT_ID` as the main
       `build-linux` job. `VCPKG_COMMIT_ID` is provided as a workflow-level env var (same
       value as in `build-linux`). The `git checkout` command above pins the clone to that
       exact commit before bootstrapping. Using a different vcpkg baseline in the packaging
       job would risk baseline atomicity violations (see `architecture/ci-cd/
       dependency-management.md`).

    4. Install vcpkg packages (Irrlicht only — all other dependencies are provided by
       the distro's system packages in step 1):

       ```yaml
       - name: Install vcpkg packages
         run: |
           $VCPKG_ROOT/vcpkg install irrlicht --triplet x64-linux
       ```

    5. CMake configure (system packages + vcpkg overlay for Irrlicht):

       ```yaml
       - name: CMake configure
         run: |
           cmake -B build -S . \
             -G Ninja \
             -DCMAKE_BUILD_TYPE=Release \
             -DENABLE_COVERAGE=OFF \
             -DBUILD_TESTING=OFF \
             -DCMAKE_INSTALL_PREFIX=/usr \
             -DAITOWN_ASSETS_DIR=/usr/share/aitown/assets \
             -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
             -DVCPKG_MANIFEST_FEATURES="" \
             -DVCPKG_INSTALLED_DIR=$VCPKG_ROOT/installed
       ```

       Testing is disabled via the standard CMake `BUILD_TESTING` option (set to `OFF`
       here) so that `find_package(GTest)` and `find_package(rapidcheck)` are not
       required. `CMakeLists.txt` must guard all test target declarations with
       `if(BUILD_TESTING)` (or the equivalent CMake `include(CTest)`/`enable_testing()`
       mechanism) for this flag to take effect — without the guard, the `find_package`
       calls will still execute and fail if the packages are not installed. The packaging
       job produces only the `aitown` binary and installer; test execution is handled by
       `build-linux` and `coverage-linux`.

    6. CMake build: `cmake --build build --parallel`

    7. CPack DEB — generate `.deb` package:

       ```yaml
       - name: Create Debian package
         working-directory: build
         run: cpack -G DEB
       ```

    8. Upload `.deb` artifact:

       ```yaml
       - name: Upload Debian package
         uses: actions/upload-artifact@<SHA-PLACEHOLDER>  # v4.6.0 — resolve SHA via: gh release view v4.6.0 --repo actions/upload-artifact; NEVER copy from this document
         with:
           name: aitown-deb-${{ matrix.distro }}-${{ github.sha }}
           path: build/aitown-*.deb
           retention-days: 30
       ```

  - **CPack DEB requirements** (to be added to `CMakeLists.txt`):
    - `CPACK_DEBIAN_PACKAGE_MAINTAINER`: project maintainer name and email
    - `CPACK_DEBIAN_PACKAGE_SECTION`: `games`
    - `CPACK_DEBIAN_PACKAGE_PRIORITY`: `optional`
    - `CPACK_DEBIAN_PACKAGE_DEPENDS`:
      `"libopenal1, libvorbis0a, libgl1, libx11-6, libxrandr2, libxinerama1"`
    - `CPACK_DEBIAN_PACKAGE_DESCRIPTION`: one-line summary + extended description
      (multi-line, indented with a leading space per Debian policy)
    - `CPACK_DEBIAN_FILE_NAME`: `DEB-DEFAULT` (generates
      `aitown_<version>_<arch>.deb` using `dpkg-architecture` output)
    - `CPACK_DEBIAN_PACKAGE_SHLIBDEPS`: `ON` — let CPack/dpkg-shlibdeps auto-detect
      shared library dependencies; this overrides the manual `DEPENDS` field above when
      `dpkg-shlibdeps` is available.
    - Install destination: `install(TARGETS aitown RUNTIME DESTINATION games)` so the
      binary lands at `/usr/games/aitown` (standard Debian policy location for games).
    - Data files (assets/): `install(DIRECTORY assets/ DESTINATION share/aitown/assets)`.
    - `default.mhr` (HRTF data file): `install(FILES ${CMAKE_BINARY_DIR}/default.mhr DESTINATION share/openal/hrtf)` — installs to `/usr/share/openal/hrtf/default.mhr`, which is one of OpenAL Soft's standard HRTF search paths (along with `~/.local/share/openal/hrtf/` for per-user overrides). Installing to `share/aitown/` would be incorrect — OpenAL Soft does NOT search that path. The same POST_BUILD `copy_if_different` rule that populates `${CMAKE_BINARY_DIR}/default.mhr` on Windows also runs on Linux builds, so the file is present before CPack executes.
    - `CPACK_PACKAGING_INSTALL_PREFIX`: `/usr` — sets the installation prefix for the
      Debian package. This must match `CMAKE_INSTALL_PREFIX=/usr` set at configure
      time. Without it, CPack uses the default prefix (`/usr/local`), causing the
      binary to land at `/usr/local/games/aitown`, assets at
      `/usr/local/share/aitown/assets`, and the HRTF file at
      `/usr/local/share/openal/hrtf/default.mhr` — all differing from the
      `/usr`-based paths documented above. Add this to `CMakeLists.txt` as
      `set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")`.
    - The `CPACK_GENERATOR` default is NOT set; always specify `-G DEB` on the command
      line.

  - **`all-checks-pass` gate**: `package-linux-deb` and `package-windows` are NOT
    added to the `needs:` list of `all-checks-pass`. Packaging jobs are release
    artifacts; a packaging failure must not block PR merges or feature integration.
    Package job failures are visible in the Actions tab and should be investigated
    before a release is cut.

  (`cicd-dev-github`)

#### 2. CI Implementation — `.github/workflows/ci.yml`

##### 2a. Remove Shader Verification from OS-Specific Jobs

- [ ] If a temporary "music stem sidecar enforcement" step (or similar source-tree file
  check) exists in `build-linux` or `coverage-linux`, handle it as follows:
  - If Check #14 (music stem JSON sidecar validation) is already implemented in
    `tools/validate_assets.py`, the temporary step is redundant — simply remove it
    from `build-linux`/`coverage-linux` without adding it to `validate-assets`
    (the validation is already covered by the `Run asset validation` step).
  - If Check #14 is NOT yet implemented in `tools/validate_assets.py`, move the
    temporary step to `validate-assets` (after "Install Python dependencies", before
    "Run asset validation") per the general consolidation rule. Do NOT leave the step
    in `build-linux`/`coverage-linux`.
  (`cicd-dev-github`)

- [ ] Remove the "Verify shader assets" step from `build-linux`:

  ```yaml
  # DELETE this block from build-linux:
  - name: Verify shader assets
    shell: bash
    run: |
      test -f assets/shaders/ui_quad.vert || { echo "Missing ui_quad.vert"; exit 1; }
      test -f assets/shaders/ui_quad.frag || { echo "Missing ui_quad.frag"; exit 1; }
  ```

  (`cicd-dev-github`)

- [ ] Remove the "Verify shader assets" step from `coverage-linux` (same step, same
  deletion). (`cicd-dev-github`)

- [ ] Add the "Verify shader assets" step to the `validate-assets` job, placed after the
  "Install Python dependencies" step and before the "Run asset validation" step:

  ```yaml
  - name: Verify shader assets
    shell: bash
    run: |
      test -f assets/shaders/ui_quad.vert || { echo "Missing ui_quad.vert"; exit 1; }
      test -f assets/shaders/ui_quad.frag || { echo "Missing ui_quad.frag"; exit 1; }
  ```

  (`cicd-dev-github`)

##### 2b. Add `package-windows` Job

- [ ] Add the `package-windows` job to `.github/workflows/ci.yml` per the spec in §1b.
  All action `uses:` lines must use the full 40-character commit SHA (no tag references
  or short SHAs — the supply-chain lint step in `build-linux` enforces this). The NSIS
  installer step must run inside the `build/` directory (`working-directory: build`).
  (`cicd-dev-github`)

- [ ] The `package-windows` job must be listed in the `on:` trigger section under
  `push.branches: [main, develop]` only. Do not add it to the pull_request trigger.
  (`cicd-dev-github`)

- [ ] Add `CPACK_PACKAGE_*` and `CPACK_NSIS_*` variables to `CMakeLists.txt` per §1b.
  All runtime DLLs produced by the vcpkg `x64-windows` triplet must be installed via
  `install(FILES ...)` or `install(DIRECTORY ...)` so CPack includes them in the
  installer. (`cicd-dev-github`)

- [ ] Add the `POST_BUILD copy_if_different` rule to `CMakeLists.txt` to copy
  `default.mhr` from the vcpkg OpenAL Soft share directory to the binary output
  directory after every build (per the requirement documented in §1b). Without this
  rule, `${CMAKE_BINARY_DIR}/default.mhr` will not exist and the CPack `install(FILES ...)`
  rule for HRTF data will fail silently. (`cicd-dev-github`)

- [ ] Verify that `AITOWN_ASSETS_DIR` (the compile-time asset path constant in `CMakeLists.txt`)
  is set correctly for installed builds, not just development builds. If it is currently
  hardcoded to `${CMAKE_SOURCE_DIR}/assets` (an absolute source-tree path), update it
  to use the correct installation prefix for packaged builds:
  - On Windows (NSIS installer): assets land alongside `aitown.exe` (DESTINATION `.`),
    so the binary can locate them via a relative path from the executable directory
    (e.g., `./assets`). Use a generator expression or install-time configuration to
    set `AITOWN_ASSETS_DIR` to `"."` or derive it relative to the executable at runtime.
  - On Linux (DEB package): assets land at `/usr/share/aitown/assets`, so
    `AITOWN_ASSETS_DIR` must be set to `/usr/share/aitown/assets` for installed builds.
  The correct approach is to pass `-DAITOWN_ASSETS_DIR=<path>` as an explicit CMake
  `-D` flag at configure time in the packaging job: for the Windows NSIS job, pass
  `-DAITOWN_ASSETS_DIR=assets` (relative path, resolved at runtime relative to the
  executable directory); for the Linux DEB job, pass
  `-DAITOWN_ASSETS_DIR=/usr/share/aitown/assets` (the FHS-compliant absolute install
  path). For this `-D` flag to work, `CMakeLists.txt` must declare `AITOWN_ASSETS_DIR`
  as a CMake `CACHE STRING` variable:
  `set(AITOWN_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE STRING "Runtime assets directory path")`.
  If the variable is currently hardcoded inside
  `target_compile_definitions(... PRIVATE AITOWN_ASSETS_DIR=...)`, the `-D` flag on the
  command line will have no effect and the packaged binary will use the source-tree path.
  The Windows relative path `assets` is resolved from the process working directory at
  runtime; NSIS shortcuts default to the installation directory as the working directory,
  so this works correctly for installed builds launched via the Start Menu shortcut.
  Because `AITOWN_ASSETS_DIR` is a compile-time `#define` baked into the binary,
  it cannot be changed at install time — it must be set correctly at CMake configure time
  in each packaging job. Without this fix, packaged applications will fail to load
  any asset (textures, shaders, audio) because the source tree path does not exist on
  the end user's machine. (`cicd-dev-github`, `graphics-dev-irrlicht`)

##### 2c. Add `package-linux-deb` Job

- [ ] Add the `package-linux-deb` job (with matrix) to `.github/workflows/ci.yml` per
  the spec in §1c. The `container:` field must reference `${{ matrix.container }}` so
  each matrix leg runs inside its distro container. (`cicd-dev-github`)

- [ ] The `package-linux-deb` job must be listed in the `on:` trigger section under
  `push.branches: [main, develop]` only. (`cicd-dev-github`)

- [ ] Add `CPACK_DEBIAN_*` and the install destination rules to `CMakeLists.txt` per
  §1c. The `CPACK_DEBIAN_FILE_NAME: DEB-DEFAULT` setting requires `dpkg-architecture`
  to be available at CPack time — this is satisfied by the `dpkg-dev` apt package
  installed in the container step. (`cicd-dev-github`)

- [ ] Verify `AITOWN_ASSETS_DIR` is correct for installed Linux builds (see §2b for the
  full requirement — the same fix applies to the Linux package). (`cicd-dev-github`,
  `graphics-dev-irrlicht`)

- [ ] Verify the same `POST_BUILD copy_if_different` rule for `default.mhr` (see §2b)
  also runs on Linux builds — the same CMakeLists.txt rule applies to all platforms.
  (`cicd-dev-github`)

#### 3. Verification Steps

- [ ] After the `validate-assets` job change: verify that `build-linux` and
  `coverage-linux` CI runs no longer contain a "Verify shader assets" step, and that the
  `validate-assets` job run log shows the step passing. (`cicd-dev-github`)

- [ ] After the `package-windows` job addition: trigger a push to `develop` and confirm
  the `package-windows` job runs, produces a `.exe` artifact, and uploads it
  successfully. Confirm the artifact installs and launches `aitown.exe` on a Windows
  machine. (`cicd-dev-github`)

- [ ] After the `package-linux-deb` job addition: trigger a push to `develop` and
  confirm all four matrix legs complete, producing `.deb` artifacts for each distro.
  Confirm `dpkg -i aitown-*.deb` succeeds on a matching distro. (`cicd-dev-github`)

---

### Exit Criteria

- [ ] `architecture/ci-cd/github-actions-workflow.md` documents the shader-asset
  verification step as a `validate-assets` deliverable (not `build-linux`/
  `coverage-linux`); the general rule that source-tree file checks belong in
  `validate-assets` is stated.
- [ ] `architecture/ci-cd/github-actions-workflow.md` documents the `package-windows`
  job (NSIS, CPack, SHA-pinned actions, trigger condition, `needs: [build-windows]`,
  artifact upload, CMakeLists.txt NSIS variables).
- [ ] `architecture/ci-cd/github-actions-workflow.md` documents the `package-linux-deb`
  job (matrix over 4 distros, container builds, system packages vs vcpkg split,
  CPack DEB, artifact upload, non-gating status).
- [ ] `.github/workflows/ci.yml`: "Verify shader assets" step absent from `build-linux`
  and `coverage-linux`; present in `validate-assets`.
- [ ] `.github/workflows/ci.yml`: `package-windows` job present; triggers on push to
  `main`/`develop` only; produces `aitown-installer-windows-<sha>.exe` artifact with
  30-day retention; does NOT appear in `all-checks-pass` `needs:`.
- [ ] `.github/workflows/ci.yml`: `package-linux-deb` job present with 4-entry matrix
  (debian-bookworm, debian-trixie, ubuntu-jammy, ubuntu-noble); triggers on push to
  `main`/`develop` only; produces `aitown-deb-<distro>-<sha>.deb` artifacts with
  30-day retention; does NOT appear in `all-checks-pass` `needs:`.
- [ ] `CMakeLists.txt`: `CPACK_NSIS_*` variables for Windows installer defined;
  `CPACK_DEBIAN_*` variables for Debian/Ubuntu packages defined; runtime DLL install
  rules for Windows present; `install(TARGETS aitown RUNTIME DESTINATION games)` present
  for Linux.
- [ ] All previously passing CI jobs (`build-linux`, `build-windows`, `coverage-linux`,
  `validate-assets`, `markdown-lint`) continue to pass after the changes. Specifically
  for `coverage-linux`: all three test routing checks (unit, integration, requires-opengl)
  execute and produce non-zero discovery, all test steps complete with exit code 0, lcov
  coverage gate enforcement runs and produces expected PASS/FAIL output, and test XML
  files are written to `test_results/`. The new `package-windows` and `package-linux-deb`
  jobs run independently and are NOT included in this continuity requirement — they do not
  run tests and do not gate `all-checks-pass`.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
  exits zero.

---

### Team

| Role | Responsibility |
|---|---|
| `cicd-dev-github` | All CI/CD changes: remove shader verification from `build-linux`/`coverage-linux`; add it to `validate-assets`; add `package-windows` job (NSIS/CPack) and `package-linux-deb` job (matrix/CPack DEB); update `CMakeLists.txt` install rules and CPack variables; update `architecture/ci-cd/github-actions-workflow.md` spec |

---

### Dependencies

- No dependency on any other Phase 11 sub-phase — this phase touches only CI/CD
  configuration and `CMakeLists.txt` install rules; it does not depend on any gameplay
  feature phase.
- Requires all actions SHAs used in new jobs to be resolved at implementation time via
  `gh release view` (never copy from documentation — supply-chain lint will catch any
  placeholder tokens).
- `package-windows` depends on NSIS being installable via Chocolatey on the
  `windows-latest` GitHub-hosted runner (confirmed available as of 2026-03).
- `package-linux-deb` uses the official distro containers; no custom Docker image
  required.
