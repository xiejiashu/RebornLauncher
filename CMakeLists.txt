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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 1 static MFC library, 2 shared MFC library
# set(CMAKE_MFC_FLAG 1)
# minizip
# unofficial-minizip CONFIG REQUIRED

set(CMAKE_TOOLCHAIN_FILE "G:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE PATH "vcpkg toolchain file")
set(VCPKG_INSTALLED_DIR "${PROJECT_SOURCE_DIR}/vcpkg_installed")
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use classic vcpkg mode with preinstalled packages")

find_package(zstd CONFIG REQUIRED)

# detours port in vcpkg does not ship a CMake config; fall back to manual locate.
find_package(detours CONFIG QUIET)
if (NOT detours_FOUND)
    find_path(DETOURS_INCLUDE_DIR detours/detours.h)
    find_library(DETOURS_LIBRARY detours)
    if (DETOURS_INCLUDE_DIR AND DETOURS_LIBRARY)
        add_library(detours::detours UNKNOWN IMPORTED)
        set_target_properties(detours::detours PROPERTIES
            IMPORTED_LOCATION "${DETOURS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${DETOURS_INCLUDE_DIR}")
        set(detours_FOUND TRUE)
    else()
        message(FATAL_ERROR "detours not found; install via vcpkg or set DETOURS_INCLUDE_DIR / DETOURS_LIBRARY")
    endif()
endif()

add_subdirectory(CryptoLoader)
add_subdirectory(RebornLauncher)
add_subdirectory(UpdateForge)
