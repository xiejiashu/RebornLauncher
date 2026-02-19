#include "framework.h"
#include "WorkThreadRuntimeUpdater.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "FileHash.h"
#include "Encoding.h"
#include "WorkThread.h"
#include "WorkThreadNetUtils.h"

namespace workthread::runtimeupdate {

namespace {

std::map<std::string, VersionConfig>::const_iterator ResolveFileConfigByPage(
	const std::map<std::string, VersionConfig>& files,
	const std::string& requestedPage) {
	auto it = files.find(requestedPage);
	if (it != files.end()) {
		return it;
	}

	std::string slashToBack = requestedPage;
	std::replace(slashToBack.begin(), slashToBack.end(), '/', '\\');
	it = files.find(slashToBack);
	if (it != files.end()) {
		return it;
	}

	std::string backToSlash = requestedPage;
	std::replace(backToSlash.begin(), backToSlash.end(), '\\', '/');
	it = files.find(backToSlash);
	if (it != files.end()) {
		return it;
	}

	return files.end();
}

} // namespace

WorkThreadRuntimeUpdater::WorkThreadRuntimeUpdater(WorkThread& worker)
	: m_worker(worker) {
}

bool WorkThreadRuntimeUpdater::Execute() {
	m_worker.m_downloadState.totalDownload = m_worker.m_versionState.runtimeList.size();
	m_worker.m_downloadState.currentDownload = 0;
	m_worker.SetLauncherStatus(L"Checking runtime files...");
	for (auto& download : m_worker.m_versionState.runtimeList)
	{
		auto cfgIt = ResolveFileConfigByPage(m_worker.m_versionState.files, download);
		if (cfgIt == m_worker.m_versionState.files.end()) {
			std::cout << "Runtime file not found in manifest file map: " << download << std::endl;
			m_worker.LogUpdateError(
				"UF-RUNTIME-MISSING",
				"WorkThreadRuntimeUpdater::Execute",
				"Runtime file is missing from manifest map",
				std::string("runtime_file=") + download);
			return false;
		}

		const std::string& resolvedPage = cfgIt->first;
		const VersionConfig& resolvedConfig = cfgIt->second;
		std::string strLocalFile = resolvedPage;
		m_worker.SetLauncherStatus(L"Preparing update: " + str2wstr(strLocalFile));
		const std::string strPage = workthread::netutils::JoinUrlPath(
			m_worker.m_networkState.page,
			std::to_string(resolvedConfig.m_qwTime) + "/" + resolvedPage);

		std::cout << "Updating runtime file from: " << strPage << std::endl;
		m_worker.SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_worker.m_downloadState.currentDownloadSize = resolvedConfig.m_qwSize;
		m_worker.m_downloadState.currentDownloadProgress = 0;

		auto it = m_worker.m_versionState.files.find(resolvedPage);
		if (it != m_worker.m_versionState.files.end())
		{
			bool md5Same = false;
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::u8path(strLocalFile), ec)) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				md5Same = it->second.m_strMd5 == strLocalFileMd5;
				std::cout << "md51:" << it->second.m_strMd5 << "vs md52:" << strLocalFileMd5 << std::endl;
			}

			if (md5Same)
			{
				m_worker.SetLauncherStatus(L"Up to date: " + str2wstr(strLocalFile));
				m_worker.m_downloadState.currentDownload += 1;
				continue;
			}
		}
		std::cout << "Updating local runtime file: " << strLocalFile << std::endl;
		m_worker.SetLauncherStatus(L"Downloading update: " + str2wstr(strLocalFile));

		if (strLocalFile.find("RebornLauncher.exe") != std::string::npos)
		{
#ifdef _DEBUG
			continue;
#else
			strLocalFile = "UpdateTemp.exe";
			m_worker.m_selfUpdateState.updateSelf = true;
#endif
		}

		const std::wstring strLocalFileW = str2wstr(strLocalFile);
		if (!strLocalFileW.empty()) {
			SetFileAttributesW(strLocalFileW.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(strLocalFileW.c_str());
		}
		else {
			std::error_code ec;
			std::filesystem::remove(std::filesystem::u8path(strLocalFile), ec);
		}

		if (!m_worker.DownloadWithResume(strPage, strLocalFile)) {
			std::cout << "Download failed for runtime file: " << strPage << std::endl;
			m_worker.LogUpdateError(
				"UF-RUNTIME-DOWNLOAD",
				"WorkThreadRuntimeUpdater::Execute",
				"Runtime file download failed",
				std::string("remote_path=") + strPage + ", local_file=" + strLocalFile);
			return false;
		}
		m_worker.m_downloadState.currentDownload += 1;
	}
	m_worker.SetLauncherStatus(L"Runtime updates complete.");

	return true;
}

} // namespace workthread::runtimeupdate
