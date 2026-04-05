# Framework

- **Google Test (GTest) + GMock** via **vcpkg** (port `gtest`) — FetchContent is NOT used.

```cmake
# Test targets link directly against vcpkg-managed GTest/GMock targets:
target_link_libraries(my_test PRIVATE GTest::gtest_main GTest::gmock)
```

- **RapidCheck** (property-based testing) via **vcpkg** (port `rapidcheck`) — FetchContent is NOT used:

```cmake
# Bare targets (no namespace) — both required for GTest integration:
target_link_libraries(my_test PRIVATE rapidcheck rapidcheck_gtest)
```

- Both `gtest` and `rapidcheck` are declared in `vcpkg.json` and installed by the vcpkg toolchain (see `cmake_minimum_required(3.21)` + `CMAKE_TOOLCHAIN_FILE`). **Do NOT add `FetchContent_Declare` blocks** for either library — vcpkg manages all test framework versions. The vcpkg `builtin-baseline` in `vcpkg.json` controls the pinned version; update the baseline to upgrade.
- `RC_ENABLE_GTEST ON` is set in `CMakeLists.txt` (required for `rapidcheck_gtest` integration).
- Windows note: `gtest` and `gmock` are built as shared DLLs (`gtest.dll`, `gmock.dll`) under the `x64-windows` vcpkg triplet; `DISCOVERY_MODE PRE_TEST` (see below) ensures DLLs are in PATH at test discovery time.
- All tests in a `tests/` directory mirroring `src/` structure; `enable_testing()` + `gtest_discover_tests()` enabled
- **`gtest_discover_tests()` must specify `WORKING_DIRECTORY`, `DISCOVERY_MODE`, `DISCOVERY_TIMEOUT`, `PROPERTIES TIMEOUT`, and test `LABELS`**:

  ```cmake
  gtest_discover_tests(my_test
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"  # REQUIRED — see note below
      DISCOVERY_MODE PRE_TEST  # REQUIRED on Windows — see note below
      DISCOVERY_TIMEOUT 30    # seconds; default 5s is insufficient for coverage-instrumented binaries
      PROPERTIES TIMEOUT 120  # per-test execution timeout; RapidCheck properties can run for seconds
      LABELS "unit"           # or "integration", "requires-opengl" — applied to ALL discovered tests in this target
  )
  ```

  Without `DISCOVERY_TIMEOUT 30`, CMake test discovery times out on coverage-instrumented (`-DENABLE_COVERAGE=ON`) binaries on loaded CI runners, silently producing 0 discovered tests and a misleading empty-coverage lcov report. Apply to ALL test targets.

  **`WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` is mandatory.** Without it, `gtest_discover_tests` defaults to `CMAKE_CURRENT_BINARY_DIR` (the build directory). When tests run from the build directory, `GTEST_OUTPUT=xml:test_results/` writes XML to `build/test_results/` instead of `<workspace>/test_results/`. The CI step that collects XML (`test_results/*.xml`) looks in the workspace root on both Linux and Windows, so an incorrect working directory silently produces zero XML files and causes `dorny/test-reporter` to fail with "No test report files were found".

  **`DISCOVERY_MODE PRE_TEST` is mandatory.** The default `POST_BUILD` mode runs the test binary immediately after linking (during `cmake --build`). On Windows with the vcpkg `x64-windows` triplet, GTest/GMock are built as shared DLLs (`gtest.dll`, `gmock.dll`) in `build/vcpkg_installed/x64-windows/bin/` — not in `build/` alongside the `.exe`. The post-build discovery step runs before the CI step that adds the vcpkg bin directory to PATH, so the test binary cannot load `gtest.dll` → exits with a DLL load error → discovery produces empty output → ctest reports `No tests were found!!!` with exit code 0 → tests silently skip and no XML is written. `PRE_TEST` defers discovery to ctest time, where the vcpkg bin directory has already been added to PATH. `PRE_TEST` requires CMake ≥ 3.18; the project's `cmake_minimum_required(3.21)` satisfies this on all runners.

  **LABELS MUST be set inside `gtest_discover_tests()`, NOT via `set_tests_properties()` afterwards.** `gtest_discover_tests()` dynamically creates CTest test entries at configure time; calling `set_tests_properties()` after `gtest_discover_tests()` targets the statically-created wrapper test, not the individually-discovered test cases — the labels do not propagate to discovered tests and `-L`/`-LE` ctest filters will silently fail to include or exclude the correct tests. The `LABELS` keyword inside `gtest_discover_tests()` is the only reliable way to assign labels to all auto-discovered GTest cases.

  **One label per target rule**: `gtest_discover_tests()` applies a single LABELS value to ALL tests in the target. A test binary cannot have some tests labeled `unit` and others labeled `integration`. **Unit and integration tests MUST be in separate CMake targets** (separate `add_executable` / `gtest_discover_tests` calls). Mixing test categories in one binary and applying multiple LABELS values is not supported — only the first LABELS value is applied. The one-label-per-target rule is enforced by convention; the CMake helper macro `aitown_add_tests()` (see below) documents and enforces this.

  **Label conventions**:
  - `unit`: pure logic tests — no Irrlicht device, no audio driver; all dependencies injected via mock/manual doubles
  - `integration`: tests that instantiate a real EDT_NULL Irrlicht device or wire multiple real (non-mock) subsystems together; no display required
  - `requires-opengl`: tests requiring a real OpenGL context under xvfb-run

  **Special case — Phase 1 compile-check test registered under `integration` label**: The `IrrlichtUIBackendCompileCheck::IsNonAbstract` test (in `tests/integration/irrlicht_ui_backend_compile_test.cpp`) is registered under the `integration` label at Phase 1 via the `integration_tests` CMake target. This is a deliberate special case and does NOT contradict the label convention above or the statement that "Phase 3 first registers integration tests with real domain assertions." The distinction is:

  - The Phase 1 test contains only a `static_assert` (compile-time non-abstract check) and a runtime `TEST() { SUCCEED(); }` body. It has no domain-specific runtime assertions, requires no Irrlicht device (EDT_NULL or otherwise), and requires no real audio backend. It is a compile-only verification test that lives in `tests/integration/` to co-locate it with the future integration test suite.
  - "Phase 3 first registers integration tests" refers specifically to tests with real domain assertions — tests that exercise multiple subsystems together using EDT_NULL Irrlicht and the null audio driver, requiring the full integration test infrastructure (MockRenderer, MockAudioSystem, CitySimulation wired to both) introduced in Phase 3.
  - The Phase 1 compile-check test satisfies the non-zero discovery requirement for `ctest -L '^integration$'`. Consequently, the integration routing verification step in `build-linux` and `coverage-linux` CAN be added in Phase 1 alongside this target — it will discover exactly 1 test and pass. The label routing verification step verifies label correctness (non-zero discovery), not phase-exclusion. Registering a compile-only test under `integration` at Phase 1 is a valid and intentional use of the label. Note: the original `integration_smoke_test.cpp` (Phase 0 bare `SUCCEED()` stub) has been removed; `irrlicht_ui_backend_compile_test.cpp` is the sole non-device test in `integration_tests`.

  See `headless-ci-testing.md` for the full `ctest` invocation commands and CI execution rules for each label.

  **`aitown_add_tests()` CMake helper macro** (defined in `cmake/AitownTestHelpers.cmake`): wraps `gtest_discover_tests()` with the correct required options, enforces the one-label rule, and provides per-category timeout overrides:

  ```cmake
  # cmake/AitownTestHelpers.cmake
  # Usage: aitown_add_tests(target_name LABEL <unit|integration|requires-opengl> [TIMEOUT <seconds>] [DISCOVERY_TIMEOUT <seconds>])
  macro(aitown_add_tests TARGET)
      cmake_parse_arguments(AITOWN_TEST "" "LABEL;TIMEOUT;DISCOVERY_TIMEOUT" "" ${ARGN})
      if(NOT AITOWN_TEST_LABEL)
          message(FATAL_ERROR "aitown_add_tests: LABEL is required (unit, integration, or requires-opengl)")
      endif()
      if(NOT AITOWN_TEST_TIMEOUT)
          set(AITOWN_TEST_TIMEOUT 120)  # default per-test timeout in seconds
      endif()
      if(NOT AITOWN_TEST_DISCOVERY_TIMEOUT)
          set(AITOWN_TEST_DISCOVERY_TIMEOUT 30)  # default discovery timeout in seconds
      endif()
      gtest_discover_tests(${TARGET}
          WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
          DISCOVERY_MODE PRE_TEST      # required on Windows — see DISCOVERY_MODE note above
          DISCOVERY_TIMEOUT ${AITOWN_TEST_DISCOVERY_TIMEOUT}
          PROPERTIES TIMEOUT ${AITOWN_TEST_TIMEOUT}
          LABELS "${AITOWN_TEST_LABEL}"
      )
  endmacro()

  # Terrain tests: longer timeouts for multi-seed generation property tests
  # aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)
  #
  # Standard unit tests:
  # aitown_add_tests(simulation_tests LABEL "unit" DISCOVERY_TIMEOUT 60)  # 60s: 8-file binary with RapidCheck property tests under coverage instrumentation
  # aitown_add_tests(ui_tests LABEL "unit" TIMEOUT 300)  # 300s: WorldInteraction_OverlayCap_100K_StillCalls (~225s unoptimized, coverage build)
  # aitown_add_tests(audio_tests LABEL "unit")
  #
  # Integration tests:
  # aitown_add_tests(integration_tests LABEL "integration")
  ```

  **Terrain test timeout**: Terrain generator tests (`tests/terrain/`) use a 300 s per-test timeout (overriding the default 120 s) because `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` runs up to 100 re-seed attempts for each RapidCheck shrinking iteration, and the multi-seed `TEST_F` cases (6 seeds × generation time) can exceed 120 s on coverage-instrumented CI runners. Call: `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)`. **Discovery timeout**: The default 30 s discovery timeout (configurable via `DISCOVERY_TIMEOUT` parameter) is necessary because coverage-instrumented binaries on loaded CI runners require more time to enumerate test cases; the default CMake value (5 s) is insufficient and silently produces 0 discovered tests. Terrain tests and simulation tests override to 60 s.

## Source Directory Structure

```text
src/
  simulation/    # economy, traffic, zoning, population — testable pure logic
  terrain/       # procedural generation — testable pure logic
  rendering/     # Irrlicht rendering adapter (excluded from coverage gate)
  audio/         # OpenAL audio adapter (excluded from coverage gate)
  platform/      # OS-specific code, file paths, window creation (excluded)
  ui/            # UIManager, UIScaler, NotificationManager, dialogs
tests/
  simulation/
  terrain/
  ui/
  audio/         # duck state machine, occlusion smoothing, crossfade, stinger milestone tests
  rendering/     # sRGB upload, setMesh grab/drop smoke test, shader compilation (requires-opengl label)
```

## CMake Test Target Decomposition

| CMake target | Source directory | Label | CTest filter | Notes |
|---|---|---|---|---|
| `simulation_tests` | `tests/simulation/` | `unit` | `-LE "integration\|requires-opengl"` | Economy, traffic, zoning, population, 60-tick, comprehensive sim, save-system tests; StrictMock/NiceMock; no display |
| `terrain_tests` | `tests/terrain/` | `unit` | `-LE "integration\|requires-opengl"` | Terrain generator property tests, fixed-seed regression, rebuild-queue deduplication; TIMEOUT 300 |
| `ui_tests` | `tests/ui/` | `unit` | `-LE "integration\|requires-opengl"` | UIManager, NotificationManager, CameraController, QueryPanel, ModalDialog; MockUIBackend |
| `audio_tests` | `tests/audio/` | `unit` | `-LE "integration\|requires-opengl"` | Duck state machine, occlusion smoothing, crossfade, stinger milestone tests; MockAudioSystem; no display required; `src/audio/` excluded from coverage gate |
| `integration_tests` | `tests/integration/` | `integration` | `-L "^integration$"` | Tests requiring a real EDT_NULL Irrlicht device or null audio driver; no xvfb needed |
| `opengl_tests` | `tests/rendering/` | `requires-opengl` | `-L "^requires-opengl$"` | sRGB upload, setMesh grab/drop smoke test, shader compilation; must run under `xvfb-run` |

**One CMake target per label** — do not mix labels within a target. `aitown_add_tests()` enforces this by requiring exactly one LABEL. Unit and integration tests must live in separate `add_executable` calls.

**`target_sources()` policy**: `opengl_tests` PROHIBITS `target_sources()` — all source files must be listed inline in `add_executable(opengl_tests ...)` to prevent ctest discovery timing issues caused by deferred source registration. `ui_tests` also PROHIBITS `target_sources()` during Phase 3 because Test-C1 requires the consolidated five-file `add_executable` call to be committed as a single amendment; `target_sources()` is PERMITTED for `ui_tests` from Phase 4 onward. All other targets PERMIT `target_sources()`, though inline listing in `add_executable` is preferred for readability.

**`ui_tests` Phase 4+ extension policy**: As new UI panel test files are added in Phase 4 and later (e.g. `tax_rate_panel_test.cpp`, `inspector_panel_test.cpp`), they MUST be appended via `target_sources(ui_tests PRIVATE tests/ui/<new_file>.cpp)` in their own CMake block — NOT by editing the top-level `add_executable(ui_tests ...)` call in the root `CMakeLists.txt`. This keeps the `ui_tests` target self-contained: each phase's additions are localized to a dedicated `target_sources()` call that can be reviewed in isolation. Modifying the root `CMakeLists.txt` `add_executable` line for every new UI test file creates unnecessary churn in a high-traffic file and risks merge conflicts. The `target_sources()` call must appear in the same `CMakeLists.txt` scope as the original `add_executable(ui_tests ...)` call (i.e., the root `CMakeLists.txt` or a dedicated `tests/ui/CMakeLists.txt` included from root), not in a subdirectory `CMakeLists.txt` where the target may not be in scope.

| CMake Target | `target_sources()` Permitted? | Notes |
|---|---|---|
| `opengl_tests` | PROHIBITED — inline `add_executable` only | Prevents ctest discovery timing issues from deferred source registration |
| `ui_tests` | PROHIBITED for Phase 3 (Test-C1); PERMITTED thereafter via `target_sources(ui_tests PRIVATE ...)` | Phase 3 requires consolidated 5-file `add_executable` committed as a single amendment; Phase 4+ additions MUST use `target_sources(ui_tests PRIVATE ...)` — never modify the root CMakeLists.txt |
| `simulation_tests` | PERMITTED — inline listing preferred | |
| `audio_tests` | PERMITTED — inline listing preferred | Phase 0 creates target with `audio_smoke_test.cpp` inline; Phase 7 MUST extend via `target_sources(audio_tests PRIVATE ...)` — do NOT re-call `add_executable(audio_tests ...)` (duplicate target causes CMake configure error). Phase 7 adds 4 source files: `duck_state_machine_test.cpp`, `occlusion_smoothing_test.cpp`, `audio_thread_test.cpp`, `ogg_header_validation_test.cpp`. Phase 10 further extends via `target_sources(audio_tests PRIVATE ...)` with 4 additional source files: `crossfade_interrupted_formula_test.cpp`, `stinger_milestone_test.cpp`, `audio_stream_bar_boundary_test.cpp`, `notification_sfx_efx_bypass_test.cpp` — do NOT re-call `add_executable(audio_tests ...)` or `aitown_add_tests(audio_tests ...)` (duplicate target). |
| `terrain_tests` | PERMITTED — inline listing preferred | |
| `integration_tests` | PERMITTED — inline listing preferred; Phase 10c onward MUST use `target_sources(integration_tests PRIVATE ...)` — do NOT re-call `add_executable(integration_tests ...)` (duplicate target causes CMake configure error) | |

```cmake
# Example CMakeLists.txt registration:
add_executable(simulation_tests tests/simulation/economy_test.cpp ...)
target_link_libraries(simulation_tests PRIVATE aitown_sim GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
# src/simulation/ is REQUIRED: simulation test files use #include "simulation_constants.h" and other
# simulation headers (e.g. simulation_types.h, city_simulation.h) via project-relative paths that
# resolve relative to src/simulation/. Without this entry, the compiler cannot find these headers
# and the build fails with "fatal error: simulation_constants.h: No such file or directory".
# Do NOT remove src/simulation/ from this list even if no current test file directly includes
# simulation headers — future test files in tests/simulation/ will need it, and the include path
# must be present before first use to avoid a retroactive CMakeLists.txt amendment.
target_include_directories(simulation_tests PRIVATE
    tests/simulation/ src/interfaces/ src/simulation/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(simulation_tests LABEL "unit")
# Phase 3 exit criterion: All 6 ManualRNG self-tests must pass.
# These tests live in tests/simulation/manual_rng_test.cpp and are registered
# under the simulation_tests target. The canonical test names are:
#   1. ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision
#   2. ManualRNG_VerifyAllConsumed_NoThrowWhenFullyConsumed
#   3. ManualRNG_EmptyIntSeq_ThrowsAtConstruction
#   4. ManualRNG_FloatSeqOutOfRange_ThrowsAtConstruction
#   5. ManualRNG_EmptyFloatSeq_ThrowsAtConstruction
#   6. ManualRNG_NextInt_OutOfRange_ThrowsAtCallTime
# Verify with:
#   ctest --test-dir build -LE 'integration|requires-opengl' -R ManualRNG --output-on-failure
# All 6 must appear and pass. A test runner that shows fewer than 6 results indicates
# a missing test body, a mismatched test name, or a CMake registration error.

add_executable(terrain_tests tests/terrain/terrain_generator_test.cpp ...)
# rapidcheck and rapidcheck_gtest are included from the start: Phase 5 adds
# TerrainGenerator_AlwaysTerminates_WithinReSeedLimit which uses rc::check from rapidcheck_gtest.
# Linking RapidCheck at Phase 0 means Phase 5 can add property tests without retroactively
# editing CMakeLists.txt.
target_link_libraries(terrain_tests PRIVATE aitown_terrain GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
target_include_directories(terrain_tests PRIVATE
    tests/simulation/ tests/terrain/ src/terrain/ src/rendering/ src/interfaces/ ${CMAKE_SOURCE_DIR})
# Phase 10b: after ITerrainRNG.h moves to src/interfaces/ (Feature 3), src/terrain/ may be
# dropped from this list — verify terrain_tests still builds cleanly after the removal.
aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)
# Phase 3 prerequisite: `terrain_stub.cpp` references `#include "src/terrain/terrain_chunk.h"`.
# This header does not exist as a full implementation until Phase 5. Phase 3 MUST create a
# minimal stub at `src/terrain/terrain_chunk.h` containing:
#
#   #pragma once
#   // Phase 3 stub -- Phase 5 replaces with full TerrainChunk implementation
#   class TerrainChunk {};
#
# This stub is required to make `terrain_tests` compile in Phase 3. Without it, the
# `target_include_directories(terrain_tests PRIVATE src/terrain/ ...)` path cannot be
# exercised and verified.

# Phase 0: initial target creation (smoke test only)
add_executable(audio_tests tests/audio/audio_smoke_test.cpp)
target_link_libraries(audio_tests PRIVATE aitown_audio GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
target_include_directories(audio_tests PRIVATE tests/simulation/ src/interfaces/ src/audio/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(audio_tests LABEL "unit")
# Phase 7: extend via target_sources — do NOT re-call add_executable(audio_tests) or
# aitown_add_tests(audio_tests) — duplicate target registration causes a CMake configure error.
# Vorbis::vorbisfile (vcpkg port libvorbis, header <vorbis/vorbisfile.h>) is required because
# Phase 7 audio tests call ov_fopen(), ov_read(), and ov_pcm_total() directly for OGG header
# validation. stb_vorbis is not used; Vorbis::vorbisfile is the sole OGG decode library.
# rapidcheck/rapidcheck_gtest already linked at Phase 0 — duplicate entries in CMake are harmless.
target_sources(audio_tests PRIVATE
    tests/audio/duck_state_machine_test.cpp
    tests/audio/occlusion_smoothing_test.cpp
    tests/audio/audio_thread_test.cpp
    tests/audio/ogg_header_validation_test.cpp)
target_link_libraries(audio_tests PRIVATE Vorbis::vorbisfile)

# Phase 0 / Phase 5 coexistence note: multiple .cpp source files may coexist in the same
# CMake target. During Phase 0, a stub file (e.g. tests/rendering/stub_succeed.cpp containing
# a single TEST that calls SUCCEED()) keeps the target buildable before real test files exist.
# In Phase 5 and later, the real test files (e.g. `srgb_upload_test.cpp`) are
# added alongside the stub.
#
# lod_swap_smoke_test.cpp phase plan:
#   Phase 0/1: add_executable(opengl_tests ...) contains stub_succeed.cpp AND
#              shader_stub_compile_test.cpp. lod_swap_smoke_test.cpp does NOT exist yet
#              and must NOT appear in the source list — a missing source file causes
#              cmake --build to fail at configure time.
#   Phase 2:   tests/rendering/lod_swap_smoke_test.cpp is CREATED and registered in
#              add_executable(opengl_tests ...) below (see implementation/phase-2.md).
#              The file is created simultaneously with the CMakeLists.txt change — the
#              file exists, so cmake --build does not fail at configure time. The test
#              body contains only GTEST_SKIP() (no real OpenGL logic yet) so the test
#              target remains buildable without a display.
#   Phase 5:   lod_swap_smoke_test.cpp body promoted to a real OpenGL test after the LOD
#              spike work is complete (see implementation/phase-5.md).
#
# The stub file may be removed once real tests cover the target, but its
# presence does not cause any build or link errors — both .cpp files are compiled and linked
# into the same test binary and all discovered tests run under the same CTest entry.
add_executable(opengl_tests
    tests/rendering/stub_succeed.cpp        # Phase 0/1: only these two files exist
    tests/rendering/shader_stub_compile_test.cpp
    # tests/rendering/lod_swap_smoke_test.cpp  -- added in Phase 2 with GTEST_SKIP() body;
    #                                             promoted to real OpenGL test in Phase 5
    # tests/rendering/cloud_plane_test.cpp   -- added INLINE here in Phase 10b;
    #                                           target_sources() PROHIBITED for opengl_tests
)
target_link_libraries(opengl_tests PRIVATE aitown_render GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
# src/rendering/ required for Phase 5 lod_swap_smoke_test.cpp (full body) which needs scene-graph and mesh buffer headers.
target_include_directories(opengl_tests PRIVATE
    src/interfaces/
    src/rendering/
    ${CMAKE_SOURCE_DIR})
aitown_add_tests(opengl_tests LABEL "requires-opengl")

# The _compile_test suffix captures the primary purpose of this file:
# it contains BOTH a static_assert (compile-time non-abstract check) AND a runtime
# TEST(IrrlichtUIBackendCompileCheck, IsNonAbstract) { SUCCEED(); } body.
# The TEST() body is REQUIRED so that gtest_discover_tests() registers at least 1 test
# under the "integration" label — a compile-only file with no TEST() cases produces 0
# discovered tests, which causes ctest -L "^integration$" to report no tests and the
# CI integration step to produce an empty (misleading) result.
# It verifies IrrlichtUIBackend is non-abstract at compile time without requiring a display.
#
# Phase 0 baseline state: integration_tests linked only GTest::gtest_main, GTest::gmock,
# rapidcheck, and rapidcheck_gtest — aitown_render and aitown_ui did not exist at Phase 0.
# aitown_add_tests(integration_tests LABEL "integration") was also absent at Phase 0
# (pre-dates aitown_add_tests usage on this target; Phase 0 used a raw gtest_discover_tests
# call or no macro at all). Phase 1 adds aitown_render, aitown_ui, the src/rendering/
# include path, and the aitown_add_tests call when IrrlichtUIBackendCompileCheck is
# registered. See implementation/phase-1.md — "IrrlichtUIBackend Non-Abstract Compile
# Check" task for the atomicity requirement.
add_executable(integration_tests tests/integration/irrlicht_ui_backend_compile_test.cpp)
target_link_libraries(integration_tests PRIVATE
    aitown_render aitown_ui
    GTest::gtest_main GTest::gmock
    rapidcheck rapidcheck_gtest)
target_include_directories(integration_tests PRIVATE
    tests/simulation/ tests/ui/ src/interfaces/ src/ui/ src/rendering/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(integration_tests LABEL "integration")
```
