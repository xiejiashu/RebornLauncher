#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct DownloadProgressState;
struct NetworkState;

namespace workthread::resume {

class ResumeDownloader {
public:
	ResumeDownloader(NetworkState& networkState, DownloadProgressState& downloadState);

	bool TryP2P(
		const std::string& normalizedUrl,
		const std::string& filePath,
		const std::function<void(uint64_t, uint64_t)>& onProgress);

	bool DownloadHttp(
		const std::string& normalizedUrl,
		const std::string& filePath,
		const std::function<void(uint64_t, uint64_t)>& onProgress);

private:
	NetworkState& m_networkState;
	DownloadProgressState& m_downloadState;
};

} // namespace workthread::resume
