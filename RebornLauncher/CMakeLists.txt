
file(GLOB SRC *.h* *.c* *.rc)
list(REMOVE_ITEM SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sprite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpriteManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Unit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LauncherMainDlg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LauncherMainDlg.h
)

find_package(httplib CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_library(LZMA_LIBRARY NAMES lzma PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
find_library(LZ4_LIBRARY NAMES lz4 PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
if(NOT LZMA_LIBRARY)
    message(FATAL_ERROR "lzma library not found in vcpkg installed directory")
endif()
if(NOT LZ4_LIBRARY)
    message(FATAL_ERROR "lz4 library not found in vcpkg installed directory")
endif()

# Target Windows 10 by defining WINVER and _WIN32_WINNT
add_compile_definitions(
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)


add_executable(RebornLauncher WIN32 ${SRC})
target_compile_definitions(
	RebornLauncher
	PRIVATE
	-DWIN32
	-D_WINDOWS
	-DUNICODE
	-D_UNICODE
	-DCPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(RebornLauncher
    httplib::httplib
    JsonCpp::JsonCpp
    SQLiteCpp
    zstd::libzstd
    OpenSSL::SSL
    OpenSSL::Crypto
    LibArchive::LibArchive
    LibDataChannel::LibDataChannel
    ZLIB::ZLIB
    BZip2::BZip2
    ${LZMA_LIBRARY}
    ${LZ4_LIBRARY}
)
#target_link_options(RebornLauncher
#	PRIVATE
#	/ENTRY:wWinMainCRTStartup
#)
set_property(TARGET RebornLauncher PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_target_properties(RebornLauncher PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator'\"")
