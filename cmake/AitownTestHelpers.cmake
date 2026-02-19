# cmake/AitownTestHelpers.cmake
# Helper macro wrapping gtest_discover_tests() with required options.
# Enforces the one-label-per-target rule and provides per-category timeout overrides.
#
# Usage: aitown_add_tests(target_name LABEL <unit|integration|requires-opengl> [TIMEOUT <seconds>])
#
# LABELS MUST be set inside gtest_discover_tests(), NOT via set_tests_properties() afterwards.
# gtest_discover_tests() dynamically creates CTest test entries at configure time;
# calling set_tests_properties() after targets the statically-created wrapper test,
# not the individually-discovered test cases — the labels do not propagate to discovered
# tests and -L/-LE ctest filters will silently fail to include or exclude the correct tests.
#
# DISCOVERY_TIMEOUT 30: default 5s is insufficient for coverage-instrumented binaries
# on loaded CI runners, silently producing 0 discovered tests and a misleading empty-coverage
# lcov report.
macro(aitown_add_tests TARGET)
    cmake_parse_arguments(AITOWN_TEST "" "LABEL;TIMEOUT" "" ${ARGN})
    if(NOT AITOWN_TEST_LABEL)
        message(FATAL_ERROR "aitown_add_tests: LABEL is required (unit, integration, or requires-opengl)")
    endif()
    if(NOT AITOWN_TEST_TIMEOUT)
        set(AITOWN_TEST_TIMEOUT 120)  # default per-test timeout in seconds
    endif()
    gtest_discover_tests(${TARGET}
        DISCOVERY_TIMEOUT 30
        PROPERTIES TIMEOUT ${AITOWN_TEST_TIMEOUT}
        LABELS "${AITOWN_TEST_LABEL}"
    )
endmacro()

# Usage examples:
#
# Terrain tests: longer timeout for multi-seed generation property tests
#   aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300)
#
# Standard unit tests:
#   aitown_add_tests(simulation_tests LABEL "unit")
#   aitown_add_tests(ui_tests LABEL "unit")
#   aitown_add_tests(audio_tests LABEL "unit")
#
# OpenGL tests (requires xvfb-run on Linux):
#   aitown_add_tests(opengl_tests LABEL "requires-opengl")
#
# Integration tests:
#   aitown_add_tests(integration_tests LABEL "integration")
