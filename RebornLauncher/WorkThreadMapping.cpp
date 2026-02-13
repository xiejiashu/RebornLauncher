#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <json/json.h>

namespace {

constexpr const char* kVersionMapMappingName = "MapleFireReborn.VersionFileMd5Map";
constexpr size_t kVersionMapMaxBytes = 8 * 1024 * 1024;

std::string TrimAscii(std::string value) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
		value.erase(value.begin());
	}
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
		value.pop_back();
	}
	return value;
}

std::string GetLowerAscii(std::string value) {
	for (auto& c : value) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return value;
}

std::string GetUpperAscii(std::string value) {
	for (auto& c : value) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return value;
}

std::string NormalizeMappingPathKey(std::filesystem::path path) {
	path = path.lexically_normal();
	std::string key = path.generic_string();
	std::replace(key.begin(), key.end(), '/', '\\');
	return GetLowerAscii(key);
}

} // namespace

void WorkThread::WriteDataToMapping()
{
	for (auto& [strPage, config] : m_mapFiles)
	{
		std::string strMemoryName = strPage;
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, static_cast<DWORD>(config.m_strMd5.length() + 1), strMemoryName.c_str());
		if (hFileMapping)
		{
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}

void WorkThread::WriteVersionToMapping(std::string& m_strRemoteVersionJson)
{
	Json::Value manifestRoot;
	Json::CharReaderBuilder readerBuilder;
	std::string errors;
	std::istringstream manifestStream(m_strRemoteVersionJson);
	if (!Json::parseFromStream(readerBuilder, manifestStream, &manifestRoot, &errors) || !manifestRoot.isObject()) {
		std::cout << "WriteVersionToMapping: invalid manifest json." << std::endl;
		return;
	}

	Json::Value mappedFiles(Json::objectValue);
	std::error_code ec;
	const std::filesystem::path currentDir = std::filesystem::current_path(ec);
	if (ec) {
		std::cout << "WriteVersionToMapping: failed to get current path." << std::endl;
		return;
	}

	const Json::Value filesJson = manifestRoot["file"];
	if (filesJson.isArray()) {
		for (const auto& fileJson : filesJson) {
			const std::string page = TrimAscii(fileJson["page"].asString());
			const std::string md5 = GetUpperAscii(TrimAscii(fileJson["md5"].asString()));
			if (page.empty() || md5.empty()) {
				continue;
			}

			try {
				const std::filesystem::path fullPath = (currentDir / std::filesystem::u8path(page)).lexically_normal();
				const std::string pathKey = NormalizeMappingPathKey(fullPath);
				if (!pathKey.empty()) {
					mappedFiles[pathKey] = md5;
				}
			}
			catch (...) {
				std::cout << "WriteVersionToMapping: skip invalid page path: " << page << std::endl;
			}
		}
	}

	std::ostringstream payloadBuilder;
	for (const auto& key : mappedFiles.getMemberNames()) {
		const Json::Value& md5 = mappedFiles[key];
		if (!md5.isString()) {
			continue;
		}
		payloadBuilder << key << '\t' << md5.asString() << '\n';
	}

	const std::string payload = payloadBuilder.str();
	if (payload.empty()) {
		std::cout << "WriteVersionToMapping: empty payload." << std::endl;
		return;
	}
	if (payload.size() + 1 > kVersionMapMaxBytes) {
		std::cout << "WriteVersionToMapping: payload too large, bytes=" << payload.size() << std::endl;
		return;
	}

	if (m_hMappingVersion) {
		CloseHandle(m_hMappingVersion);
		m_hMappingVersion = nullptr;
	}

	m_hMappingVersion = CreateFileMappingA(
		INVALID_HANDLE_VALUE,
		nullptr,
		PAGE_READWRITE,
		0,
		static_cast<DWORD>(payload.size() + 1),
		kVersionMapMappingName);
	if (!m_hMappingVersion) {
		std::cout << "WriteVersionToMapping: CreateFileMappingA failed, error=" << GetLastError() << std::endl;
		return;
	}

	void* view = MapViewOfFile(m_hMappingVersion, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!view) {
		std::cout << "WriteVersionToMapping: MapViewOfFile failed, error=" << GetLastError() << std::endl;
		CloseHandle(m_hMappingVersion);
		m_hMappingVersion = nullptr;
		return;
	}

	memcpy(view, payload.data(), payload.size());
	static_cast<char*>(view)[payload.size()] = '\0';
	UnmapViewOfFile(view);
}
