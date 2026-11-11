#===============================================================================
# PCAL9555 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   pcal9555, consumed as pcal9555::pcal9555 both through add_subdirectory() and
#   through find_package(pcal9555) after install.
#
#   cmake -S . -B build -DPCAL9555_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/pcal9555_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${PCAL9555_SOURCE_FILES}
        INCLUDE_DIRS ${PCAL9555_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${PCAL9555_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(PCAL9555_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(pcal9555_driver
    VERSION ${PCAL9555_VERSION}
    DESCRIPTION "C++20 driver for the PCA9555 and PCAL9555A 16-bit I2C GPIO expanders."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because pcal9555.hpp includes
# "../src/pcal9555.ipp" relative to itself:
#   <prefix>/include/pcal9555/inc/*.hpp, pcal9555_version.h
#   <prefix>/include/pcal9555/src/pcal9555.ipp
set(PCAL9555_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/pcal9555/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${PCAL9555_TARGET_NAME} INTERFACE)
target_include_directories(${PCAL9555_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${PCAL9555_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${PCAL9555_INSTALL_INCLUDEDIR}>
)
target_compile_features(${PCAL9555_TARGET_NAME} INTERFACE cxx_std_20)

# pcal9555::pcal9555 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(pcal9555::pcal9555 ALIAS ${PCAL9555_TARGET_NAME})
set_target_properties(${PCAL9555_TARGET_NAME} PROPERTIES EXPORT_NAME pcal9555)

option(PCAL9555_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links pcal9555::pcal9555" OFF)
if(PCAL9555_ENABLE_WARNINGS)
    target_compile_options(${PCAL9555_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(PCAL9555_BUILD_TESTS "Build the host tests in tests/" OFF)
option(PCAL9555_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(PCAL9555_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${PCAL9555_TARGET_NAME} EXPORT pcal9555Targets
    INCLUDES DESTINATION ${PCAL9555_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${PCAL9555_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${PCAL9555_VERSION_HEADER}" DESTINATION ${PCAL9555_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pcal9555/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT pcal9555Targets
    FILE pcal9555Targets.cmake
    NAMESPACE pcal9555::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcal9555)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/pcal9555Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pcal9555Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcal9555)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pcal9555ConfigVersion.cmake"
    VERSION ${PCAL9555_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pcal9555Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pcal9555ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcal9555)
