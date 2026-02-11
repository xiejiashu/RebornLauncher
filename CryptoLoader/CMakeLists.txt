set(SRC dllmain.cpp)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "CryptoLoader must be built as Win32 (x86).")
endif()

add_library(CryptoLoader SHARED ${SRC})

if (CMAKE_SIZEOF_VOID_P EQUAL 4)
  foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_u)
    set_target_properties(CryptoLoader PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${cfg_u} "${PROJECT_SOURCE_DIR}/bin_x86/${cfg}"
      LIBRARY_OUTPUT_DIRECTORY_${cfg_u} "${PROJECT_SOURCE_DIR}/bin_x86/${cfg}"
      ARCHIVE_OUTPUT_DIRECTORY_${cfg_u} "${PROJECT_SOURCE_DIR}/bin_x86/${cfg}")
  endforeach()
endif()

# 加入detours 库
target_link_libraries(CryptoLoader PRIVATE
  detours::detours
  advapi32
  winhttp
)

target_compile_definitions(
    CryptoLoader
    PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        _WINDOWS
)

set_property(TARGET CryptoLoader PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
