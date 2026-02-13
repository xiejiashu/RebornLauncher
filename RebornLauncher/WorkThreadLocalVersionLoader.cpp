#include "framework.h"
#include "WorkThreadLocalVersionLoader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <json/json.h>

#include "FileHash.h"
#include "WorkThread.h"

namespace workthread::versionload {

WorkThreadLocalVersionLoader::WorkThreadLocalVersionLoader(WorkThread& worker)
	: m_worker(worker) {
}

void WorkThreadLocalVersionLoader::Execute() {
	std::string strLocalVersionDatContent;
	std::ifstream ifs("Version.dat", std::ios::binary);
	if (ifs.is_open()) {
		std::stringstream buffer;
		buffer << ifs.rdbuf();
		strLocalVersionDatContent = buffer.str();
		ifs.close();
	}
	m_worker.m_versionState.localVersionMD5 = FileHash::string_md5(strLocalVersionDatContent);

	if (strLocalVersionDatContent.empty()) {
		return;
	}

	std::string strLocalVersionDat = m_worker.DecryptVersionDat(strLocalVersionDatContent);
	std::string strLocalManifestJson;
	Json::Value root;
	Json::Reader reader;
	bool parsedLocalManifest = false;

	if (!strLocalVersionDat.empty() && reader.parse(strLocalVersionDat, root)) {
		strLocalManifestJson = strLocalVersionDat;
		parsedLocalManifest = true;
	}
	else if (reader.parse(strLocalVersionDatContent, root)) {
		strLocalManifestJson = strLocalVersionDatContent;
		parsedLocalManifest = true;
	}

	if (!parsedLocalManifest) {
		return;
	}

	m_worker.WriteVersionToMapping(strLocalManifestJson);
	Json::Value filesJson = root["file"];
	for (auto& fileJson : filesJson) {
		VersionConfig config;
		config.m_strMd5 = fileJson["md5"].asString();
		config.m_qwTime = fileJson["time"].asInt64();
		config.m_qwSize = fileJson["size"].asInt64();
		config.m_strPage = fileJson["page"].asString();
		if (config.m_strPage.empty()) {
			continue;
		}
		m_worker.m_versionState.files[config.m_strPage] = config;
		try {
			const std::filesystem::path localPath =
				std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
			std::error_code ec;
			const auto parent = localPath.parent_path();
			if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
				std::filesystem::create_directories(parent, ec);
			}
			ec.clear();
			if (!std::filesystem::exists(localPath, ec)) {
				std::ofstream ofs(localPath, std::ios::binary);
				ofs.close();
			}
		}
		catch (...) {
			std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
			m_worker.m_versionState.files.erase(config.m_strPage);
		}
	}

	Json::Value downloadList = root["runtime"];
	for (auto& download : downloadList) {
		m_worker.m_versionState.runtimeList.push_back(download.asString());
	}
}

} // namespace workthread::versionload
