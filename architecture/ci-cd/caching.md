# Caching

- **vcpkg packages** cached by `actions/cache`; cache key must include **all four** of: `vcpkg.json` hash, `vcpkgGitCommitId` (pinned commit), OS, and compiler version. **Compiler version must be detected dynamically**:
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
  - **BOTH `build-linux` AND `coverage-linux` must include their own independent compiler-detect step.** These are fully independent jobs running on separate runner instances with no shared state. A compiler-detect step present only in `build-linux` has no effect on the `coverage-linux` job — `$GITHUB_ENV` is not shared between jobs. Each job must include the following step before its `actions/cache` step:

    ```yaml
    - name: Detect compiler version
      shell: bash
      run: echo "COMPILER_VERSION=$(gcc -dumpfullversion -dumpversion)" >> $GITHUB_ENV
    ```

    This step is identical in both jobs. The `coverage-linux` job uses it to build the ccache key (`${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}`) and the FetchContent cache key (`fetchcontent-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}`). Omitting this step from `coverage-linux` causes `env.COMPILER_VERSION` to be empty, producing a cache key of `fetchcontent-Linux--<hash>` — a different key from `build-linux`'s `fetchcontent-Linux-13.x.x-<hash>`, breaking cache sharing and adding unnecessary re-download time.
- **FetchContent caching**: When `FETCHCONTENT_BASE_DIR` is set to `.fetchcontent_cache` (outside the build tree), this directory must be explicitly cached by `actions/cache`. The FetchContent cache key must include **the `CMakeLists.txt` + `cmake/**` hash AND the compiler version** — a FetchContent dependency pinned by SHA can still produce incompatible ABI binaries if the compiler version changes, and a stale FetchContent cache from a different compiler will cause link errors. **Required key format**: `fetchcontent-${{ runner.os }}-${{ env.COMPILER_VERSION }}-${{ hashFiles('CMakeLists.txt', 'cmake/**') }}`. The `cmake/**` glob is required because googletest and RapidCheck SHA pins may reside in `cmake/` include files — omitting it allows a changed SHA pin to reuse a stale cache. This key format MUST be identical between dependency-management.md and this file — any discrepancy creates inconsistent CI behavior. Missing the COMPILER_VERSION component from the FetchContent key allows stale pre-built FetchContent artifacts from a prior compiler to persist after a runner upgrade, causing ABI-incompatible link errors that are difficult to diagnose.
- **`mkdir test_results` before ctest**: Both the `build-linux` job and the `coverage-linux` job must include an explicit `mkdir -p test_results` step **before** each `ctest` invocation. On Windows (`build-windows`), use `New-Item -ItemType Directory -Force -Path test_results`. Without this step, `GTEST_OUTPUT=xml:test_results/` silently writes nothing if the directory does not exist — the artifact upload step uploads an empty directory with no test XML.
- **Pin all third-party GitHub Actions to full commit SHAs** (not version tags): `actions/checkout`, `actions/cache`, `actions/upload-artifact`, `dorny/test-reporter`, `lukka/run-vcpkg`, `hendrikmuhs/ccache-action` must all use SHA pins (e.g. `actions/checkout@a5ac7e51b41094c92402da3b24376905380afc29`). Version tags (e.g. `@v4`) can be silently redirected by a compromised tag. Include a comment with the human-readable version alongside each SHA for maintainability. Reference SHAs for key actions (verify at implementation time using `gh release view` or the GitHub Actions marketplace before committing):
  - `dorny/test-reporter@31a54ee7ebcacc03a09ea97a7e5465a47b84aea5 # v1.9.1` — **IMPORTANT**: Verify this SHA matches `gh release view v1.9.1 --repo dorny/test-reporter --json tagName,targetCommitish` before committing. The SHA must match exactly across ALL workflow files — using different SHAs in `caching.md` vs `github-actions-workflow.md` creates a supply-chain inconsistency.
  - `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1 # v11.5` — SHA verified via `gh release view v11.5 --repo lukka/run-vcpkg --json tagName,targetCommitish`. This SHA must match the pin in `dependency-management.md` exactly — inconsistent SHAs between the two files create a supply-chain inconsistency.
  - `actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11 # v4.1.1`
  - `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830 # v4.3.0`
  - `actions/upload-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08 # v4.6.0`
  - `hendrikmuhs/ccache-action@ed74d11c0b343532753ecead8a951bb09bb34bc9 # v1.2.14` — Linux only (`if: runner.os == 'Linux'`); do NOT use on Windows runners.
  - `softprops/action-gh-release@<40-CHAR-SHA>  # resolve at implementation time` — used by the `release` job to create GitHub releases and upload package assets; SHA resolved via `gh release view --repo softprops/action-gh-release --json tagName,targetCommitish`.
- **Compiler output caching — platform-specific rules**:
  - **Linux (GCC/Clang)**: Use `hendrikmuhs/ccache-action` with CMake flags `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`. ccache is safe across runner instances for GCC and Clang.
  - **Windows (MSVC)**: Do NOT use ccache — ccache does not support `cl.exe`. The vcpkg binary cache provided by `lukka/run-vcpkg` is the primary Windows caching benefit. The `hendrikmuhs/ccache-action` step must be **skipped or omitted** on the Windows job (use `if: runner.os == 'Linux'`).
- **ccache key differentiation for coverage-linux**: The `coverage-linux` job MUST use a distinct ccache key from `build-linux`. Append a `-coverage` suffix to the ccache key (e.g., `${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}`). Coverage-instrumented object files compiled with `-fprofile-arcs -ftest-coverage` are ABI-incompatible with non-instrumented objects. If both jobs share the same ccache key, `build-linux` may restore coverage-instrumented `.o` files from the `coverage-linux` run, producing a non-coverage binary that still carries `.gcno` sidecar files. This is intentional key separation, not accidental duplication.
- **Do NOT cache the CMake build directory**: `CMakeCache.txt` embeds absolute paths that break across ephemeral runner instances
