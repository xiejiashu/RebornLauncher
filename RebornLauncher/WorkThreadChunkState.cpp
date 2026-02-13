#include "framework.h"
#include "WorkThreadChunkState.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <json/json.h>

namespace workthread::chunkstate {

uint64_t ChunkStateStore::ComputeDownloadedBytes(const ChunkState& state) const {
	uint64_t total = 0;
	for (const auto& chunk : state.chunks) {
		const uint64_t chunkLength = chunk.end >= chunk.start ? (chunk.end - chunk.start + 1) : 0;
		total += (std::min)(chunk.downloaded, chunkLength);
	}
	return total;
}

bool ChunkStateStore::AreAllChunksDone(const ChunkState& state) const {
	for (const auto& chunk : state.chunks) {
		if (!chunk.done) {
			return false;
		}
	}
	return !state.chunks.empty();
}

void ChunkStateStore::Initialize(ChunkState& state, const std::string& url, uint64_t fileSize, uint64_t chunkSize) const {
	state.url = url;
	state.fileSize = fileSize;
	state.chunkSize = chunkSize;
	state.chunks.clear();
	if (fileSize == 0 || chunkSize == 0) {
		return;
	}

	for (uint64_t begin = 0; begin < fileSize; begin += chunkSize) {
		ChunkRecord chunk;
		chunk.start = begin;
		chunk.end = (std::min)(fileSize - 1, begin + chunkSize - 1);
		chunk.downloaded = 0;
		chunk.done = false;
		state.chunks.push_back(chunk);
	}
}

bool ChunkStateStore::SaveToJson(const std::string& statePath, const ChunkState& state) const {
	Json::Value root;
	root["url"] = state.url;
	root["file_size"] = Json::UInt64(state.fileSize);
	root["chunk_size"] = Json::UInt64(state.chunkSize);

	Json::Value chunksJson(Json::arrayValue);
	for (size_t i = 0; i < state.chunks.size(); ++i) {
		const auto& chunk = state.chunks[i];
		Json::Value c;
		c["id"] = Json::UInt64(i);
		c["start"] = Json::UInt64(chunk.start);
		c["end"] = Json::UInt64(chunk.end);
		c["downloaded"] = Json::UInt64(chunk.downloaded);
		c["done"] = chunk.done;
		chunksJson.append(c);
	}
	root["chunks"] = chunksJson;

	Json::StreamWriterBuilder builder;
	builder["indentation"] = "  ";
	const std::string json = Json::writeString(builder, root);

	const std::string tmpPath = statePath + ".writing";
	std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open()) {
		return false;
	}
	ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
	ofs.close();
	if (!ofs.good()) {
		return false;
	}

	std::error_code ec;
	std::filesystem::remove(statePath, ec);
	std::filesystem::rename(tmpPath, statePath, ec);
	if (ec) {
		std::filesystem::remove(tmpPath, ec);
		return false;
	}
	return true;
}

bool ChunkStateStore::LoadFromJson(const std::string& statePath, ChunkState& outState) const {
	std::ifstream ifs(statePath, std::ios::binary);
	if (!ifs.is_open()) {
		return false;
	}

	std::stringstream buffer;
	buffer << ifs.rdbuf();
	ifs.close();

	Json::CharReaderBuilder builder;
	std::string errors;
	Json::Value root;
	std::istringstream jsonInput(buffer.str());
	if (!Json::parseFromStream(builder, jsonInput, &root, &errors) || !root.isObject()) {
		return false;
	}

	if (!root["url"].isString() || !root["file_size"].isUInt64() || !root["chunk_size"].isUInt64() || !root["chunks"].isArray()) {
		return false;
	}

	ChunkState parsed;
	parsed.url = root["url"].asString();
	parsed.fileSize = root["file_size"].asUInt64();
	parsed.chunkSize = root["chunk_size"].asUInt64();

	for (const auto& chunkJson : root["chunks"]) {
		if (!chunkJson["start"].isUInt64() || !chunkJson["end"].isUInt64() || !chunkJson["downloaded"].isUInt64()) {
			return false;
		}
		ChunkRecord chunk;
		chunk.start = chunkJson["start"].asUInt64();
		chunk.end = chunkJson["end"].asUInt64();
		chunk.downloaded = chunkJson["downloaded"].asUInt64();
		chunk.done = chunkJson["done"].asBool();
		parsed.chunks.push_back(chunk);
	}

	outState = std::move(parsed);
	return true;
}

bool ChunkStateStore::EnsureSizedTempFile(const std::string& path, uint64_t fileSize) const {
	std::error_code ec;
	const bool exists = std::filesystem::exists(path, ec);
	if (exists) {
		const uint64_t currentSize = std::filesystem::file_size(path, ec);
		if (!ec && currentSize == fileSize) {
			return true;
		}
		std::filesystem::remove(path, ec);
	}

	std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open()) {
		return false;
	}
	if (fileSize > 0) {
		ofs.seekp(static_cast<std::streamoff>(fileSize - 1), std::ios::beg);
		char zero = 0;
		ofs.write(&zero, 1);
	}
	ofs.close();
	return ofs.good();
}

} // namespace workthread::chunkstate
