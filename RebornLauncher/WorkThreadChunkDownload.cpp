#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <json/json.h>

namespace {

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

bool ParseHttpUrl(const std::string& url, bool& useTls, std::string& host, int& port, std::string& path) {
	std::regex urlRegex(R"((https?)://([^/:]+)(?::(\d+))?(\/.*)?)");
	std::smatch match;
	if (!std::regex_match(url, match, urlRegex)) {
		return false;
	}
	useTls = match[1].str() == "https";
	host = match[2].str();
	port = match[3].matched ? std::stoi(match[3].str()) : (useTls ? 443 : 80);
	path = match[4].matched ? match[4].str() : "/";
	if (path.empty()) {
		path = "/";
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return true;
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

uint64_t ComputeDownloadedBytes(const ChunkState& state) {
	uint64_t total = 0;
	for (const auto& chunk : state.chunks) {
		const uint64_t chunkLength = chunk.end >= chunk.start ? (chunk.end - chunk.start + 1) : 0;
		total += (std::min)(chunk.downloaded, chunkLength);
	}
	return total;
}

bool AreAllChunksDone(const ChunkState& state) {
	for (const auto& chunk : state.chunks) {
		if (!chunk.done) {
			return false;
		}
	}
	return !state.chunks.empty();
}

void InitializeChunkState(ChunkState& state, const std::string& url, uint64_t fileSize, uint64_t chunkSize) {
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

bool SaveChunkStateToJson(const std::string& statePath, const ChunkState& state) {
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

bool LoadChunkStateFromJson(const std::string& statePath, ChunkState& outState) {
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

bool EnsureSizedTempFile(const std::string& path, uint64_t fileSize) {
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

} // namespace

bool WorkThread::DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath)
{
	bool useTls = false;
	std::string host;
	int port = 0;
	std::string path;
	if (!ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}

	auto queryRemoteTotalSize = [&](auto& client) -> uint64_t {
		httplib::Headers headers;
		headers.insert({ "Range", "bytes=0-0" });
		auto metaRes = client.Get(path.c_str(), headers);
		if (metaRes && (metaRes->status == 200 || metaRes->status == 206)) {
			return ParseTotalSizeFromResponse(*metaRes);
		}
		return 0;
	};

	uint64_t remoteTotalSize = 0;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (useTls) {
		httplib::SSLClient client(host, port);
		client.set_follow_location(true);
		client.set_connection_timeout(8, 0);
		client.set_read_timeout(20, 0);
		remoteTotalSize = queryRemoteTotalSize(client);
	}
	else
#endif
	{
		httplib::Client client(host, port);
		client.set_follow_location(true);
		client.set_connection_timeout(8, 0);
		client.set_read_timeout(20, 0);
		remoteTotalSize = queryRemoteTotalSize(client);
	}

	if (remoteTotalSize > 0) {
		m_nCurrentDownloadSize = static_cast<int>(remoteTotalSize);
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
			m_nCurrentDownloadProgress = static_cast<int>(remoteTotalSize);
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
		m_nCurrentDownloadProgress += static_cast<int>(dataLength);
		return true;
	};

	httplib::Result res;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (useTls) {
		httplib::SSLClient client(host, port);
		client.set_follow_location(true);
		client.set_connection_timeout(8, 0);
		client.set_read_timeout(120, 0);
		res = client.Get(path.c_str(), onBody);
	}
	else
#endif
	{
		httplib::Client client(host, port);
		client.set_follow_location(true);
		client.set_connection_timeout(8, 0);
		client.set_read_timeout(120, 0);
		res = client.Get(path.c_str(), onBody);
	}

	file.close();
	if (!res || (res->status != 200 && res->status != 206)) {
		return false;
	}

	const auto total = ParseTotalSizeFromResponse(*res);
	if (total > 0) {
		m_nCurrentDownloadSize = static_cast<int>(total);
	}
	return true;
}

bool WorkThread::DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount)
{
	bool useTls = false;
	std::string host;
	int port = 0;
	std::string path;
	if (!ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}

	auto queryRemoteTotalSize = [&](uint64_t& sizeOut) -> bool {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
		if (useTls) {
			httplib::SSLClient client(host, port);
			client.set_follow_location(true);
			client.set_connection_timeout(8, 0);
			client.set_read_timeout(30, 0);
			httplib::Headers headers;
			headers.insert({ "Range", "bytes=0-0" });
			auto res = client.Get(path.c_str(), headers);
			if (!res || (res->status != 200 && res->status != 206)) {
				return false;
			}
			sizeOut = ParseTotalSizeFromResponse(*res);
			return sizeOut > 0;
		}
		else
#endif
		{
			httplib::Client client(host, port);
			client.set_follow_location(true);
			client.set_connection_timeout(8, 0);
			client.set_read_timeout(30, 0);
			httplib::Headers headers;
			headers.insert({ "Range", "bytes=0-0" });
			auto res = client.Get(path.c_str(), headers);
			if (!res || (res->status != 200 && res->status != 206)) {
				return false;
			}
			sizeOut = ParseTotalSizeFromResponse(*res);
			return sizeOut > 0;
		}
	};

	uint64_t remoteTotalSize = 0;
	if (!queryRemoteTotalSize(remoteTotalSize) || remoteTotalSize == 0) {
		return false;
	}

	m_nCurrentDownloadSize = static_cast<int>(remoteTotalSize);

	{
		std::error_code ec;
		if (std::filesystem::exists(filePath, ec)) {
			const uint64_t localSize = std::filesystem::file_size(filePath, ec);
			if (!ec && localSize == remoteTotalSize) {
				m_nCurrentDownloadProgress = static_cast<int>(remoteTotalSize);
				return true;
			}
		}
	}

	const std::string tmpPath = filePath + ".tmp";
	const std::string statePath = filePath + ".chunks.json";
	const uint64_t chunkSize = 8ULL * 1024ULL * 1024ULL;
	threadCount = (std::max<size_t>)(1, threadCount);

	ChunkState state;
	bool loadedState = LoadChunkStateFromJson(statePath, state);
	const bool stateMismatch = !loadedState
		|| state.url != absoluteUrl
		|| state.fileSize != remoteTotalSize
		|| state.chunkSize != chunkSize
		|| state.chunks.empty();
	if (stateMismatch) {
		InitializeChunkState(state, absoluteUrl, remoteTotalSize, chunkSize);
	}

	for (auto& chunk : state.chunks) {
		const uint64_t chunkLength = chunk.end >= chunk.start ? (chunk.end - chunk.start + 1) : 0;
		if (chunk.downloaded >= chunkLength) {
			chunk.downloaded = chunkLength;
			chunk.done = true;
		}
	}

	if (!EnsureSizedTempFile(tmpPath, remoteTotalSize)) {
		return false;
	}

	{
		ChunkState snapshot = state;
		SaveChunkStateToJson(statePath, snapshot);
	}

	std::fstream tmpFile(tmpPath, std::ios::binary | std::ios::in | std::ios::out);
	if (!tmpFile.is_open()) {
		return false;
	}

	std::mutex writeMutex;
	std::mutex stateMutex;
	const uint64_t initialDownloaded = ComputeDownloadedBytes(state);
	std::atomic<uint64_t> downloadedTotal{ initialDownloaded };
	m_nCurrentDownloadProgress = static_cast<int>((std::min)(downloadedTotal.load(), remoteTotalSize));

	auto persistState = [&]() {
		ChunkState snapshot;
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			snapshot = state;
		}
		SaveChunkStateToJson(statePath, snapshot);
	};

	std::atomic<bool> failed{ false };

	auto setFailure = [&](const std::string&) {
		bool expected = false;
		failed.compare_exchange_strong(expected, true);
	};

	auto worker = [&](size_t workerId) {
		for (size_t chunkIndex = workerId; chunkIndex < state.chunks.size(); chunkIndex += threadCount) {
			if (failed.load()) {
				return;
			}

			int attempt = 0;
			while (attempt < 4 && !failed.load()) {
				uint64_t chunkStart = 0;
				uint64_t chunkEnd = 0;
				uint64_t chunkDownloaded = 0;
				bool chunkDone = false;
				{
					std::lock_guard<std::mutex> lock(stateMutex);
					const auto& chunk = state.chunks[chunkIndex];
					chunkStart = chunk.start;
					chunkEnd = chunk.end;
					chunkDownloaded = chunk.downloaded;
					chunkDone = chunk.done;
				}

				if (chunkDone) {
					break;
				}

				if (chunkEnd < chunkStart) {
					setFailure("Invalid chunk range.");
					return;
				}

				const uint64_t chunkLength = chunkEnd - chunkStart + 1;
				if (chunkDownloaded >= chunkLength) {
					{
						std::lock_guard<std::mutex> lock(stateMutex);
						auto& chunk = state.chunks[chunkIndex];
						chunk.downloaded = chunkLength;
						chunk.done = true;
					}
					persistState();
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
						std::lock_guard<std::mutex> lock(writeMutex);
						tmpFile.seekp(static_cast<std::streamoff>(writeOffset), std::ios::beg);
						if (!tmpFile.good()) {
							return false;
						}
						tmpFile.write(data, static_cast<std::streamsize>(dataLength));
						if (!tmpFile.good()) {
							return false;
						}
					}

					writeOffset += static_cast<uint64_t>(dataLength);
					receivedThisAttempt += static_cast<uint64_t>(dataLength);

					{
						std::lock_guard<std::mutex> lock(stateMutex);
						auto& chunk = state.chunks[chunkIndex];
						const uint64_t remaining = chunkLength - chunk.downloaded;
						const uint64_t committed = (std::min)(remaining, static_cast<uint64_t>(dataLength));
						chunk.downloaded += committed;
						if (chunk.downloaded >= chunkLength) {
							chunk.downloaded = chunkLength;
							chunk.done = true;
						}
						const uint64_t totalDone = downloadedTotal.fetch_add(committed) + committed;
						m_nCurrentDownloadProgress = static_cast<int>((std::min)(totalDone, remoteTotalSize));
					}

					return true;
				};

				httplib::Result res;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
				if (useTls) {
					httplib::SSLClient client(host, port);
					client.set_follow_location(true);
					client.set_connection_timeout(8, 0);
					client.set_read_timeout(120, 0);
					res = client.Get(path.c_str(), headers, receiver);
				}
				else
#endif
				{
					httplib::Client client(host, port);
					client.set_follow_location(true);
					client.set_connection_timeout(8, 0);
					client.set_read_timeout(120, 0);
					res = client.Get(path.c_str(), headers, receiver);
				}

				persistState();

				if (!res || (res->status != 206 && res->status != 200)) {
					++attempt;
					continue;
				}

				if (res->status == 200 && !(requestStart == 0 && requestEnd + 1 == remoteTotalSize)) {
					setFailure("Server does not support ranged chunk download.");
					return;
				}

				bool doneNow = false;
				{
					std::lock_guard<std::mutex> lock(stateMutex);
					doneNow = state.chunks[chunkIndex].done;
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
				std::lock_guard<std::mutex> lock(stateMutex);
				doneFinally = state.chunks[chunkIndex].done;
			}
			if (!doneFinally) {
				setFailure("Chunk download retries exceeded.");
				return;
			}
		}
	};

	const size_t actualThreads = (std::min)(threadCount, state.chunks.size());
	std::vector<std::thread> workers;
	workers.reserve(actualThreads);
	for (size_t i = 0; i < actualThreads; ++i) {
		workers.emplace_back(worker, i);
	}
	for (auto& t : workers) {
		if (t.joinable()) {
			t.join();
		}
	}

	persistState();
	tmpFile.close();

	if (failed.load()) {
		return false;
	}

	ChunkState finalState;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		finalState = state;
	}
	if (!AreAllChunksDone(finalState)) {
		return false;
	}

	std::error_code ec;
	std::filesystem::remove(filePath, ec);
	std::filesystem::rename(tmpPath, filePath, ec);
	if (ec) {
		return false;
	}
	std::filesystem::remove(statePath, ec);
	m_nCurrentDownloadProgress = static_cast<int>(remoteTotalSize);
	return true;
}
