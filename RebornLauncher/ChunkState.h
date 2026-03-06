#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace workthread::chunkstate {

struct ChunkRecord {
	uint64_t start{ 0 };
	uint64_t end{ 0 };
	uint64_t downloaded{ 0 };
	bool done{ false };
};

struct ChunkState {
	std::string url;
	uint64_t fileSize{ 0 };
	uint64_t chunkSize{ 0 };
	std::vector<ChunkRecord> chunks;
};

class ChunkStateStore {
public:
	uint64_t ComputeDownloadedBytes(const ChunkState& state) const;
	bool AreAllChunksDone(const ChunkState& state) const;
	void Initialize(ChunkState& state, const std::string& url, uint64_t fileSize, uint64_t chunkSize) const;
	bool SaveToJson(const std::string& statePath, const ChunkState& state) const;
	bool LoadFromJson(const std::string& statePath, ChunkState& outState) const;
	bool EnsureSizedTempFile(const std::string& path, uint64_t fileSize) const;
};

} // namespace workthread::chunkstate
