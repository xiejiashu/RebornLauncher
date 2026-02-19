#include "VersionConfig.h"
#include "P2PClient.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace httplib
{
	class Client;
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

struct RuntimeState {
	BOOL run{ TRUE };
	HANDLE thread{ nullptr };
	std::vector<std::shared_ptr<tagGameInfo>> gameInfos;
	mutable std::mutex gameInfosMutex;
	std::vector<HANDLE> fileMappings;
	HANDLE mappingVersion{ nullptr };
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
	class WorkThreadRunCoordinator;
}
namespace workthread::runtimeupdate {
	class WorkThreadRuntimeUpdater;
}
namespace workthread::versionload {
	class WorkThreadLocalVersionLoader;
}

class WorkThread
{
public:
	WorkThread(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings = {});
	~WorkThread();
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

	void Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	void ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(const std::string& url, const std::string& file_path, DWORD ownerProcessId = 0);
	bool DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath);
	bool DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount);

	// Publish downloaded file hashes into shared memory mappings for the game clients.
	void WriteDataToMapping();

	void WriteVersionToMapping(std::string& m_strRemoteVersionJson);

	void WebServiceThread();

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
private:
	bool InitializeDownloadEnvironment();
	bool EnsureBasePackageReady();
	void LoadLocalVersionState();
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
	void CleanupExitedGameInfos();
	bool HasRunningGameProcess();
	void TerminateAllGameProcesses();
	std::filesystem::path GetUpdateLogFilePath() const;
	std::string FormatSystemError(DWORD errorCode) const;
	friend class workthread::runflow::WorkThreadRunCoordinator;
	friend class workthread::runtimeupdate::WorkThreadRuntimeUpdater;
	friend class workthread::versionload::WorkThreadLocalVersionLoader;

	LPCTSTR m_szProcessName = TEXT("MapleFireReborn.exe");

	RuntimeState m_runtimeState;
	VersionState m_versionState;

	NetworkState m_networkState;
	DownloadProgressState m_downloadState;

	SelfUpdateState m_selfUpdateState;
	std::wstring m_launcherStatus{ L"Initializing launcher..." };
	mutable std::mutex m_statusMutex;
	mutable std::mutex m_logMutex;
	std::mutex m_launchFlowMutex;
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

} // namespace workthread::loggingdetail

inline std::filesystem::path WorkThread::GetUpdateLogFilePath() const {
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

inline std::string WorkThread::FormatSystemError(DWORD errorCode) const {
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

inline void WorkThread::LogUpdateError(
	const char* code,
	const char* source,
	const std::string& reason,
	const std::string& details,
	DWORD systemError,
	int httpStatus,
	int httpError) const {
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

inline void WorkThread::SetLauncherStatus(const std::wstring& status) {
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_launcherStatus = status;
}

inline std::wstring WorkThread::GetLauncherStatus() const {
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return m_launcherStatus;
}
