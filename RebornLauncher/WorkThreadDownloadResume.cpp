#include "framework.h"
#include "WorkThread.h"

#include <iostream>

#include "Encoding.h"
#include "WorkThreadNetUtils.h"
#include "WorkThreadResumeDownload.h"

namespace {

using workthread::netutils::NormalizeRelativeUrlPath;

} // namespace

bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path, DWORD ownerProcessId) {
	std::string strUrl = NormalizeRelativeUrlPath(url);
	std::cout << __FILE__ << ":" << __LINE__ << " url:" << m_networkState.url << " page:" << strUrl << std::endl;
	const std::wstring filePathW = str2wstr(file_path, static_cast<int>(file_path.length()));
	SetCurrentDownloadFile(filePathW);
	MarkClientDownloadStart(ownerProcessId, filePathW);
	MarkClientDownloadProgress(ownerProcessId, 0, 0);

	workthread::resume::ResumeDownloader downloader(m_networkState, m_downloadState);
	const auto reportProgress = [this, ownerProcessId](uint64_t downloaded, uint64_t total) {
		MarkClientDownloadProgress(ownerProcessId, downloaded, total);
	};

	if (downloader.TryP2P(strUrl, file_path, reportProgress)) {
		return true;
	}

	if (m_networkState.client == nullptr) {
		MarkClientDownloadFinished(ownerProcessId);
		return false;
	}

	const bool ok = downloader.DownloadHttp(strUrl, file_path, reportProgress);
	MarkClientDownloadFinished(ownerProcessId);
	return ok;
}
