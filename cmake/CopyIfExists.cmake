# CopyIfExists.cmake — copies SRC to DST only when SRC exists.
# Invoked as: cmake -DSRC=<path> -DDST=<path> -P CopyIfExists.cmake
# Silently skips (with a status message) when SRC is absent, so the build
# does not fail when vcpkg was installed without the overlay that provides the file.
if(EXISTS "${SRC}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST}"
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${SRC} to ${DST}")
    endif()
else()
    message(STATUS "CopyIfExists: ${SRC} not found — skipping (HRTF data unavailable)")
endif()
