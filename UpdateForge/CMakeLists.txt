
file(GLOB SRC *.h* *.c* *.rc)

find_package(cryptopp CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_executable(UpdateForge WIN32 ${SRC})
target_compile_definitions(
	UpdateForge
	PRIVATE
		WINVER=0x0601
		_WIN32_WINNT=0x0601
		_WINDOWS
		_UNICODE
)
target_link_libraries(UpdateForge cryptopp::cryptopp JsonCpp::JsonCpp comctl32)
set_property(TARGET UpdateForge PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
