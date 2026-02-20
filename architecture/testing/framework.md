# Framework

- **Google Test (GTest) + GMock** via CMake `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571  # v1.14.0 — SHA-pinned; never use the mutable tag string (it can be force-pushed). The CI supply-chain lint does NOT check CMakeLists.txt GIT_TAG values — this must be manually maintained. Verify: gh release view v1.14.0 --repo google/googletest --json tagName,targetCommitish
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # required on Windows
FetchContent_MakeAvailable(googletest)

# Each test target links both:
target_link_libraries(my_test PRIVATE GTest::gtest_main GTest::gmock)
```

- **RapidCheck** (property-based testing) is a firm dependency, also via `FetchContent`:

```cmake
FetchContent_Declare(
  rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG        b96a4e626ef4c7348dcd16c500353c2f997a9f3f  # pinned SHA — no versioned tag available
)
set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidcheck)

# Link RapidCheck GTest integration:
target_link_libraries(my_test PRIVATE rapidcheck rapidcheck_gtest)
```

- **SHA pin is mandatory** — RapidCheck has no stable release tags; always pin to a specific commit SHA. Update intentionally; do not use HEAD or branch refs.
- All tests in a `tests/` directory mirroring `src/` structure; `enable_testing()` + `gtest_discover_tests()` enabled
- **`gtest_discover_tests()` must specify `WORKING_DIRECTORY`, `DISCOVERY_TIMEOUT`, `PROPERTIES TIMEOUT`, and test `LABELS`**:

  ```cmake
  gtest_discover_tests(my_test
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"  # REQUIRED — see note below
      DISCOVERY_TIMEOUT 30    # seconds; default 5s is insufficient for coverage-instrumented binaries
      PROPERTIES TIMEOUT 120  # per-test execution timeout; RapidCheck properties can run for seconds
      LABELS "unit"           # or "integration", "requires-opengl" — applied to ALL discovered tests in this target
  )
  ```

  Without `DISCOVERY_TIMEOUT 30`, CMake test discovery times out on coverage-instrumented (`-DENABLE_COVERAGE=ON`) binaries on loaded CI runners, silently producing 0 discovered tests and a misleading empty-coverage lcov report. Apply to ALL test targets.

  **`WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` is mandatory.** Without it, `gtest_discover_tests` defaults to `CMAKE_CURRENT_BINARY_DIR` (the build directory). When tests run from the build directory, `GTEST_OUTPUT=xml:test_results/` writes XML to `build/test_results/` instead of `<workspace>/test_results/`. The CI step that collects XML (`test_results/*.xml`) looks in the workspace root on both Linux and Windows, so an incorrect working directory silently produces zero XML files and causes `dorny/test-reporter` to fail with "No test report files were found". This is especially visible on Windows multi-config MSBuild builds where the executable lives in `build/Release/` and the default working directory could be even further from the workspace root.

  **LABELS MUST be set inside `gtest_discover_tests()`, NOT via `set_tests_properties()` afterwards.** `gtest_discover_tests()` dynamically creates CTest test entries at configure time; calling `set_tests_properties()` after `gtest_discover_tests()` targets the statically-created wrapper test, not the individually-discovered test cases — the labels do not propagate to discovered tests and `-L`/`-LE` ctest filters will silently fail to include or exclude the correct tests. The `LABELS` keyword inside `gtest_discover_tests()` is the only reliable way to assign labels to all auto-discovered GTest cases.

  **One label per target rule**: `gtest_discover_tests()` applies a single LABELS value to ALL tests in the target. A test binary cannot have some tests labeled `unit` and others labeled `integration`. **Unit and integration tests MUST be in separate CMake targets** (separate `add_executable` / `gtest_discover_tests` calls). Mixing test categories in one binary and applying multiple LABELS values is not supported — only the first LABELS value is applied. The one-label-per-target rule is enforced by convention; the CMake helper macro `aitown_add_tests()` (see below) documents and enforces this.

  **Label conventions**:
  - `unit`: pure logic tests, no display or audio required
  - `integration`: tests requiring EDT_NULL Irrlicht + null audio
  - `requires-opengl`: tests requiring a real OpenGL context under xvfb-run

  See `headless-ci-testing.md` for the full `ctest` invocation commands and CI execution rules for each label.

  **`aitown_add_tests()` CMake helper macro** (defined in `cmake/AitownTestHelpers.cmake`): wraps `gtest_discover_tests()` with the correct required options, enforces the one-label rule, and provides per-category timeout overrides:

  ```cmake
  # cmake/AitownTestHelpers.cmake
  # Usage: aitown_add_tests(target_name LABEL <unit|integration|requires-opengl> [TIMEOUT <seconds>])
  macro(aitown_add_tests TARGET)
      cmake_parse_arguments(AITOWN_TEST "" "LABEL;TIMEOUT" "" ${ARGN})
      if(NOT AITOWN_TEST_LABEL)
          message(FATAL_ERROR "aitown_add_tests: LABEL is required (unit, integration, or requires-opengl)")
      endif()
      if(NOT AITOWN_TEST_TIMEOUT)
          set(AITOWN_TEST_TIMEOUT 120)  # default per-test timeout
      endif()
      gtest_discover_tests(${TARGET}
          WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
          DISCOVERY_TIMEOUT 30
          PROPERTIES TIMEOUT ${AITOWN_TEST_TIMEOUT}
          LABELS "${AITOWN_TEST_LABEL}"
      )
  endmacro()

  # Terrain tests: longer timeout for multi-seed generation property tests
  # aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300)
  #
  # Standard unit tests:
  # aitown_add_tests(simulation_tests LABEL "unit")
  # aitown_add_tests(ui_tests LABEL "unit")
  #
  # Integration tests:
  # aitown_add_tests(integration_tests LABEL "integration")
  ```

  **Terrain test timeout**: Terrain generator tests (`tests/terrain/`) use a 300 s per-test timeout (overriding the default 120 s) because `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` runs up to 100 re-seed attempts for each RapidCheck shrinking iteration, and the multi-seed `TEST_F` cases (6 seeds × generation time) can exceed 120 s on coverage-instrumented CI runners. Call: `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300)`.

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
| `simulation_tests` | `tests/simulation/` | `unit` | `-LE "integration\|requires-opengl"` | Economy, traffic, zoning, population tests; StrictMock/NiceMock; no display |
| `terrain_tests` | `tests/terrain/` | `unit` | `-LE "integration\|requires-opengl"` | Terrain generator property tests and fixed-seed regression; TIMEOUT 300 |
| `ui_tests` | `tests/ui/` | `unit` | `-LE "integration\|requires-opengl"` | UIManager, NotificationManager, CameraController, QueryPanel, ModalDialog; MockUIBackend |
| `audio_tests` | `tests/audio/` | `unit` | `-LE "integration\|requires-opengl"` | Duck state machine, occlusion smoothing, crossfade, stinger milestone tests; MockAudioSystem; no display required; `src/audio/` excluded from coverage gate |
| `integration_tests` | `tests/integration/` | `integration` | `-L "^integration$"` | Multi-subsystem tests with EDT_NULL Irrlicht + null audio driver; no xvfb needed |
| `opengl_tests` | `tests/rendering/` | `requires-opengl` | `-L "^requires-opengl$"` | sRGB upload, setMesh grab/drop smoke test, shader compilation; must run under `xvfb-run` |

**One CMake target per label** — do not mix labels within a target. `aitown_add_tests()` enforces this by requiring exactly one LABEL. Unit and integration tests must live in separate `add_executable` calls.

```cmake
# Example CMakeLists.txt registration:
add_executable(simulation_tests tests/simulation/economy_test.cpp ...)
target_link_libraries(simulation_tests PRIVATE aitown_sim GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
aitown_add_tests(simulation_tests LABEL "unit")

add_executable(terrain_tests tests/terrain/terrain_generator_test.cpp ...)
# rapidcheck and rapidcheck_gtest are included from the start: Phase 2 adds
# TerrainGenerator_AlwaysTerminates_WithinReSeedLimit which uses rc::check from rapidcheck_gtest.
# Linking RapidCheck at Phase 0 means Phase 2 can add property tests without retroactively
# editing CMakeLists.txt.
target_link_libraries(terrain_tests PRIVATE aitown_terrain GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300)

add_executable(audio_tests tests/audio/duck_state_test.cpp ...)
# rapidcheck and rapidcheck_gtest are included proactively: removing them later is trivial,
# but omitting them causes a confusing link failure if a property test is added to audio_tests.
# Vorbis::vorbisfile (vcpkg port libvorbis, header <vorbis/vorbisfile.h>) is required because
# Phase 4 audio tests call ov_fopen(), ov_read(), and ov_pcm_total() directly for OGG header
# validation. stb_vorbis is not used; Vorbis::vorbisfile is the sole OGG decode library.
target_link_libraries(audio_tests PRIVATE aitown_audio Vorbis::vorbisfile GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
target_include_directories(audio_tests PRIVATE tests/simulation/ src/interfaces/ src/audio/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(audio_tests LABEL "unit")

# Phase 0 / Phase 6 coexistence note: multiple .cpp source files may coexist in the same
# CMake target. During Phase 0, a stub file (e.g. tests/rendering/stub_succeed.cpp containing
# a single TEST that calls SUCCEED()) keeps the target buildable before real test files exist.
# In Phase 6 and later, the real test files (e.g. `srgb_upload_test.cpp`) are
# added alongside the stub.
#
# lod_swap_smoke_test.cpp phase plan:
#   Phase 1: tests/rendering/lod_swap_smoke_test.cpp is CREATED with a SUCCEED() placeholder body
#            and registered in add_executable(opengl_tests ...) below. The file is created
#            simultaneously with the CMakeLists.txt change — the file exists, so cmake --build
#            does not fail at configure time.
#   Phase 6: The REAL TEST BODY is filled in after the LOD spike is complete. The Phase 6
#            restriction applies to the real test body, NOT to the file creation or CMake
#            registration. The placeholder file may remain in CMakeLists.txt unchanged from
#            Phase 1 through Phase 5; only the .cpp contents change in Phase 6.
#
# The stub file may be removed once real tests cover the target, but its
# presence does not cause any build or link errors — both .cpp files are compiled and linked
# into the same test binary and all discovered tests run under the same CTest entry.
add_executable(opengl_tests
    tests/rendering/stub_succeed.cpp
    tests/rendering/shader_stub_compile_test.cpp
    tests/rendering/lod_swap_smoke_test.cpp
)
target_link_libraries(opengl_tests PRIVATE aitown_render GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)
target_include_directories(opengl_tests PRIVATE src/interfaces/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(opengl_tests LABEL "requires-opengl")

add_executable(integration_tests tests/integration/irrlicht_ui_backend_test.cpp)
target_link_libraries(integration_tests PRIVATE
    aitown_render aitown_ui
    GTest::gtest_main GTest::gmock
    rapidcheck rapidcheck_gtest)
target_include_directories(integration_tests PRIVATE
    tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})
aitown_add_tests(integration_tests LABEL "integration")
```
