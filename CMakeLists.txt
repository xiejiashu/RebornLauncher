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
# 鏀寔Win7浠ヤ笅
# 璁╂墍鏈夊簱鏀寔win7浠ヤ笅缂栬瘧

set(CMAKE_TOOLCHAIN_FILE "G:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE PATH "vcpkg toolchain file")
set(VCPKG_INSTALLED_DIR "G:/vcpkg/installed/x86-windows")

find_package(zstd CONFIG REQUIRED)
find_package(detours CONFIG REQUIRED)

add_subdirectory(CryptoLoader)
add_subdirectory(RebornLauncher)
add_subdirectory(UpdateForge)
