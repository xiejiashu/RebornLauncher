#include "framework.h"
#include "ResumeDownload.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <httplib.h>

#include "LauncherUpdateCoordinator.h"
#include "NetUtils.h"

namespace {

bool IsImgFilePath(const std::string& filePath) {
	std::string extension = std::filesystem::u8path(filePath).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return extension == ".img";
}

} // namespace

namespace workthread::resume {

ResumeDownloader::ResumeDownloader(NetworkState& networkState, DownloadProgressState& downloadState)
	: m_networkState(networkState), m_downloadState(downloadState) {
}

bool ResumeDownloader::TryP2P(
	const std::string& normalizedUrl,
	const std::string& filePath,
	const std::function<void(uint64_t, uint64_t)>& onProgress) {
	P2PSettings settingsCopy;
	{
		std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
		settingsCopy = m_networkState.p2pSettings;
	}
	if (!settingsCopy.enabled || !m_networkState.p2pClient) {
		return false;
	}

	m_networkState.p2pClient->UpdateSettings(settingsCopy);
	const bool p2pOk = m_networkState.p2pClient->TryDownload(
		normalizedUrl,
		filePath,
		[this, &onProgress](uint64_t current, uint64_t total) {
			m_downloadState.currentDownloadProgress = static_cast<int>(current);
			if (total > 0) {
				m_downloadState.currentDownloadSize = static_cast<int>(total);
			}
			onProgress(current, total);
		});
	if (!p2pOk) {
		return false;
	}

	if (m_downloadState.currentDownloadSize <= 0) {
		std::error_code ec;
		const auto size = std::filesystem::file_size(std::filesystem::u8path(filePath), ec);
		if (!ec) {
			m_downloadState.currentDownloadSize = static_cast<int>(size);
		}
	}
	m_downloadState.currentDownloadProgress = m_downloadState.currentDownloadSize > 0
		? m_downloadState.currentDownloadSize
		: m_downloadState.currentDownloadProgress;
	onProgress(
		static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadProgress)),
		static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
	return true;
}

bool ResumeDownloader::DownloadHttp(
	const std::string& normalizedUrl,
	const std::string& filePath,
	const std::function<void(uint64_t, uint64_t)>& onProgress) {
	if (m_networkState.client == nullptr) {
		return false;
	}
	m_networkState.client->set_follow_location(true);
	m_networkState.client->set_connection_timeout(8, 0);
	m_networkState.client->set_read_timeout(30, 0);
	m_networkState.client->set_write_timeout(15, 0);
	const bool disableResume = IsImgFilePath(filePath);

	httplib::Result res;
	{
		if (disableResume) {
			res = m_networkState.client->Head(normalizedUrl.c_str());
		}
		else {
			httplib::Headers headers;
			headers.insert({ "Range", "bytes=0-0" });
			res = m_networkState.client->Get(normalizedUrl.c_str(), headers);
		}
		if (res && (res->status == 200 || res->status == 206)) {
			m_downloadState.currentDownloadSize = static_cast<int>(workthread::netutils::ParseTotalSizeFromResponse(*res));
			onProgress(0, static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
		}
		else {
			m_downloadState.currentDownloadSize = 0;
			onProgress(0, 0);
		}
	}

	std::ifstream existingFile(std::filesystem::u8path(filePath), std::ios::binary | std::ios::ate);
	size_t existingFileSize = 0;
	if (existingFile.is_open()) {
		existingFileSize = static_cast<size_t>(existingFile.tellg());
		existingFile.close();
	}
	if (disableResume) {
		existingFileSize = 0;
	}
	if (m_downloadState.currentDownloadSize > 0) {
		if (!disableResume && existingFileSize == static_cast<size_t>(m_downloadState.currentDownloadSize)) {
			onProgress(
				static_cast<uint64_t>(existingFileSize),
				static_cast<uint64_t>(m_downloadState.currentDownloadSize));
			return true;
		}
		if (!disableResume && existingFileSize > static_cast<size_t>(m_downloadState.currentDownloadSize)) {
			std::error_code ec;
			std::filesystem::remove(std::filesystem::u8path(filePath), ec);
			existingFileSize = 0;
		}
	}

	httplib::Headers headers;
	const bool requestedResume = existingFileSize > 0;
	if (existingFileSize > 0) {
		m_downloadState.currentDownloadProgress = static_cast<int>(existingFileSize);
		onProgress(
			static_cast<uint64_t>(existingFileSize),
			static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
		std::string rangeValue = "bytes=" + std::to_string(existingFileSize) + "-";
		if (m_downloadState.currentDownloadSize > 0) {
			const size_t totalSize = static_cast<size_t>(m_downloadState.currentDownloadSize);
			if (totalSize > existingFileSize) {
				rangeValue += std::to_string(totalSize - 1);
			}
		}
		headers.insert({ "Range", rangeValue });
	}

	const std::filesystem::path targetPath = std::filesystem::u8path(filePath);
	std::ofstream file;
	std::streamoff nextWriteOffset = static_cast<std::streamoff>(existingFileSize);
	bool truncateOnFirstWrite = disableResume || existingFileSize == 0;
	const auto openOutputForWrite = [&]() -> bool {
		if (!file.is_open()) {
			std::error_code ec;
			const auto parent = targetPath.parent_path();
			if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
				std::filesystem::create_directories(parent, ec);
			}

			if (truncateOnFirstWrite) {
				file.open(targetPath, std::ios::binary | std::ios::out | std::ios::trunc);
			}
			else {
				file.open(targetPath, std::ios::binary | std::ios::in | std::ios::out);
				if (!file.is_open()) {
					std::ofstream createFile(targetPath, std::ios::binary | std::ios::out);
					createFile.close();
					file.open(targetPath, std::ios::binary | std::ios::in | std::ios::out);
				}
			}
		}
		if (!file.is_open()) {
			return false;
		}

		truncateOnFirstWrite = false;
		file.clear();
		file.seekp(nextWriteOffset, std::ios::beg);
		return static_cast<bool>(file);
	};
	const auto handleResponse = [&](const httplib::Response& response) {
		if (requestedResume && response.status == 200) {
			// Server ignored Range; restart from beginning to avoid appending full payload.
			nextWriteOffset = 0;
			m_downloadState.currentDownloadProgress = 0;
			truncateOnFirstWrite = true;
			onProgress(0, static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
		}
		const uint64_t responseTotal = workthread::netutils::ParseTotalSizeFromResponse(response);
		if (responseTotal > 0) {
			m_downloadState.currentDownloadSize = static_cast<int>(responseTotal);
		}
		return true;
	};

	res = m_networkState.client->Get(normalizedUrl.c_str(), headers, handleResponse, [&](const char* data, size_t dataLength) {
		if (!openOutputForWrite()) {
			return false;
		}
		file.write(data, static_cast<std::streamsize>(dataLength));
		if (!file) {
			return false;
		}
		file.flush();
		nextWriteOffset += static_cast<std::streamoff>(dataLength);
		m_downloadState.currentDownloadProgress += static_cast<int>(dataLength);
		onProgress(
			static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadProgress)),
			static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
		return true;
	});
	if (file.is_open()) {
		file.close();
	}

	if (!res || (res->status != 200 && res->status != 206)) {
		return false;
	}

	onProgress(
		static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)),
		static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
	return true;
}

} // namespace workthread::resume
