#===============================================================================
# PCAL9555 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(PCAL9555_TARGET_NAME "pcal9555")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(PCAL9555_VERSION_MAJOR 0)
set(PCAL9555_VERSION_MINOR 1)
set(PCAL9555_VERSION_PATCH 0)
set(PCAL9555_VERSION "${PCAL9555_VERSION_MAJOR}.${PCAL9555_VERSION_MINOR}.${PCAL9555_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(PCAL9555_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/pcal9555_version.h.in")
set(PCAL9555_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/pcal9555_generated")
set(PCAL9555_VERSION_HEADER "${PCAL9555_VERSION_HEADER_DIR}/pcal9555_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${PCAL9555_VERSION_HEADER_DIR}")
configure_file("${PCAL9555_VERSION_TEMPLATE}" "${PCAL9555_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(PCAL9555_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${PCAL9555_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/pcal9555.ipp, included by inc/pcal9555.hpp.
set(PCAL9555_SOURCE_FILES)

set(PCAL9555_IDF_REQUIRES)
