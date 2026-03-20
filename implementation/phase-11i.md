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

#### 1. Spec Updates — `architecture/ci-cd/github-actions-workflow.md`

##### 1a. Asset Validation Consolidation

- [ ] Add a **Shader Asset Verification** subsection to the `validate-assets` job
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

- [ ] Update the `build-linux` job documentation: remove the "Verify shader assets" step
  from the mandatory step sequence. Update the ordered step list in the spec to reflect
  that this step no longer appears in `build-linux`. Document the rationale: source-tree
  checks belong in `validate-assets`; `build-linux` only validates build artifacts and
  test execution. (`cicd-dev-github`)

- [ ] Update the `coverage-linux` job documentation: remove the "Verify shader assets"
  step from its mandatory step sequence (step 11 in the current ordered list). Renumber
  subsequent steps accordingly. (`cicd-dev-github`)

- [ ] Add a general rule to the `validate-assets` job documentation:
  **Any CI step that checks source-tree file existence, file format, or file content and
  requires no compiled binary must be placed in `validate-assets`, not in
  `build-linux`, `build-windows`, or `coverage-linux`.** This rule prevents future
  duplication. (`cicd-dev-github`)

##### 1b. `package-windows` Job

- [ ] Add a **`package-windows` job** section documenting the following:

  - **Trigger condition**: runs only on push to `main` or `develop`
    (`if: github.event_name == 'push' && (github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop')`).
    Does NOT run on pull requests. Does NOT block `all-checks-pass`.

  - **Runner**: `windows-latest`.

  - **Timeout**: `timeout-minutes: 60` (NSIS installer creation adds time on top of the
    full build).

  - **Permissions**: `contents: read`.

  - **Dependencies**: `needs: [build-windows]` — packaging runs only after the Windows
    build job passes, ensuring the binary is known-good before packaging.

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
    5. CMake configure: `cmake --preset ci-windows`
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
         uses: actions/upload-artifact@<SHA>  # v4.6.0
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
    - The `CPACK_GENERATOR` default is NOT set in `CMakeLists.txt`; the generator is
      always specified explicitly on the `cpack` command line (`-G NSIS`, `-G DEB`).

  (`cicd-dev-github`)

##### 1c. `package-linux-deb` Job

- [ ] Add a **`package-linux-deb` job** section documenting the following:

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
           /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
           echo "VCPKG_ROOT=/opt/vcpkg" >> $GITHUB_ENV
       ```

       **Pinning**: The vcpkg clone must use the same `VCPKG_COMMIT_ID` as the main
       `build-linux` job — set `git -C /opt/vcpkg checkout ${VCPKG_COMMIT_ID}` after
       the clone, where `VCPKG_COMMIT_ID` is provided as a workflow-level env var (same
       value as in `build-linux`). Using a different vcpkg baseline in the packaging
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
             -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
             -DVCPKG_MANIFEST_FEATURES="" \
             -DVCPKG_INSTALLED_DIR=$VCPKG_ROOT/installed
       ```

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
         uses: actions/upload-artifact@<SHA>  # v4.6.0
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
  `validate-assets`, `markdown-lint`) continue to pass after the changes.
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
