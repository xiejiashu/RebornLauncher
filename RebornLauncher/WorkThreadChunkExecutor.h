#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

#include "WorkThreadChunkState.h"
#include "WorkThreadHttpSession.h"

namespace workthread::chunkdownload {

class ChunkDownloadExecutor {
public:
	ChunkDownloadExecutor(
		const workthread::http::DownloadHttpSession& session,
		workthread::chunkstate::ChunkState& state,
		std::fstream& tmpFile,
		const std::string& statePath,
		const workthread::chunkstate::ChunkStateStore& stateStore,
		std::atomic<uint64_t>& downloadedTotal,
		const std::function<void(uint64_t)>& onProgress);

	bool Run(size_t threadCount, uint64_t remoteTotalSize);

private:
	void PersistState();
	void MarkFailure();
	void WorkerLoop(size_t workerId);

	const workthread::http::DownloadHttpSession& m_session;
	workthread::chunkstate::ChunkState& m_state;
	std::fstream& m_tmpFile;
	const std::string m_statePath;
	const workthread::chunkstate::ChunkStateStore& m_stateStore;
	std::atomic<uint64_t>& m_downloadedTotal;
	std::function<void(uint64_t)> m_onProgress;

	std::mutex m_writeMutex;
	std::mutex m_stateMutex;
	std::atomic<bool> m_failed{ false };

	size_t m_threadCount{ 1 };
	uint64_t m_remoteTotalSize{ 0 };
};

} // namespace workthread::chunkdownload
