#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <httplib.h>

#include "Encoding.h"
#include "ChunkExecutor.h"
#include "ChunkState.h"
#include "HttpSession.h"
#include "NetUtils.h"

namespace {

using workthread::chunkstate::ChunkState;
using workthread::chunkstate::ChunkStateStore;
using workthread::netutils::ParseTotalSizeFromResponse;
ChunkStateStore kChunkStateStore;
constexpr int kSingleStreamReadTimeoutSec = 45;

void MarkFileHidden(const std::string& path) {
	std::error_code ec;
	const auto fsPath = std::filesystem::u8path(path);
	if (!std::filesystem::exists(fsPath, ec)) {
		return;
	}
	const std::wstring pathW = fsPath.wstring();
	DWORD attrs = GetFileAttributesW(pathW.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return;
	}
	if ((attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
		SetFileAttributesW(pathW.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
	}
}

} // namespace

bool LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath)
{
	SetLauncherStatus(L"HTTP single-stream download: " + str2wstr(filePath));
	workthread::http::DownloadHttpSession session;
	if (!workthread::http::DownloadHttpSession::CreateFromAbsoluteUrl(absoluteUrl, session)) {
		LogUpdateError(
			"UF-CHUNK-URL",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Failed to create HTTP session from URL",
			std::string("url=") + absoluteUrl + ", file=" + filePath);
		return false;
	}

	uint64_t remoteTotalSize = 0;
	if (session.ProbeRemoteTotalSize(remoteTotalSize, 20)) {
		m_downloadState.currentDownloadSize = static_cast<int>(remoteTotalSize);
	}

	std::error_code ec;
	const std::filesystem::path targetPath = std::filesystem::u8path(filePath);
	const auto parent = targetPath.parent_path();
	if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			LogUpdateError(
				"UF-CHUNK-FILE",
				"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
				"Failed to create parent directory for target file",
				std::string("file=") + filePath + ", error=" + ec.message());
			return false;
		}
	}

	std::ofstream file(targetPath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		LogUpdateError(
			"UF-CHUNK-FILE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Failed to open local file for write",
			std::string("file=") + filePath,
			GetLastError());
		return false;
	}

	uint64_t writtenBytes = 0;
	bool writeFailed = false;
	auto onBody = [&](const char* data, size_t dataLength) {
		if (writeFailed) {
			return false;
		}
		file.write(data, static_cast<std::streamsize>(dataLength));
		if (!file.good()) {
			writeFailed = true;
			return false;
		}
		writtenBytes += static_cast<uint64_t>(dataLength);
		m_downloadState.currentDownloadProgress += static_cast<int>(dataLength);
		return true;
	};

	httplib::Result res;
	res = session.Get(onBody, kSingleStreamReadTimeoutSec);

	file.close();
	if (writeFailed) {
		LogUpdateError(
			"UF-CHUNK-FILE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Single-stream write callback failed",
			std::string("url=") + absoluteUrl + ", file=" + filePath);
		return false;
	}
	if (!res || (res->status != 200 && res->status != 206)) {
		LogUpdateError(
			"UF-CHUNK-HTTP",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Single-stream HTTP download failed",
			std::string("url=") + absoluteUrl + ", file=" + filePath,
			0,
			res ? res->status : 0,
			static_cast<int>(res.error()));
		return false;
	}

	const auto total = ParseTotalSizeFromResponse(*res);
	if (total > 0) {
		m_downloadState.currentDownloadSize = static_cast<int>(total);
	}

	if (remoteTotalSize > 0 && writtenBytes != remoteTotalSize) {
		LogUpdateError(
			"UF-CHUNK-SIZE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Single-stream downloaded size mismatch",
			std::string("url=") + absoluteUrl + ", file=" + filePath +
			", expected_size=" + std::to_string(remoteTotalSize) +
			", written_bytes=" + std::to_string(writtenBytes));
		return false;
	}

	ec.clear();
	const uint64_t actualSize = std::filesystem::file_size(targetPath, ec);
	if (ec) {
		LogUpdateError(
			"UF-CHUNK-SIZE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Failed to read downloaded file size",
			std::string("file=") + filePath + ", error=" + ec.message());
		return false;
	}
	if (actualSize != writtenBytes) {
		LogUpdateError(
			"UF-CHUNK-SIZE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Downloaded file size does not match written bytes",
			std::string("file=") + filePath +
			", actual_size=" + std::to_string(actualSize) +
			", written_bytes=" + std::to_string(writtenBytes));
		return false;
	}
	if (remoteTotalSize > 0 && actualSize != remoteTotalSize) {
		LogUpdateError(
			"UF-CHUNK-SIZE",
			"LauncherUpdateCoordinator::DownloadFileFromAbsoluteUrl",
			"Downloaded file size mismatch after write",
			std::string("file=") + filePath +
			", expected_size=" + std::to_string(remoteTotalSize) +
			", actual_size=" + std::to_string(actualSize));
		return false;
	}

	return true;
}

bool LauncherUpdateCoordinator::DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount)
{
	SetLauncherStatus(L"HTTP chunked download: " + str2wstr(filePath));
	workthread::http::DownloadHttpSession session;
	if (!workthread::http::DownloadHttpSession::CreateFromAbsoluteUrl(absoluteUrl, session)) {
		LogUpdateError(
			"UF-CHUNK-URL",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Failed to create HTTP session from URL",
			std::string("url=") + absoluteUrl + ", file=" + filePath);
		return false;
	}

	uint64_t remoteTotalSize = 0;
	if (!session.ProbeRemoteTotalSize(remoteTotalSize, 30) || remoteTotalSize == 0) {
		LogUpdateError(
			"UF-CHUNK-PROBE",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Failed to probe remote file size",
			std::string("url=") + absoluteUrl + ", file=" + filePath);
		return false;
	}

	m_downloadState.currentDownloadSize = static_cast<int>(remoteTotalSize);
	const std::string tmpPath = filePath + ".tmp";
	const std::string statePath = filePath + ".chunks.json";

	const uint64_t chunkSize = 8ULL * 1024ULL * 1024ULL;
	threadCount = (std::max<size_t>)(1, threadCount);

	ChunkState state;
	bool loadedState = kChunkStateStore.LoadFromJson(statePath, state);
	const bool stateMismatch = !loadedState
		|| state.url != absoluteUrl
		|| state.fileSize != remoteTotalSize
		|| state.chunkSize != chunkSize
		|| state.chunks.empty();
	if (stateMismatch) {
		kChunkStateStore.Initialize(state, absoluteUrl, remoteTotalSize, chunkSize);
	}

	for (auto& chunk : state.chunks) {
		const uint64_t chunkLength = chunk.end >= chunk.start ? (chunk.end - chunk.start + 1) : 0;
		if (chunk.downloaded >= chunkLength) {
			chunk.downloaded = chunkLength;
			chunk.done = true;
		}
	}

	if (!kChunkStateStore.EnsureSizedTempFile(tmpPath, remoteTotalSize)) {
		LogUpdateError(
			"UF-CHUNK-TMP",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Failed to prepare chunk temp file",
			std::string("tmp_path=") + tmpPath + ", target=" + filePath);
		return false;
	}

	{
		ChunkState snapshot = state;
		kChunkStateStore.SaveToJson(statePath, snapshot);
	}

	std::fstream tmpFile(tmpPath, std::ios::binary | std::ios::in | std::ios::out);
	if (!tmpFile.is_open()) {
		LogUpdateError(
			"UF-CHUNK-TMP",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Failed to open chunk temp file",
			std::string("tmp_path=") + tmpPath,
			GetLastError());
		return false;
	}

	const uint64_t initialDownloaded = kChunkStateStore.ComputeDownloadedBytes(state);
	std::atomic<uint64_t> downloadedTotal{ initialDownloaded };
	m_downloadState.currentDownloadProgress = static_cast<int>((std::min)(downloadedTotal.load(), remoteTotalSize));

	workthread::chunkdownload::ChunkDownloadExecutor executor(
		session,
		state,
		tmpFile,
		statePath,
		kChunkStateStore,
		downloadedTotal,
		[this, remoteTotalSize](uint64_t totalDone) {
			m_downloadState.currentDownloadProgress = static_cast<int>((std::min)(totalDone, remoteTotalSize));
		});
	const bool downloadOk = executor.Run(threadCount, remoteTotalSize);
	tmpFile.close();

	if (!downloadOk) {
		SetLauncherStatus(L"Chunked download failed, retrying single-stream: " + str2wstr(filePath));
		m_downloadState.currentDownloadSize = static_cast<int>(remoteTotalSize);
		m_downloadState.currentDownloadProgress = 0;
		const bool fallbackOk = DownloadFileFromAbsoluteUrl(absoluteUrl, filePath);
		if (!fallbackOk) {
			LogUpdateError(
				"UF-CHUNK-FALLBACK",
				"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
				"Chunked download failed and single-stream fallback also failed",
				std::string("url=") + absoluteUrl + ", file=" + filePath);
			return false;
		}

		std::error_code ec;
		const uint64_t fallbackSize = std::filesystem::file_size(std::filesystem::u8path(filePath), ec);
		if (ec || fallbackSize != remoteTotalSize) {
			LogUpdateError(
				"UF-CHUNK-SIZE",
				"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
				"Single-stream fallback produced invalid file size",
				std::string("file=") + filePath +
				", expected_size=" + std::to_string(remoteTotalSize) +
				", actual_size=" + std::to_string(fallbackSize));
			return false;
		}

		std::filesystem::remove(tmpPath, ec);
		std::filesystem::remove(statePath, ec);
		MarkFileHidden(filePath);
		m_downloadState.currentDownloadProgress = static_cast<int>(remoteTotalSize);
		return true;
	}

	if (!kChunkStateStore.AreAllChunksDone(state)) {
		LogUpdateError(
			"UF-CHUNK-INCOMPLETE",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Chunk download finished with incomplete state",
			std::string("url=") + absoluteUrl + ", file=" + filePath);
		return false;
	}

	std::error_code ec;
	std::filesystem::remove(filePath, ec);
	std::filesystem::rename(tmpPath, filePath, ec);
	if (ec) {
		LogUpdateError(
			"UF-CHUNK-RENAME",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Failed to rename chunk temp file to target file",
			std::string("tmp_path=") + tmpPath + ", target=" + filePath + ", error=" + ec.message());
		return false;
	}

	ec.clear();
	const uint64_t finalSize = std::filesystem::file_size(std::filesystem::u8path(filePath), ec);
	if (ec || finalSize != remoteTotalSize) {
		LogUpdateError(
			"UF-CHUNK-SIZE",
			"LauncherUpdateCoordinator::DownloadFileChunkedWithResume",
			"Chunked download final file size mismatch",
			std::string("file=") + filePath +
			", expected_size=" + std::to_string(remoteTotalSize) +
			", actual_size=" + std::to_string(finalSize));
		return false;
	}
	MarkFileHidden(filePath);
	MarkFileHidden(statePath);
	m_downloadState.currentDownloadProgress = static_cast<int>(remoteTotalSize);
	SetLauncherStatus(L"Chunked download complete: " + str2wstr(filePath));
	return true;
}
