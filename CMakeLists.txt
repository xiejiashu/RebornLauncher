﻿cmake_minimum_required(VERSION 3.20)

project(maple
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} /utf-8)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin) # exe

# 1 static MFC library, 2 shared MFC library
# set(CMAKE_MFC_FLAG 1)
# minizip
# unofficial-minizip CONFIG REQUIRED
# 支持Win7以下
# 让所有库支持win7以下编译

find_package(cryptopp CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(zstd CONFIG REQUIRED)

find_package(LibArchive REQUIRED)


message(STATUS "Linking zstd target: ${ZSTD_LIBRARIES}")

add_subdirectory(RebornLauncher)
add_subdirectory(GenerateUpdateInformation)