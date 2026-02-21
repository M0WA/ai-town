# cmake/FindIrrlicht.cmake
# Fallback find-module for Irrlicht if the vcpkg port does not ship a config-mode package.
# The irrlicht vcpkg port ships irrlicht-config.cmake (wrapping irrlicht-targets.cmake) in
# share/irrlicht/. If that config file is absent (e.g. manual install, non-vcpkg setup),
# this module locates the library and creates an IMPORTED target.
#
# SPIKE RESOLVED — Architecture Decision:
#   Irrlicht CMake target name: Irrlicht (no namespace prefix)
#   Verified by inspecting the CMakeLists.txt used by the vcpkg irrlicht port
#   (adrido/irrlicht-vcpkg at microsoft/vcpkg baseline f7423ee):
#     install(TARGETS Irrlicht EXPORT Irrlicht ...)
#     install(EXPORT Irrlicht FILE irrlicht-targets.cmake DESTINATION share/irrlicht)
#   No NAMESPACE is specified in install(EXPORT ...), so the exported target name
#   in irrlicht-targets.cmake is the bare name Irrlicht, not Irrlicht::Irrlicht.
#   vcpkg_fixup_cmake_targets does NOT inject namespaces — it only moves cmake files.
#   CMakeLists.txt uses: target_link_libraries(aitown_render PRIVATE Irrlicht)
#
# This fallback module creates an IMPORTED target named Irrlicht (matching the vcpkg
# config-mode target name) so that CMakeLists.txt does not need to branch on whether
# the package was found via config mode or this find-module.

# Search for the Irrlicht header — the presence of irrlicht.h determines if the install is valid.
find_path(IRRLICHT_INCLUDE_DIR
    NAMES irrlicht.h
    PATH_SUFFIXES irrlicht
    HINTS
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
        $ENV{IRRLICHT_HOME}/include
        /usr/include
        /usr/local/include
)

# Search for the Irrlicht library.
find_library(IRRLICHT_LIBRARY
    NAMES Irrlicht irrlicht
    HINTS
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib
        $ENV{IRRLICHT_HOME}/lib
        /usr/lib
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Irrlicht
    REQUIRED_VARS IRRLICHT_LIBRARY IRRLICHT_INCLUDE_DIR
)

if(Irrlicht_FOUND AND NOT TARGET Irrlicht)
    # Target name is Irrlicht (no namespace) — matches the vcpkg config-mode exported name.
    # See SPIKE RESOLVED note at the top of this file.
    add_library(Irrlicht UNKNOWN IMPORTED)
    set_target_properties(Irrlicht PROPERTIES
        IMPORTED_LOCATION "${IRRLICHT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${IRRLICHT_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(IRRLICHT_INCLUDE_DIR IRRLICHT_LIBRARY)
