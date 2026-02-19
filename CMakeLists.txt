cmake_minimum_required(VERSION 3.21)
project(aitown VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# FETCHCONTENT BASE DIR — must be outside build tree to avoid cache invalidation
# when the build directory is cleared. This ensures the FetchContent cache key
# in CI (keyed on .fetchcontent_cache path, not build/_deps) works correctly.
# ============================================================================
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent_cache"
    CACHE PATH "FetchContent download directory (outside build tree)")

# ============================================================================
# COVERAGE SUPPORT
# ============================================================================
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation (Linux only)" OFF)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Coverage instrumentation enabled")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# ============================================================================
# CMAKE MODULE PATH — must be set before find_package calls so FindIrrlicht.cmake
# fallback module is found if the vcpkg port does not ship a config-mode package.
# ============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ============================================================================
# VCPKG INTEGRATION
# CMake toolchain file is specified via:
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
# ============================================================================

# ============================================================================
# FIND PACKAGES
# ============================================================================

# Irrlicht: SPIKE RESOLVED — target name is Irrlicht (no namespace).
# Architecture Decision:
#   Irrlicht CMake target name: Irrlicht
#   Verified by inspecting the upstream CMakeLists.txt used by the vcpkg irrlicht port
#   (adrido/irrlicht-vcpkg, consumed by microsoft/vcpkg at baseline f7423ee):
#     add_library(Irrlicht ...)
#     install(TARGETS Irrlicht EXPORT Irrlicht ...)
#     install(EXPORT Irrlicht FILE irrlicht-targets.cmake DESTINATION share/irrlicht)
#   No NAMESPACE is declared in install(EXPORT ...), so the exported target in
#   irrlicht-targets.cmake is the bare name Irrlicht, not Irrlicht::Irrlicht.
#   Sources: adrido/irrlicht-vcpkg CMakeLists.txt (raw fetch confirmed);
#            microsoft/vcpkg irrlicht portfile (vcpkg_fixup_cmake_targets does NOT add namespaces);
#            irrlicht.sourceforge.io forum post confirming target_link_libraries(main Irrlicht).
find_package(Irrlicht REQUIRED)

# OpenAL Soft: required for aitown_audio
find_package(OpenAL REQUIRED)

# Vorbis: vcpkg port name is "libvorbis" (NOT "libvorbisfile" — no such port exists).
# "libvorbis" installs both libvorbis and libvorbisfile; CMake target is Vorbis::vorbisfile.
find_package(Vorbis REQUIRED)

# ============================================================================
# FETCHCONTENT — GTest and RapidCheck
# All FetchContent_Declare and FetchContent_MakeAvailable calls MUST appear ONLY
# in top-level CMakeLists.txt or files under cmake/. Placing them in subdirectory
# CMakeLists.txt files causes FETCHCONTENT_BASE_DIR to be evaluated relative to
# the subdirectory, defeating the .fetchcontent_cache placement.
# ============================================================================
include(FetchContent)

# Google Test v1.14.0 — SHA-pinned commit (NEVER use the mutable tag string "v1.14.0"
# as GIT_TAG — it can be force-pushed; always use the full 40-character commit SHA).
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571  # v1.14.0
)
# Required on Windows to prevent CRT mismatch (MSVC shared vs. static CRT).
# Must appear AFTER FetchContent_Declare and BEFORE FetchContent_MakeAvailable.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# RapidCheck — pinned SHA (no versioned tag available; always pin to specific commit SHA)
FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        b96a4e626ef4c7348dcd16c500353c2f997a9f3f  # pinned SHA
)
# RC_ENABLE_GTEST enables the rapidcheck_gtest CMake target.
# Omitting it means rapidcheck_gtest is unavailable at configure time and all
# simulation/audio tests that use rc::gtest::check fail to configure.
# Must appear AFTER FetchContent_Declare and BEFORE FetchContent_MakeAvailable.
set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidcheck)

# ============================================================================
# TESTING INFRASTRUCTURE
# ============================================================================
enable_testing()
include(GoogleTest)
include(cmake/AitownTestHelpers.cmake)

# ============================================================================
# LIBRARY STUBS
# ============================================================================

# ----------------------------------------------------------------------------
# aitown_render — Irrlicht rendering adapter
# Links Irrlicht (PRIVATE scope — Irrlicht headers must NOT propagate to test targets).
# Phase 1 replaces placeholder.cpp with RenderSystem.cpp.
# ----------------------------------------------------------------------------
add_library(aitown_render STATIC
    src/rendering/placeholder.cpp
)
target_include_directories(aitown_render
    PRIVATE   src/rendering/       # RenderSystem-internal headers (Phase 1+)
    PRIVATE   src/interfaces/      # needed for RenderSystem.cpp's own includes
    INTERFACE src/interfaces/      # propagates to consumers (test targets, etc.)
)
# AITOWN_ASSETS_DIR — absolute path expanded at configure time.
# Correct for both Linux and Windows. Using CMAKE_CURRENT_SOURCE_DIR is incorrect
# if the target is configured from a subdirectory.
target_compile_definitions(aitown_render PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
# SPIKE RESOLVED: Irrlicht CMake target name is Irrlicht (no namespace).
# Verified via adrido/irrlicht-vcpkg CMakeLists.txt: install(EXPORT Irrlicht ...) without NAMESPACE.
# PRIVATE scope is mandatory — Irrlicht headers must NOT propagate to test targets.
# Phase 1 replaces placeholder.cpp with RenderSystem.cpp.
target_link_libraries(aitown_render PRIVATE Irrlicht)

# ----------------------------------------------------------------------------
# aitown_audio — OpenAL Soft audio adapter
# PRIVATE scope for OpenAL and Vorbis is required: PUBLIC would propagate their
# headers transitively to all test targets that link aitown_audio, violating the
# IAudioSystem interface's contract ("no OpenAL include dependencies").
# Phase 4 replaces placeholder.cpp with AudioSystem.cpp.
# ----------------------------------------------------------------------------
add_library(aitown_audio STATIC
    src/audio/placeholder.cpp
)
target_include_directories(aitown_audio
    PRIVATE   src/audio/           # audio_constants.h and AudioSystem-internal headers
    PRIVATE   src/interfaces/      # needed for AudioSystem.cpp's own includes (IClock, etc.)
    INTERFACE src/interfaces/      # propagates to consumers
)
target_compile_definitions(aitown_audio PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
# PRIVATE scope: OpenAL and Vorbis headers must NOT propagate to test targets.
# Omitting either library causes a cmake --build build link failure on Linux and Windows.
target_link_libraries(aitown_audio PRIVATE
    OpenAL::OpenAL
    Vorbis::vorbisfile
)

# ----------------------------------------------------------------------------
# aitown_sim — City simulation (economy, traffic, zoning, population)
# Phase 3 replaces placeholder.cpp with CitySimulation.cpp.
# ----------------------------------------------------------------------------
add_library(aitown_sim STATIC
    src/simulation/placeholder.cpp
)
target_include_directories(aitown_sim
    PRIVATE   src/simulation/      # implementation headers — NOT propagated
    PRIVATE   src/interfaces/      # needed for CitySimulation.cpp's own includes
                                   # (IClock, IAudioSystem, ISimulationRNG, ISimulationPauser, ICitySimulation)
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
)

# ----------------------------------------------------------------------------
# aitown_ui — UIManager, UIScaler, NotificationManager, dialogs
# Phase 1 replaces placeholder.cpp with UIManager.cpp.
# Note: IUIBackend.h lives in src/ui/ and is propagated via INTERFACE src/ui/.
# Consumers linking aitown_ui can include IUIBackend.h via the project-root-relative
# form #include "src/ui/IUIBackend.h" as long as ${CMAKE_SOURCE_DIR} is on their
# include path (always present via the canonical include pattern).
# ----------------------------------------------------------------------------
add_library(aitown_ui STATIC
    src/ui/placeholder.cpp
    src/ui/CameraController.cpp    # Phase 1 implements body; placeholder at Phase 0
)
target_include_directories(aitown_ui
    PRIVATE   src/ui/              # implementation headers
    PRIVATE   src/interfaces/      # needed for UIManager.cpp's own includes
                                   # (IClock, ICitySimulation, IAudioSystem, ISimulationPauser)
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
    INTERFACE src/ui/              # IUIBackend.h is a consumer-visible interface header
)

# ----------------------------------------------------------------------------
# aitown_terrain — Terrain generator (Phase 0 stub)
# Phase 2 adds TerrainGenerator.cpp and ITerrainRNG.h.
# ----------------------------------------------------------------------------
add_library(aitown_terrain STATIC
    src/terrain/placeholder.cpp
)
target_include_directories(aitown_terrain
    PRIVATE   src/terrain/         # implementation headers — NOT propagated
    PRIVATE   src/interfaces/      # needed for TerrainGenerator.cpp's own includes (IClock, etc.)
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
)

# ============================================================================
# TEST TARGETS
# Each test target links both GTest and RapidCheck (proactively — removing later
# is trivial; omitting causes a link failure if property tests are added later).
# target_include_directories cross-target wiring documented per shared-mock pattern
# in architecture/testing/testability-architecture.md.
# ============================================================================

# ----------------------------------------------------------------------------
# simulation_tests — economy, traffic, zoning, population; no display required
# ----------------------------------------------------------------------------
add_executable(simulation_tests
    tests/simulation/simulation_smoke_test.cpp
)
target_link_libraries(simulation_tests PRIVATE
    aitown_sim
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# tests/simulation/ — owns shared mock headers (mock_audio_system.h, mock_renderer.h,
#                     manual_clock.h, manual_rng.h); must be on the include path.
# src/interfaces/ — for project-root-relative includes like #include "src/interfaces/IClock.h"
# ${CMAKE_SOURCE_DIR} — for project-root-relative includes
target_include_directories(simulation_tests PRIVATE
    tests/simulation/
    src/interfaces/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(simulation_tests LABEL "unit")

# ----------------------------------------------------------------------------
# terrain_tests — terrain generator property tests and fixed-seed regression
# TIMEOUT 300: multi-seed property tests can exceed 120 s on coverage-instrumented runners
# ----------------------------------------------------------------------------
add_executable(terrain_tests
    tests/terrain/terrain_smoke_test.cpp
)
target_link_libraries(terrain_tests PRIVATE
    aitown_terrain
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# tests/simulation/ — for ManualClock (if timing tests added in Phase 2+)
# tests/terrain/   — for MockTerrainRNG
# src/terrain/     — for ITerrainRNG.h (included by mock_terrain_rng.h)
# ${CMAKE_SOURCE_DIR} — for project-root-relative includes
target_include_directories(terrain_tests PRIVATE
    tests/simulation/
    tests/terrain/
    src/terrain/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300)

# ----------------------------------------------------------------------------
# ui_tests — UIManager, NotificationManager, CameraController, QueryPanel, ModalDialog
# ----------------------------------------------------------------------------
add_executable(ui_tests
    tests/ui/ui_smoke_test.cpp
)
target_link_libraries(ui_tests PRIVATE
    aitown_ui
    aitown_sim         # MockCitySimulation includes ICitySimulation.h which includes simulation_types.h
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# tests/simulation/ — MockAudioSystem, MockRenderer, ManualClock from tests/simulation/
# tests/ui/         — MockUIBackend, MockCitySimulation, MockSimulationPauser
# src/interfaces/   — interface headers
# src/ui/           — IUIBackend.h and other UI headers
# ${CMAKE_SOURCE_DIR} — project-root-relative includes
target_include_directories(ui_tests PRIVATE
    tests/simulation/
    tests/ui/
    src/interfaces/
    src/ui/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(ui_tests LABEL "unit")

# ----------------------------------------------------------------------------
# audio_tests — duck state machine, occlusion smoothing, crossfade, stinger milestone tests
# Vorbis::vorbisfile must be listed EXPLICITLY — it CANNOT resolve through aitown_audio's
# PRIVATE scope, which deliberately does NOT propagate to consumers.
# ----------------------------------------------------------------------------
add_executable(audio_tests
    tests/audio/audio_smoke_test.cpp
)
target_link_libraries(audio_tests PRIVATE
    aitown_audio
    Vorbis::vorbisfile   # must be explicit — PRIVATE scope in aitown_audio does not propagate
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# tests/simulation/ — MockAudioSystem and ManualClock
# src/interfaces/   — IAudioSystem.h and other interface headers
# src/audio/        — audio_constants.h
# ${CMAKE_SOURCE_DIR} — project-root-relative includes
target_include_directories(audio_tests PRIVATE
    tests/simulation/
    src/interfaces/
    src/audio/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(audio_tests LABEL "unit")

# ----------------------------------------------------------------------------
# integration_tests — multi-subsystem tests with EDT_NULL Irrlicht + null audio
# ----------------------------------------------------------------------------
add_executable(integration_tests
    tests/integration/integration_smoke_test.cpp
)
target_link_libraries(integration_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# Shared mock paths must be present from Phase 0 even though Phase 0 stub does not use mocks.
# Omitting requires retroactive CMakeLists.txt edits when Phase 1+ integration tests exercise
# UI paths and reference IUIBackend.h and mock headers.
target_include_directories(integration_tests PRIVATE
    tests/simulation/
    tests/ui/
    src/interfaces/
    src/ui/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(integration_tests LABEL "integration")

# ----------------------------------------------------------------------------
# opengl_tests — sRGB upload, setMesh grab/drop smoke test, shader compilation
# Must run under xvfb-run on Linux.
# Phase 0: ONLY stub_succeed.cpp in source list.
# lod_swap_smoke_test.cpp is a Phase 6 deliverable — DO NOT add before Phase 6.
# ----------------------------------------------------------------------------
add_executable(opengl_tests
    tests/rendering/stub_succeed.cpp       # Phase 0 stub — remove when real tests cover target
    # tests/rendering/lod_swap_smoke_test.cpp  # Phase 6 and later — uncomment when file exists
)
target_link_libraries(opengl_tests PRIVATE
    aitown_render
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# src/interfaces/   — IRenderer.h propagated via INTERFACE from aitown_render, but
#                     explicit belt-and-suspenders path ensures #include "vec3.h" inside
#                     IRenderer.h resolves even if aitown_render's INTERFACE scope is refactored.
# ${CMAKE_SOURCE_DIR} — project-root-relative includes
target_include_directories(opengl_tests PRIVATE
    src/interfaces/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(opengl_tests LABEL "requires-opengl")
