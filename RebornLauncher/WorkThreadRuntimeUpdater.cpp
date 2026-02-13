#include "framework.h"
#include "WorkThreadRuntimeUpdater.h"

#include <filesystem>
#include <iostream>

#include "FileHash.h"
#include "Encoding.h"
#include "WorkThread.h"
#include "WorkThreadNetUtils.h"

namespace workthread::runtimeupdate {

WorkThreadRuntimeUpdater::WorkThreadRuntimeUpdater(WorkThread& worker)
	: m_worker(worker) {
}

bool WorkThreadRuntimeUpdater::Execute() {
	m_worker.m_downloadState.totalDownload = m_worker.m_versionState.runtimeList.size();
	m_worker.m_downloadState.currentDownload = 0;
	for (auto& download : m_worker.m_versionState.runtimeList)
	{
		std::string strLocalFile = download;
		const std::string strPage = workthread::netutils::JoinUrlPath(
			m_worker.m_networkState.page,
			std::to_string(m_worker.m_versionState.files[download].m_qwTime) + "/" + download);

		std::cout << "Updating runtime file from: " << strPage << std::endl;
		m_worker.SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_worker.m_downloadState.currentDownloadSize = m_worker.m_versionState.files[download].m_qwSize;
		m_worker.m_downloadState.currentDownloadProgress = 0;

		auto it = m_worker.m_versionState.files.find(strLocalFile);
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
				m_worker.m_downloadState.currentDownload += 1;
				continue;
			}
		}
		std::cout << "Updating local runtime file: " << strLocalFile << std::endl;

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
			return false;
		}
		m_worker.m_downloadState.currentDownload += 1;
	}

	return true;
}

} // namespace workthread::runtimeupdate
