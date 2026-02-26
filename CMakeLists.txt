cmake_minimum_required(VERSION 3.21)
project(aitown VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# fmt: required because openal-soft is built with the devendor-fmt vcpkg patch, which
# replaces its bundled copy of fmt with the external vcpkg fmt package.  libopenal.a
# therefore references fmt symbols (e.g. fmt::v12::report_error) that must be satisfied
# at link time.  Linking fmt::fmt PRIVATE to aitown_audio satisfies those symbols for
# every target that links aitown_audio (including all audio_tests).
find_package(fmt CONFIG REQUIRED)

# Vorbis: vcpkg port name is "libvorbis" (NOT "libvorbisfile" — no such port exists).
# "libvorbis" installs both libvorbis and libvorbisfile; CMake target is Vorbis::vorbisfile.
find_package(Vorbis REQUIRED)

# GLEW: required for RenderSystem::glewInit() call immediately after createDevice().
# GLEW::GLEW MUST be linked BEFORE Irrlicht in aitown_render to avoid symbol duplication
# (GLEW symbol duplication mitigation-2 — Irrlicht bundles its own GLEW subset;
# linking ours first ensures our symbols win the linker conflict).
find_package(GLEW REQUIRED)

# Irrlicht transitive dependencies (Linux static link only).
# The adrido/irrlicht-vcpkg port builds Irrlicht as a STATIC library on Linux.
# Its CMake exported target does NOT declare INTERFACE_LINK_LIBRARIES for the
# system libs that Irrlicht links against internally.  Any executable that links
# libIrrlicht.a must also link these explicitly:
#   - bzip2   (CZipReader / BSP loader)
#   - JPEG    (CImageLoaderJPG / CImageWriterJPG)
#   - PNG     (CImageLoaderPNG / CImageWriterPNG)
#   - ZLIB    (CZipReader compressed archives)
#   - Xxf86vm (CIrrDeviceLinux fullscreen / gamma)
# All four vcpkg packages are installed as transitive deps of the irrlicht port
# (listed in ports/irrlicht/vcpkg.json at the pinned baseline).
# libXxf86vm is a system-only library; found via find_library.
if(UNIX AND NOT APPLE)
    find_package(JPEG REQUIRED)
    find_package(PNG  REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(BZip2 REQUIRED)
    find_library(XXFV86VM_LIB NAMES Xxf86vm)
endif()

# ============================================================================
# TESTING DEPENDENCIES — GTest and RapidCheck (via vcpkg)
# googletest: vcpkg port "gtest" — CMake targets: GTest::gtest, GTest::gtest_main,
#             GTest::gmock, GTest::gmock_main (namespaced, same as FetchContent).
# rapidcheck: vcpkg port "rapidcheck" — CMake targets: rapidcheck, rapidcheck_gtest
#             (bare names — upstream installs(EXPORT ...) has no NAMESPACE argument).
#             rapidcheck_gtest is an INTERFACE library (headers only); no GTest dep.
# ============================================================================
find_package(GTest CONFIG REQUIRED)
find_package(rapidcheck CONFIG REQUIRED)

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
# GLEW::GLEW MUST be linked BEFORE Irrlicht (GLEW symbol duplication mitigation-2).
# PRIVATE scope — Irrlicht and GLEW headers must NOT propagate to test targets.
# Phase 1 delivers RenderSystem.cpp, IrrlichtRenderer.cpp, IrrlichtUIBackend.h (inline).
# Phase 3 splits IrrlichtUIBackend into declaration-only .h + IrrlichtUIBackend.cpp
# so that Irrlicht headers are confined to the .cpp (testability isolation rule).
# ----------------------------------------------------------------------------
add_library(aitown_render STATIC
    src/rendering/RenderSystem.cpp
    src/rendering/IrrlichtRenderer.cpp
    src/rendering/IrrlichtUIBackend.cpp
    src/rendering/TextureCache.cpp
    src/rendering/SceneEntityManager.cpp
    src/rendering/TerrainShaderCallback.cpp
)
target_include_directories(aitown_render
    PRIVATE   src/rendering/       # RenderSystem/IrrlichtRenderer internal headers
    PRIVATE   src/interfaces/      # IClock, IRenderer, etc.
    PRIVATE   src/ui/              # UIManager.h (included by IrrlichtRenderer.cpp)
    PRIVATE   src/platform/        # input_event.h (included by UIManager.cpp chain)
    PRIVATE   ${CMAKE_SOURCE_DIR}  # project-root-relative includes (src/ui/UIManager.h etc.)
    INTERFACE src/interfaces/      # propagates to consumers (test targets, etc.)
    INTERFACE src/rendering/       # IrrlichtUIBackend.h and IrrlichtRenderer.h for consumers
)
# AITOWN_ASSETS_DIR — absolute path expanded at configure time.
# Correct for both Linux and Windows. Using CMAKE_CURRENT_SOURCE_DIR is incorrect
# if the target is configured from a subdirectory.
target_compile_definitions(aitown_render PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
# GLEW::GLEW BEFORE Irrlicht — mandatory link order (GLEW symbol duplication mitigation-2).
# PRIVATE scope: Irrlicht and GLEW headers must NOT propagate to test targets.
target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht)

# Windows post-build: copy Irrlicht.dll and GLEW32.dll to the output directory.
# Hard-fail in CI if either DLL is missing (build-windows job verifies both).
if(WIN32)
    add_custom_command(TARGET aitown_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Irrlicht>
            $<TARGET_FILE_DIR:aitown_render>
        COMMENT "Copying Irrlicht.dll"
    )
    add_custom_command(TARGET aitown_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GLEW::GLEW>
            $<TARGET_FILE_DIR:aitown_render>
        COMMENT "Copying GLEW32.dll"
    )
endif()

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
# PRIVATE scope: OpenAL, Vorbis, and fmt headers must NOT propagate to test targets.
# Omitting any of these causes a cmake --build build link failure on Linux and Windows.
# fmt::fmt is required because openal-soft's devendor-fmt patch causes libopenal.a to
# reference fmt symbols; failing to link it produces "undefined reference to
# fmt::v12::report_error" when linking audio_tests.
target_link_libraries(aitown_audio PRIVATE
    OpenAL::OpenAL
    Vorbis::vorbisfile
    fmt::fmt
)

# ----------------------------------------------------------------------------
# aitown_sim — City simulation (economy, traffic, zoning, population)
# Phase 6 replaces placeholder.cpp with CitySimulation.cpp.
# ----------------------------------------------------------------------------
add_library(aitown_sim STATIC
    src/simulation/CitySimulation.cpp
)
target_include_directories(aitown_sim
    PRIVATE   src/simulation/      # implementation headers — NOT propagated
    PRIVATE   src/interfaces/      # needed for CitySimulation.cpp's own includes
                                   # (IClock, IAudioSystem, ISimulationRNG, ISimulationPauser, ICitySimulation)
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
)

# ----------------------------------------------------------------------------
# aitown_ui — UIManager, UIScaler, CameraController, NotificationManager, dialogs
# Phase 1 delivers UIManager.cpp, UIScaler.cpp, CameraController.cpp (full bodies).
# Note: IUIBackend.h lives in src/ui/ and is propagated via INTERFACE src/ui/.
# Consumers linking aitown_ui can include IUIBackend.h via the project-root-relative
# form #include "src/ui/IUIBackend.h" as long as ${CMAKE_SOURCE_DIR} is on their
# include path (always present via the canonical include pattern).
# ----------------------------------------------------------------------------
add_library(aitown_ui STATIC
    src/ui/UIManager.cpp
    src/ui/UIScaler.cpp
    src/ui/CameraController.cpp
    src/ui/NotificationManager.cpp
)
target_include_directories(aitown_ui
    PRIVATE   src/ui/              # UIManager.h, UIScaler.h, CameraController.h
    PRIVATE   src/interfaces/      # IClock, ICitySimulation, IAudioSystem, ISimulationPauser
    PRIVATE   src/platform/        # input_event.h (included by UIManager.cpp)
    PRIVATE   ${CMAKE_SOURCE_DIR}  # project-root-relative includes
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
    INTERFACE src/ui/              # IUIBackend.h is a consumer-visible interface header
)
# Irrlicht: CameraController.cpp includes <irrlicht.h> for ICameraSceneNode full type.
# PRIVATE scope — Irrlicht headers must NOT propagate to test targets (testability rule).
target_link_libraries(aitown_ui PRIVATE Irrlicht)

# ----------------------------------------------------------------------------
# aitown_terrain — Terrain chunk and system (Phase 5: promoted from INTERFACE to STATIC)
# Phase 3: was an INTERFACE library (headers only).
# Phase 5: promoted to STATIC; TerrainChunk.cpp and TerrainSystem.cpp are compiled in.
# SceneEntityManager lives in aitown_render (NOT here) to avoid a circular dep:
#   aitown_terrain -> aitown_render -> Irrlicht (one direction only)
#   aitown_render must NEVER depend on aitown_terrain.
# target_include_directories changed from INTERFACE to PUBLIC to maintain transitive
# include visibility for consumers (terrain_tests, integration_tests, opengl_tests).
# ----------------------------------------------------------------------------
add_library(aitown_terrain STATIC)
target_sources(aitown_terrain PRIVATE
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainSystem.cpp
)
target_include_directories(aitown_terrain
    PUBLIC  src/terrain/           # ITerrainRNG.h, TerrainChunk.h, TerrainSystem.h
    PUBLIC  src/interfaces/        # shared interfaces propagated to consumers (IClock, IRenderer)
)
# aitown_terrain -> aitown_render: TerrainSystem holds IRenderer*, which requires
# src/interfaces/ (already PUBLIC above) plus the rendered-side include paths.
# Link direction is aitown_terrain -> aitown_render ONLY — never the reverse.
#
# GLEW::GLEW and Irrlicht are PRIVATE here: TerrainChunk.cpp builds SMesh directly
# (Phase 5 promotion from INTERFACE to STATIC). aitown_render links them PRIVATE,
# so they are NOT propagated to aitown_terrain via the PUBLIC aitown_render link.
# We add them PRIVATE here so TerrainChunk.cpp can include <GL/glew.h> and <irrlicht.h>.
# PRIVATE scope: headers must NOT re-propagate to terrain_tests (that target links them directly).
target_link_libraries(aitown_terrain PUBLIC aitown_render)
target_link_libraries(aitown_terrain PRIVATE GLEW::GLEW Irrlicht)

# ============================================================================
# MAIN EXECUTABLE
# WallClock.cpp MUST be in the executable target (NOT in any library) to avoid
# cross-domain dependency (aitown_audio + aitown_render both consume IClock).
# See phase-1.md §WallClock CMake target ownership.
# ============================================================================
add_executable(aitown
    src/main.cpp
    src/platform/EventReceiver.cpp
    src/platform/WallClock.cpp
)
target_include_directories(aitown PRIVATE
    src/rendering/        # IrrlichtUIBackend.h, IrrlichtRenderer.h, RenderSystem.h
    src/ui/               # UIManager.h, UIScaler.h, CameraController.h
    src/platform/         # EventReceiver.h, WallClock.h (via src/interfaces/)
    src/interfaces/       # WallClock.h, IClock.h, camera_state.h, etc.
    src/simulation/       # CitySimulation.h, simulation_constants.h (Phase 6)
    src/terrain/          # TerrainSystem.h (Phase 6 main-loop wiring)
    ${CMAKE_SOURCE_DIR}   # project-root-relative includes
)
# Irrlicht: main.cpp includes <irrlicht.h> directly.
# GLEW::GLEW: RenderSystem.cpp uses GLEW (propagated via aitown_render but we link explicitly).
# Phase 6: aitown_sim (CitySimulation::tick in main loop), aitown_terrain (TerrainSystem as
#          ITerrainQuery* passed to CitySimulation constructor), aitown_audio (IAudioSystem*
#          concrete implementation required by CitySimulation constructor).
target_link_libraries(aitown PRIVATE
    aitown_render
    aitown_ui
    aitown_sim
    aitown_terrain
    aitown_audio
    Irrlicht
    GLEW::GLEW
)
# Irrlicht transitive deps (Linux only) — see find_package block above for rationale.
if(UNIX AND NOT APPLE)
    target_link_libraries(aitown PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(aitown PRIVATE ${XXFV86VM_LIB})
    endif()
endif()

# Windows: copy DLLs to executable output directory (same as aitown_render post-build).
if(WIN32)
    add_custom_command(TARGET aitown POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Irrlicht>
            $<TARGET_FILE_DIR:aitown>
        COMMENT "Copying Irrlicht.dll to aitown output"
    )
    add_custom_command(TARGET aitown POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GLEW::GLEW>
            $<TARGET_FILE_DIR:aitown>
        COMMENT "Copying GLEW32.dll to aitown output"
    )
endif()

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
    tests/simulation/economy_test.cpp
    tests/simulation/traffic_test.cpp
    tests/simulation/zoning_test.cpp
    tests/simulation/service_coverage_test.cpp
    tests/simulation/population_test.cpp
    tests/simulation/save_system_test.cpp        # Phase 6 stub — full impl in Phase 11
    tests/simulation/undo_system_test.cpp        # Phase 6 stub — simulation-side undo only
    tests/simulation/manual_rng_test.cpp
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
    src/simulation/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(simulation_tests LABEL "unit" DISCOVERY_TIMEOUT 60)

# ----------------------------------------------------------------------------
# terrain_tests — terrain generator property tests and fixed-seed regression
# TIMEOUT 300: multi-seed property tests can exceed 120 s on coverage-instrumented runners
# DISCOVERY_TIMEOUT 60: allow extra time for test discovery on slow CI runners
# ----------------------------------------------------------------------------
add_executable(terrain_tests
    tests/terrain/terrain_smoke_test.cpp
    tests/terrain/terrain_stub.cpp        # Phase 3: compile-only stub for all terrain headers
    tests/terrain/terrain_chunk_test.cpp  # Phase 5: TerrainChunk vertex count + bounding box tests
    tests/terrain/terrain_system_test.cpp # Phase 5: TerrainSystem rebuild deque + hysteresis tests
    tests/terrain/texture_cache_test.cpp  # Phase 5: TextureCache unit tests (EDT_NULL guard + ref counts)
)
# aitown_terrain is now STATIC (Phase 5 promotion) and transitively links aitown_render
# via PUBLIC scope.  aitown_render links Irrlicht and GLEW::GLEW PRIVATE — so terrain_tests
# must link them directly to access Irrlicht and GLEW headers (required by TextureCache.h).
# Irrlicht transitive deps (Linux only) — same rationale as integration_tests and opengl_tests:
# the adrido/irrlicht-vcpkg port does NOT export INTERFACE_LINK_LIBRARIES for system libs.
target_link_libraries(terrain_tests PRIVATE
    aitown_terrain
    Irrlicht
    GLEW::GLEW
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
if(UNIX AND NOT APPLE)
    target_link_libraries(terrain_tests PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(terrain_tests PRIVATE ${XXFV86VM_LIB})
    endif()
endif()
# tests/simulation/ — ManualClock (manual_clock.h) and MockRenderer (mock_renderer.h)
# tests/terrain/   — MockTerrainRNG (mock_terrain_rng.h) and terrain_stub.cpp includes
# src/terrain/     — ITerrainRNG.h (included by mock_terrain_rng.h)
# src/rendering/   — TextureCache.h (referenced by texture_cache_test.cpp)
# src/interfaces/  — IClock.h, IRenderer.h (propagated via aitown_terrain PUBLIC but
#                    listed explicitly as belt-and-suspenders for header resolution)
# ${CMAKE_SOURCE_DIR} — project-root-relative includes (e.g. src/terrain/TerrainChunk.h)
target_include_directories(terrain_tests PRIVATE
    tests/simulation/
    tests/terrain/
    src/terrain/
    src/rendering/
    src/interfaces/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)

# ----------------------------------------------------------------------------
# ui_tests — UIManager, NotificationManager, CameraController, QueryPanel, ModalDialog
# Phase 3 Test-C1: consolidated 5-file form (ui_smoke_test.cpp dropped — its compile
# checks are covered by the Phase 3 files below).
# ----------------------------------------------------------------------------
add_executable(ui_tests
    tests/ui/camera_controller_test.cpp      # Phase 1: 9+1 CameraController unit tests
    tests/ui/ui_scaler_test.cpp              # Phase 1: 6+1 UIScaler unit tests
    tests/ui/key_bindings_test.cpp           # Phase 3: KeyBindings unit tests
    tests/ui/ui_manager_draw_order_test.cpp  # Phase 3: UIManager 10-slot draw order
    tests/ui/ui_manager_modal_test.cpp       # Phase 3: UIManager modal smoke tests
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
# src/ui/           — IUIBackend.h, CameraController.h, UIScaler.h, and other UI headers
# ${CMAKE_SOURCE_DIR} — project-root-relative includes (e.g. src/platform/input_event.h)
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
# Phase 1 adds irrlicht_ui_backend_compile_test.cpp (IrrlichtUIBackend non-abstract check).
# Links aitown_render and aitown_ui — IrrlichtUIBackend.h depends on Irrlicht headers
# available only when aitown_render is linked.
# ----------------------------------------------------------------------------
add_executable(integration_tests
    tests/integration/integration_smoke_test.cpp
    tests/integration/irrlicht_ui_backend_compile_test.cpp  # Phase 1: non-abstract compile check
    tests/integration/irrlicht_ui_backend_test.cpp          # Phase 3: EDT_NULL runtime handle test
    tests/integration/texture_cache_integration_test.cpp    # Phase 5: TextureCache + SceneEntityManager EDT_NULL tests
)
# aitown_render and aitown_ui MUST be present: IrrlichtUIBackend.h depends on Irrlicht.
# Irrlicht MUST be linked directly: IrrlichtUIBackend.h includes <irrlicht.h> at compile time;
# aitown_render links Irrlicht PRIVATE so its include path does NOT propagate to consumers.
# aitown_terrain: Phase 5 adds texture_cache_integration_test.cpp which tests TextureCache
# and SceneEntityManager; both live in aitown_render/aitown_terrain.
# GLEW::GLEW: TextureCache.h includes <GL/glew.h> at the top.
# rapidcheck and rapidcheck_gtest always linked (proactive policy — architecture/testing/framework.md).
target_link_libraries(integration_tests PRIVATE
    aitown_render
    aitown_ui
    aitown_terrain
    Irrlicht
    GLEW::GLEW
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# Irrlicht transitive deps (Linux only) — same set as opengl_tests and aitown executable.
# The adrido/irrlicht-vcpkg port does NOT export INTERFACE_LINK_LIBRARIES for these.
# Phase 3 adds the EDT_NULL runtime test (irrlicht_ui_backend_test.cpp) which calls
# createDevice(), pulling in CImageLoaderJPG, CImageLoaderPNG, CIrrDeviceLinux, etc.
if(UNIX AND NOT APPLE)
    target_link_libraries(integration_tests PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(integration_tests PRIVATE ${XXFV86VM_LIB})
    endif()
endif()
# src/rendering/ MUST be present: IrrlichtUIBackend.h lives in src/rendering/.
# src/terrain/   Phase 5: TextureCache integration tests include TerrainChunk/TerrainSystem headers.
target_include_directories(integration_tests PRIVATE
    tests/simulation/
    tests/ui/
    src/interfaces/
    src/ui/
    src/rendering/
    src/terrain/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(integration_tests LABEL "integration")

# ----------------------------------------------------------------------------
# opengl_tests — sRGB upload, setMesh grab/drop smoke test, shader compilation
# Must run under xvfb-run on Linux.
# Phase 0: ONLY stub_succeed.cpp in source list.
# Phase 2 atomicity commit adds: lod_swap_smoke_test.cpp (GTEST_SKIP body), ShaderLoadingTest body,
# 6 GLSL stubs, and getGPUProgrammingServices() null check — all four items in one commit.
# ----------------------------------------------------------------------------
add_executable(opengl_tests
    tests/rendering/stub_succeed.cpp           # Phase 0 stub — remove when real tests cover target
    tests/rendering/shader_stub_compile_test.cpp  # Phase 1+2: shader stub compile check
    tests/rendering/lod_swap_smoke_test.cpp    # Phase 2: LOD swap grab/drop contract smoke test
)
# Irrlicht and GLEW::GLEW MUST be linked directly: shader_stub_compile_test.cpp includes
# <irrlicht.h> and <GL/glew.h>; aitown_render links both PRIVATE so their include paths
# do NOT propagate to consumers (same rationale as integration_tests linking Irrlicht directly).
target_link_libraries(opengl_tests PRIVATE
    aitown_render
    Irrlicht
    GLEW::GLEW
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
)
# Irrlicht transitive deps (Linux only) — same set as aitown executable.
# The adrido/irrlicht-vcpkg port does NOT export INTERFACE_LINK_LIBRARIES for these.
# Required when system dynamic libs (libjpeg, libpng, etc.) are absent (e.g. devcontainer).
if(UNIX AND NOT APPLE)
    target_link_libraries(opengl_tests PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(opengl_tests PRIVATE ${XXFV86VM_LIB})
    endif()
endif()
# src/interfaces/   — IRenderer.h propagated via INTERFACE from aitown_render, but
#                     explicit belt-and-suspenders path ensures #include "vec3.h" inside
#                     IRenderer.h resolves even if aitown_render's INTERFACE scope is refactored.
# ${CMAKE_SOURCE_DIR} — project-root-relative includes
target_include_directories(opengl_tests PRIVATE
    src/interfaces/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(opengl_tests LABEL "requires-opengl")
