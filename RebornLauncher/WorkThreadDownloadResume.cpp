#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Encoding.h"
#include "FileHash.h"
#include "WorkThreadNetUtils.h"
#include "WorkThreadResumeDownload.h"

namespace {

using workthread::netutils::NormalizeRelativeUrlPath;

std::string ToLowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		if (ch >= 'A' && ch <= 'Z') {
			return static_cast<char>(ch - 'A' + 'a');
		}
		return static_cast<char>(ch);
	});
	return value;
}

const VersionConfig* ResolveVersionConfigByPath(
	const std::map<std::string, VersionConfig>& files,
	const std::string& filePath) {
	auto it = files.find(filePath);
	if (it != files.end()) {
		return &it->second;
	}

	std::string slashToBack = filePath;
	std::replace(slashToBack.begin(), slashToBack.end(), '/', '\\');
	it = files.find(slashToBack);
	if (it != files.end()) {
		return &it->second;
	}

	std::string backToSlash = filePath;
	std::replace(backToSlash.begin(), backToSlash.end(), '\\', '/');
	it = files.find(backToSlash);
	if (it != files.end()) {
		return &it->second;
	}

	return nullptr;
}

} // namespace

bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path, DWORD ownerProcessId) {
	std::string strUrl = NormalizeRelativeUrlPath(url);
	std::cout << __FILE__ << ":" << __LINE__ << " url:" << m_networkState.url << " page:" << strUrl << std::endl;
	const std::wstring filePathW = str2wstr(file_path, static_cast<int>(file_path.length()));
	SetLauncherStatus(L"Downloading: " + filePathW);
	SetCurrentDownloadFile(filePathW);
	MarkClientDownloadStart(ownerProcessId, filePathW);
	MarkClientDownloadProgress(ownerProcessId, 0, 0);

	workthread::resume::ResumeDownloader downloader(m_networkState, m_downloadState);
	const auto reportProgress = [this, ownerProcessId](uint64_t downloaded, uint64_t total) {
		MarkClientDownloadProgress(ownerProcessId, downloaded, total);
	};
	const auto resetLocalFile = [&]() -> bool {
		std::error_code ec;
		const auto fsPath = std::filesystem::u8path(file_path);
		std::filesystem::remove(fsPath, ec);
		ec.clear();
		if (!std::filesystem::exists(fsPath, ec)) {
			return true;
		}

		std::ofstream trunc(fsPath, std::ios::binary | std::ios::trunc);
		if (!trunc.is_open()) {
			LogUpdateError(
				"UF-DL-RESET",
				"WorkThread::DownloadWithResume",
				"Failed to reset local file before retry",
				std::string("file=") + file_path,
				GetLastError());
			return false;
		}
		trunc.close();

		ec.clear();
		const auto afterSize = std::filesystem::file_size(fsPath, ec);
		if (ec || afterSize != 0) {
			LogUpdateError(
				"UF-DL-RESET",
				"WorkThread::DownloadWithResume",
				"Local file reset verification failed",
				std::string("file=") + file_path + ", size_after_reset=" + std::to_string(afterSize));
			return false;
		}
		return true;
	};
	const auto verifyDownloadedFile = [&]() -> bool {
		const auto fsPath = std::filesystem::u8path(file_path);
		std::error_code ec;
		if (!std::filesystem::exists(fsPath, ec)) {
			LogUpdateError(
				"UF-DL-VERIFY",
				"WorkThread::DownloadWithResume",
				"Downloaded file is missing after transfer",
				std::string("url=") + strUrl + ", file=" + file_path);
			return false;
		}

		const uint64_t fileSize = std::filesystem::file_size(fsPath, ec);
		if (ec) {
			LogUpdateError(
				"UF-DL-VERIFY",
				"WorkThread::DownloadWithResume",
				"Failed to read downloaded file size",
				std::string("url=") + strUrl + ", file=" + file_path + ", error=" + ec.message());
			return false;
		}

		const VersionConfig* cfg = ResolveVersionConfigByPath(m_versionState.files, file_path);
		if (cfg && cfg->m_qwSize > 0) {
			const uint64_t expectedSize = static_cast<uint64_t>(cfg->m_qwSize);
			if (fileSize != expectedSize) {
				LogUpdateError(
					"UF-DL-VERIFY",
					"WorkThread::DownloadWithResume",
					"Downloaded file size mismatch",
					std::string("url=") + strUrl + ", file=" + file_path +
					", expected_size=" + std::to_string(expectedSize) +
					", actual_size=" + std::to_string(fileSize));
				return false;
			}
		}
		else if (m_downloadState.currentDownloadSize > 0) {
			const uint64_t expectedSize = static_cast<uint64_t>(m_downloadState.currentDownloadSize);
			if (fileSize != expectedSize) {
				LogUpdateError(
					"UF-DL-VERIFY",
					"WorkThread::DownloadWithResume",
					"Downloaded file size mismatch (progress state)",
					std::string("url=") + strUrl + ", file=" + file_path +
					", expected_size=" + std::to_string(expectedSize) +
					", actual_size=" + std::to_string(fileSize));
				return false;
			}
		}

		if (cfg && !cfg->m_strMd5.empty()) {
			const std::string actualMd5 = ToLowerAscii(FileHash::file_md5(file_path));
			const std::string expectedMd5 = ToLowerAscii(cfg->m_strMd5);
			if (actualMd5.empty() || actualMd5 != expectedMd5) {
				LogUpdateError(
					"UF-DL-VERIFY",
					"WorkThread::DownloadWithResume",
					"Downloaded file MD5 mismatch",
					std::string("url=") + strUrl + ", file=" + file_path +
					", expected_md5=" + expectedMd5 +
					", actual_md5=" + actualMd5);
				return false;
			}
		}
		return true;
	};

	if (downloader.TryP2P(strUrl, file_path, reportProgress)) {
		if (verifyDownloadedFile()) {
			SetLauncherStatus(L"Downloaded via P2P: " + filePathW);
			MarkClientDownloadFinished(ownerProcessId);
			return true;
		}
		SetLauncherStatus(L"P2P file verification failed, retrying HTTP...");
		if (!resetLocalFile()) {
			MarkClientDownloadFinished(ownerProcessId);
			return false;
		}
	}

	if (m_networkState.client == nullptr) {
		SetLauncherStatus(L"Failed: HTTP client unavailable.");
		LogUpdateError(
			"UF-DL-HTTPCLIENT",
			"WorkThread::DownloadWithResume",
			"HTTP client is not initialized after P2P fallback",
			std::string("url=") + strUrl + ", file=" + file_path);
		MarkClientDownloadFinished(ownerProcessId);
		return false;
	}

	bool ok = false;
	for (int attempt = 1; attempt <= 2; ++attempt) {
		ok = downloader.DownloadHttp(strUrl, file_path, reportProgress);
		if (!ok) {
			LogUpdateError(
				"UF-DL-HTTP",
				"WorkThread::DownloadWithResume",
				"HTTP resume download failed",
				std::string("url=") + strUrl + ", file=" + file_path +
				", attempt=" + std::to_string(attempt));
			continue;
		}

		if (verifyDownloadedFile()) {
			SetLauncherStatus(L"Downloaded: " + filePathW);
			break;
		}

		ok = false;
		if (attempt < 2) {
			SetLauncherStatus(L"Downloaded file incomplete, retrying...");
			if (!resetLocalFile()) {
				break;
			}
		}
	}

	if (!ok) {
		SetLauncherStatus(L"Failed: HTTP download " + filePathW);
	}

	MarkClientDownloadFinished(ownerProcessId);
	return ok;
}
