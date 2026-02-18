#include "framework.h"
#include "WorkThreadChunkExecutor.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace workthread::chunkdownload {

namespace {

constexpr int kChunkMaxAttemptsPerWorker = 2;
constexpr int kChunkReadTimeoutSec = 30;

} // namespace

ChunkDownloadExecutor::ChunkDownloadExecutor(
	const workthread::http::DownloadHttpSession& session,
	workthread::chunkstate::ChunkState& state,
	std::fstream& tmpFile,
	const std::string& statePath,
	const workthread::chunkstate::ChunkStateStore& stateStore,
	std::atomic<uint64_t>& downloadedTotal,
	const std::function<void(uint64_t)>& onProgress)
	: m_session(session),
	m_state(state),
	m_tmpFile(tmpFile),
	m_statePath(statePath),
	m_stateStore(stateStore),
	m_downloadedTotal(downloadedTotal),
	m_onProgress(onProgress) {
}

bool ChunkDownloadExecutor::Run(size_t threadCount, uint64_t remoteTotalSize) {
	m_threadCount = (std::max<size_t>)(1, threadCount);
	m_remoteTotalSize = remoteTotalSize;

	const size_t actualThreads = (std::min)(m_threadCount, m_state.chunks.size());
	std::vector<std::thread> workers;
	workers.reserve(actualThreads);
	for (size_t i = 0; i < actualThreads; ++i) {
		workers.emplace_back([this, i]() {
			WorkerLoop(i);
		});
	}
	for (auto& t : workers) {
		if (t.joinable()) {
			t.join();
		}
	}

	PersistState();
	return !m_failed.load();
}

void ChunkDownloadExecutor::PersistState() {
	workthread::chunkstate::ChunkState snapshot;
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		snapshot = m_state;
	}
	m_stateStore.SaveToJson(m_statePath, snapshot);
}

void ChunkDownloadExecutor::MarkFailure() {
	bool expected = false;
	m_failed.compare_exchange_strong(expected, true);
}

void ChunkDownloadExecutor::WorkerLoop(size_t workerId) {
	for (size_t chunkIndex = workerId; chunkIndex < m_state.chunks.size(); chunkIndex += m_threadCount) {
		if (m_failed.load()) {
			return;
		}

		int attempt = 0;
		while (attempt < kChunkMaxAttemptsPerWorker && !m_failed.load()) {
			uint64_t chunkStart = 0;
			uint64_t chunkEnd = 0;
			uint64_t chunkDownloaded = 0;
			bool chunkDone = false;
			{
				std::lock_guard<std::mutex> lock(m_stateMutex);
				const auto& chunk = m_state.chunks[chunkIndex];
				chunkStart = chunk.start;
				chunkEnd = chunk.end;
				chunkDownloaded = chunk.downloaded;
				chunkDone = chunk.done;
			}

			if (chunkDone) {
				break;
			}

			if (chunkEnd < chunkStart) {
				MarkFailure();
				return;
			}

			const uint64_t chunkLength = chunkEnd - chunkStart + 1;
			if (chunkDownloaded >= chunkLength) {
				{
					std::lock_guard<std::mutex> lock(m_stateMutex);
					auto& chunk = m_state.chunks[chunkIndex];
					chunk.downloaded = chunkLength;
					chunk.done = true;
				}
				PersistState();
				break;
			}

			const uint64_t requestStart = chunkStart + chunkDownloaded;
			const uint64_t requestEnd = chunkEnd;
			uint64_t writeOffset = requestStart;
			uint64_t receivedThisAttempt = 0;

			httplib::Headers headers;
			headers.insert({ "Range", "bytes=" + std::to_string(requestStart) + "-" + std::to_string(requestEnd) });

			auto receiver = [&](const char* data, size_t dataLength) {
				if (dataLength == 0) {
					return true;
				}

				{
					std::lock_guard<std::mutex> lock(m_writeMutex);
					m_tmpFile.seekp(static_cast<std::streamoff>(writeOffset), std::ios::beg);
					if (!m_tmpFile.good()) {
						return false;
					}
					m_tmpFile.write(data, static_cast<std::streamsize>(dataLength));
					if (!m_tmpFile.good()) {
						return false;
					}
				}

				writeOffset += static_cast<uint64_t>(dataLength);
				receivedThisAttempt += static_cast<uint64_t>(dataLength);

				{
					std::lock_guard<std::mutex> lock(m_stateMutex);
					auto& chunk = m_state.chunks[chunkIndex];
					const uint64_t remaining = chunkLength - chunk.downloaded;
					const uint64_t committed = (std::min)(remaining, static_cast<uint64_t>(dataLength));
					chunk.downloaded += committed;
					if (chunk.downloaded >= chunkLength) {
						chunk.downloaded = chunkLength;
						chunk.done = true;
					}
					const uint64_t totalDone = m_downloadedTotal.fetch_add(committed) + committed;
					m_onProgress((std::min)(totalDone, m_remoteTotalSize));
				}

				return true;
			};

			httplib::Result res = m_session.Get(headers, receiver, kChunkReadTimeoutSec);

			PersistState();

			if (!res || (res->status != 206 && res->status != 200)) {
				++attempt;
				continue;
			}

			if (res->status == 200 && !(requestStart == 0 && requestEnd + 1 == m_remoteTotalSize)) {
				MarkFailure();
				return;
			}

			bool doneNow = false;
			{
				std::lock_guard<std::mutex> lock(m_stateMutex);
				doneNow = m_state.chunks[chunkIndex].done;
			}
			if (doneNow) {
				break;
			}
			if (receivedThisAttempt == 0) {
				++attempt;
				continue;
			}
		}

		bool doneFinally = false;
		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			doneFinally = m_state.chunks[chunkIndex].done;
		}
		if (!doneFinally) {
			MarkFailure();
			return;
		}
	}
}

} // namespace workthread::chunkdownload
