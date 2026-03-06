#include "VersionConfig.h"
#include "P2PClient.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace httplib
{
	class Client;
	class Server;
}

struct DataBlock {
	std::string filePath;
	long long fileOffset;
	struct archive_entry* entry;
};

struct tagGameInfo
{
	HANDLE hProcess{ nullptr };
	HWND hMainWnd{ nullptr };
	DWORD dwProcessId{ 0 };
	bool downloading{ false };
	std::wstring downloadFile;
	uint64_t downloadDoneBytes{ 0 };
	uint64_t downloadTotalBytes{ 0 };
};

struct VersionState {
	std::map<std::string, VersionConfig> files;
	std::vector<std::string> runtimeList;
	std::string manifestPath;
	std::vector<std::string> basePackageUrls;
	std::string extractRootPrefix;
	std::string localVersionMD5;
	std::unordered_set<std::string> localOnlyFiles;
};

enum class UpdateLogLevel : int {
	Debug = 1,
	Info = 2,
	Warn = 3,
	Error = 4
};

struct DownloadProgressState {
	int totalDownload{ 0 };
	int currentDownload{ 0 };
	std::wstring currentFile;
	int currentDownloadSize{ 0 };
	int currentDownloadProgress{ 0 };
	std::mutex mutex;
};

struct SelfUpdateState {
	bool updateSelf{ false };
	std::wstring modulePath;
	std::wstring moduleDir;
};

struct DeferredFileUpdateTask {
	std::string remoteUrl;
	std::string localPath;
	DWORD ownerProcessId{ 0 };
	uint32_t retryCount{ 0 };
	ULONGLONG nextRetryTick{ 0 };
};

struct RuntimeState {
	BOOL run{ TRUE };
	HANDLE thread{ nullptr };
	std::vector<std::shared_ptr<tagGameInfo>> gameInfos;
	mutable std::mutex gameInfosMutex;
	std::vector<HANDLE> fileMappings;
	HANDLE mappingVersion{ nullptr };
	std::deque<DeferredFileUpdateTask> deferredUpdateQueue;
	std::unordered_set<std::string> deferredQueuedPaths;
	mutable std::mutex deferredUpdateMutex;
	HWND mainWnd{ nullptr };
};

struct NetworkState {
	std::string url;
	std::string page;
	std::unique_ptr<httplib::Client> client;
	std::unique_ptr<P2PClient> p2pClient;
	P2PSettings p2pSettings;
	mutable std::mutex p2pMutex;
};

namespace workthread::runflow {
	class RunCoordinator;
}
namespace workthread::runtimeupdate {
	class RuntimeUpdater;
}
namespace workthread::versionload {
	class LocalVersionLoader;
}

class LauncherUpdateCoordinator
{
public:
	LauncherUpdateCoordinator(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings = {});
	~LauncherUpdateCoordinator();
public:
	static DWORD WINAPI ThreadProc(LPVOID lpParameter);
public:
	DWORD Run();
	void HandleError(const char* msg);
	std::string DecryptConfigPayload(const std::string& ciphertext);
	std::string DecryptVersionDat(const std::string& ciphertext);
	bool DownloadRunTimeFile();

	HWND FindGameWindowByProcessId(std::vector<std::shared_ptr<tagGameInfo>>& gameInfos, DWORD processId);
	int GetTotalDownload() const;
	int GetCurrentDownload() const;
	std::wstring GetCurrentDownloadFile();
	void SetCurrentDownloadFile(const std::wstring& strFile);

	int GetCurrentDownloadSize() const;
	int GetCurrentDownloadProgress() const;
	std::vector<tagGameInfo> GetGameInfosSnapshot() const;
	void SetLauncherStatus(const std::wstring& status);
	std::wstring GetLauncherStatus() const;

	bool DownloadBasePackage();

	bool Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	bool ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(
		const std::string& url,
		const std::string& file_path,
		DWORD ownerProcessId = 0,
		bool allowDeferredOnBusy = false,
		bool* queuedForDeferred = nullptr,
		bool allowP2P = false);
	bool DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath);
	bool DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount);

	// Publish downloaded file hashes into shared memory mappings for the game clients.
	void WriteDataToMapping();

	void WriteVersionToMapping(std::string& m_strRemoteVersionJson);

	void WebServiceThread();
	void RequestWebServiceRecovery();

	void Stop();

	bool FetchBootstrapConfig();

	bool RefreshRemoteVersionManifest();

	void UpdateP2PSettings(const P2PSettings& settings);
	P2PSettings GetP2PSettings() const;
	void LogUpdateError(
		const char* code,
		const char* source,
		const std::string& reason,
		const std::string& details = {},
		DWORD systemError = 0,
		int httpStatus = 0,
		int httpError = 0) const;
	void LogUpdateInfo(
		const char* code,
		const char* source,
		const std::string& message,
		const std::string& details = {}) const;
	template <typename... Args>
	void LogUpdateInfoFmt(
		const char* code,
		const char* source,
		std::string_view messageFmt,
		Args&&... args) const;
	void LogUpdateWarn(
		const char* code,
		const char* source,
		const std::string& message,
		const std::string& details = {}) const;
	template <typename... Args>
	void LogUpdateWarnFmt(
		const char* code,
		const char* source,
		std::string_view messageFmt,
		Args&&... args) const;
	void LogUpdateDebug(
		const char* code,
		const char* source,
		const std::string& message,
		const std::string& details = {}) const;
	template <typename... Args>
	void LogUpdateDebugFmt(
		const char* code,
		const char* source,
		std::string_view messageFmt,
		Args&&... args) const;
	template <typename... Args>
	void LogUpdateErrorFmt(
		const char* code,
		const char* source,
		std::string_view reasonFmt,
		Args&&... args) const;
	template <typename... Args>
	void LogUpdateErrorDetailsFmt(
		const char* code,
		const char* source,
		const std::string& reason,
		std::string_view detailsFmt,
		Args&&... args) const;
	void SetLogLevel(int level);
	int GetLogLevel() const;
private:
	bool InitializeDownloadEnvironment();
	bool EnsureBasePackageReady();
	void LoadLocalVersionState();
	bool IsRuntimeUpdateSkipped(const std::string& localPath) const;
	void RefreshRemoteManifestIfChanged();
	bool HandleSelfUpdateAndExit();
	bool PublishMappingsAndLaunchInitialClient();
	void MonitorClientsUntilShutdown();

	bool LaunchGameClient();
	HWND FindGameWindowByProcessId(DWORD processId) const;
	void UpdateGameMainWindows();
	void MarkClientDownloadStart(DWORD processId, const std::wstring& fileName);
	void MarkClientDownloadProgress(DWORD processId, uint64_t downloaded, uint64_t total);
	void MarkClientDownloadFinished(DWORD processId);
	bool EnqueueDeferredFileUpdate(const std::string& remoteUrl, const std::string& localPath, DWORD ownerProcessId);
	void ProcessDeferredFileUpdates();
	bool VerifyArchiveReadable(const std::string& archivePath);
	void CleanupExitedGameInfos();
	bool HasRunningGameProcess();
	void TerminateAllGameProcesses();
	std::filesystem::path GetUpdateLogFilePath() const;
	std::string FormatSystemError(DWORD errorCode) const;
	friend class workthread::runflow::RunCoordinator;
	friend class workthread::runtimeupdate::RuntimeUpdater;
	friend class workthread::versionload::LocalVersionLoader;

	LPCTSTR m_szProcessName = TEXT("MapleFireReborn.exe");

	RuntimeState m_runtimeState;
	VersionState m_versionState;

	NetworkState m_networkState;
	DownloadProgressState m_downloadState;

	SelfUpdateState m_selfUpdateState;
	std::wstring m_launcherStatus{ L"\u542f\u52a8\u5668\u521d\u59cb\u5316\u4e2d..." };
	mutable std::mutex m_statusMutex;
	mutable std::mutex m_logMutex;
	std::atomic<int> m_logLevel{ static_cast<int>(UpdateLogLevel::Warn) };
	std::mutex m_launchFlowMutex;
	mutable std::mutex m_webServiceMutex;
	std::shared_ptr<httplib::Server> m_activeWebServer;
	std::atomic<bool> m_webServiceRecoveryRequested{ false };
};

namespace workthread::loggingdetail {

inline std::string BuildLocalTimestamp() {
	SYSTEMTIME st{};
	GetLocalTime(&st);
	char buffer[64]{};
	sprintf_s(
		buffer,
		sizeof(buffer),
		"%04u-%02u-%02u %02u:%02u:%02u.%03u",
		static_cast<unsigned>(st.wYear),
		static_cast<unsigned>(st.wMonth),
		static_cast<unsigned>(st.wDay),
		static_cast<unsigned>(st.wHour),
		static_cast<unsigned>(st.wMinute),
		static_cast<unsigned>(st.wSecond),
		static_cast<unsigned>(st.wMilliseconds));
	return buffer;
}

inline std::string BuildDateStamp() {
	SYSTEMTIME st{};
	GetLocalTime(&st);
	char buffer[32]{};
	sprintf_s(
		buffer,
		sizeof(buffer),
		"%04u-%02u-%02u",
		static_cast<unsigned>(st.wYear),
		static_cast<unsigned>(st.wMonth),
		static_cast<unsigned>(st.wDay));
	return buffer;
}

inline std::string SanitizeForLog(const std::string& value) {
	std::string sanitized = value;
	for (char& ch : sanitized) {
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			ch = ' ';
		}
	}
	return sanitized;
}

inline int ClampLogLevel(int level) {
	if (level < static_cast<int>(UpdateLogLevel::Debug)) {
		return static_cast<int>(UpdateLogLevel::Debug);
	}
	if (level > static_cast<int>(UpdateLogLevel::Error)) {
		return static_cast<int>(UpdateLogLevel::Error);
	}
	return level;
}

template <typename... Args>
inline std::string FormatToString(std::string_view pattern, Args&&... args) {
	try {
		return std::vformat(pattern, std::make_format_args(std::forward<Args>(args)...));
	}
	catch (const std::format_error&) {
		return std::string(pattern);
	}
}

} // namespace workthread::loggingdetail

inline std::filesystem::path LauncherUpdateCoordinator::GetUpdateLogFilePath() const {
	std::error_code ec;
	std::filesystem::path baseDir = std::filesystem::current_path(ec);
	if (ec) {
		baseDir = std::filesystem::path(".");
	}
	std::filesystem::path logDir = baseDir / "Logs";
	std::filesystem::create_directories(logDir, ec);
	const std::string fileName = "update-" + workthread::loggingdetail::BuildDateStamp() + ".log";
	return logDir / std::filesystem::u8path(fileName);
}

inline std::string LauncherUpdateCoordinator::FormatSystemError(DWORD errorCode) const {
	if (errorCode == 0) {
		return {};
	}

	LPSTR messageBuffer = nullptr;
	const DWORD size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPSTR>(&messageBuffer),
		0,
		nullptr);

	std::string message;
	if (size > 0 && messageBuffer != nullptr) {
		message.assign(messageBuffer, size);
		LocalFree(messageBuffer);
	}

	while (!message.empty() && std::isspace(static_cast<unsigned char>(message.back())) != 0) {
		message.pop_back();
	}
	return workthread::loggingdetail::SanitizeForLog(message);
}

inline void LauncherUpdateCoordinator::SetLogLevel(int level) {
	m_logLevel.store(
		workthread::loggingdetail::ClampLogLevel(level),
		std::memory_order_relaxed);
}

inline int LauncherUpdateCoordinator::GetLogLevel() const {
	return workthread::loggingdetail::ClampLogLevel(
		m_logLevel.load(std::memory_order_relaxed));
}

inline void LauncherUpdateCoordinator::LogUpdateError(
	const char* code,
	const char* source,
	const std::string& reason,
	const std::string& details,
	DWORD systemError,
	int httpStatus,
	int httpError) const {
	if (static_cast<int>(UpdateLogLevel::Error) < GetLogLevel()) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_logMutex);
	const std::filesystem::path logPath = GetUpdateLogFilePath();
	std::ofstream stream(logPath, std::ios::binary | std::ios::app);
	if (!stream.is_open()) {
		return;
	}

	stream << "[" << workthread::loggingdetail::BuildLocalTimestamp() << "] "
		<< "level=ERROR "
		<< "code=" << (code ? code : "UF-UNKNOWN") << " "
		<< "source=\"" << workthread::loggingdetail::SanitizeForLog(source ? source : "unknown") << "\" "
		<< "reason=\"" << workthread::loggingdetail::SanitizeForLog(reason) << "\"";

	if (!details.empty()) {
		stream << " details=\"" << workthread::loggingdetail::SanitizeForLog(details) << "\"";
	}
	if (systemError != 0) {
		stream << " win32_code=" << systemError;
		const std::string systemMessage = FormatSystemError(systemError);
		if (!systemMessage.empty()) {
			stream << " win32_message=\"" << systemMessage << "\"";
		}
	}
	if (httpStatus != 0) {
		stream << " http_status=" << httpStatus;
	}
	if (httpError != 0) {
		stream << " http_error=" << httpError;
	}
	stream << std::endl;
}

inline void LauncherUpdateCoordinator::LogUpdateInfo(
	const char* code,
	const char* source,
	const std::string& message,
	const std::string& details) const {
	if (static_cast<int>(UpdateLogLevel::Info) < GetLogLevel()) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_logMutex);
	const std::filesystem::path logPath = GetUpdateLogFilePath();
	std::ofstream stream(logPath, std::ios::binary | std::ios::app);
	if (!stream.is_open()) {
		return;
	}

	stream << "[" << workthread::loggingdetail::BuildLocalTimestamp() << "] "
		<< "level=INFO "
		<< "code=" << (code ? code : "UF-UNKNOWN") << " "
		<< "source=\"" << workthread::loggingdetail::SanitizeForLog(source ? source : "unknown") << "\" "
		<< "message=\"" << workthread::loggingdetail::SanitizeForLog(message) << "\"";
	if (!details.empty()) {
		stream << " details=\"" << workthread::loggingdetail::SanitizeForLog(details) << "\"";
	}
	stream << std::endl;
}

template <typename... Args>
inline void LauncherUpdateCoordinator::LogUpdateInfoFmt(
	const char* code,
	const char* source,
	std::string_view messageFmt,
	Args&&... args) const {
	LogUpdateInfo(
		code,
		source,
		workthread::loggingdetail::FormatToString(messageFmt, std::forward<Args>(args)...));
}

inline void LauncherUpdateCoordinator::LogUpdateWarn(
	const char* code,
	const char* source,
	const std::string& message,
	const std::string& details) const {
	if (static_cast<int>(UpdateLogLevel::Warn) < GetLogLevel()) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_logMutex);
	const std::filesystem::path logPath = GetUpdateLogFilePath();
	std::ofstream stream(logPath, std::ios::binary | std::ios::app);
	if (!stream.is_open()) {
		return;
	}

	stream << "[" << workthread::loggingdetail::BuildLocalTimestamp() << "] "
		<< "level=WARN "
		<< "code=" << (code ? code : "UF-UNKNOWN") << " "
		<< "source=\"" << workthread::loggingdetail::SanitizeForLog(source ? source : "unknown") << "\" "
		<< "message=\"" << workthread::loggingdetail::SanitizeForLog(message) << "\"";
	if (!details.empty()) {
		stream << " details=\"" << workthread::loggingdetail::SanitizeForLog(details) << "\"";
	}
	stream << std::endl;
}

template <typename... Args>
inline void LauncherUpdateCoordinator::LogUpdateWarnFmt(
	const char* code,
	const char* source,
	std::string_view messageFmt,
	Args&&... args) const {
	LogUpdateWarn(
		code,
		source,
		workthread::loggingdetail::FormatToString(messageFmt, std::forward<Args>(args)...));
}

inline void LauncherUpdateCoordinator::LogUpdateDebug(
	const char* code,
	const char* source,
	const std::string& message,
	const std::string& details) const {
	if (static_cast<int>(UpdateLogLevel::Debug) < GetLogLevel()) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_logMutex);
	const std::filesystem::path logPath = GetUpdateLogFilePath();
	std::ofstream stream(logPath, std::ios::binary | std::ios::app);
	if (!stream.is_open()) {
		return;
	}

	stream << "[" << workthread::loggingdetail::BuildLocalTimestamp() << "] "
		<< "level=DEBUG "
		<< "code=" << (code ? code : "UF-UNKNOWN") << " "
		<< "source=\"" << workthread::loggingdetail::SanitizeForLog(source ? source : "unknown") << "\" "
		<< "message=\"" << workthread::loggingdetail::SanitizeForLog(message) << "\"";
	if (!details.empty()) {
		stream << " details=\"" << workthread::loggingdetail::SanitizeForLog(details) << "\"";
	}
	stream << std::endl;
}

template <typename... Args>
inline void LauncherUpdateCoordinator::LogUpdateDebugFmt(
	const char* code,
	const char* source,
	std::string_view messageFmt,
	Args&&... args) const {
	LogUpdateDebug(
		code,
		source,
		workthread::loggingdetail::FormatToString(messageFmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void LauncherUpdateCoordinator::LogUpdateErrorFmt(
	const char* code,
	const char* source,
	std::string_view reasonFmt,
	Args&&... args) const {
	LogUpdateError(
		code,
		source,
		workthread::loggingdetail::FormatToString(reasonFmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void LauncherUpdateCoordinator::LogUpdateErrorDetailsFmt(
	const char* code,
	const char* source,
	const std::string& reason,
	std::string_view detailsFmt,
	Args&&... args) const {
	LogUpdateError(
		code,
		source,
		reason,
		workthread::loggingdetail::FormatToString(detailsFmt, std::forward<Args>(args)...));
}

namespace workthread::localization {

struct StatusExactMap {
	const wchar_t* english;
	const wchar_t* chinese;
};

struct StatusPrefixMap {
	const wchar_t* englishPrefix;
	const wchar_t* chinesePrefix;
};

inline std::wstring TranslateLauncherStatus(const std::wstring& status) {
	if (status.empty()) {
		return status;
	}

	static const StatusExactMap kExactMap[] = {
		{ L"Initializing launcher...", L"\u542f\u52a8\u5668\u521d\u59cb\u5316\u4e2d..." },
		{ L"Web service stopped, restarting...", L"\u672c\u5730\u670d\u52a1\u5df2\u505c\u6b62\uff0c\u6b63\u5728\u91cd\u542f..." },
		{ L"Launching game client...", L"\u6b63\u5728\u542f\u52a8\u6e38\u620f\u5ba2\u6237\u7aef..." },
		{ L"Failed: game launch process creation.", L"\u5931\u8d25\uff1a\u521b\u5efa\u6e38\u620f\u8fdb\u7a0b\u5931\u8d25\u3002" },
		{ L"Game client launched.", L"\u6e38\u620f\u5ba2\u6237\u7aef\u5df2\u542f\u52a8\u3002" },
		{ L"Warning: game exited shortly after launch.", L"\u8b66\u544a\uff1a\u6e38\u620f\u542f\u52a8\u540e\u5f88\u5feb\u9000\u51fa\u3002" },
		{ L"Stopping launcher services...", L"\u6b63\u5728\u505c\u6b62\u542f\u52a8\u5668\u670d\u52a1..." },
		{ L"Fetching bootstrap configuration...", L"\u6b63\u5728\u83b7\u53d6\u542f\u52a8\u914d\u7f6e..." },
		{ L"Failed: bootstrap configuration request.", L"\u5931\u8d25\uff1a\u542f\u52a8\u914d\u7f6e\u8bf7\u6c42\u5931\u8d25\u3002" },
		{ L"Failed: invalid bootstrap configuration.", L"\u5931\u8d25\uff1a\u542f\u52a8\u914d\u7f6e\u65e0\u6548\u3002" },
		{ L"Failed: malformed bootstrap content.", L"\u5931\u8d25\uff1a\u542f\u52a8\u914d\u7f6e\u683c\u5f0f\u9519\u8bef\u3002" },
		{ L"Failed: missing manifest URL in bootstrap.", L"\u5931\u8d25\uff1a\u542f\u52a8\u914d\u7f6e\u7f3a\u5c11 manifest URL\u3002" },
		{ L"Failed: invalid manifest URL.", L"\u5931\u8d25\uff1amanifest URL \u65e0\u6548\u3002" },
		{ L"Failed: invalid update root URL.", L"\u5931\u8d25\uff1a\u66f4\u65b0\u6839 URL \u65e0\u6548\u3002" },
		{ L"Failed: unresolved relative update URL.", L"\u5931\u8d25\uff1a\u65e0\u6cd5\u89e3\u6790\u76f8\u5bf9\u66f4\u65b0 URL\u3002" },
		{ L"Failed: cannot resolve download host.", L"\u5931\u8d25\uff1a\u65e0\u6cd5\u89e3\u6790\u4e0b\u8f7d\u4e3b\u673a\u3002" },
		{ L"Failed: missing base package URL.", L"\u5931\u8d25\uff1a\u7f3a\u5c11\u57fa\u7840\u5305 URL\u3002" },
		{ L"Bootstrap configuration ready.", L"\u542f\u52a8\u914d\u7f6e\u5df2\u5c31\u7eea\u3002" },
		{ L"Refreshing remote manifest...", L"\u6b63\u5728\u5237\u65b0\u8fdc\u7a0b manifest..." },
		{ L"Failed: remote manifest request.", L"\u5931\u8d25\uff1a\u8fdc\u7a0b manifest \u8bf7\u6c42\u5931\u8d25\u3002" },
		{ L"Failed: manifest content is empty.", L"\u5931\u8d25\uff1amanifest \u5185\u5bb9\u4e3a\u7a7a\u3002" },
		{ L"Failed: manifest format parse.", L"\u5931\u8d25\uff1amanifest \u683c\u5f0f\u89e3\u6790\u5931\u8d25\u3002" },
		{ L"Remote manifest refreshed.", L"\u8fdc\u7a0b manifest \u5df2\u5237\u65b0\u3002" },
		{ L"Checking runtime files...", L"\u6b63\u5728\u68c0\u67e5\u8fd0\u884c\u65f6\u6587\u4ef6..." },
		{ L"Runtime updates complete.", L"\u8fd0\u884c\u65f6\u66f4\u65b0\u5b8c\u6210\u3002" },
		{ L"Initializing update environment...", L"\u66f4\u65b0\u73af\u5883\u521d\u59cb\u5316\u4e2d..." },
		{ L"Failed: initialize update environment.", L"\u5931\u8d25\uff1a\u66f4\u65b0\u73af\u5883\u521d\u59cb\u5316\u5931\u8d25\u3002" },
		{ L"Checking base package...", L"\u6b63\u5728\u68c0\u67e5\u57fa\u7840\u5305..." },
		{ L"Failed: base package is unavailable.", L"\u5931\u8d25\uff1a\u57fa\u7840\u5305\u4e0d\u53ef\u7528\u3002" },
		{ L"Loading local version state...", L"\u6b63\u5728\u52a0\u8f7d\u672c\u5730\u7248\u672c\u72b6\u6001..." },
		{ L"Failed: remote manifest refresh.", L"\u5931\u8d25\uff1a\u8fdc\u7a0b manifest \u5237\u65b0\u5931\u8d25\u3002" },
		{ L"Applying runtime updates...", L"\u6b63\u5728\u5e94\u7528\u8fd0\u884c\u65f6\u66f4\u65b0..." },
		{ L"Failed: runtime update download.", L"\u5931\u8d25\uff1a\u8fd0\u884c\u65f6\u66f4\u65b0\u4e0b\u8f7d\u5931\u8d25\u3002" },
		{ L"Failed: game client launch.", L"\u5931\u8d25\uff1a\u542f\u52a8\u6e38\u620f\u5ba2\u6237\u7aef\u5931\u8d25\u3002" },
		{ L"Game is running.", L"\u6e38\u620f\u6b63\u5728\u8fd0\u884c\u3002" },
		{ L"P2P file verification failed, retrying HTTP...", L"P2P \u6587\u4ef6\u6821\u9a8c\u5931\u8d25\uff0c\u6b63\u5728\u56de\u9000 HTTP..." },
		{ L"Failed: HTTP client unavailable.", L"\u5931\u8d25\uff1aHTTP \u5ba2\u6237\u7aef\u4e0d\u53ef\u7528\u3002" },
		{ L"Downloaded file incomplete, retrying...", L"\u4e0b\u8f7d\u6587\u4ef6\u4e0d\u5b8c\u6574\uff0c\u6b63\u5728\u91cd\u8bd5..." },
		{ L"Processing queued async file update...", L"\u6b63\u5728\u5904\u7406\u961f\u5217\u4e2d\u7684\u5f02\u6b65\u6587\u4ef6\u66f4\u65b0..." },
		{ L"Received file download request from client...", L"\u5df2\u6536\u5230\u5ba2\u6237\u7aef\u6587\u4ef6\u4e0b\u8f7d\u8bf7\u6c42..." },
		{ L"Failed: requested client file not found.", L"\u5931\u8d25\uff1a\u672a\u627e\u5230\u5ba2\u6237\u7aef\u8bf7\u6c42\u7684\u6587\u4ef6\u3002" },
		{ L"Queued async file update completed.", L"\u961f\u5217\u5f02\u6b65\u6587\u4ef6\u66f4\u65b0\u5b8c\u6210\u3002" },
		{ L"Client file request completed.", L"\u5ba2\u6237\u7aef\u6587\u4ef6\u8bf7\u6c42\u5b8c\u6210\u3002" },
		{ L"Failed: queued async file update.", L"\u5931\u8d25\uff1a\u961f\u5217\u5f02\u6b65\u6587\u4ef6\u66f4\u65b0\u5931\u8d25\u3002" },
		{ L"Failed: client file request download.", L"\u5931\u8d25\uff1a\u5ba2\u6237\u7aef\u6587\u4ef6\u8bf7\u6c42\u4e0b\u8f7d\u5931\u8d25\u3002" },
		{ L"RunClient requested: checking updates...", L"RunClient \u8bf7\u6c42\uff1a\u6b63\u5728\u68c0\u67e5\u66f4\u65b0..." },
		{ L"Failed: RunClient manifest refresh.", L"\u5931\u8d25\uff1aRunClient \u5237\u65b0 manifest \u5931\u8d25\u3002" },
		{ L"Failed: RunClient update download.", L"\u5931\u8d25\uff1aRunClient \u66f4\u65b0\u4e0b\u8f7d\u5931\u8d25\u3002" },
		{ L"RunClient: launching game...", L"RunClient\uff1a\u6b63\u5728\u542f\u52a8\u6e38\u620f..." },
		{ L"Failed: RunClient launch.", L"\u5931\u8d25\uff1aRunClient \u542f\u52a8\u5931\u8d25\u3002" },
		{ L"RunClient launch succeeded.", L"RunClient \u542f\u52a8\u6210\u529f\u3002" },
		{ L"Retrying local HTTP service...", L"\u6b63\u5728\u91cd\u8bd5\u672c\u5730 HTTP \u670d\u52a1..." },
		{ L"Restarting local HTTP service...", L"\u6b63\u5728\u91cd\u542f\u672c\u5730 HTTP \u670d\u52a1..." },
	};

	for (const auto& item : kExactMap) {
		if (status == item.english) {
			return item.chinese;
		}
	}

	static const StatusPrefixMap kPrefixMap[] = {
		{ L"HTTP single-stream download: ", L"HTTP \u5355\u7ebf\u7a0b\u4e0b\u8f7d\uff1a" },
		{ L"HTTP chunked download: ", L"HTTP \u5206\u5757\u4e0b\u8f7d\uff1a" },
		{ L"Chunked download failed, retrying single-stream: ", L"\u5206\u5757\u4e0b\u8f7d\u5931\u8d25\uff0c\u6b63\u5728\u56de\u9000\u5355\u7ebf\u7a0b\uff1a" },
		{ L"Chunked download complete: ", L"\u5206\u5757\u4e0b\u8f7d\u5b8c\u6210\uff1a" },
		{ L"Preparing update: ", L"\u6b63\u5728\u51c6\u5907\u66f4\u65b0\uff1a" },
		{ L"Up to date: ", L"\u5df2\u662f\u6700\u65b0\uff1a" },
		{ L"Downloading update: ", L"\u6b63\u5728\u4e0b\u8f7d\u66f4\u65b0\uff1a" },
		{ L"Downloading: ", L"\u6b63\u5728\u4e0b\u8f7d\uff1a" },
		{ L"Downloaded via P2P: ", L"\u5df2\u901a\u8fc7 P2P \u4e0b\u8f7d\uff1a" },
		{ L"Downloaded: ", L"\u5df2\u4e0b\u8f7d\uff1a" },
		{ L"Failed: HTTP download ", L"\u5931\u8d25\uff1aHTTP \u4e0b\u8f7d " },
		{ L"Warning: ", L"\u8b66\u544a\uff1a" },
		{ L"Failed: ", L"\u5931\u8d25\uff1a" },
	};

	for (const auto& item : kPrefixMap) {
		const size_t prefixLength = std::char_traits<wchar_t>::length(item.englishPrefix);
		if (status.size() >= prefixLength &&
			status.compare(0, prefixLength, item.englishPrefix) == 0) {
			return std::wstring(item.chinesePrefix) + status.substr(prefixLength);
		}
	}

	return status;
}

} // namespace workthread::localization

inline void LauncherUpdateCoordinator::SetLauncherStatus(const std::wstring& status) {
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_launcherStatus = workthread::localization::TranslateLauncherStatus(status);
}

inline std::wstring LauncherUpdateCoordinator::GetLauncherStatus() const {
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return m_launcherStatus;
}
