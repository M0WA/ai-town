cmake_minimum_required(VERSION 3.21)
project(aitown VERSION 0.1.0 LANGUAGES CXX)

# C++ standard is set per-target via target_compile_features(... PRIVATE cxx_std_17).
# Do NOT set CMAKE_CXX_STANDARD globally — per-target features are more precise.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# COVERAGE SUPPORT
# ============================================================================
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation (Linux only)" OFF)

# aitown_coverage_flags: INTERFACE library carrying --coverage / -O0 / -g flags.
# Linked PRIVATELY to targets that feed lcov (all library and executable targets).
# Only active when ENABLE_COVERAGE=ON so the fast build-linux job is unaffected.
add_library(aitown_coverage_flags INTERFACE)
if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Coverage instrumentation enabled")
    target_compile_options(aitown_coverage_flags INTERFACE --coverage -O0 -g)
    target_link_options(aitown_coverage_flags INTERFACE --coverage)
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

# OpenAL Soft: required for aitown_audio.
# cmake_minimum_required is 3.21 — FindOpenAL.cmake in CMake 3.21+ creates the
# OpenAL::OpenAL imported target natively; no manual workaround is needed.
find_package(OpenAL REQUIRED)

# fmt: required because openal-soft is built with the devendor-fmt vcpkg patch, which
# replaces its bundled copy of fmt with the external vcpkg fmt package.  libopenal.a
# therefore references fmt symbols (e.g. fmt::v12::report_error) that must be satisfied
# at link time.  Linking fmt::fmt PRIVATE to aitown_audio satisfies those symbols for
# every target that links aitown_audio (including all audio_tests).
# HRTF data check: default.mhr must be present in the vcpkg install tree.
# Installed only when VCPKG_OVERLAY_PORTS=vcpkg-overlays is active (the overlay
# portfile enables -DALSOFT_INSTALL_HRTF_DATA=ON for openal-soft 1.23.1).
# The ci-linux / ci-linux-coverage / ci-windows presets set this automatically.
# If you see this error, reconfigure with: cmake --preset ci-linux
# or: cmake -B build -DVCPKG_OVERLAY_PORTS=${CMAKE_SOURCE_DIR}/vcpkg-overlays ...
set(_HRTF_SRC "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr")
if(NOT EXISTS "${_HRTF_SRC}")
    message(FATAL_ERROR
        "default.mhr not found at:\n  ${_HRTF_SRC}\n"
        "HRTF data is installed by the vcpkg overlay (vcpkg-overlays/openal-soft).\n"
        "Reconfigure using the CMake preset:  cmake --preset ci-linux\n"
        "or pass:  -DVCPKG_OVERLAY_PORTS=${CMAKE_SOURCE_DIR}/vcpkg-overlays")
endif()

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
    if(NOT XXFV86VM_LIB)
        message(FATAL_ERROR
            "libXxf86vm not found.\n"
            "Install the development package: apt-get install libxxf86vm-dev\n"
            "This library is required by Irrlicht for fullscreen/gamma support on Linux.")
    endif()
endif()

# ============================================================================
# aitown_irrlicht_system_deps — INTERFACE library for Irrlicht transitive deps.
# The adrido/irrlicht-vcpkg port does NOT export INTERFACE_LINK_LIBRARIES for
# the system libraries Irrlicht links against internally. Any executable or test
# target that links libIrrlicht.a must also link these five system libraries.
# Linux only — on Windows Irrlicht ships as a DLL with no extra system deps.
# Replace five verbatim if(UNIX AND NOT APPLE) blocks with one PRIVATE link.
# ============================================================================
add_library(aitown_irrlicht_system_deps INTERFACE)
if(UNIX AND NOT APPLE)
    # XXFV86VM_LIB is guaranteed non-empty here — configure aborts above if not found.
    target_link_libraries(aitown_irrlicht_system_deps INTERFACE
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
        BZip2::BZip2
        ${XXFV86VM_LIB}
    )
endif()

# ============================================================================
# BUILD_TESTING — standard CMake option to enable/disable test target compilation.
# Default ON so dev builds and CI include tests. Packaging jobs pass -DBUILD_TESTING=OFF
# to skip test compilation (GTest and rapidcheck are not installed in distro containers).
# ============================================================================
option(BUILD_TESTING "Build test targets (requires GTest and rapidcheck)" ON)

# ============================================================================
# AITOWN_ASSETS_DIR — compile-time asset path constant.
# Default: absolute source-tree path (correct for dev builds).
# Override at configure time for packaging: -DAITOWN_ASSETS_DIR=assets (Windows NSIS)
# or -DAITOWN_ASSETS_DIR=/usr/share/aitown/assets (Linux DEB).
# ============================================================================
set(AITOWN_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE STRING "Runtime assets directory path")

# ============================================================================
# aitown_project_root_includes — INTERFACE library exposing ${CMAKE_SOURCE_DIR}.
# Link PRIVATE to targets that need project-root-relative includes (e.g. the
# canonical #include "src/foo/Bar.h" form). Avoids repeating the raw variable
# in every target_include_directories call.
# ============================================================================
add_library(aitown_project_root_includes INTERFACE)
target_include_directories(aitown_project_root_includes INTERFACE ${CMAKE_SOURCE_DIR})

# ============================================================================
# aitown_copy_runtime_dlls — macro for Windows DLL post-build copy pattern.
# Copies Irrlicht.dll and GLEW32.dll to the target's output directory.
# Call as: aitown_copy_runtime_dlls(<target>)
# No-op on non-Windows platforms (macro body is guarded by if(WIN32)).
# ============================================================================
macro(aitown_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Irrlicht>
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying Irrlicht.dll to ${target} output"
        )
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:GLEW::GLEW>
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying GLEW32.dll to ${target} output"
        )
    endif()
endmacro()

# ============================================================================
# LIBRARY STUBS
# ============================================================================

# ----------------------------------------------------------------------------
# aitown_render — Irrlicht rendering adapter
# GLEW::GLEW MUST be linked BEFORE Irrlicht (GLEW symbol duplication mitigation-2).
# PRIVATE scope — Irrlicht and GLEW headers must NOT propagate to test targets.
# ----------------------------------------------------------------------------
add_library(aitown_render STATIC
    src/rendering/RenderSystem.cpp
    src/rendering/IrrlichtRenderer.cpp
    src/rendering/IrrlichtUIBackend.cpp
    src/rendering/TextureCache.cpp
    src/rendering/SceneEntityManager.cpp
    src/rendering/TerrainShaderCallback.cpp
    src/rendering/LODNode.cpp
    src/rendering/BuildingShaderCallback.cpp
    src/rendering/RoadShaderCallback.cpp
    src/rendering/BuildingAssetLoader.cpp
)
target_include_directories(aitown_render
    PRIVATE   src/rendering/       # RenderSystem/IrrlichtRenderer internal headers
    PRIVATE   src/interfaces/      # IClock, IRenderer, etc.
    PRIVATE   src/ui/              # UIManager.h (included by IrrlichtRenderer.cpp)
    PRIVATE   src/platform/        # input_event.h (included by UIManager.cpp chain)
    INTERFACE src/interfaces/      # propagates to consumers (test targets, etc.)
    INTERFACE src/rendering/       # IrrlichtUIBackend.h and IrrlichtRenderer.h for consumers
)
# AITOWN_ASSETS_DIR — set via CACHE STRING at configure time (see top of file).
# Default is the source-tree path for dev builds; packaging jobs override via -D.
target_compile_definitions(aitown_render PRIVATE
    AITOWN_ASSETS_DIR="${AITOWN_ASSETS_DIR}"
)
# GLEW::GLEW BEFORE Irrlicht — mandatory link order (GLEW symbol duplication mitigation-2).
# PRIVATE scope: Irrlicht and GLEW headers must NOT propagate to test targets.
target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht aitown_coverage_flags aitown_project_root_includes)
target_compile_features(aitown_render PRIVATE cxx_std_17)

# Windows post-build: copy Irrlicht.dll and GLEW32.dll to the output directory.
# Hard-fail in CI if either DLL is missing (build-windows job verifies both).
aitown_copy_runtime_dlls(aitown_render)

# ----------------------------------------------------------------------------
# aitown_audio — OpenAL Soft audio adapter
# PRIVATE scope for OpenAL and Vorbis is required: PUBLIC would propagate their
# headers transitively to all test targets that link aitown_audio, violating the
# IAudioSystem interface's contract ("no OpenAL include dependencies").
# ----------------------------------------------------------------------------
add_library(aitown_audio STATIC
    src/audio/AudioSystem.cpp
    src/audio/AudioSourcePool.cpp
    src/audio/AudioStream.cpp
)
target_include_directories(aitown_audio
    PRIVATE   src/audio/           # audio_constants.h and AudioSystem-internal headers
    PRIVATE   src/interfaces/      # needed for AudioSystem.cpp's own includes (IClock, etc.)
    INTERFACE src/interfaces/      # propagates to consumers
    INTERFACE src/audio/           # AudioSystem.h visible to consumers (e.g. main.cpp)
)
target_compile_definitions(aitown_audio PRIVATE
    AITOWN_ASSETS_DIR="${AITOWN_ASSETS_DIR}"
)
# PRIVATE scope: OpenAL, Vorbis, fmt, and Irrlicht headers must NOT propagate to test
# targets (headless CI guarantee — audio test TUs compile without a real AL device or
# Irrlicht context; AudioSystem.h forward-declares irr::ILogger rather than including
# Irrlicht headers, so only AudioSystem.cpp needs the Irrlicht include path).
# Omitting fmt causes "undefined reference to fmt::v12::report_error" when linking audio_tests
# (openal-soft's devendor-fmt patch references fmt symbols in libopenal.a).
# Irrlicht is PRIVATE: AudioSystem.cpp calls irr::ILogger::log() for diagnostic output;
# AudioSystem.h only forward-declares the type.
target_link_libraries(aitown_audio PRIVATE
    OpenAL::OpenAL
    Vorbis::vorbisfile
    fmt::fmt
    Irrlicht
    aitown_coverage_flags
    aitown_project_root_includes
)
target_compile_features(aitown_audio PRIVATE cxx_std_17)

# ----------------------------------------------------------------------------
# aitown_sim — City simulation (economy, traffic, zoning, population)
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
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
    INTERFACE src/simulation/      # CitySimulation.h visible to consumers
)
target_link_libraries(aitown_sim PRIVATE aitown_coverage_flags aitown_project_root_includes)
target_compile_features(aitown_sim PRIVATE cxx_std_17)

# ----------------------------------------------------------------------------
# aitown_ui — UIManager, UIScaler, CameraController, NotificationManager, dialogs
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
    INTERFACE src/interfaces/      # shared interfaces propagated to consumers
    INTERFACE src/ui/              # IUIBackend.h is a consumer-visible interface header
)
# Irrlicht: CameraController.cpp includes <irrlicht.h> for ICameraSceneNode full type.
# PRIVATE scope — Irrlicht headers must NOT propagate to test targets (testability rule).
# aitown_sim: UIManager.cpp includes SaveSystem.h for save wiring.
target_link_libraries(aitown_ui PRIVATE Irrlicht aitown_sim aitown_coverage_flags aitown_project_root_includes)
target_compile_features(aitown_ui PRIVATE cxx_std_17)

# Per-panel .cpp translation units.
target_sources(aitown_ui PRIVATE
    src/ui/HUD.cpp
    src/ui/FinancesPanel.cpp
    src/ui/QueryPanel.cpp
    src/ui/Minimap.cpp
    src/ui/ModalDialog.cpp
    src/ui/SettingsPanel.cpp
    src/ui/PauseMenuPanel.cpp
    src/ui/MainMenuPanel.cpp
)

# KeyBindingsPanel: Controls tab keybinding table + capture-mode state machine.
target_sources(aitown_ui PRIVATE
    src/ui/KeyBindingsPanel.cpp
)

# KeyBindings::load() in .cpp — keeps ILogger.h out of test target include graphs
# (testability isolation rule).
target_sources(aitown_ui PRIVATE
    src/ui/key_bindings.cpp
)

# ----------------------------------------------------------------------------
# aitown_terrain — Terrain chunk and system (STATIC library)
# SceneEntityManager lives in aitown_render (NOT here) to avoid a circular dep:
#   aitown_terrain -> aitown_render -> Irrlicht (one direction only)
#   aitown_render must NEVER depend on aitown_terrain.
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
# GLEW::GLEW and Irrlicht are PRIVATE: TerrainChunk.cpp builds SMesh directly;
# headers must NOT re-propagate to terrain_tests (that target links them directly).
# aitown_render is PRIVATE (not PUBLIC) to prevent Irrlicht/GLEW headers leaking
# into the terrain test graph via the transitive PUBLIC link.
target_link_libraries(aitown_terrain PRIVATE aitown_render)
target_link_libraries(aitown_terrain PRIVATE GLEW::GLEW Irrlicht aitown_coverage_flags)
target_compile_features(aitown_terrain PRIVATE cxx_std_17)

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
    src/simulation/       # CitySimulation.h, simulation_constants.h
    src/terrain/          # TerrainSystem.h
)
# Irrlicht: main.cpp includes <irrlicht.h> directly.
# GLEW::GLEW: RenderSystem.cpp uses GLEW (propagated via aitown_render but we link explicitly).
target_link_libraries(aitown PRIVATE
    aitown_render
    aitown_ui
    aitown_sim
    aitown_terrain
    aitown_audio
    Irrlicht
    GLEW::GLEW
    aitown_irrlicht_system_deps
    aitown_coverage_flags
    aitown_project_root_includes
)
target_compile_features(aitown PRIVATE cxx_std_17)

# Windows: copy DLLs to executable output directory.
aitown_copy_runtime_dlls(aitown)
if(WIN32)
    # vcpkg installs OpenAL Soft on Windows as OpenAL32.dll (not soft_oal.dll).
    # Copy it to the output dir renamed as soft_oal.dll, which is the canonical
    # bundled name for shipping OpenAL Soft alongside a game binary. The CI
    # build-windows hard-fail step verifies presence of build/soft_oal.dll.
    # Note: $<TARGET_FILE:OpenAL::OpenAL> resolves to OpenAL32.lib (import lib),
    # so we must use the explicit vcpkg bin path for the source DLL.
    add_custom_command(TARGET aitown POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/OpenAL32.dll"
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
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr"
        "$<TARGET_FILE_DIR:aitown>/default.mhr"
    COMMENT "Copying default.mhr (OpenAL Soft HRTF data) to aitown output"
)

# ============================================================================
# aitown_benchmark — standalone benchmark for VRAM/FPS/anisotropy profiling
# Links aitown_render (which already links GLEW::GLEW, Irrlicht PRIVATE).
# GLEW::GLEW and Irrlicht must be linked directly here because aitown_render
# links them PRIVATE — their headers and symbols do not propagate to consumers.
# Not run in CI (hardware-dependent); run manually before shipping.
# See architecture/graphics-architecture/benchmark-tool.md for full spec.
# ============================================================================
add_executable(aitown_benchmark
    src/benchmark/benchmark_main.cpp
    src/rendering/VRAMProfiler.cpp
)
target_link_libraries(aitown_benchmark PRIVATE
    aitown_render
    GLEW::GLEW
    Irrlicht
    aitown_irrlicht_system_deps
    aitown_coverage_flags
    aitown_project_root_includes
)
target_include_directories(aitown_benchmark PRIVATE src/)
target_compile_definitions(aitown_benchmark PRIVATE
    AITOWN_ASSETS_DIR="${AITOWN_ASSETS_DIR}"
)
target_compile_features(aitown_benchmark PRIVATE cxx_std_17)
# Windows: copy Irrlicht.dll and GLEW32.dll next to the benchmark binary.
aitown_copy_runtime_dlls(aitown_benchmark)

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
target_link_libraries(aitown_model_validator PRIVATE
    aitown_render
    GLEW::GLEW
    Irrlicht
    aitown_irrlicht_system_deps
    aitown_coverage_flags
    aitown_project_root_includes
)
target_include_directories(aitown_model_validator PRIVATE src/)
target_compile_definitions(aitown_model_validator PRIVATE
    AITOWN_ASSETS_DIR="${AITOWN_ASSETS_DIR}"
)
target_compile_features(aitown_model_validator PRIVATE cxx_std_17)
# Windows: copy Irrlicht.dll and GLEW32.dll next to the model validator binary.
aitown_copy_runtime_dlls(aitown_model_validator)

if(BUILD_TESTING)
# ============================================================================
# TESTING DEPENDENCIES and INFRASTRUCTURE — GTest and RapidCheck (via vcpkg)
# googletest: vcpkg port "gtest" — CMake targets: GTest::gtest, GTest::gtest_main,
#             GTest::gmock, GTest::gmock_main (namespaced, same as FetchContent).
# rapidcheck: vcpkg port "rapidcheck" — CMake targets: rapidcheck, rapidcheck_gtest
#             (bare names — upstream installs(EXPORT ...) has no NAMESPACE argument).
#             rapidcheck_gtest is an INTERFACE library (headers only); no GTest dep.
# ============================================================================
    find_package(GTest CONFIG REQUIRED)
    find_package(rapidcheck CONFIG REQUIRED)
    enable_testing()
    include(GoogleTest)
    include(cmake/AitownTestHelpers.cmake)

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
    tests/simulation/save_system_test.cpp
    tests/simulation/undo_system_test.cpp
    tests/simulation/manual_rng_test.cpp
)
# Adaptive music intensity and city simulation render tests.
# adaptive_music_intensity_test.cpp — tests music intensity state machine driven by
#   CitySimulation state (crisis/growth/calm routing and forced-Calm time-of-day override).
# city_simulation_render_test.cpp — tests CitySimulation render-loop integration
#   (headless render tick with MockRenderer, time-of-day propagation to AudioSystem).
# Both files are conditionally compiled with AITOWN_TESTING_ENABLED=1 (see below).
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
    aitown_project_root_includes
)
target_compile_features(simulation_tests PRIVATE cxx_std_17)
# tests/simulation/ — owns shared mock headers (mock_audio_system.h, mock_renderer.h,
#                     manual_clock.h, manual_rng.h); must be on the include path.
# src/interfaces/ — for project-root-relative includes like #include "src/interfaces/IClock.h"
target_include_directories(simulation_tests PRIVATE
    tests/simulation/
    src/interfaces/
    src/simulation/
)
aitown_add_tests(simulation_tests LABEL "unit" DISCOVERY_TIMEOUT 60)

# Audio callback tests for placeRoad SFX.
target_sources(simulation_tests PRIVATE
    tests/simulation/audio_sim_test.cpp
)

# (simulation_coverage_gap_test.cpp dissolved — tests distributed to semantic files)

target_sources(simulation_tests PRIVATE
    tests/simulation/terrain_flattening_sim_test.cpp
)

target_sources(simulation_tests PRIVATE
    tests/simulation/save_system_real_test.cpp
)

# Extra simulation tests: static helpers, speed/pause control, accessors, JSON round-trip,
# signal logic, demolish branches, smoothstep, time-of-day, bond issuance, escape sequences.
target_sources(simulation_tests PRIVATE
    tests/simulation/city_simulation_extra_test.cpp
)

target_sources(simulation_tests PRIVATE
    tests/simulation/placement_conflict_test.cpp
)

target_sources(simulation_tests PRIVATE
    tests/simulation/agent_render_sync_test.cpp
)

target_sources(simulation_tests PRIVATE
    tests/simulation/footprint_test.cpp
)

# AITOWN_TESTING_ENABLED=1 enables CitySimulation::testForceUnlockDensityTier().
# MUST be on simulation_tests ONLY — never on the main aitown binary target.
target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)

# ----------------------------------------------------------------------------
# terrain_tests — terrain generator property tests and fixed-seed regression
# TIMEOUT 300: multi-seed property tests can exceed 120 s on coverage-instrumented runners
# DISCOVERY_TIMEOUT 60: allow extra time for test discovery on slow CI runners
# ----------------------------------------------------------------------------
add_executable(terrain_tests
    tests/terrain/terrain_smoke_test.cpp
    tests/terrain/terrain_chunk_test.cpp
    tests/terrain/terrain_system_test.cpp
    tests/terrain/texture_cache_test.cpp
    tests/terrain/terrain_flattening_test.cpp
)
# aitown_terrain is STATIC; aitown_render links Irrlicht and GLEW::GLEW PRIVATE —
# terrain_tests must link them directly to access Irrlicht and GLEW headers.
target_link_libraries(terrain_tests PRIVATE
    aitown_terrain
    Irrlicht
    GLEW::GLEW
    aitown_irrlicht_system_deps
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
    aitown_project_root_includes
)
target_compile_features(terrain_tests PRIVATE cxx_std_17)
# tests/simulation/ — ManualClock (manual_clock.h) and MockRenderer (mock_renderer.h)
# tests/terrain/   — MockTerrainRNG (mock_terrain_rng.h)
# src/terrain/     — ITerrainRNG.h (included by mock_terrain_rng.h)
# src/rendering/   — TextureCache.h (referenced by texture_cache_test.cpp)
# src/interfaces/  — IClock.h, IRenderer.h (propagated via aitown_terrain PUBLIC but
#                    listed explicitly as belt-and-suspenders for header resolution)
target_include_directories(terrain_tests PRIVATE
    tests/simulation/
    tests/terrain/
    src/rendering/
    src/interfaces/
)
aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)

# Coverage gap: enqueueAllChunks() — not covered by any existing terrain test.
target_sources(terrain_tests PRIVATE
    tests/terrain/terrain_enqueue_all_chunks_test.cpp
)

# Road lane geometry constant verification tests.
target_sources(terrain_tests PRIVATE
    tests/terrain/road_lane_test.cpp
)

# Terrain stitching-hole fix boundary tests.
target_sources(terrain_tests PRIVATE
    tests/terrain/terrain_boundary_test.cpp
)

# Reclassified from integration_tests: pure STL stub with no Irrlicht or project headers.
target_sources(terrain_tests PRIVATE
    tests/terrain/terrain_loading_test.cpp
)

# ----------------------------------------------------------------------------
# ui_tests — UIManager, NotificationManager, CameraController, QueryPanel, ModalDialog
# ----------------------------------------------------------------------------
add_executable(ui_tests
    tests/ui/camera_controller_test.cpp
    tests/ui/ui_scaler_test.cpp
    tests/ui/key_bindings_test.cpp
    tests/ui/ui_manager_draw_order_test.cpp
    tests/ui/ui_manager_modal_test.cpp
)
target_link_libraries(ui_tests PRIVATE
    aitown_ui
    aitown_sim         # MockCitySimulation includes ICitySimulation.h which includes simulation_types.h
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
    aitown_project_root_includes
)
target_compile_features(ui_tests PRIVATE cxx_std_17)
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
)
# TIMEOUT 300: WorldInteraction_OverlayCap_100K_StillCalls iterates 100,001 times
# (GMock overhead + overlay insert + setZoneOverlay per iteration) and requires
# approximately 225 s on an unoptimised build.  All other ui_tests complete < 1 s.
aitown_add_tests(ui_tests LABEL "unit" TIMEOUT 300)

target_sources(ui_tests PRIVATE
    tests/ui/modal_dialog_test.cpp
    tests/ui/undo_button_test.cpp
    tests/ui/notification_system_test.cpp
    tests/ui/query_panel_test.cpp
    tests/ui/keybindings_test.cpp
)
target_sources(ui_tests PRIVATE
    tests/ui/settings_panel_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/world_interaction_test.cpp
)

# (coverage_gap_test.cpp and query_panel_coverage_test.cpp dissolved —
#  tests distributed to semantically correct files)
# main_menu_flow_test.cpp: MainMenuPanel keyboard navigation and lifecycle tests.
target_sources(ui_tests PRIVATE
    tests/ui/main_menu_flow_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/ui_manager_stinger_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/keybindings_load_test.cpp
)

# HUD display tests: ratingName() variants, formatDollar() negative branch,
# grace-period fade/expire, budget-flash setElementAlpha.
target_sources(ui_tests PRIVATE
    tests/ui/hud_display_test.cpp
)

# UIManager quit tests: pending-quit state machine (unsaved-changes modal,
# PendingQuitAction::Desktop/ToMenu dispatch), setSaveSystem/setSaveAvailable/setSaveStatusText.
target_sources(ui_tests PRIVATE
    tests/ui/ui_manager_quit_test.cpp
)

# NOTE: key_bindings_panel_test.cpp depends on src/ui/KeyBindingsPanel.h.
# The remaining four test files depend on src/interfaces/ISaveSystem.h and
# UIManager::setSaveSystem(ISaveSystem*).
target_sources(ui_tests PRIVATE
    tests/ui/settings_panel_background_test.cpp
    tests/ui/key_bindings_panel_test.cpp
    tests/ui/ui_manager_unsaved_quit_test.cpp
    tests/ui/ui_manager_save_failure_test.cpp
    tests/ui/main_menu_save_state_test.cpp
)

# UI panel tests: ModalDialog showRestoreDefaultsConfirm, SettingsPanel
# WASD preset + restore-defaults polling, MainMenuPanel loading-screen paths,
# KeyBindingsPanel draw/resetToDefaults/arrow-key capture, UIManager keybindings
# persistence and save-failure retry, budget-tick forwarding.
target_sources(ui_tests PRIVATE
    tests/ui/ui_panel_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/service_coverage_overlay_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/placement_conflict_ui_test.cpp
)

# Minimap Traffic and ServiceCoverage overlay rendering tests.
# Exercises Minimap::draw() branches for both overlay modes using NiceMock<MockCitySimulation>
# to stub getRoadSegmentSpeeds() and getServiceCoverage() return values.
target_sources(ui_tests PRIVATE
    tests/ui/minimap_overlay_test.cpp
)

# Coverage boost — targets specific uncovered lines identified by lcov analysis:
#   SettingsPanel.cpp: WASD/RestoreDefaults modal poll Accept/Cancel (lines 379-409),
#     Controls Apply no-conflict callback (504-508), RestoreDefaults button (517-521),
#     WASD preset button (525-528), getInteractiveElementCount Controls/Audio (557, 594),
#     countdown-active branch in showGraphicsTab (300), destructor (148).
#   UIManager.cpp: PauseMenu save success (1313), transitionToMainMenu in quit-flow
#     Accept/Decline branches (1362, 1371), speed-selector handle polling (1491-1499),
#     showForcedLoanDialog with saveSystem (1606), loadKeybindings file-exists path (1834).
target_sources(ui_tests PRIVATE
    tests/ui/coverage_boost_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/budget_breakdown_test.cpp
    tests/ui/demolition_input_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/finances_panel_test.cpp
)

target_sources(ui_tests PRIVATE
    tests/ui/uimanager_zone_overlay_test.cpp
    tests/ui/hud_grace_period_test.cpp
    tests/ui/uimanager_save_state_test.cpp
    tests/ui/uimanager_new_game_reset_test.cpp
    tests/ui/finances_panel_background_test.cpp
)

# FontTier boundary tests — pure static method, no Irrlicht device required.
target_sources(ui_tests PRIVATE
    tests/ui/font_tier_test.cpp
)

# AITOWN_TESTING_ENABLED=1 MUST remain on aitown_ui PRIVATE because
# handleNewGameRequest and setGameSessionActiveForTest are non-inline functions
# defined in UIManager.cpp; compiling aitown_ui without this flag would cause
# undefined-reference link errors in ui_tests.
# See architecture/testing/testability-architecture.md.
target_compile_definitions(aitown_ui PRIVATE AITOWN_TESTING_ENABLED=1)
# Enable on ui_tests so test code that calls the test seams also sees the guard.
target_compile_definitions(ui_tests PRIVATE AITOWN_TESTING_ENABLED=1)

target_sources(simulation_tests PRIVATE
    tests/simulation/city_simulation_road_proximity_test.cpp
    tests/simulation/city_simulation_terrain_flatten_test.cpp
)

# reset(), applyLoadedJson(), isWithinRoadRange() coverage tests.
target_sources(simulation_tests PRIVATE
    tests/simulation/city_simulation_reset_test.cpp
)

# Reclassified from integration_tests: pure mock-based simulation tests with no Irrlicht device.
# Do NOT call add_executable(simulation_tests ...) or aitown_add_tests(...) again.
target_sources(simulation_tests PRIVATE
    tests/simulation/simulation_60tick_test.cpp
    tests/simulation/simulation_comprehensive_integration_test.cpp
    tests/simulation/save_system_integration_test.cpp
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
target_sources(audio_tests PRIVATE
    tests/audio/crossfade_interrupted_formula_test.cpp
    tests/audio/stinger_milestone_test.cpp
    tests/audio/audio_stream_bar_boundary_test.cpp
    tests/audio/notification_sfx_efx_bypass_test.cpp
    tests/audio/audio_crossfade_property_test.cpp
)
target_link_libraries(audio_tests PRIVATE
    aitown_audio
    Vorbis::vorbisfile   # must be explicit — PRIVATE scope in aitown_audio does not propagate
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
    aitown_project_root_includes
)
# tests/simulation/ — MockAudioSystem and ManualClock
# src/interfaces/   — IAudioSystem.h and other interface headers
# src/audio/        — audio_constants.h
target_include_directories(audio_tests PRIVATE
    tests/simulation/
    tests/audio/
    src/interfaces/
    src/audio/
)
target_compile_features(audio_tests PRIVATE cxx_std_17)
aitown_add_tests(audio_tests LABEL "unit")

# Vorbis::vorbisfile is already linked above; do NOT re-list it here.
target_sources(audio_tests PRIVATE
    tests/audio/duck_state_machine_test.cpp
    tests/audio/occlusion_smoothing_test.cpp
    tests/audio/audio_thread_test.cpp
    tests/audio/ogg_header_validation_test.cpp
)

target_sources(audio_tests PRIVATE
    tests/audio/volume_control_test.cpp
)

# Vehicle audio positioning tests (lane offset coordination).
target_sources(audio_tests PRIVATE
    tests/audio/vehicle_audio_positioning_test.cpp
)

target_sources(audio_tests PRIVATE
    tests/audio/audio_system_transition_main_menu_test.cpp
)

# Copy default.mhr (OpenAL Soft HRTF data) next to audio_tests binary.
# Required when audio_tests runs on a machine with real AL hardware (HRTF init).
# On machines without AL hardware, audio_tests uses MockAlcFunctions (no device opened).
# The copy is cross-platform: both Linux and Windows CI include this step.
add_custom_command(TARGET audio_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/openal/hrtf/default.mhr"
        "$<TARGET_FILE_DIR:audio_tests>/default.mhr"
    COMMENT "Copying default.mhr (OpenAL Soft HRTF data) next to audio_tests"
)

# ----------------------------------------------------------------------------
# integration_tests — multi-subsystem tests with EDT_NULL Irrlicht + null audio
# Links aitown_render and aitown_ui — IrrlichtUIBackend.h depends on Irrlicht headers
# available only when aitown_render is linked.
# ----------------------------------------------------------------------------
add_executable(integration_tests
    tests/integration/irrlicht_ui_backend_compile_test.cpp
    tests/integration/irrlicht_ui_backend_test.cpp
    tests/integration/texture_cache_integration_test.cpp
    tests/integration/camera_controller_live_test.cpp
)
# aitown_render and aitown_ui MUST be present: IrrlichtUIBackend.h depends on Irrlicht.
# Irrlicht MUST be linked directly: IrrlichtUIBackend.h includes <irrlicht.h> at compile time;
# aitown_render links Irrlicht PRIVATE so its include path does NOT propagate to consumers.
# aitown_terrain: texture_cache_integration_test.cpp tests TextureCache and SceneEntityManager;
# both live in aitown_render/aitown_terrain.
# GLEW::GLEW: TextureCache.h includes <GL/glew.h> at the top.
# aitown_sim: needed by ui_manager_integration_test.cpp and terrain_system_integration_test.cpp.
# aitown_ui: camera_controller_live_test.cpp (CameraController live-camera path) and UIManager panel tests.
# rapidcheck and rapidcheck_gtest always linked (proactive policy — architecture/testing/framework.md).
target_link_libraries(integration_tests PRIVATE
    aitown_render
    aitown_ui
    aitown_terrain
    aitown_sim
    Irrlicht
    GLEW::GLEW
    aitown_irrlicht_system_deps
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
    aitown_project_root_includes
)
target_compile_features(integration_tests PRIVATE cxx_std_17)
# src/rendering/  MUST be present: IrrlichtUIBackend.h lives in src/rendering/.
# src/terrain/    TextureCache integration tests include TerrainChunk/TerrainSystem headers.
# src/simulation/ needed by ui_manager_integration_test.cpp (ICitySimulation mock includes simulation headers).
target_include_directories(integration_tests PRIVATE
    tests/simulation/
    tests/ui/
    src/interfaces/
    src/ui/
    src/rendering/
    src/terrain/
    src/simulation/
)
aitown_add_tests(integration_tests LABEL "integration")

# Terrain shader wiring integration tests: TerrainShaderWiring_EDT_NULL_InitDoesNotCrash and
# TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded.
# terrainMaterialTypeForTest() / setTerrainMaterialTypeForTest() accessors must be present
# in IrrlichtRenderer.h for this file to compile.
target_sources(integration_tests PRIVATE
    tests/integration/terrain_shader_wiring_test.cpp
)

# Multi-tile footprint flatten integration tests: verifies that placeBuildingMesh() calls
# setTileHeight() for all (N+1)x(N+1) corner vertices of the building footprint.
target_sources(integration_tests PRIVATE
    tests/integration/irrlicht_renderer_flatten_test.cpp
)

# Coverage-boost integration tests — exercise UIManager panels, TerrainSystem,
# and TerrainChunk via EDT_NULL Irrlicht + NiceMock doubles.
# Note: simulation_comprehensive_integration_test.cpp was reclassified to simulation_tests
# (unit label) — it uses only NiceMock doubles, no EDT_NULL Irrlicht device.
target_sources(integration_tests PRIVATE
    tests/integration/ui_manager_integration_test.cpp
    tests/integration/terrain_system_integration_test.cpp
    tests/integration/ui_manager_events_integration_test.cpp
    tests/integration/camera_and_keybindings_integration_test.cpp
    tests/integration/keybindings_panel_integration_test.cpp
)

# EDT_NULL constructor test — verifies m_hudFont/m_hudMonoFont null on headless.
target_sources(integration_tests PRIVATE
    tests/integration/font_tier_edt_null_test.cpp
)

# ----------------------------------------------------------------------------
# opengl_tests — sRGB upload, setMesh grab/drop smoke test, shader compilation
# Must run under xvfb-run on Linux.
# ----------------------------------------------------------------------------
add_executable(opengl_tests
    tests/rendering/shader_loading_test.cpp
    tests/rendering/lod_swap_smoke_test.cpp
    tests/rendering/building_lod_swap_test.cpp
    tests/rendering/building_srgb_fallback_test.cpp
    tests/rendering/cloud_plane_test.cpp
    tests/rendering/texture_cache_atlas_test.cpp
)
# Irrlicht and GLEW::GLEW MUST be linked directly: shader_loading_test.cpp includes
# <irrlicht.h> and <GL/glew.h>; aitown_render links both PRIVATE so their include paths
# do NOT propagate to consumers (same rationale as integration_tests linking Irrlicht directly).
target_link_libraries(opengl_tests PRIVATE
    aitown_render
    aitown_ui      # required: IrrlichtRenderer.cpp references UIManager::draw()
    Irrlicht
    GLEW::GLEW
    aitown_irrlicht_system_deps
    GTest::gtest_main
    GTest::gmock
    rapidcheck
    rapidcheck_gtest
    aitown_project_root_includes
)
target_compile_features(opengl_tests PRIVATE cxx_std_17)
# src/interfaces/   — IRenderer.h propagated via INTERFACE from aitown_render, but
#                     explicit belt-and-suspenders path ensures #include "vec3.h" inside
#                     IRenderer.h resolves even if aitown_render's INTERFACE scope is refactored.
# ${CMAKE_SOURCE_DIR} — project-root-relative includes
target_include_directories(opengl_tests PRIVATE
    src/interfaces/
)
aitown_add_tests(opengl_tests LABEL "requires-opengl")

endif() # BUILD_TESTING

# ============================================================================
# INSTALL RULES
# Windows (NSIS): binary + DLLs + assets/ + default.mhr alongside aitown.exe.
# Linux (DEB): binary to /usr/games; assets to /usr/share/aitown/assets;
#              default.mhr to /usr/share/openal/hrtf.
# ============================================================================
if(WIN32)
    install(TARGETS aitown RUNTIME DESTINATION .)
    # Assets directory — no trailing slash: installs assets/ as a subdirectory alongside exe.
    install(DIRECTORY assets DESTINATION .)
    # OpenAL Soft runtime (renamed to soft_oal.dll per Windows naming convention).
    install(FILES "${CMAKE_BINARY_DIR}/soft_oal.dll" DESTINATION .)
    # HRTF data file (required for 3D spatial audio; sourced by POST_BUILD copy rule).
    install(FILES "${CMAKE_BINARY_DIR}/default.mhr" DESTINATION .)
    # vcpkg runtime DLLs alongside the binary.
    install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
        DESTINATION .
        FILES_MATCHING PATTERN "*.dll"
    )
else()
    install(TARGETS aitown RUNTIME DESTINATION games)
    # Assets directory — trailing slash: copies contents into share/aitown/assets/.
    install(DIRECTORY assets/ DESTINATION share/aitown/assets)
    # HRTF data file to OpenAL Soft standard search path.
    install(FILES "${CMAKE_BINARY_DIR}/default.mhr" DESTINATION share/openal/hrtf)
endif()

# ============================================================================
# CPACK — Windows NSIS installer and Linux DEB packages.
# Generator is NOT set here; always specify -G NSIS or -G DEB on the cpack command line.
# ============================================================================
set(CPACK_PACKAGE_NAME "AI Town")
set(CPACK_PACKAGE_VENDOR "AI Town Contributors")
if(NOT CPACK_PACKAGE_VERSION)
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
if(NOT CPACK_PACKAGE_FILE_NAME)
  set(CPACK_PACKAGE_FILE_NAME "aitown-${CPACK_PACKAGE_VERSION}")
endif()
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI Town — 3D city simulator")
set(CPACK_PACKAGE_DESCRIPTION
"AI Town is a Sim City-style 3D city simulator built with C++, Irrlicht, and OpenAL Soft.
 Design and manage a growing city: zone land, build roads, manage services,
 and keep your citizens happy.")

# NSIS (Windows installer) settings
set(CPACK_NSIS_DISPLAY_NAME "AI Town")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortcut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\AI Town.lnk' '$INSTDIR\\\\aitown.exe'
     CreateShortcut '$DESKTOP\\\\AI Town.lnk' '$INSTDIR\\\\aitown.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\AI Town.lnk'
     Delete '$DESKTOP\\\\AI Town.lnk'")

# DEB (Debian/Ubuntu package) settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "AI Town Contributors <aitown@example.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libopenal1, libvorbis0a, libgl1, libx11-6, libxrandr2, libxinerama1")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"AI Town — 3D city simulator
 AI Town is a Sim City-style 3D city simulator built with C++, Irrlicht, and OpenAL Soft.
 Design and manage a growing city: zone land, build roads, manage services,
 and keep your citizens happy.")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

include(CPack)
