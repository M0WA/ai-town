# cmake/AitownTestHelpers.cmake
# Helper macro wrapping gtest_discover_tests() with required options.
# Enforces the one-label-per-target rule and provides per-category timeout overrides.
#
# Usage: aitown_add_tests(target_name LABEL <unit|integration|requires-opengl>
#        [TIMEOUT <seconds>] [DISCOVERY_TIMEOUT <seconds>] [ENVIRONMENT <VAR=value>])
#
# LABELS MUST be set inside gtest_discover_tests(), NOT via set_tests_properties() afterwards.
# gtest_discover_tests() dynamically creates CTest test entries at configure time;
# calling set_tests_properties() after targets the statically-created wrapper test,
# not the individually-discovered test cases — the labels do not propagate to discovered
# tests and -L/-LE ctest filters will silently fail to include or exclude the correct tests.
#
# DISCOVERY_TIMEOUT default 30: default 5s is insufficient for coverage-instrumented binaries
# on loaded CI runners, silently producing 0 discovered tests and a misleading empty-coverage
# lcov report. Per-target override allows terrain_tests to use 60s.
macro(aitown_add_tests TARGET)
    cmake_parse_arguments(AITOWN_TEST "" "LABEL;TIMEOUT;DISCOVERY_TIMEOUT;ENVIRONMENT" "" ${ARGN})
    if(NOT AITOWN_TEST_LABEL)
        message(FATAL_ERROR "aitown_add_tests: LABEL is required (unit, integration, or requires-opengl)")
    endif()
    if(NOT AITOWN_TEST_TIMEOUT)
        set(AITOWN_TEST_TIMEOUT 120)  # default per-test timeout in seconds
    endif()
    if(NOT AITOWN_TEST_DISCOVERY_TIMEOUT)
        set(AITOWN_TEST_DISCOVERY_TIMEOUT 30)  # default discovery timeout in seconds
    endif()
    set(_AITOWN_ENV_PROPS "")
    if(AITOWN_TEST_ENVIRONMENT)
        set(_AITOWN_ENV_PROPS ENVIRONMENT "${AITOWN_TEST_ENVIRONMENT}")
    endif()
    # WORKING_DIRECTORY must be CMAKE_SOURCE_DIR (project root), NOT the default
    # CMAKE_CURRENT_BINARY_DIR (the build tree).  gtest_discover_tests defaults to
    # the binary dir, which means GTEST_OUTPUT=xml:test_results/ writes XML into
    # build/test_results/ instead of <workspace>/test_results/.  The CI step that
    # collects test XML always looks in <workspace>/test_results/ on both Linux and
    # Windows, so an incorrect working directory silently produces zero XML files
    # and causes dorny/test-reporter to fail with "No test report files were found".
    #
    # DISCOVERY_MODE PRE_TEST: discover tests at ctest time, not at build time.
    # With POST_BUILD (the default), CMake runs the test binary immediately after
    # linking to enumerate test cases.  On Windows with vcpkg x64-windows triplet,
    # GTest/GMock are built as shared DLLs (gtest.dll, gmock.dll) in
    # build/vcpkg_installed/x64-windows/bin/ — NOT in build/ alongside the .exe.
    # The post-build discovery runs before the CI step that adds vcpkg bin to PATH,
    # so the test binary cannot load gtest.dll → exits with a DLL-not-found error →
    # discovery produces empty output → ctest reports "No tests were found!!!" →
    # tests silently skip and no XML is written.
    # PRE_TEST defers discovery to ctest time (inside the test step), where the
    # vcpkg bin directory has already been added to PATH.
    # PRE_TEST was introduced in CMake 3.18; our cmake_minimum_required(3.21) satisfies
    # this requirement on both Linux (ubuntu-latest runner) and Windows (VS2022 runner,
    # CMake 3.28+).
    gtest_discover_tests(${TARGET}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT ${AITOWN_TEST_DISCOVERY_TIMEOUT}
        PROPERTIES TIMEOUT ${AITOWN_TEST_TIMEOUT}
        LABELS "${AITOWN_TEST_LABEL}"
        ${_AITOWN_ENV_PROPS}
    )
endmacro()

# Usage examples:
#
# Terrain tests: longer timeouts for multi-seed generation property tests
#   aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)
#
# Standard unit tests:
#   aitown_add_tests(simulation_tests LABEL "unit" DISCOVERY_TIMEOUT 60)  # 60s: 8-file binary with RapidCheck property tests under coverage instrumentation
#   aitown_add_tests(ui_tests LABEL "unit")
#   aitown_add_tests(audio_tests LABEL "unit")
#
# OpenGL tests (requires xvfb-run on Linux):
#   aitown_add_tests(opengl_tests LABEL "requires-opengl")
#
# Integration tests:
#   aitown_add_tests(integration_tests LABEL "integration")
#   aitown_add_tests(integration_tests LABEL "integration" ENVIRONMENT "ALSOFT_DRIVERS=null")
