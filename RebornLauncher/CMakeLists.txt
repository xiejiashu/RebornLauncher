
file(GLOB SRC *.h* *.c* *.rc)

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
	-D_MBCS # 多字节
	#-D_AFXDLL
	-DCPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(RebornLauncher cryptopp::cryptopp httplib::httplib JsonCpp::JsonCpp SQLiteCpp zstd::libzstd OpenSSL::SSL 
	#unofficial::minizip::minizip
	OpenSSL::Crypto 
	LibArchive::LibArchive
)
#target_link_options(RebornLauncher
#	PRIVATE
#	/ENTRY:wWinMainCRTStartup
#)
# 设置MT/MTd
set_property(TARGET RebornLauncher PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 设置链接清单UAC权限运行
set_target_properties(RebornLauncher PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator'\"")