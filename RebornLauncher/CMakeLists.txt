
file(GLOB SRC *.h* *.c* *.rc)
# 排除旧的动画/精灵渲染文件（已替换为新界面）
list(REMOVE_ITEM SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sprite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpriteManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Unit.cpp
)

find_package(cryptopp CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibArchive CONFIG REQUIRED)
find_package(rtc CONFIG REQUIRED)

# Target Windows 7 by defining WINVER and _WIN32_WINNT
add_compile_definitions(
    WINVER=0x0601
    _WIN32_WINNT=0x0601
)

# Set the toolset version for Visual Studio 2015
set(CMAKE_GENERATOR_TOOLSET "v140")

# Optionally set the system version, though it's often not necessary with the toolset
set(CMAKE_SYSTEM_VERSION 7.0)


add_executable(RebornLauncher WIN32 ${SRC})
target_compile_definitions(
	RebornLauncher
	PRIVATE
	-DWIN32
	-D_WINDOWS
	#-D_UNICODE
	#-DUNICODE
	-D_MBCS # 澶氬瓧鑺?
	#-D_AFXDLL
	-DCPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(RebornLauncher cryptopp::cryptopp httplib::httplib JsonCpp::JsonCpp SQLiteCpp zstd::libzstd OpenSSL::SSL 
	#unofficial::minizip::minizip
	OpenSSL::Crypto 
	LibArchive::LibArchive
    rtc
)
#target_link_options(RebornLauncher
#	PRIVATE
#	/ENTRY:wWinMainCRTStartup
#)
# 璁剧疆MT/MTd
set_property(TARGET RebornLauncher PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 璁剧疆閾炬帴娓呭崟UAC鏉冮檺杩愯
set_target_properties(RebornLauncher PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator'\"")
