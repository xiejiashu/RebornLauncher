#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <httplib.h>

#include "WorkThreadChunkExecutor.h"
#include "WorkThreadChunkState.h"
#include "WorkThreadHttpSession.h"
#include "WorkThreadNetUtils.h"

namespace {

using workthread::chunkstate::ChunkState;
using workthread::chunkstate::ChunkStateStore;
using workthread::netutils::ParseTotalSizeFromResponse;
ChunkStateStore kChunkStateStore;

} // namespace

bool WorkThread::DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath)
{
	workthread::http::DownloadHttpSession session;
	if (!workthread::http::DownloadHttpSession::CreateFromAbsoluteUrl(absoluteUrl, session)) {
		return false;
	}

	uint64_t remoteTotalSize = 0;
	if (session.ProbeRemoteTotalSize(remoteTotalSize, 20)) {
		m_downloadState.currentDownloadSize = static_cast<int>(remoteTotalSize);
	}

	size_t localFileSize = 0;
	{
		std::ifstream existingFile(filePath, std::ios::binary | std::ios::ate);
		if (existingFile.is_open()) {
			localFileSize = static_cast<size_t>(existingFile.tellg());
		}
	}

	if (remoteTotalSize > 0) {
		if (localFileSize == static_cast<size_t>(remoteTotalSize)) {
			m_downloadState.currentDownloadProgress = static_cast<int>(remoteTotalSize);
			return true;
		}
		if (localFileSize > static_cast<size_t>(remoteTotalSize)) {
			std::error_code ec;
			std::filesystem::remove(filePath, ec);
		}
	}

	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	auto onBody = [&](const char* data, size_t dataLength) {
		file.write(data, static_cast<std::streamsize>(dataLength));
		m_downloadState.currentDownloadProgress += static_cast<int>(dataLength);
		return true;
	};

	httplib::Result res;
	res = session.Get(onBody, 120);

	file.close();
	if (!res || (res->status != 200 && res->status != 206)) {
		return false;
	}

	const auto total = ParseTotalSizeFromResponse(*res);
	if (total > 0) {
		m_downloadState.currentDownloadSize = static_cast<int>(total);
	}
	return true;
}

bool WorkThread::DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount)
{
	workthread::http::DownloadHttpSession session;
	if (!workthread::http::DownloadHttpSession::CreateFromAbsoluteUrl(absoluteUrl, session)) {
		return false;
	}

	uint64_t remoteTotalSize = 0;
	if (!session.ProbeRemoteTotalSize(remoteTotalSize, 30) || remoteTotalSize == 0) {
		return false;
	}

	m_downloadState.currentDownloadSize = static_cast<int>(remoteTotalSize);

	{
		std::error_code ec;
		if (std::filesystem::exists(filePath, ec)) {
			const uint64_t localSize = std::filesystem::file_size(filePath, ec);
			if (!ec && localSize == remoteTotalSize) {
				m_downloadState.currentDownloadProgress = static_cast<int>(remoteTotalSize);
				return true;
			}
		}
	}

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
		return false;
	}

	{
		ChunkState snapshot = state;
		kChunkStateStore.SaveToJson(statePath, snapshot);
	}

	std::fstream tmpFile(tmpPath, std::ios::binary | std::ios::in | std::ios::out);
	if (!tmpFile.is_open()) {
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
		return false;
	}

	if (!kChunkStateStore.AreAllChunksDone(state)) {
		return false;
	}

	std::error_code ec;
	std::filesystem::remove(filePath, ec);
	std::filesystem::rename(tmpPath, filePath, ec);
	if (ec) {
		return false;
	}
	std::filesystem::remove(statePath, ec);
	m_downloadState.currentDownloadProgress = static_cast<int>(remoteTotalSize);
	return true;
}
