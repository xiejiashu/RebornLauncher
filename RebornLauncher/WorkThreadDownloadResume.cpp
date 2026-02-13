#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <httplib.h>

#include "Encoding.h"

namespace {

std::string NormalizeRelativeUrlPath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (path.empty()) {
		return {};
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}

uint64_t ParseTotalSizeFromResponse(const httplib::Response& response) {
	const std::string contentRange = response.get_header_value("Content-Range");
	if (!contentRange.empty()) {
		const size_t slash = contentRange.rfind('/');
		if (slash != std::string::npos && slash + 1 < contentRange.size()) {
			try {
				return static_cast<uint64_t>(std::stoull(contentRange.substr(slash + 1)));
			}
			catch (...) {
			}
		}
	}

	const std::string contentLength = response.get_header_value("Content-Length");
	if (!contentLength.empty()) {
		try {
			return static_cast<uint64_t>(std::stoull(contentLength));
		}
		catch (...) {
		}
	}
	return 0;
}

} // namespace

bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path, DWORD ownerProcessId) {
	std::string strUrl = NormalizeRelativeUrlPath(url);
	std::cout << __FILE__ << ":" << __LINE__ << " url:" << m_strUrl << " page:" << strUrl << std::endl;
	const std::wstring filePathW = str2wstr(file_path, static_cast<int>(file_path.length()));
	SetCurrentDownloadFile(filePathW);
	MarkClientDownloadStart(ownerProcessId, filePathW);
	MarkClientDownloadProgress(ownerProcessId, 0, 0);

	{
		P2PSettings settingsCopy;
		{
			std::lock_guard<std::mutex> lock(m_p2pMutex);
			settingsCopy = m_p2pSettings;
		}
		if (settingsCopy.enabled && m_p2pClient) {
			m_p2pClient->UpdateSettings(settingsCopy);
			const bool p2pOk = m_p2pClient->TryDownload(strUrl, file_path, [this, ownerProcessId](uint64_t current, uint64_t total) {
				m_nCurrentDownloadProgress = static_cast<int>(current);
				if (total > 0) {
					m_nCurrentDownloadSize = static_cast<int>(total);
				}
				MarkClientDownloadProgress(ownerProcessId, current, total);
			});
			if (p2pOk) {
				if (m_nCurrentDownloadSize <= 0) {
					std::error_code ec;
					const auto size = std::filesystem::file_size(std::filesystem::u8path(file_path), ec);
					if (!ec) {
						m_nCurrentDownloadSize = static_cast<int>(size);
					}
				}
				m_nCurrentDownloadProgress = m_nCurrentDownloadSize > 0
					? m_nCurrentDownloadSize
					: m_nCurrentDownloadProgress;
				MarkClientDownloadProgress(ownerProcessId,
					static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadProgress)),
					static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
				return true;
			}
		}
	}

	if (m_client == nullptr) {
		MarkClientDownloadFinished(ownerProcessId);
		return false;
	}

	httplib::Result res;
	{
		httplib::Headers headers;
		headers.insert({ "Range", "bytes=0-0" });
		res = m_client->Get(strUrl.c_str(), headers);
		if (res && (res->status == 200 || res->status == 206)) {
			m_nCurrentDownloadSize = static_cast<int>(ParseTotalSizeFromResponse(*res));
			MarkClientDownloadProgress(ownerProcessId, 0, static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
		}
		else {
			std::cout << "Failed to get file size, status code: " << (res ? res->status : -1) << std::endl;
			m_nCurrentDownloadSize = 0;
			MarkClientDownloadProgress(ownerProcessId, 0, 0);
		}
	}

	std::ifstream existing_file(std::filesystem::u8path(file_path), std::ios::binary | std::ios::ate);
	size_t existing_file_size = 0;
	if (existing_file.is_open()) {
		existing_file_size = static_cast<size_t>(existing_file.tellg());
		existing_file.close();
	}
	if (m_nCurrentDownloadSize > 0) {
		if (existing_file_size == static_cast<size_t>(m_nCurrentDownloadSize)) {
			MarkClientDownloadProgress(ownerProcessId,
				static_cast<uint64_t>(existing_file_size),
				static_cast<uint64_t>(m_nCurrentDownloadSize));
			MarkClientDownloadFinished(ownerProcessId);
			return true;
		}
		else if (existing_file_size > static_cast<size_t>(m_nCurrentDownloadSize)) {
			std::error_code ec;
			std::filesystem::remove(std::filesystem::u8path(file_path), ec);
			existing_file_size = 0;
		}
	}

	httplib::Headers headers;
	if (existing_file_size > 0) {
		m_nCurrentDownloadProgress = static_cast<int>(existing_file_size);
		MarkClientDownloadProgress(ownerProcessId,
			static_cast<uint64_t>(existing_file_size),
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
		headers.insert({ "Range", "bytes=" + std::to_string(existing_file_size) + "-" + std::to_string(m_nCurrentDownloadSize) });
		std::cout << "Resuming download from byte: " << existing_file_size << std::endl;
	}

	std::ofstream file(std::filesystem::u8path(file_path), std::ios::binary | std::ios::app);
	if (!file.is_open()) {
		MarkClientDownloadFinished(ownerProcessId);
		return false;
	}

	res = m_client->Get(strUrl.c_str(), headers, [&](const char* data, size_t data_length) {
		file.write(data, static_cast<std::streamsize>(data_length));
		file.flush();
		m_nCurrentDownloadProgress += static_cast<int>(data_length);
		MarkClientDownloadProgress(ownerProcessId,
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadProgress)),
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
		return true;
	});
	file.close();

	if (res && res->status == 200) {
		std::cout << "Download completed!" << std::endl;
		MarkClientDownloadProgress(ownerProcessId,
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)),
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
		MarkClientDownloadFinished(ownerProcessId);
		return true;
	}
	else if (res && res->status == 206) {
		std::cout << "Download resumed and completed!" << std::endl;
		MarkClientDownloadProgress(ownerProcessId,
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)),
			static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
		MarkClientDownloadFinished(ownerProcessId);
		return true;
	}
	else {
		std::cerr << "Download failed with status code: " << (res ? res->status : -1) << std::endl;
		MarkClientDownloadFinished(ownerProcessId);
		return false;
	}
}
