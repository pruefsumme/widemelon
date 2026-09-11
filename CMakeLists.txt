cmake_minimum_required(VERSION 3.16)

cmake_policy(VERSION 3.15)
if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DefaultBuildFlags.cmake")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

option(USE_VCPKG "Use vcpkg for dependency packages" OFF)
if (USE_VCPKG)
	include(ConfigureVcpkg)
endif()

project(melonDS
    VERSION 1.1
    DESCRIPTION "WideMelon widescreen Nintendo DS emulator"
    HOMEPAGE_URL "https://github.com/pruefsumme/widemelon"
    LANGUAGES C CXX)

# WideMelon releases have their own version; retain the upstream engine version.
set(WIDEMELON_VERSION "1.0.0")

include(CheckSymbolExists)
include(CheckLibraryExists)
include(CMakeDependentOption)
include(CheckIPOSupported)

include(SetupCCache)
include(Sanitizers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

detect_architecture("__x86_64__" x86_64)
detect_architecture("__i386__" x86)
detect_architecture("__arm__" ARM)
detect_architecture("__aarch64__" ARM64)

cmake_dependent_option(ENABLE_JIT "Enable JIT recompiler" ON
    "ARCHITECTURE STREQUAL x86_64 OR ARCHITECTURE STREQUAL ARM64;NOT ARCHITECTURE STREQUAL x86_64 OR NOT APPLE" OFF)
cmake_dependent_option(ENABLE_JIT_PROFILING "Enable JIT profiling with VTune" OFF "ENABLE_JIT" OFF)
option(ENABLE_OGLRENDERER "Enable OpenGL renderer" ON)

check_ipo_supported(RESULT IPO_SUPPORTED)
cmake_dependent_option(ENABLE_LTO_RELEASE "Enable link-time optimizations for release builds" ON "IPO_SUPPORTED" OFF)
cmake_dependent_option(ENABLE_LTO "Enable link-time optimizations" OFF "IPO_SUPPORTED" OFF)

if (ENABLE_LTO_RELEASE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
endif()

if (WIN32)
    option(BUILD_STATIC "Statically link dependencies" OFF)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    if (NOT MINGW)
        add_definitions(-D__WIN32__ -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES)
        if (BUILD_STATIC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    elseif (BUILD_STATIC AND MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_GDBSTUB "Enable GDB stub" ON)
if (ENABLE_GDBSTUB)
    add_definitions(-DGDBSTUB_ENABLED)
endif()

option(BUILD_QT_SDL "Build Qt/SDL frontend" ON)

add_subdirectory(src)

if (BUILD_QT_SDL)
    add_subdirectory(src/frontend/qt_sdl)
endif()

option(WIDEMELON_ENABLE_DEB "Generate a Debian binary package with CPack" OFF)
set(WIDEMELON_PACKAGE_VERSION "${WIDEMELON_VERSION}" CACHE STRING
    "Version used for distribution packages")

if (WIDEMELON_ENABLE_DEB)
    if (NOT UNIX OR APPLE)
        message(FATAL_ERROR "Debian packages can only be generated on Linux")
    endif()

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "widemelon")
    set(CPACK_PACKAGE_VENDOR "WideMelon contributors")
    set(CPACK_PACKAGE_CONTACT "https://github.com/pruefsumme/widemelon/issues")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "WideMelon contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Widescreen Nintendo DS emulator based on melonDS")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/pruefsumme/widemelon")
    set(CPACK_PACKAGE_VERSION "${WIDEMELON_PACKAGE_VERSION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    set(CPACK_DEBIAN_FILE_NAME
        "widemelon_${WIDEMELON_PACKAGE_VERSION}-1_amd64.deb")
    include(CPack)
endif()
