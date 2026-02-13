#include "VersionConfig.h"
#include "P2PClient.h"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
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

	int GetTotalDownload() const;
	int GetCurrentDownload() const;
	std::wstring GetCurrentDownloadFile();
	void SetCurrentDownloadFile(const std::wstring& strFile);

	int GetCurrentDownloadSize() const;
	int GetCurrentDownloadProgress() const;
	std::vector<tagGameInfo> GetGameInfosSnapshot() const;

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
	friend class workthread::runflow::WorkThreadRunCoordinator;
	friend class workthread::runtimeupdate::WorkThreadRuntimeUpdater;
	friend class workthread::versionload::WorkThreadLocalVersionLoader;

	LPCTSTR m_szProcessName = TEXT("MapleFireReborn.exe");

	RuntimeState m_runtimeState;
	VersionState m_versionState;

	NetworkState m_networkState;
	DownloadProgressState m_downloadState;

	SelfUpdateState m_selfUpdateState;
};
