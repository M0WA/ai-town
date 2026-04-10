# Caching

- **vcpkg packages**: Linux builds use a pre-baked Docker CI image that includes `/opt/vcpkg_installed` with all vcpkg packages already installed. Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and read from `/opt/vcpkg_installed` — **no `actions/cache` step for vcpkg is required or used in `_build-linux.yml` or `_coverage-linux.yml`**. Linux ccache caching goes through `hendrikmuhs/ccache-action`, which manages its own `actions/cache` invocation internally. The `actions/cache` step appears directly in four workflows: `_build-windows.yml` (v5.0.4, `@668228422ae6a00e4ad889ee87cd7109ec5666a7`), `sonarcloud.yml` (v4.3.0, `@0057852bfaa89a56745cba8c7296529d2fc39830`), `_package-linux-deb.yml` (v4.3.0, same SHA), and `msvc.yml` (v4.3.0, same SHA). The 4-component cache key below applies to the **Windows job only** (`_build-windows.yml`). Windows job cache key must include **all four** of: `${{ runner.os }}`, `COMPILER_VERSION`, `hashFiles('vcpkg.json')`, and `vcpkg_commit_id`. **Compiler version must be detected dynamically**:
  - Linux: `$(gcc -dumpfullversion -dumpversion)` (no additional env setup required)
  - Windows: use `vswhere.exe` (pre-installed on GitHub Actions Windows runners). The PowerShell step to detect the MSVC version and write it to `$GITHUB_ENV` must use the `>> $env:GITHUB_ENV` redirection syntax (PowerShell 5.1 compatible — the default shell on GitHub Actions Windows runners):

    ```yaml
    - name: Detect MSVC version
      shell: pwsh
      run: |
        $version = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
          -latest -property installationVersion
        "COMPILER_VERSION=$version" >> $env:GITHUB_ENV
    ```

    Do NOT use `cl 2>&1 | head -1` — `cl` requires the MSVC Developer Command Prompt environment initialized first; `head` is not in the Windows default shell. Do NOT use `echo "COMPILER_VERSION=$version" >> $env:GITHUB_ENV` with the `echo` command — on PowerShell 5.1, `echo` writes an `[object]` string that does not produce a valid key=value env entry. The `"KEY=VALUE" >> $env:GITHUB_ENV` form (without `echo`) is the correct PowerShell idiom. The `Set-Content` alternative (`Set-Content -Path $env:GITHUB_FILE -Value "COMPILER_VERSION=$version"`) is equivalent but more verbose; `>> $env:GITHUB_ENV` is preferred for readability.
  - Hard-coded versions (e.g. `gcc-13`) fail to invalidate the cache on runner image upgrades. Missing any of these four components allows stale pre-built packages to survive a vcpkg baseline or compiler upgrade.
  - **Step ordering is mandatory**: the compiler-version detect step must write to `$GITHUB_ENV` as a **separate, named step that runs before the `actions/cache` step** (for both Linux and Windows jobs). The `actions/cache` step reads environment variables at the step level — values written to `$GITHUB_ENV` in the same step are not yet visible. A combined detect+cache step will produce a blank or stale compiler version in the cache key. For Linux: `echo "COMPILER_VERSION=$(gcc -dumpfullversion -dumpversion)" >> $GITHUB_ENV`; for Windows: the PowerShell step shown above.
  - **BOTH `build-linux` AND `coverage-linux` must include their own independent compiler-detect step.** These are fully independent jobs running on separate runner instances with no shared state. A compiler-detect step present only in `build-linux` has no effect on the `coverage-linux` job — `$GITHUB_ENV` is not shared between jobs. Each job must include the following step before its ccache setup step:

    ```yaml
    - name: Detect compiler version
      shell: bash
      run: echo "COMPILER_VERSION=$(gcc -dumpfullversion -dumpversion)" >> $GITHUB_ENV
    ```

    This step is identical in both jobs. The `coverage-linux` job uses it to build the ccache key (`${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}`). Omitting this step from `coverage-linux` causes `env.COMPILER_VERSION` to be empty, producing a different ccache key and breaking cache sharing between CI runs on the same compiler version.
- **Windows vcpkg cache key** (`_build-windows.yml`): Uses `inputs.vcpkg_commit_id` (not `env.VCPKG_COMMIT_ID`) because the `env` context is not populated from the caller's `env:` block in reusable workflows called via `uses:` — the value must be passed explicitly via `inputs` (sourced from the `prepare` job output). Full key format:

  ```yaml
  key: vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('vcpkg.json') }}-${{ inputs.vcpkg_commit_id }}
  restore-keys: |
    vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ inputs.vcpkg_commit_id }}-
    vcpkg-${{ runner.os }}-${{ env.COMPILER_VERSION }}-
  ```

  Cache path: `C:\Users\runneradmin\AppData\Local\vcpkg\archives`. The two-tier `restore-keys` allows partial cache hits: tier 1 reuses any prior build for the same OS/compiler/baseline (ignoring vcpkg.json changes), tier 2 reuses any prior build for the same OS/compiler (ignoring baseline changes).

- **`_package-linux-deb.yml` vcpkg cache** (`actions/cache` v4.3.0): Uses path `/opt/vcpkg/packages` (container-local path, distinct from Windows). Key includes `${{ matrix.codename }}` for distro-level ABI isolation (different glibc versions across bookworm/trixie/jammy/noble produce ABI-incompatible vcpkg archives that cannot be shared):

  ```yaml
  key: vcpkg-deb-${{ matrix.codename }}-${{ hashFiles('vcpkg.json') }}-${{ inputs.vcpkg_commit_id }}
  restore-keys: |
    vcpkg-deb-${{ matrix.codename }}-${{ inputs.vcpkg_commit_id }}-
    vcpkg-deb-${{ matrix.codename }}-
  ```

- **FetchContent**: FetchContent is NOT used in this project. All dependencies — including `googletest` and `rapidcheck` — are managed via vcpkg. No `.fetchcontent_cache` directory exists and no `actions/cache` step for FetchContent is needed in any CI job. See `dependency-management.md` ("Why vcpkg (not FetchContent)") for rationale.
- **`mkdir test_results` before ctest**: Both the `build-linux` job and the `coverage-linux` job must include an explicit `mkdir -p test_results` step **before** each `ctest` invocation. On Windows (`build-windows`), use `New-Item -ItemType Directory -Force -Path test_results`. Without this step, `GTEST_OUTPUT=xml:test_results/` silently writes nothing if the directory does not exist — the artifact upload step uploads an empty directory with no test XML.
- **Pin all third-party GitHub Actions to full commit SHAs** (not version tags): Version tags (e.g. `@v4`) can be silently redirected by a compromised tag. Include a comment with the human-readable version alongside each SHA for maintainability. Deployed SHA pins (verify at implementation time using `gh release view` or the GitHub Actions marketplace):

  **Core CI actions**:
  - `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd # v6.0.2` — used in all workflow files
  - `actions/cache@668228422ae6a00e4ad889ee87cd7109ec5666a7 # v5.0.4` — canonical version for new workflow additions; used in `_build-windows.yml`. Note: `sonarcloud.yml`, `_package-linux-deb.yml`, and `msvc.yml` continue to use v4.3.0 (`@0057852bfaa89a56745cba8c7296529d2fc39830`) — these should be updated to v5.0.4 in a future baseline bump.
  - `actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f # v7.0.0` — used in all workflow files
  - `actions/download-artifact@37930b1c2abaa49bbe596cd826c3c89aef350131 # v7.0.0` — used in multiple workflow files (test-linux downloads from build-linux; release downloads from build/coverage jobs)
  - `dorny/test-reporter@a43b3a5f7366b97d083190328d2c652e1a8b6aa2 # v3.0.0` — used in `_test-linux.yml`, `_coverage-linux.yml`, `_build-windows.yml`. The SHA must match exactly across ALL workflow files.
  - `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1 # v11.5` — Windows build only; node20 action with no node24 release; `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` must be set at job level.
  - `hendrikmuhs/ccache-action@33522472633dbd32578e909b315f5ee43ba878ce # v1.2.22` — Linux-dedicated workflows only (`_build-linux.yml`, `_coverage-linux.yml`); do NOT use on Windows runners or in `_asan-linux.yml` (ASAN-instrumented objects must not enter ccache).
  - `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c # v1.13.0` — Windows build/package only; node20 action with no node24 release; `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` must be set at job level.
  - `actions/setup-python@a309ff8b426b58ec0e2a45f0f869d46889d02405 # v6.2.0` — used in `_validate-assets.yml`

  **Docker CI image actions** (used in `docker-ci-image.yml`):
  - `docker/login-action@4907a6ddec9925e35a0a9e82d7399ccc52663121 # v4.1.0`
  - `docker/setup-buildx-action@4d04d5d9486b7bd6fa91e7baf45bbb4f8b9deedd # v4.0.0`
  - `docker/build-push-action@d08e5c354a6adb9ed34480a06d141179aa583294 # v7.0.0`

  **Security analysis actions**:
  - `david-a-wheeler/flawfinder@8e4a779ad59dbfaee5da586aa9210853b701959c` — used in `flawfinder.yml`
  - `github/codeql-action/upload-sarif@5c8a8a642e79153f5d047b10ec1cba1d1cc65699 # v3.35.1` — used in `flawfinder.yml`
  - `github/codeql-action/upload-sarif@0e9f55954318745b37b7933c693bc093f7336125 # v4.35.1` — used in `msvc.yml` (note: different major version from `flawfinder.yml`; recommend standardising on v4 in a future pass)
  - `microsoft/msvc-code-analysis-action@04825f6d9e00f87422d6bf04e1a38b1f3ed60d99` — used in `msvc.yml`

  **Release action**: The `release` job uses the `gh` CLI (`gh release create`) directly — `softprops/action-gh-release` is NOT used in this project.
- **Compiler output caching — platform-specific rules**:
  - **Linux (GCC/Clang)**: Use `hendrikmuhs/ccache-action` with CMake flags `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`. ccache is safe across runner instances for GCC and Clang. In Linux-only reusable workflows (`_build-linux.yml`, `_coverage-linux.yml`), the `if: runner.os == 'Linux'` guard is unnecessary and is omitted in the deployed source — those workflows always run in a Linux container. The guard is only required in workflows where jobs may run on multiple OS types.
  - **Windows (MSVC)**: Do NOT use ccache — ccache does not support `cl.exe`. The vcpkg binary cache provided by `lukka/run-vcpkg` is the primary Windows caching benefit. The `hendrikmuhs/ccache-action` step must be omitted from all Windows jobs.
- **ccache key differentiation for coverage-linux**: The `coverage-linux` job MUST use a distinct ccache key from `build-linux`. Append a `-coverage` suffix to the ccache key (e.g., `${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}`). Coverage-instrumented object files compiled with `-fprofile-arcs -ftest-coverage` are ABI-incompatible with non-instrumented objects. If both jobs share the same ccache key, `build-linux` may restore coverage-instrumented `.o` files from the `coverage-linux` run, producing a non-coverage binary that still carries `.gcno` sidecar files. This is intentional key separation, not accidental duplication.
- **ccache exclusion for asan-linux**: The `asan-linux` job MUST NOT use ccache at all. The `ci-linux-asan` CMake preset sets `CMAKE_C_COMPILER_LAUNCHER: ""` and `CMAKE_CXX_COMPILER_LAUNCHER: ""` (empty strings) to disable the ccache launchers inherited from `ci-linux`. ASAN-instrumented object files contain sanitizer metadata and must not enter the shared ccache — `build-linux` shares the same ccache key, and restoring ASAN-instrumented `.o` files into a non-ASAN build would silently produce a binary linked against sanitizer-instrumented objects. The `_asan-linux.yml` workflow omits the `hendrikmuhs/ccache-action` step entirely. A distinct ccache key (like the coverage approach) would also be safe, but disabling ccache is simpler and avoids any risk of cross-contamination since ASAN builds are infrequent and the compile-time cost is acceptable.
- **Do NOT cache the CMake build directory**: `CMakeCache.txt` embeds absolute paths that break across ephemeral runner instances
