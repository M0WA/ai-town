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
    # Phase 9: LODNode — LOD swap prototype (pre-condition for building asset production)
    src/rendering/LODNode.cpp
    # Phase 9: building facade shader callback
    src/rendering/BuildingShaderCallback.cpp
    # Phase 9: road tile shader callback
    src/rendering/RoadShaderCallback.cpp
    # Phase 9: building asset loader
    src/rendering/BuildingAssetLoader.cpp
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
# Phase 7 replaces placeholder.cpp with AudioSystem.cpp, AudioSourcePool.cpp,
# AudioStream.cpp, and al_check.cpp (real error-checking implementations).
# ----------------------------------------------------------------------------
add_library(aitown_audio STATIC
    src/audio/AudioSystem.cpp
    src/audio/AudioSourcePool.cpp
    src/audio/AudioStream.cpp
)
target_include_directories(aitown_audio
    PRIVATE   src/audio/           # audio_constants.h and AudioSystem-internal headers
    PRIVATE   src/interfaces/      # needed for AudioSystem.cpp's own includes (IClock, etc.)
    PRIVATE   ${CMAKE_SOURCE_DIR}  # project-root-relative includes: src/audio/AudioSystem.h etc.
    INTERFACE src/interfaces/      # propagates to consumers
    INTERFACE src/audio/           # AudioSystem.h visible to consumers (e.g. main.cpp)
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
# Phase 11 adds SaveSystem.cpp (save/load serialization).
# ----------------------------------------------------------------------------
add_library(aitown_sim STATIC
    src/simulation/CitySimulation.cpp
    src/simulation/SaveSystem.cpp
)
target_include_directories(aitown_sim
    PRIVATE   src/simulation/      # implementation headers — NOT propagated
    PRIVATE   src/interfaces/      # needed for CitySimulation.cpp's own includes
                                   # (IClock, IAudioSystem, ISimulationRNG, ISimulationPauser, ICitySimulation)
    PRIVATE   src/audio/           # sound_ids.h and AudioSystem.h for SFX wiring
    PRIVATE   ${CMAKE_SOURCE_DIR}  # project-root-relative includes
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
    INTERFACE src/simulation/      # CitySimulation.h visible to consumers
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
# aitown_sim: UIManager.cpp includes SaveSystem.h (Phase 11 save wiring).
target_link_libraries(aitown_ui PRIVATE Irrlicht aitown_sim)

# Phase 8: extend aitown_ui with per-panel .cpp translation units.
# Do NOT edit the Phase 3 add_library(aitown_ui ...) call above.
# Each panel .cpp currently includes its header (inline stubs); Phase 8 implementation
# will move method bodies from headers into these .cpp files.
target_sources(aitown_ui PRIVATE
    src/ui/HUD.cpp
    src/ui/BudgetDetailPanel.cpp
    src/ui/TaxRatePanel.cpp
    src/ui/QueryPanel.cpp
    src/ui/Minimap.cpp
    src/ui/ModalDialog.cpp
    src/ui/SettingsPanel.cpp
    src/ui/PauseMenuPanel.cpp
    src/ui/MainMenuPanel.cpp
)

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
    # vcpkg installs OpenAL Soft on Windows as OpenAL32.dll (not soft_oal.dll).
    # Copy it to the output dir renamed as soft_oal.dll, which is the canonical
    # bundled name for shipping OpenAL Soft alongside a game binary. The CI
    # build-windows hard-fail step verifies presence of build/soft_oal.dll.
    # Note: $<TARGET_FILE:OpenAL::OpenAL> resolves to OpenAL32.lib (import lib),
    # so we must use the explicit vcpkg bin path for the source DLL.
    add_custom_command(TARGET aitown POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll"
            "$<TARGET_FILE_DIR:aitown>/soft_oal.dll"
        COMMENT "Copying OpenAL32.dll (OpenAL Soft runtime) to aitown output as soft_oal.dll"
    )
endif()

# Copy default.mhr (OpenAL Soft HRTF data) to the build output directory.
# Required at runtime for 3D spatial audio HRTF initialisation.
# Source: vcpkg installs HRTF data to share/openal/hrtf/ via install(DIRECTORY hrtf ...).
# VCPKG_OVERLAY_PORTS=vcpkg-overlays enables -DALSOFT_INSTALL_HRTF_DATA=ON in the
# overlay portfile (openal-soft 1.23.1) so the hrtf/ directory is installed.
# The copy is cross-platform — both Linux and Windows CI verify its presence in
# the build directory.
add_custom_command(TARGET aitown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr"
        "$<TARGET_FILE_DIR:aitown>/default.mhr"
    COMMENT "Copying default.mhr (OpenAL Soft HRTF data) to aitown output"
)

# ============================================================================
# aitown_benchmark — standalone benchmark for VRAM/FPS/anisotropy profiling
# Phase 9 deliverable: VRAM budget compliance tool.
# Links aitown_render (which already links GLEW::GLEW, Irrlicht PRIVATE).
# GLEW::GLEW and Irrlicht must be linked directly here because aitown_render
# links them PRIVATE — their headers and symbols do not propagate to consumers.
# Irrlicht transitive deps (Linux only) — same rationale as aitown executable.
# Not run in CI (hardware-dependent); run manually before shipping.
# See architecture/graphics-architecture/benchmark-tool.md for full spec.
# ============================================================================
add_executable(aitown_benchmark
    src/benchmark/benchmark_main.cpp
    src/rendering/VRAMProfiler.cpp
)
target_link_libraries(aitown_benchmark PRIVATE aitown_render GLEW::GLEW Irrlicht)
target_include_directories(aitown_benchmark PRIVATE src/ ${CMAKE_SOURCE_DIR})
target_compile_definitions(aitown_benchmark PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
# Irrlicht transitive deps (Linux only) — see find_package block above for rationale.
if(UNIX AND NOT APPLE)
    target_link_libraries(aitown_benchmark PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(aitown_benchmark PRIVATE ${XXFV86VM_LIB})
    endif()
endif()
# Windows: copy Irrlicht.dll and GLEW32.dll next to the benchmark binary.
if(WIN32)
    add_custom_command(TARGET aitown_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Irrlicht>
            $<TARGET_FILE_DIR:aitown_benchmark>
        COMMENT "Copying Irrlicht.dll to aitown_benchmark output"
    )
    add_custom_command(TARGET aitown_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GLEW::GLEW>
            $<TARGET_FILE_DIR:aitown_benchmark>
        COMMENT "Copying GLEW32.dll to aitown_benchmark output"
    )
endif()

# ============================================================================
# aitown_model_validator — standalone interactive tool for visual asset verification.
# Displays all V1 building and vehicle B3D assets category-by-category with an
# orbiting camera so operators can confirm mesh, texture, and scale before release.
# Not run in CI (requires a real OpenGL display and human visual sign-off).
# See architecture/graphics-architecture/model-validator-tool.md for full spec.
# ============================================================================
add_executable(aitown_model_validator
    src/benchmark/model_validator_main.cpp
)
target_link_libraries(aitown_model_validator PRIVATE aitown_render GLEW::GLEW Irrlicht)
target_include_directories(aitown_model_validator PRIVATE src/ ${CMAKE_SOURCE_DIR})
target_compile_definitions(aitown_model_validator PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
# Irrlicht transitive deps (Linux only) — same rationale as aitown_benchmark.
if(UNIX AND NOT APPLE)
    target_link_libraries(aitown_model_validator PRIVATE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
    )
    if(XXFV86VM_LIB)
        target_link_libraries(aitown_model_validator PRIVATE ${XXFV86VM_LIB})
    endif()
endif()
# Windows: copy Irrlicht.dll and GLEW32.dll next to the model validator binary.
if(WIN32)
    add_custom_command(TARGET aitown_model_validator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Irrlicht>
            $<TARGET_FILE_DIR:aitown_model_validator>
        COMMENT "Copying Irrlicht.dll to aitown_model_validator output"
    )
    add_custom_command(TARGET aitown_model_validator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:GLEW::GLEW>
            $<TARGET_FILE_DIR:aitown_model_validator>
        COMMENT "Copying GLEW32.dll to aitown_model_validator output"
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
# Phase 10: add adaptive music intensity and city simulation render tests via
# target_sources (not add_executable) to preserve the Phase 0 executable definition.
# adaptive_music_intensity_test.cpp — tests music intensity state machine driven by
#   CitySimulation state (crisis/growth/calm routing and forced-Calm time-of-day override).
# city_simulation_render_test.cpp — tests CitySimulation render-loop integration
#   (headless render tick with MockRenderer, time-of-day propagation to AudioSystem).
# Both files are Phase 10 deliverables from sound-dev-opensoftal and are conditionally
# compiled with AITOWN_TESTING_ENABLED=1 (see target_compile_definitions below).
target_sources(simulation_tests PRIVATE
    tests/simulation/adaptive_music_intensity_test.cpp
    tests/simulation/city_simulation_render_test.cpp
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

# Phase 9b: audio callback tests for placeRoad SFX.
target_sources(simulation_tests PRIVATE
    tests/simulation/audio_sim_test.cpp
)

# Phase 10: simulation test sources (adaptive music intensity + render dispatch).
target_sources(simulation_tests PRIVATE
    tests/simulation/adaptive_music_intensity_test.cpp
    tests/simulation/city_simulation_render_test.cpp
)

# (simulation_coverage_gap_test.cpp dissolved — tests distributed to semantic files)

# Phase 10b: terrain flattening simulation test.
target_sources(simulation_tests PRIVATE
    tests/simulation/terrain_flattening_sim_test.cpp
)

# Phase 11: real SaveSystem coverage tests (exercises SaveSystem.cpp directly;
# existing save_system_test.cpp uses stubs and contributes 0% lcov coverage).
target_sources(simulation_tests PRIVATE
    tests/simulation/save_system_real_test.cpp
)

# Coverage gap tests: target the ~30.8% miss identified in CI (static helpers,
# speed/pause control, accessors, JSON round-trip, signal logic, demolish branches).
target_sources(simulation_tests PRIVATE
    tests/simulation/city_simulation_coverage_test.cpp
)

# Phase 10: AITOWN_TESTING_ENABLED=1 enables CitySimulation::testForceUnlockDensityTier().
# MUST be on simulation_tests ONLY — never on the main aitown binary target.
target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)

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
    tests/terrain/terrain_flattening_test.cpp # Phase 10b: setTileHeight enqueue + neighbour blend tests
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
# TIMEOUT 300: WorldInteraction_OverlayCap_100K_StillCalls iterates 100,001 times
# (GMock overhead + overlay insert + setZoneOverlay per iteration) and requires
# approximately 225 s on an unoptimised build.  All other ui_tests complete < 1 s.
aitown_add_tests(ui_tests LABEL "unit" TIMEOUT 300)

# Phase 8: extend ui_tests with per-panel test files.
# Do NOT edit the Phase 3 add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) calls above.
# Bulk add: 6 files covering modal dialog, undo button, notification system,
# budget detail panel, query panel, and keybindings conflict-detection tests.
target_sources(ui_tests PRIVATE
    tests/ui/modal_dialog_test.cpp
    tests/ui/undo_button_test.cpp
    tests/ui/notification_system_test.cpp
    tests/ui/budget_detail_panel_test.cpp
    tests/ui/query_panel_test.cpp
    tests/ui/keybindings_test.cpp
)
# Standalone add: settings panel test (Graphics tab countdown + volume slider tests).
# Separate target_sources call to match phase-8.md deliverable structure.
target_sources(ui_tests PRIVATE
    tests/ui/settings_panel_test.cpp
)

# Phase 9b: extend ui_tests with world-interaction tests (Deliverable G).
# Do NOT call add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) again —
# that would produce a duplicate target error.
target_sources(ui_tests PRIVATE
    tests/ui/world_interaction_test.cpp
)

# (coverage_gap_test.cpp and query_panel_coverage_test.cpp dissolved —
#  tests distributed to semantically correct files)
# main_menu_coverage_test.cpp: MainMenuPanel keyboard navigation and lifecycle tests.
target_sources(ui_tests PRIVATE
    tests/ui/main_menu_coverage_test.cpp
)

# Phase 11: stinger_milestone per-frame polling tests.
target_sources(ui_tests PRIVATE
    tests/ui/ui_manager_stinger_test.cpp
)

# Phase 11: KeyBindings::load() unit tests (6 cases: all-fields, Q/E reserved rejection,
# absent file, unknown key, partial JSON).
target_sources(ui_tests PRIVATE
    tests/ui/keybindings_load_test.cpp
)

# ----------------------------------------------------------------------------
# audio_tests — duck state machine, occlusion smoothing, crossfade, stinger milestone tests
# Vorbis::vorbisfile must be listed EXPLICITLY — it CANNOT resolve through aitown_audio's
# PRIVATE scope, which deliberately does NOT propagate to consumers.
# ----------------------------------------------------------------------------
add_executable(audio_tests
    tests/audio/audio_smoke_test.cpp
    tests/audio/audio_crossfade_test.cpp
)
# Phase 10: add the four audio unit tests via target_sources (not add_executable) to
# preserve the Phase 0 executable definition per the target_sources policy.
#
# crossfade_interrupted_formula_test.cpp — Crossfade_InterruptedFormula_NoDomainErrorAtBoundary:
#   verify t_offset at current_gain_out=1.0 returns 0 and at 0.0 returns 1 with no arccos
#   domain error; exercises the constant-power interrupted crossfade math.
#
# stinger_milestone_test.cpp — StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation:
#   100K population without City Rating transition → stinger NOT triggered;
#   1K population at Village→Town transition → stinger triggered.
#
# audio_stream_bar_boundary_test.cpp — AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake
#   and AudioStream_BarBoundary_StreamStart_NoFalseFire:
#   verifies AL_BUFFERS_QUEUED is read once per wake; same value passed to both
#   computeSamplesPlayed() and computeNextBarBoundary(); crossfade condition does
#   not fire when m_samplesQueued < buffersQueued * kSamplesPerBuffer and
#   m_nextBarBoundary == 0.
#
# notification_sfx_efx_bypass_test.cpp — NotificationSFX_EFXBypass_DirectFilterSetToNull:
#   verifies alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL) is called on each of the
#   10 non-positional notification-category SFX; verifies sfx_fire_alert and
#   sfx_police_alert do NOT have EFX bypass applied.
target_sources(audio_tests PRIVATE
    tests/audio/crossfade_interrupted_formula_test.cpp
    tests/audio/stinger_milestone_test.cpp
    tests/audio/audio_stream_bar_boundary_test.cpp
    tests/audio/notification_sfx_efx_bypass_test.cpp
    tests/audio/audio_crossfade_property_test.cpp  # Phase 10: rc::check property tests (constant-power invariant, t_offset bounds, monotonicity)
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
    tests/audio/
    src/interfaces/
    src/audio/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(audio_tests LABEL "unit")

# Phase 7: extend audio_tests with duck state machine, occlusion, thread, and OGG validation tests
# Do NOT call add_executable(audio_tests ...) or aitown_add_tests(audio_tests ...) again —
# duplicate target registration causes a CMake configure error.
# Vorbis::vorbisfile is already linked above (Phase 0); do NOT re-list it here.
target_sources(audio_tests PRIVATE
    tests/audio/duck_state_machine_test.cpp
    tests/audio/occlusion_smoothing_test.cpp
    tests/audio/audio_thread_test.cpp
    tests/audio/ogg_header_validation_test.cpp
)

# Phase 8: extend audio_tests with headless volume control and stinger drop-if-playing tests.
# Do NOT call add_executable(audio_tests ...) or aitown_add_tests(audio_tests ...) again.
target_sources(audio_tests PRIVATE
    tests/audio/volume_control_test.cpp
)

# Copy default.mhr (OpenAL Soft HRTF data) next to audio_tests binary.
# Required when audio_tests runs on a machine with real AL hardware (HRTF init).
# On machines without AL hardware, audio_tests uses MockAlcFunctions (no device opened).
# The copy is cross-platform: both Linux and Windows CI include this step.
add_custom_command(TARGET audio_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr"
        "$<TARGET_FILE_DIR:audio_tests>/default.mhr"
    COMMENT "Copying default.mhr (OpenAL Soft HRTF data) next to audio_tests"
)

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
    tests/integration/simulation_60tick_test.cpp            # Phase 6: 60-tick no-crash + SimulationTime exit criterion
    tests/integration/camera_controller_live_test.cpp       # Phase 6: CameraController live-camera path coverage
    tests/integration/terrain_loading_test.cpp              # Phase 11: flushPendingRebuilds deduplication test
    tests/integration/save_system_integration_test.cpp      # Phase 11: SaveSystem auto-save suspension during modal
)
# aitown_render and aitown_ui MUST be present: IrrlichtUIBackend.h depends on Irrlicht.
# Irrlicht MUST be linked directly: IrrlichtUIBackend.h includes <irrlicht.h> at compile time;
# aitown_render links Irrlicht PRIVATE so its include path does NOT propagate to consumers.
# aitown_terrain: Phase 5 adds texture_cache_integration_test.cpp which tests TextureCache
# and SceneEntityManager; both live in aitown_render/aitown_terrain.
# GLEW::GLEW: TextureCache.h includes <GL/glew.h> at the top.
# aitown_sim: Phase 6 adds simulation_60tick_test.cpp which constructs CitySimulation directly.
# aitown_ui: Phase 6 adds camera_controller_live_test.cpp (CameraController live-camera path).
# rapidcheck and rapidcheck_gtest always linked (proactive policy — architecture/testing/framework.md).
target_link_libraries(integration_tests PRIVATE
    aitown_render
    aitown_ui
    aitown_terrain
    aitown_sim
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
# src/rendering/  MUST be present: IrrlichtUIBackend.h lives in src/rendering/.
# src/terrain/    Phase 5: TextureCache integration tests include TerrainChunk/TerrainSystem headers.
# src/simulation/ Phase 6: simulation_60tick_test.cpp includes CitySimulation.h and simulation_constants.h.
target_include_directories(integration_tests PRIVATE
    tests/simulation/
    tests/ui/
    src/interfaces/
    src/ui/
    src/rendering/
    src/terrain/
    src/simulation/
    ${CMAKE_SOURCE_DIR}
)
aitown_add_tests(integration_tests LABEL "integration")

# Phase 10c Feature 3: terrain shader wiring integration tests.
# Both tests (TerrainShaderWiring_EDT_NULL_InitDoesNotCrash and
# TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded) live in one file.
# Do NOT call add_executable(integration_tests ...) again — duplicate target error.
# Do NOT call aitown_add_tests(integration_tests ...) again — duplicate registration.
# Per architecture/testing/framework.md §target_sources() policy:
#   integration_tests PERMITS target_sources() from Phase 10c onward.
# terrainMaterialTypeForTest() / setTerrainMaterialTypeForTest() accessors are added
# by Feature 1 (graphics-dev-irrlicht); this file forward-compiles once those
# accessors land in IrrlichtRenderer.h.
target_sources(integration_tests PRIVATE
    tests/integration/terrain_shader_wiring_test.cpp  # Phase 10c: terrain shader wiring
)

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
    tests/rendering/building_lod_swap_test.cpp        # Phase 9: LOD swap bounding box integration test
    tests/rendering/building_srgb_fallback_test.cpp   # Phase 9: building facade sRGB fallback test
    tests/rendering/cloud_plane_test.cpp       # Phase 10b: cloud plane init test (EDT_OPENGL + EDT_NULL)
)
# Irrlicht and GLEW::GLEW MUST be linked directly: shader_stub_compile_test.cpp includes
# <irrlicht.h> and <GL/glew.h>; aitown_render links both PRIVATE so their include paths
# do NOT propagate to consumers (same rationale as integration_tests linking Irrlicht directly).
target_link_libraries(opengl_tests PRIVATE
    aitown_render
    aitown_ui      # required: IrrlichtRenderer.cpp references UIManager::draw()
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
