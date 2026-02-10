set(SRC dllmain.cpp)

add_library(CryptoLoader SHARED ${SRC})

# 加入detours 库
target_link_libraries(CryptoLoader PRIVATE detours::detours)

target_compile_definitions(
    CryptoLoader
    PRIVATE
        WINVER=0x0601
        _WIN32_WINNT=0x0601
        _WINDOWS
)

set_property(TARGET CryptoLoader PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
