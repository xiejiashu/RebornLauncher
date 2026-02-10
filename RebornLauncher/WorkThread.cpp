#include "framework.h"
#include "WorkThread.h"
#include <filesystem>
#include <httplib.h>
#include <zstd.h>
#include <json/json.h>
#include <wincrypt.h>
#include "FileHash.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <shellapi.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include "Encoding.h"

#pragma comment(lib, "advapi32.lib")
extern bool g_bRendering;
extern bool IsProcessRunning(DWORD dwProcessId);

namespace {

constexpr const char* kBootstrapHost = "https://gitee.com";
constexpr const char* kBootstrapPath = "/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt";

// Normalize a relative URL path with forward slashes and a leading '/'.
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

// Join base and child URL paths with a single separator.
std::string JoinUrlPath(const std::string& basePath, const std::string& childPath) {
	std::string base = basePath;
	std::string child = childPath;
	std::replace(base.begin(), base.end(), '\\', '/');
	std::replace(child.begin(), child.end(), '\\', '/');

	if (base.empty()) {
		return NormalizeRelativeUrlPath(child);
	}
	if (base.front() != '/') {
		base.insert(base.begin(), '/');
	}
	if (!base.empty() && base.back() != '/') {
		base.push_back('/');
	}
	while (!child.empty() && child.front() == '/') {
		child.erase(child.begin());
	}
	return base + child;
}

// Build the signaling endpoint URL from base and page path.
std::string BuildSignalEndpoint(const std::string& baseUrl, const std::string& pagePath) {
	return baseUrl + JoinUrlPath(pagePath, "signal");
}

// Return true when the string starts with http/https.
bool IsHttpUrl(const std::string& value) {
	return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

// Parse http/https URL into components.
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

// Split an absolute URL into base URL and path.
bool ExtractBaseAndPath(const std::string& absoluteUrl, std::string& baseUrl, std::string& path) {
	bool useTls = false;
	std::string host;
	int port = 0;
	if (!ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}
	const bool defaultPort = (useTls && port == 443) || (!useTls && port == 80);
	baseUrl = (useTls ? "https://" : "http://") + host;
	if (!defaultPort) {
		baseUrl += ":" + std::to_string(port);
	}
	return true;
}

// Trim ASCII whitespace from both ends of a string.
std::string TrimAscii(std::string value) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
		value.erase(value.begin());
	}
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
		value.pop_back();
	}
	return value;
}

// Return directory portion of a URL path, keeping a trailing '/'.
std::string DirnamePath(std::string path) {
	if (path.empty()) {
		return "/";
	}
	std::replace(path.begin(), path.end(), '\\', '/');
	const size_t queryPos = path.find('?');
	if (queryPos != std::string::npos) {
		path = path.substr(0, queryPos);
	}
	size_t slash = path.find_last_of('/');
	if (slash == std::string::npos) {
		return "/";
	}
	path = path.substr(0, slash + 1);
	if (path.empty()) {
		return "/";
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}

// Extract a file name from a URL path.
std::string GetFileNameFromUrl(std::string url) {
	const size_t hashPos = url.find('#');
	if (hashPos != std::string::npos) {
		url = url.substr(0, hashPos);
	}
	const size_t queryPos = url.find('?');
	if (queryPos != std::string::npos) {
		url = url.substr(0, queryPos);
	}
	std::replace(url.begin(), url.end(), '\\', '/');
	const size_t slash = url.find_last_of('/');
	if (slash == std::string::npos) {
		return url;
	}
	if (slash + 1 >= url.size()) {
		return {};
	}
	return url.substr(slash + 1);
}

// Decode a hex-encoded body to raw bytes.
bool HexBodyToBytes(const std::string& body, std::string& out) {
	std::string hex;
	hex.reserve(body.size());
	for (unsigned char ch : body) {
		if (std::isxdigit(ch) != 0) {
			hex.push_back(static_cast<char>(ch));
		}
	}
	if (hex.empty() || (hex.size() % 2) != 0) {
		return false;
	}

	std::string decoded;
	decoded.reserve(hex.size() / 2);
	for (size_t i = 0; i < hex.size(); i += 2) {
		char pair[3] = { hex[i], hex[i + 1], 0 };
		char* endPtr = nullptr;
		const long value = strtol(pair, &endPtr, 16);
		if (endPtr == nullptr || *endPtr != '\0') {
			return false;
		}
		decoded.push_back(static_cast<char>(value & 0xFF));
	}
	out = std::move(decoded);
	return true;
}

// Append unique non-empty strings from source into target.
void MergeUnique(std::vector<std::string>& target, const std::vector<std::string>& source) {
	for (const auto& item : source) {
		if (item.empty()) {
			continue;
		}
		if (std::find(target.begin(), target.end(), item) == target.end()) {
			target.push_back(item);
		}
	}
}

// Read an array of trimmed strings from a JSON key.
std::vector<std::string> ReadStringArray(const Json::Value& parent, const char* key) {
	std::vector<std::string> values;
	if (!parent.isMember(key) || !parent[key].isArray()) {
		return values;
	}
	for (const auto& v : parent[key]) {
		if (v.isString()) {
			std::string s = TrimAscii(v.asString());
			if (!s.empty()) {
				values.push_back(s);
			}
		}
	}
	return values;
}

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

// Sum per-chunk downloaded bytes with bounds checking.
uint64_t ComputeDownloadedBytes(const ChunkState& state) {
	uint64_t total = 0;
	for (const auto& chunk : state.chunks) {
		const uint64_t chunkLength = chunk.end >= chunk.start ? (chunk.end - chunk.start + 1) : 0;
		total += (std::min)(chunk.downloaded, chunkLength);
	}
	return total;
}

// Return true when all chunks are marked complete.
bool AreAllChunksDone(const ChunkState& state) {
	for (const auto& chunk : state.chunks) {
		if (!chunk.done) {
			return false;
		}
	}
	return !state.chunks.empty();
}

// Initialize chunk ranges for a file download.
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

// Persist chunk download state to JSON on disk.
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

// Load chunk download state from JSON on disk.
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

// Ensure a temp file exists and matches the requested size.
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

// Normalize archive paths to a safe, relative, forward-slash form.
std::string NormalizeArchivePath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	while (!path.empty() && (path.front() == '/' || path.front() == '.')) {
		if (path.front() == '.') {
			if (path.size() >= 2 && path[1] == '/') {
				path.erase(path.begin(), path.begin() + 2);
				continue;
			}
			break;
		}
		path.erase(path.begin());
	}
	return path;
}

// Return the parent directory of an archive path.
std::string GetArchiveParentPath(const std::string& path) {
	const size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return {};
	}
	return path.substr(0, pos);
}

// Extract the file name from an archive path.
std::string GetArchiveFileName(const std::string& path) {
	const size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return path;
	}
	return path.substr(pos + 1);
}

// Split an archive path into its components.
std::vector<std::string> SplitArchivePath(const std::string& path) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (start < path.size()) {
		size_t slash = path.find('/', start);
		if (slash == std::string::npos) {
			slash = path.size();
		}
		if (slash > start) {
			parts.push_back(path.substr(start, slash - start));
		}
		start = slash + 1;
	}
	return parts;
}

// Join archive path components with '/'.
std::string JoinArchivePath(const std::vector<std::string>& parts) {
	if (parts.empty()) {
		return {};
	}
	std::string out = parts.front();
	for (size_t i = 1; i < parts.size(); ++i) {
		out.push_back('/');
		out.append(parts[i]);
	}
	return out;
}

// Lowercase ASCII characters in a string.
std::string GetLowerAscii(std::string value) {
	for (auto& c : value) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return value;
}

// Find the common parent path for executable entries.
std::string DetermineExeRootPrefix(const std::vector<DataBlock>& files) {
	std::vector<std::vector<std::string>> exeParentParts;
	for (const auto& file : files) {
		const std::string fileName = GetArchiveFileName(file.filePath);
		const std::string lowerName = GetLowerAscii(fileName);
		if (lowerName.size() < 4 || lowerName.substr(lowerName.size() - 4) != ".exe") {
			continue;
		}

		const std::string parent = GetArchiveParentPath(file.filePath);
		if (parent.empty()) {
			return {};
		}
		exeParentParts.push_back(SplitArchivePath(parent));
	}

	if (exeParentParts.empty()) {
		return {};
	}

	std::vector<std::string> common = exeParentParts.front();
	for (size_t i = 1; i < exeParentParts.size(); ++i) {
		const auto& parts = exeParentParts[i];
		size_t commonLen = 0;
		while (commonLen < common.size() && commonLen < parts.size() && common[commonLen] == parts[commonLen]) {
			++commonLen;
		}
		common.resize(commonLen);
		if (common.empty()) {
			break;
		}
	}

	return JoinArchivePath(common);
}

// Strip an archive path prefix when present.
std::string StripArchivePrefix(const std::string& fullPath, const std::string& prefix) {
	if (prefix.empty()) {
		return fullPath;
	}
	if (fullPath == prefix) {
		return {};
	}
	if (fullPath.size() > prefix.size() &&
		fullPath.compare(0, prefix.size(), prefix) == 0 &&
		fullPath[prefix.size()] == '/') {
		return fullPath.substr(prefix.size() + 1);
	}
	return fullPath;
}

// Validate and normalize a safe relative path.
bool MakeSafeRelativePath(const std::string& input, std::string& out) {
	std::filesystem::path p(input);
	p = p.lexically_normal();
	if (p.empty() || p.is_absolute()) {
		return false;
	}

	for (const auto& part : p) {
		const std::string token = part.string();
		if (token.empty() || token == ".") {
			continue;
		}
		if (token == "..") {
			return false;
		}
	}

	out = p.generic_string();
	while (!out.empty() && out.front() == '/') {
		out.erase(out.begin());
	}
	return !out.empty();
}

// Parse content length or range total from a response.
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

// Write data to a file opened with shared read/write access.
void WriteToFileWithSharedAccess(const std::wstring& strLocalFile, const std::string& data) {
	HANDLE hFile = CreateFile(
		strLocalFile.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to open file, error code: " << GetLastError() << std::endl;
		return;
	}

	DWORD dwBytesWritten = 0;
	if (!WriteFile(hFile, data.c_str(), data.size(), &dwBytesWritten, NULL)) {
		std::cerr << "Failed to write file, error code: " << GetLastError() << std::endl;
	}

	CloseHandle(hFile);
}


// Initialize the worker thread and background web service.
WorkThread::WorkThread(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings)
	: m_hMainWnd(hWnd), m_strModulePath(strModulePath), m_strModuleName(strModuleName), m_strModuleDir(strModuleDir)
	, m_bUpdateSelf(false), m_nTotalDownload(0), m_nCurrentDownload(0), m_nCurrentDownloadSize(0), m_nCurrentDownloadProgress(0)
	, m_hGameProcess{ nullptr }
	, m_dwGameProcessId()
	, m_qwVersion(0)
	, m_bRun(TRUE)
	, m_strCurrentDownload(L"")
	, m_hThread(nullptr)
	, m_p2pClient(std::make_unique<P2PClient>())
{
	m_p2pSettings = initialP2PSettings;
	if (m_p2pSettings.stunServers.empty()) {
		m_p2pSettings.stunServers = {
			"stun:stun.l.google.com:19302",
			"stun:global.stun.twilio.com:3478",
			"stun:stun.cloudflare.com:3478",
			"stun:127.0.0.1:3478"
		};
	}
	DWORD dwThreadId = 0;
	for (auto hFileMapping : m_hFileMappings)
	{
		CloseHandle(hFileMapping);
	}

	m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	if (m_hThread == NULL) {
		HandleError("CreateThread failed");
	}

	std::thread WebTr([this]() {
		WebServiceThread();
	});
	WebTr.detach();
}

// Cleanup hook for WorkThread lifecycle.
WorkThread::~WorkThread()
{
}

// Thread entry point that terminates stray processes and runs the worker.
DWORD __stdcall WorkThread::ThreadProc(LPVOID lpParameter)
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(PROCESSENTRY32);
		if (Process32First(hSnapshot, &pe)) {
			do {
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
				if (hProcess) {
					TCHAR processPath[MAX_PATH];
					DWORD dwSize = MAX_PATH;
					if (QueryFullProcessImageName(hProcess, 0, processPath, &dwSize))
					{
						std::wstring strProcessPath = processPath;
						if (strProcessPath.find(L"MapleFireReborn.exe") != std::string::npos){
							TerminateProcess(hProcess, 0);
						}
						if (strProcessPath.find(L"MapleStory.exe") != std::string::npos) {
							TerminateProcess(hProcess, 0);
						}
					}
					CloseHandle(hProcess);
				}
			} while (Process32Next(hSnapshot, &pe));
		}
		CloseHandle(hSnapshot);
	}

	WorkThread* pThis = (WorkThread*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

// 主流程：拉取配置、检查更新、启动客户端并进入监控循环。
DWORD WorkThread::Run()
{
	// 记录当前工作目录
	m_strCurrentDir = std::filesystem::current_path().string();
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// 拉取启动配置（包含下载地址与 P2P 参数）
	if (!FetchBootstrapConfig())
	{
		MessageBox(m_hMainWnd, L"Failed to fetch bootstrap config.", L"Error", MB_OK);
		Stop();
		return 0;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// 补齐信令地址并同步 P2P 配置到客户端
	{
		std::lock_guard<std::mutex> lock(m_p2pMutex);
		if (m_p2pSettings.signalEndpoint.empty()) {
			m_p2pSettings.signalEndpoint = BuildSignalEndpoint(m_strUrl, m_strPage);
		}
		if (m_p2pClient) {
			m_p2pClient->UpdateSettings(m_p2pSettings);
		}
	}

	// 初始化主下载客户端
	m_client = new httplib::Client(m_strUrl);

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// 基础包不存在时先下载并解压
	if (!std::filesystem::exists("./Data"))
	{
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		if (!DownloadBasePackage()) {
			MessageBox(m_hMainWnd, L"Failed to download base package.", L"Error", MB_OK);
			Stop();
			return 0;
		}
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	{
		std::cout << "9999999999999999999" << std::endl;
		// 读取本地 Version.dat，并计算 MD5
		std::string strLocalVersionDatContent;
		std::ifstream ifs("Version.dat", std::ios::binary);
		if (ifs.is_open()) {
			std::stringstream buffer;
			buffer << ifs.rdbuf();
			strLocalVersionDatContent = buffer.str();
			ifs.close();
		}
		std::cout << "aaaaaaaaaaaaaaaaaaaaaaa" << std::endl;
		m_strLocalVersionMD5 = FileHash::string_md5(strLocalVersionDatContent);

		// 解析本地清单，初始化本地文件表与运行时列表
		if (!strLocalVersionDatContent.empty())
		{
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strLocalVersionDat, root)) {
				m_qwVersion = root["time"].asInt64();
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					if (config.m_strPage.empty()) {
						continue;
					}
					m_mapFiles[config.m_strPage] = config;
					// 为每个清单路径准备本地文件占位
					try {
						const std::filesystem::path localPath =
							std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
						std::error_code ec;
						const auto parent = localPath.parent_path();
						if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
							std::filesystem::create_directories(parent, ec);
						}
						ec.clear();
						if (!std::filesystem::exists(localPath, ec)) {
							std::ofstream ofs(localPath, std::ios::binary);
							ofs.close();
						}
					}
					catch (...) {
						std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
						m_mapFiles.erase(config.m_strPage);
					}
				}

				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}

	// 拉取远端清单并下载运行时更新
	RefreshRemoteVersionManifest();
	if (!DownloadRunTimeFile())
	{
		MessageBox(m_hMainWnd, L"Failed to download update files.", L"Error", MB_OK);
		Stop();
		return 0;
	}

	// 需要自更新时，启动临时更新程序并退出
	if (m_bUpdateSelf)
	{
		Stop();
		ShellExecute(NULL, L"open", L"UpdateTemp.exe", m_strModulePath.c_str(), m_strModuleDir.c_str(), SW_SHOWNORMAL);
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		WriteProfileString(TEXT("MapleFireReborn"), TEXT("pid"), TEXT("0"));
		ExitProcess(0);
		return 0;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	unsigned long long dwTick = GetTickCount64();
	// 写入共享内存供客户端读取文件校验信息
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping elapsed ms: " << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// 启动客户端进程
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	// 最小化托盘，并停止渲染
	PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
	g_bRendering = false;

	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess elapsed ms: " << dwNewTick - dwTick << std::endl;

	// 记录主客户端进程句柄与 PID
	m_hGameProcess[0] = pi.hProcess;
	m_dwGameProcessId[0] = pi.dwProcessId;

	// 写入配置以便其他模块读取 PID
	TCHAR szPID[32] = { 0 };
	_itow_s(pi.dwProcessId, szPID, 10);
	WriteProfileString(TEXT("MapleFireReborn"), TEXT("Client1PID"), szPID);

	if (pi.hThread)
	{
		CloseHandle(pi.hThread);
	}

	// 轮询监控客户端进程是否仍在运行
	do
	{
		bool bHaveGameRun = false;
		for (int i = 0; i < sizeof(m_dwGameProcessId) / sizeof(m_dwGameProcessId[0]); ++i)
		{
			if (m_dwGameProcessId[0])
			{
				if (IsProcessRunning(m_dwGameProcessId[i]))
				{
					bHaveGameRun = true;
				}
				else {
					m_dwGameProcessId[i] = 0;
					m_hGameProcess[i] = nullptr;
				}
			}
		}

		// 客户端都退出且 UI 不渲染时，关闭托盘并结束
		if (bHaveGameRun == false && g_bRendering == false)
		{
			PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
			break;
		}

		Sleep(1);
	} while (m_bRun);

	// 清理残留的客户端进程句柄
	for (int i = 0; i < sizeof(m_hGameProcess) / sizeof(m_hGameProcess[0]); ++i)
	{
		if (m_hGameProcess[i])
		{
			TerminateProcess(m_hGameProcess[i], 0);
			CloseHandle(m_hGameProcess[i]);
			m_hGameProcess[i] = nullptr;
			m_dwGameProcessId[i] = 0;
		}
	}

	// 关闭工作线程句柄
	CloseHandle(m_hThread);
	m_hThread = nullptr;

	return 0;
}

// Log a Windows error and abort.
void WorkThread::HandleError(const char* msg) {
	std::cerr << msg << " Error: " << GetLastError() << std::endl;
	exit(1);
}

// Decrypt bootstrap config payload using a fixed AES key.
std::string WorkThread::DecryptConfigPayload(const std::string& ciphertext)
{
	HCRYPTPROV hProv;
	HCRYPTKEY hKey;
	HCRYPTHASH hHash;

	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
		HandleError("CryptDeriveKey failed");
		return ciphertext;
	}

	DWORD dataLen = ciphertext.length();
	DWORD bufferLen = dataLen;

	if (bufferLen > 0)
	{
		std::string buffer(bufferLen + 1, 0);
		memcpy(&buffer[0], ciphertext.c_str(), dataLen);

		if (!CryptDecrypt(hKey, 0, TRUE, 0, (BYTE*)buffer.data(), &dataLen)) {
			HandleError("CryptDecrypt failed");
		}
		buffer.resize(dataLen);

		return buffer;
	}

	return ciphertext;
}

// Decompress and decode Version.dat payload.
std::string WorkThread::DecryptVersionDat(const std::string& ciphertext)
{
	if (ciphertext.empty()) {
		return {};
	}

	const size_t decompressBound = ZSTD_getFrameContentSize(ciphertext.data(), ciphertext.size());
	if (decompressBound == ZSTD_CONTENTSIZE_ERROR || decompressBound == ZSTD_CONTENTSIZE_UNKNOWN || decompressBound == 0 ||
		decompressBound > (64ULL * 1024ULL * 1024ULL)) {
		std::cout << "Invalid Version.dat payload size: " << decompressBound << std::endl;
		return {};
	}

	const std::string strDict = "D2Qbzy7hnmLh1zqgmDKx";
	ZSTD_DDict* ddict = ZSTD_createDDict(strDict.data(), strDict.size());
	if (!ddict) {
		return {};
	}

	ZSTD_DCtx* dctx = ZSTD_createDCtx();
	if (!dctx) {
		ZSTD_freeDDict(ddict);
		return {};
	}

	std::string strJson;
	strJson.resize(decompressBound);
	const size_t decompressSize = ZSTD_decompress_usingDDict(
		dctx, &strJson[0], decompressBound, ciphertext.data(), ciphertext.size(), ddict);

	ZSTD_freeDDict(ddict);
	ZSTD_freeDCtx(dctx);

	if (ZSTD_isError(decompressSize) != 0) {
		std::cout << "Failed to decompress Version.dat: " << ZSTD_getErrorName(decompressSize) << std::endl;
		return {};
	}

	strJson.resize(decompressSize);
	return strJson;
}

// Download runtime files listed in the manifest.
bool WorkThread::DownloadRunTimeFile()
{
	m_nTotalDownload = m_vecRunTimeList.size();
	m_nCurrentDownload = 0;
	for (auto& download : m_vecRunTimeList)
	{
		std::string strLocalFile = download;
		const std::string strPage = JoinUrlPath(
			m_strPage, "Update/" + std::to_string(m_mapFiles[download].m_qwTime) + "/" + download);

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;
		SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_nCurrentDownloadSize = m_mapFiles[download].m_qwSize;
		m_nCurrentDownloadProgress = 0;

		auto it = m_mapFiles.find(strLocalFile);
		if (it != m_mapFiles.end())
		{
			bool Md5Same = false;
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::u8path(strLocalFile), ec)) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				Md5Same = it->second.m_strMd5 == strLocalFileMd5;
				std::cout << "md51:" << it->second.m_strMd5 << "vs md52:"<< strLocalFileMd5 << std::endl;
			}

			if (Md5Same)
			{
				m_nCurrentDownload += 1;
				continue;
			}
		}
		std::cout << __FILE__ << ":" << __LINE__ << " File: " << strLocalFile << std::endl;

		if (strLocalFile.find("RebornLauncher.exe") != std::string::npos)
		{
#ifdef _DEBUG
			continue;
#else
			strLocalFile = "UpdateTemp.exe";
			m_bUpdateSelf = true;
#endif
		}

		const std::wstring strLocalFileW = str2wstr(strLocalFile);
		if (!strLocalFileW.empty()) {
			SetFileAttributesW(strLocalFileW.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(strLocalFileW.c_str());
		}
		else {
			std::error_code ec;
			std::filesystem::remove(std::filesystem::u8path(strLocalFile), ec);
		}

		if (!DownloadWithResume(strPage, strLocalFile)) {
			std::cout << "Download failed for runtime file: " << strPage << std::endl;
			return false;
		}
		m_nCurrentDownload += 1;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	return true;
}

// Return total download count.
int WorkThread::GetTotalDownload() const
{
	return m_nTotalDownload;
}

// Return current download index.
int WorkThread::GetCurrentDownload() const
{
	return m_nCurrentDownload;
}

// Return current download file name.
std::wstring WorkThread::GetCurrentDownloadFile()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_strCurrentDownload;
}

// Set the UI-visible current download file name.
void WorkThread::SetCurrentDownloadFile(const std::wstring& strFile)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strCurrentDownload = strFile;
}

// Return current download total size.
int WorkThread::GetCurrentDownloadSize() const
{
	return m_nCurrentDownloadSize;
}

// Return current download progress.
int WorkThread::GetCurrentDownloadProgress() const
{
	return m_nCurrentDownloadProgress;
}

// Download a file from an absolute URL without resume.
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

// Download a file with ranged, chunked resume support.
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

// Download and extract the base package archive.
bool WorkThread::DownloadBasePackage()
{
	m_nTotalDownload = 1;
	m_nCurrentDownload = 0;
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	if (m_basePackageUrls.empty()) {
		std::cout << "Bootstrap config missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	bool downloaded = false;
	std::string downloadedArchivePath;
	for (const auto& packageUrl : m_basePackageUrls) {
		std::string absolutePackageUrl = packageUrl;
		if (!IsHttpUrl(absolutePackageUrl)) {
			absolutePackageUrl = m_strUrl + NormalizeRelativeUrlPath(packageUrl);
		}
		std::string localArchivePath = GetFileNameFromUrl(absolutePackageUrl);
		if (localArchivePath.empty()) {
			localArchivePath = "base_package.7z";
		}

		for (int attempt = 0; attempt < 2; ++attempt) {
			m_nCurrentDownloadProgress = 0;
			m_nCurrentDownloadSize = 0;
			downloaded = DownloadFileChunkedWithResume(absolutePackageUrl, localArchivePath, 2);
			if (downloaded) {
				downloadedArchivePath = localArchivePath;
				break;
			}
		}
		if (downloaded) {
			break;
		}
	}

	if (!downloaded) {
		std::cout << "Base package download failed for all candidates." << std::endl;
		return false;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	Extract7z(downloadedArchivePath, "./");
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	std::filesystem::remove(downloadedArchivePath);
	return true;
}

// Extract a 7z archive with parallel workers.
void WorkThread::Extract7z(const std::string& filename, const std::string& destPath)
{
	std::vector<DataBlock> allFiles = ScanArchive(filename);

	// Total file count
	m_nCurrentDownloadSize = allFiles.size();
	m_nCurrentDownloadProgress = 0;
	if (m_nCurrentDownloadSize > 0)
	{
		// Determine thread count; ensure at least one worker.
		const unsigned int hwThreads = std::thread::hardware_concurrency();
		const size_t preferredThreads = hwThreads == 0 ? 1 : static_cast<size_t>(hwThreads);
		const size_t numThreads = (std::min)(preferredThreads, allFiles.size());
		const size_t filesPerThread = (allFiles.size() + numThreads - 1) / numThreads;

		std::vector<std::thread> threads;
		for (size_t i = 0; i < numThreads; ++i) {
			size_t start = i * filesPerThread;
			size_t end = (i == numThreads - 1) ? allFiles.size() : (i + 1) * filesPerThread;
			if (start >= end) {
				continue;
			}
			std::vector<DataBlock> files(allFiles.begin() + start, allFiles.begin() + end);
			threads.emplace_back(&WorkThread::ExtractFiles, this, filename, destPath, files);
		}

		// Wait for all threads to complete
		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}

		std::cout << filename << " extraction completed" << std::endl;
	}
	else
	{
		std::cout << filename << " is empty, no files to extract" << std::endl;
	}
}

// Scan archive file and get list of file info
// Scan a 7z archive and return file metadata.
std::vector<DataBlock> WorkThread::ScanArchive(const std::string& archivePath) {
	struct archive* a;
	struct archive_entry* entry;
	std::vector<DataBlock> files;

	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);

	if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
		std::cerr << "Failed to open archive: " << archive_error_string(a) << std::endl;
		archive_read_free(a);
		return files;
	}

	// Scan each file
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char* entryPath = archive_entry_pathname(entry);
		if (entryPath == nullptr || entryPath[0] == '\0') {
			archive_read_data_skip(a);
			continue;
		}
		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_read_data_skip(a);
			continue;
		}

		DataBlock fileInfo;
		fileInfo.filePath = NormalizeArchivePath(entryPath);
		if (fileInfo.filePath.empty()) {
			archive_read_data_skip(a);
			continue;
		}
		fileInfo.fileOffset = archive_read_header_position(a);
		files.push_back(fileInfo);
		archive_read_data_skip(a); // Skip file data, only read header info
	}

	archive_read_close(a);
	archive_read_free(a);

	const std::string extractRootPrefix = DetermineExeRootPrefix(files);

	if (!extractRootPrefix.empty()) {
		m_extractRootPrefix = extractRootPrefix;
	}
	else {
		m_extractRootPrefix.clear();
	}

	return files;
}

// Thread function to extract a group of files
// Extract a subset of archive files on a worker thread.
void WorkThread::ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files) {
	struct archive* a;
	struct archive_entry* entry;
	const std::string extractRootPrefix = m_extractRootPrefix;
	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);
	do {

		if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
			std::cerr << "Failed to reopen archive for file: " << std::endl;
			break;
		}

		for (const auto& fileInfo : files) {
			while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
				const char* entryPath = archive_entry_pathname(entry);
				if (entryPath == nullptr || entryPath[0] == '\0') {
					archive_read_data_skip(a);
					continue;
				}
				if (archive_entry_filetype(entry) != AE_IFREG) {
					archive_read_data_skip(a);
					continue;
				}
				const std::string normalizedEntryPath = NormalizeArchivePath(entryPath);
				if (fileInfo.filePath == normalizedEntryPath) {
					std::string relativePath = StripArchivePrefix(normalizedEntryPath, extractRootPrefix);
					relativePath = NormalizeArchivePath(relativePath);
					if (relativePath.empty()) {
						relativePath = GetArchiveFileName(normalizedEntryPath);
					}

					std::string safeRelativePath;
					if (!MakeSafeRelativePath(relativePath, safeRelativePath)) {
						archive_read_data_skip(a);
						break;
					}

					std::string outputPath = outPath + "/" + safeRelativePath;
					std::filesystem::path filePath = outputPath;
					if (!std::filesystem::exists(filePath.parent_path()))
					{
						std::filesystem::create_directories(filePath.parent_path());
					}
					std::ofstream outputFile(outputPath, std::ios::binary);
					if (!outputFile.is_open()) {
						std::cerr << "Failed to create output file: " << outputPath << std::endl;
						break;
					}

					const void* buffer;
					size_t size;
					la_int64_t offset;
					while (archive_read_data_block(a, &buffer, &size, &offset) == ARCHIVE_OK) {
						outputFile.write((const char*)buffer, size);
					}

					outputFile.close();
					{
						m_nCurrentDownloadProgress++;
						// std::cout << "Progress: " << m_nCurrentDownloadProgress << "/" << m_nCurrentDownloadSize << std::endl;
					}
					break;
				}
			}
		}
	} while (false);
	archive_read_close(a);
	archive_read_free(a);
}

// Download a file with HTTP resume (or P2P when enabled).
bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path) {

	std::string strUrl = NormalizeRelativeUrlPath(url);
	std::cout << __FILE__ << ":" << __LINE__ << " url:" << m_strUrl << " page:" << strUrl << std::endl;
	SetCurrentDownloadFile(str2wstr(file_path, static_cast<int>(file_path.length())));

	{
		P2PSettings settingsCopy;
		{
			std::lock_guard<std::mutex> lock(m_p2pMutex);
			settingsCopy = m_p2pSettings;
		}
		if (settingsCopy.enabled && m_p2pClient) {
			m_p2pClient->UpdateSettings(settingsCopy);
			const bool p2pOk = m_p2pClient->TryDownload(strUrl, file_path, [this](uint64_t current, uint64_t total) {
				m_nCurrentDownloadProgress = static_cast<int>(current);
				if (total > 0) {
					m_nCurrentDownloadSize = static_cast<int>(total);
				}
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
				return true;
			}
		}
	}

	if (m_client == nullptr) {
		return false;
	}

	// Request file size using Range to avoid HEAD incompatibilities
	httplib::Result res;
	{
		httplib::Headers headers;
		headers.insert({ "Range", "bytes=0-0" });
		res = m_client->Get(strUrl.c_str(), headers);
		if (res && (res->status == 200 || res->status == 206)) {
			m_nCurrentDownloadSize = static_cast<int>(ParseTotalSizeFromResponse(*res));
		}
		else {
			std::cout << "Failed to get file size, status code: " << (res ? res->status : -1) << std::endl;
			m_nCurrentDownloadSize = 0;
		}
	}

	// Check if the file already exists locally
	std::ifstream existing_file(std::filesystem::u8path(file_path), std::ios::binary | std::ios::ate);
	size_t existing_file_size = 0;
	if (existing_file.is_open()) {
		existing_file_size = existing_file.tellg();
		existing_file.close();
	}
	if (m_nCurrentDownloadSize > 0) {
		if (existing_file_size == static_cast<size_t>(m_nCurrentDownloadSize)) {
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
		// Resume download from where we left off
		m_nCurrentDownloadProgress = static_cast<int>(existing_file_size);
		headers.insert({ "Range", "bytes=" + std::to_string(existing_file_size) + "-" + std::to_string(m_nCurrentDownloadSize) });
		std::cout << "Resuming download from byte: " << existing_file_size << std::endl;
	}

	// Open file for append
	std::ofstream file(std::filesystem::u8path(file_path), std::ios::binary | std::ios::app);
	if (!file.is_open()) {
		return false;
	}

	res = m_client->Get(strUrl.c_str(), headers, [&](const char* data, size_t data_length) {
		file.write(data, static_cast<std::streamsize>(data_length));
		file.flush();
		m_nCurrentDownloadProgress += static_cast<int>(data_length);
		return true; // keep downloading
	});
	file.close();

	if (res && res->status == 200) {
		std::cout << "Download completed!" << std::endl;
		return true;
	}
	else if (res && res->status == 206) {
		std::cout << "Download resumed and completed!" << std::endl;
		return true;
	}
	else {
		std::cerr << "Download failed with status code: " << (res ? res->status : -1) << std::endl;
		return false;
	}
}


// Publish file hashes into named shared memory blocks.
void WorkThread::WriteDataToMapping()
{
	// m_mapFiles
	for (auto& [strPage, config] : m_mapFiles)
	{
		std::string strMemoryName = strPage;
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, config.m_strMd5.length() + 1, strMemoryName.c_str());
		if (hFileMapping)
		{
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}

// Run local HTTP control endpoints for download/launch.
void WorkThread::WebServiceThread()
{
	httplib::Server svr;
		svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
		g_bRendering = true;
		m_nTotalDownload = 1;
		m_nCurrentDownload = 0;
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		std::string strPage = req.get_param_value("page");
		SetCurrentDownloadFile(str2wstr(strPage, static_cast<int>(strPage.length())));
		auto it = m_mapFiles.find(strPage);
		if (it != m_mapFiles.end()) {
			res.status = 200;
			res.set_content("OK", "text/plain");
		} else {
			res.status = 404;
			res.set_content("Not Found", "text/plain");
		}
		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

svr.Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {

		std::cout << "RunClient request" << std::endl;

		RefreshRemoteVersionManifest();
		// Download runtime files before launching clients
		if (!DownloadRunTimeFile()) {
			res.status = 502;
			res.set_content("Update download failed.", "text/plain");
			return;
		}

		bool launched = false;
		for (int i = 0; i < static_cast<int>(sizeof(m_dwGameProcessId) / sizeof(m_dwGameProcessId[0])); ++i)
		{
			if (m_dwGameProcessId[i] != 0 && IsProcessRunning(m_dwGameProcessId[i]))
			{
				std::cout << "Process " << m_dwGameProcessId[i] << " already running" << std::endl;
				continue;
			}

			STARTUPINFO si = { sizeof(si) };
			PROCESS_INFORMATION pi{};
			if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
				HandleError("CreateProcess failed");
				continue;
			}

			m_hGameProcess[i] = pi.hProcess;
			m_dwGameProcessId[i] = pi.dwProcessId;

			TCHAR szPID[32] = { 0 };
			_itow_s(pi.dwProcessId, szPID, 10);
			if (i == 0)
			{
				WriteProfileString(TEXT("MapleFireReborn"), TEXT("Client1PID"), szPID);
			}
			else if (i == 1)
			{
				WriteProfileString(TEXT("MapleFireReborn"), TEXT("Client2PID"), szPID);
			}

			if (pi.hThread)
			{
				CloseHandle(pi.hThread);
			}

			std::cout << "Client launched" << std::endl;
			res.status = 200;
			res.set_content("OK", "text/plain");
			launched = true;
			break;
		}

		if (!launched)
		{
			res.status = 429;
			res.set_content("Client limit reached (max 2).", "text/plain");
		}

		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

	svr.Get("/Stop", [&svr,this](const httplib::Request& req, httplib::Response& res) {
		for (int i = 0; i < sizeof(m_hGameProcess) / sizeof(m_hGameProcess[0]); ++i)
		{
			if (m_hGameProcess[i])
			{
				TerminateProcess(m_hGameProcess[i], 0);
				CloseHandle(m_hGameProcess[i]);
				m_hGameProcess[i] = nullptr;
			}
		}
		res.status = 200;
		res.set_content("OK", "text/plain");
		svr.stop();
	});

	svr.listen("localhost", 12345);


	std::cout << "Web service thread finished" << std::endl;
}

// Stop worker execution and shut down HTTP clients.
void WorkThread::Stop()
{
	// notify the local server that we're done
	httplib::Client cli("localhost",12345);
	cli.Get("/Stop");
	m_bRun = FALSE;
	if (m_client) {
		m_client->stop();
		m_client = nullptr;
	}
}

// Update P2P settings with thread safety.
void WorkThread::UpdateP2PSettings(const P2PSettings& settings)
{
	std::lock_guard<std::mutex> lock(m_p2pMutex);
	m_p2pSettings = settings;
}

// Return a snapshot of current P2P settings.
P2PSettings WorkThread::GetP2PSettings() const
{
	std::lock_guard<std::mutex> lock(m_p2pMutex);
	return m_p2pSettings;
}

// Fetch and parse the bootstrap configuration payload.
bool WorkThread::FetchBootstrapConfig()
{
	std::cout << "FetchBootstrapConfig" << std::endl;
	httplib::Client cli{ kBootstrapHost };
	auto res = cli.Get(kBootstrapPath);
	if (!res || res->status != 200) {
		if (res) {
			std::cout << "Failed to fetch bootstrap payload, status: " << res->status << std::endl;
		}
		else {
			std::cout << "Failed to fetch bootstrap payload, http error: " << res.error() << std::endl;
		}
		return false;
	}

	std::string ciphertext;
	if (!HexBodyToBytes(res->body, ciphertext)) {
		ciphertext = res->body;
	}
	const std::string decrypted = DecryptConfigPayload(ciphertext);

	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(decrypted, root) || !root.isObject()) {
		std::cout << "Bootstrap payload is not valid JSON." << std::endl;
		return false;
	}

	Json::Value content = root["content"];
	if (!content.isObject()) {
		content = root["download"];
	}
	if (!content.isObject()) {
		std::cout << "Bootstrap JSON missing content/download object." << std::endl;
		return false;
	}

	std::string versionManifestUrl = TrimAscii(content["version_manifest_url"].asString());
	if (versionManifestUrl.empty()) {
		versionManifestUrl = TrimAscii(content["version_dat_url"].asString());
	}
	if (versionManifestUrl.empty()) {
		std::cout << "Bootstrap JSON missing version_manifest_url." << std::endl;
		return false;
	}

	std::string updateRootUrl = TrimAscii(content["update_package_root_url"].asString());
	if (updateRootUrl.empty()) {
		updateRootUrl = TrimAscii(content["runtime_root_url"].asString());
	}
	if (updateRootUrl.empty()) {
		updateRootUrl = TrimAscii(content["update_root_url"].asString());
	}

	m_basePackageUrls = ReadStringArray(content, "base_package_urls");
	if (m_basePackageUrls.empty()) {
		const std::string singleBasePackageUrl = TrimAscii(content["base_package_url"].asString());
		if (!singleBasePackageUrl.empty()) {
			m_basePackageUrls.push_back(singleBasePackageUrl);
		}
	}

	std::string versionBaseUrl;
	std::string versionPath;
	const bool versionManifestIsAbsolute = IsHttpUrl(versionManifestUrl);
	if (versionManifestIsAbsolute) {
		if (!ExtractBaseAndPath(versionManifestUrl, versionBaseUrl, versionPath)) {
			std::cout << "Invalid version_manifest_url: " << versionManifestUrl << std::endl;
			return false;
		}
		m_strVersionManifestPath = versionManifestUrl;
	}

	if (!updateRootUrl.empty() && IsHttpUrl(updateRootUrl)) {
		if (!ExtractBaseAndPath(updateRootUrl, m_strUrl, m_strPage)) {
			std::cout << "Invalid update_package_root_url: " << updateRootUrl << std::endl;
			return false;
		}
	}
	else if (!updateRootUrl.empty()) {
		if (!versionBaseUrl.empty()) {
			m_strUrl = versionBaseUrl;
		}
		else if (m_strUrl.empty()) {
			std::cout << "Relative update_package_root_url requires absolute version_manifest_url." << std::endl;
			return false;
		}
		m_strPage = NormalizeRelativeUrlPath(updateRootUrl);
		if (m_strPage.back() != '/') {
			m_strPage.push_back('/');
		}
	}
	else if (!versionBaseUrl.empty()) {
		m_strUrl = versionBaseUrl;
		m_strPage = DirnamePath(versionPath);
	}
	else {
		std::cout << "Bootstrap JSON cannot resolve download host/path." << std::endl;
		return false;
	}

	if (m_strPage.empty()) {
		m_strPage = "/";
	}
	if (m_strPage.front() != '/') {
		m_strPage.insert(m_strPage.begin(), '/');
	}
	if (m_strPage.back() != '/') {
		m_strPage.push_back('/');
	}
	if (!versionManifestIsAbsolute) {
		const bool rootedPath = !versionManifestUrl.empty() &&
			(versionManifestUrl.front() == '/' || versionManifestUrl.front() == '\\');
		if (rootedPath) {
			m_strVersionManifestPath = NormalizeRelativeUrlPath(versionManifestUrl);
		}
		else {
			m_strVersionManifestPath = JoinUrlPath(m_strPage, versionManifestUrl);
		}
	}
	if (m_strVersionManifestPath.empty()) {
		m_strVersionManifestPath = JoinUrlPath(m_strPage, "Version.dat");
	}

	if (m_basePackageUrls.empty()) {
		std::cout << "Bootstrap JSON missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	Json::Value p2pJson = root["p2p"];
	if (p2pJson.isObject()) {
		std::lock_guard<std::mutex> lock(m_p2pMutex);

		const std::string signalUrl = TrimAscii(p2pJson["signal_url"].asString());
		if (!signalUrl.empty() && m_p2pSettings.signalEndpoint.empty()) {
			m_p2pSettings.signalEndpoint = signalUrl;
		}

		const std::string signalToken = TrimAscii(p2pJson["signal_auth_token"].asString());
		if (!signalToken.empty() && m_p2pSettings.signalAuthToken.empty()) {
			m_p2pSettings.signalAuthToken = signalToken;
		}

		auto remoteStuns = ReadStringArray(p2pJson, "stun_servers");
		if (!remoteStuns.empty()) {
			MergeUnique(remoteStuns, m_p2pSettings.stunServers);
			m_p2pSettings.stunServers = std::move(remoteStuns);
		}
	}

	std::cout << "Bootstrap resolved: host=" << m_strUrl
		<< " updateRoot=" << m_strPage
		<< " versionPath=" << m_strVersionManifestPath << std::endl;
	return true;
}

// Download and parse the remote version manifest.
bool WorkThread::RefreshRemoteVersionManifest()
{
	// 计算当前应该拉取的 Version.dat 路径（优先使用显式路径）
	const std::string strVersionDatPath = m_strVersionManifestPath.empty()
		? JoinUrlPath(m_strPage, "Version.dat")
		: m_strVersionManifestPath;

	// 封装请求逻辑：支持绝对 URL 与已有客户端
	auto fetchManifest = [this](const std::string& requestTarget) -> httplib::Result {
		httplib::Headers headers;
		headers.insert({ "Accept", "application/octet-stream" });
		headers.insert({ "Cache-Control", "no-cache" });

		if (IsHttpUrl(requestTarget)) {
			// 绝对 URL：临时创建客户端拉取
			bool useTls = false;
			std::string host;
			int port = 0;
			std::string path;
			if (!ParseHttpUrl(requestTarget, useTls, host, port, path)) {
				return httplib::Result();
			}
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
			if (useTls) {
				httplib::SSLClient client(host, port);
				client.set_follow_location(true);
				client.set_connection_timeout(8, 0);
				client.set_read_timeout(60, 0);
				return client.Get(path.c_str(), headers);
			}
			else
#endif
			{
				httplib::Client client(host, port);
				client.set_follow_location(true);
				client.set_connection_timeout(8, 0);
				client.set_read_timeout(60, 0);
				return client.Get(path.c_str(), headers);
			}
		}

		// 相对路径：复用主下载客户端
		if (!m_client) {
			return httplib::Result();
		}
		return m_client->Get(requestTarget.c_str(), headers);
	};

	// 首次尝试拉取清单
	httplib::Result res = fetchManifest(strVersionDatPath);
	std::string resolvedPath = strVersionDatPath;

	// 本地相对路径失败时，回退到同目录默认文件名
	if ((!res || res->status != 200 || res->body.empty()) && !IsHttpUrl(strVersionDatPath)) {
		const std::string fileName = GetFileNameFromUrl(strVersionDatPath);
		const std::string fallbackPath = JoinUrlPath(m_strPage, fileName.empty() ? "Version.dat" : fileName);
		if (fallbackPath != strVersionDatPath) {
			httplib::Result fallbackRes = fetchManifest(fallbackPath);
			if (fallbackRes && fallbackRes->status == 200 && !fallbackRes->body.empty()) {
				res = std::move(fallbackRes);
				resolvedPath = fallbackPath;
				m_strVersionManifestPath = fallbackPath;
			}
		}
	}

	// 状态码/内容校验
	if (!res || res->status != 200) {
		const std::string statusText = res ? std::to_string(res->status) : std::string("none");
		const int errorCode = static_cast<int>(res.error());
		std::cout << "Failed to fetch Version.dat from " << resolvedPath
			<< ", status: " << statusText
			<< ", error: " << errorCode
			<< std::endl;
		return false;
	}
	if (res->body.empty()) {
		std::cout << "Version.dat response body is empty, path: " << resolvedPath << std::endl;
		return false;
	}

	// JSON 解析器封装
	auto parseJsonObject = [](const std::string& text, Json::Value& out) -> bool {
		Json::CharReaderBuilder builder;
		std::string errors;
		std::istringstream iss(text);
		return Json::parseFromStream(builder, iss, &out, &errors) && out.isObject();
	};

	// 尝试解析清单（支持明文 JSON / 旧版压缩 / 兼容 hex 编码）
	std::string manifestBinary = res->body;
	std::string manifestJsonText;
	Json::Value root;

	// 新格式：来自 UpdateForge 的纯 JSON 主体。
	if (parseJsonObject(manifestBinary, root)) {
		manifestJsonText = manifestBinary;
	}
	else {
		// 旧版格式：zstd 压缩的 Version.dat 有效负载。
		const std::string decompressed = DecryptVersionDat(manifestBinary);
		if (!decompressed.empty() && parseJsonObject(decompressed, root)) {
			manifestJsonText = decompressed;
		}
		else {
			// 兼容性处理：如果有效负载是 hex 编码文本，解码一次后重试。
			std::string decoded;
			if (HexBodyToBytes(manifestBinary, decoded)) {
				if (parseJsonObject(decoded, root)) {
					manifestBinary = decoded;
					manifestJsonText = decoded;
				}
				else {
					const std::string decodedDecompressed = DecryptVersionDat(decoded);
					if (!decodedDecompressed.empty() && parseJsonObject(decodedDecompressed, root)) {
						manifestBinary = decoded;
						manifestJsonText = decodedDecompressed;
					}
				}
			}
		}
	}

	// 无法识别格式则失败
	if (manifestJsonText.empty()) {
		std::cout << "Version.dat format not supported, path: " << resolvedPath
			<< ", body_size: " << res->body.size() << std::endl;
		return false;
	}

	// 对比 MD5，仅当远端不同才刷新本地清单
	std::string strRemoteVersionDatMd5 = FileHash::string_md5(manifestBinary);
	if (strRemoteVersionDatMd5 != m_strLocalVersionMD5)
	{
		// 写回 Version.dat 并更新内存清单结构
		std::ofstream ofs("Version.dat", std::ios::binary);
		ofs.write(manifestBinary.data(), manifestBinary.size());
		ofs.close();
		m_qwVersion = root["time"].asInt64();
		m_mapFiles.clear();
		Json::Value filesJson = root["file"];
		for (auto& fileJson : filesJson) {
			VersionConfig config;
			config.m_strMd5 = fileJson["md5"].asString();
			config.m_qwTime = fileJson["time"].asInt64();
			config.m_qwSize = fileJson["size"].asInt64();
			config.m_strPage = fileJson["page"].asString();
			if (config.m_strPage.empty()) {
				continue;
			}
			m_mapFiles[config.m_strPage] = config;
			// 确保本地路径存在或创建占位文件
			try {
				const std::filesystem::path localPath =
					std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
				std::error_code ec;
				const auto parent = localPath.parent_path();
				if (!parent.empty() && !std::filesystem::exists(parent, ec))
				{
					std::filesystem::create_directories(parent, ec);
				}
				ec.clear();
				if (!std::filesystem::exists(localPath, ec))
				{
					std::ofstream ofs(localPath, std::ios::binary);
					ofs.close();
				}
			}
			catch (...) {
				std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
				m_mapFiles.erase(config.m_strPage);
			}
		}
		// 刷新运行时下载列表
		m_vecRunTimeList.clear();
		Json::Value downloadList = root["runtime"];
		for (auto& download : downloadList) {
			m_vecRunTimeList.push_back(download.asString());
		}
	}

	return true;
}
