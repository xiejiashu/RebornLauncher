
file(GLOB SRC *.h* *.c* *.rc)

add_executable(RebornLauncher WIN32 ${SRC})
target_compile_definitions(
	RebornLauncher
	PRIVATE
	-DWIN32
	-D_WINDOWS
	-D_UNICODE
	#-DUNICODE
	#-D_MBCS # 多字节
	#-D_AFXDLL
)
target_link_libraries(RebornLauncher cryptopp::cryptopp httplib::httplib JsonCpp::JsonCpp SQLiteCpp)
#target_link_options(RebornLauncher
#	PRIVATE
#	/ENTRY:wWinMainCRTStartup
#)
# 设置MT/MTd
set_property(TARGET RebornLauncher PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  