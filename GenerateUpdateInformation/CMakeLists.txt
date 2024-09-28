
file(GLOB SRC *.h* *.c* *.rc)

add_executable(GenerateUpdateInformation WIN32 ${SRC})
target_compile_definitions(
	GenerateUpdateInformation
	PRIVATE
	-DWIN32
	-D_WINDOWS
	-D_UNICODE
	#-DUNICODE
	#-D_MBCS # 多字节
	#-D_AFXDLL
)
target_link_libraries(GenerateUpdateInformation cryptopp::cryptopp httplib::httplib JsonCpp::JsonCpp SQLiteCpp zstd::libzstd)
#target_link_options(GenerateUpdateInformation
#	PRIVATE
#	/ENTRY:wWinMainCRTStartup
#)
# 设置MT/MTd
set_property(TARGET GenerateUpdateInformation PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  