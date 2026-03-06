#include "framework.h"
#include "RuntimeUpdater.h"

#include <algorithm>
#include <iostream>

#include "Encoding.h"
#include "LauncherUpdateCoordinator.h"
#include "NetUtils.h"

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

RuntimeUpdater::RuntimeUpdater(LauncherUpdateCoordinator& worker)
	: m_worker(worker) {
}

bool RuntimeUpdater::Execute() {
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
				"RuntimeUpdater::Execute",
				"Runtime file is missing from manifest map",
				std::string("runtime_file=") + download);
			return false;
		}

		const std::string& resolvedPage = cfgIt->first;
		const VersionConfig& resolvedConfig = cfgIt->second;
		std::string strLocalFile = resolvedPage;
		if (m_worker.IsRuntimeUpdateSkipped(strLocalFile)) {
			std::cout << "Skipping runtime update by NoUPdate.txt: " << strLocalFile << std::endl;
			m_worker.SetLauncherStatus(L"Up to date: " + str2wstr(strLocalFile));
			m_worker.m_downloadState.currentDownload += 1;
			continue;
		}
		m_worker.SetLauncherStatus(L"Preparing update: " + str2wstr(strLocalFile));
		const std::string strPage = workthread::netutils::JoinUrlPath(
			m_worker.m_networkState.page,
			std::to_string(resolvedConfig.m_qwTime) + "/" + resolvedPage);

		std::cout << "Updating runtime file from: " << strPage << std::endl;
		m_worker.SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_worker.m_downloadState.currentDownloadSize = resolvedConfig.m_qwSize;
		m_worker.m_downloadState.currentDownloadProgress = 0;
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

		bool allowDeferredOnBusy = false;
#ifndef _DEBUG
		if (strLocalFile == "UpdateTemp.exe") {
			allowDeferredOnBusy = false;
			const std::wstring updateTempW = str2wstr(strLocalFile);
			if (!updateTempW.empty()) {
				SetFileAttributesW(updateTempW.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(updateTempW.c_str());
			}
			else {
				std::error_code ec;
				std::filesystem::remove(std::filesystem::u8path(strLocalFile), ec);
			}
		}
#endif

		bool queuedForDeferred = false;
		if (!m_worker.DownloadWithResume(strPage, strLocalFile, 0, allowDeferredOnBusy, &queuedForDeferred)) {
			std::cout << "Download failed for runtime file: " << strPage << std::endl;
			m_worker.LogUpdateError(
				"UF-RUNTIME-DOWNLOAD",
				"RuntimeUpdater::Execute",
				"Runtime file download failed",
				std::string("remote_path=") + strPage + ", local_file=" + strLocalFile);
			return false;
		}
		if (queuedForDeferred) {
			std::cout << "Runtime file is busy, queued for deferred retry: " << strLocalFile << std::endl;
		}
		m_worker.m_downloadState.currentDownload += 1;
	}
	m_worker.SetLauncherStatus(L"Runtime updates complete.");

	return true;
}

} // namespace workthread::runtimeupdate
