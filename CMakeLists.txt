cmake_minimum_required(VERSION 3.20)

project(UpdateSuite LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    $<$<C_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

if (MSVC)
    if (CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
        add_compile_definitions(_X86_)
    elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
        add_compile_definitions(_AMD64_)
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 1 static MFC library, 2 shared MFC library
# set(CMAKE_MFC_FLAG 1)
# minizip
# unofficial-minizip CONFIG REQUIRED

set(CMAKE_TOOLCHAIN_FILE "G:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE PATH "vcpkg toolchain file")
set(VCPKG_INSTALLED_DIR "${PROJECT_SOURCE_DIR}/vcpkg_installed")
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use classic vcpkg mode with preinstalled packages")

option(UPDATESUITE_BUILD_CRYPTOLOADER "Build CryptoLoader" ON)
option(UPDATESUITE_BUILD_REBORNLAUNCHER "Build RebornLauncher" ON)
option(UPDATESUITE_BUILD_UPDATEFORGE "Build UpdateForge" ON)

# detours port in vcpkg does not ship a CMake config; fall back to manual locate.
find_package(detours CONFIG QUIET)
if (NOT detours_FOUND)
    find_path(DETOURS_INCLUDE_DIR detours/detours.h
        PATHS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        NO_DEFAULT_PATH)

    find_library(DETOURS_LIBRARY_RELEASE NAMES detours
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        NO_DEFAULT_PATH)
    find_library(DETOURS_LIBRARY_DEBUG NAMES detours
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        NO_DEFAULT_PATH)

    if (DETOURS_INCLUDE_DIR AND (DETOURS_LIBRARY_RELEASE OR DETOURS_LIBRARY_DEBUG))
        add_library(detours::detours UNKNOWN IMPORTED)
        set_target_properties(detours::detours PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${DETOURS_INCLUDE_DIR}")

        if (DETOURS_LIBRARY_RELEASE)
            set_target_properties(detours::detours PROPERTIES IMPORTED_LOCATION_RELEASE "${DETOURS_LIBRARY_RELEASE}")
        endif()
        if (DETOURS_LIBRARY_DEBUG)
            set_target_properties(detours::detours PROPERTIES IMPORTED_LOCATION_DEBUG "${DETOURS_LIBRARY_DEBUG}")
        endif()

        if (NOT DETOURS_LIBRARY_RELEASE AND DETOURS_LIBRARY_DEBUG)
            set_target_properties(detours::detours PROPERTIES IMPORTED_LOCATION "${DETOURS_LIBRARY_DEBUG}")
        elseif (DETOURS_LIBRARY_RELEASE)
            set_target_properties(detours::detours PROPERTIES IMPORTED_LOCATION "${DETOURS_LIBRARY_RELEASE}")
        endif()
        set(detours_FOUND TRUE)
    else()
        message(FATAL_ERROR "detours not found; install via vcpkg or set DETOURS_INCLUDE_DIR / DETOURS_LIBRARY")
    endif()
endif()

if (UPDATESUITE_BUILD_CRYPTOLOADER)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_subdirectory(CryptoLoader)
    else()
        message(STATUS "Skipping CryptoLoader on non-Win32 generator (CryptoLoader is x86-only).")
    endif()
endif()
if (UPDATESUITE_BUILD_REBORNLAUNCHER)
    add_subdirectory(RebornLauncher)
endif()
if (UPDATESUITE_BUILD_UPDATEFORGE)
    add_subdirectory(UpdateForge)
endif()
