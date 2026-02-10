#include "VersionConfig.h"
#include "P2PClient.h"
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
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
	
	bool DownloadBasePackage();

	void Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	void ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(const std::string& url, const std::string& file_path);
	bool DownloadFileFromAbsoluteUrl(const std::string& absoluteUrl, const std::string& filePath);
	bool DownloadFileChunkedWithResume(const std::string& absoluteUrl, const std::string& filePath, size_t threadCount);

	// Publish downloaded file hashes into shared memory mappings for the game clients.
	void WriteDataToMapping();

	void WebServiceThread();

	void Stop();

	bool FetchBootstrapConfig();

	bool RefreshRemoteVersionManifest();

	void UpdateP2PSettings(const P2PSettings& settings);
	P2PSettings GetP2PSettings() const;
private:

	BOOL m_bRun{ TRUE };

	LPCTSTR m_szProcessName = TEXT("MapleFireReborn.exe");

	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	std::vector<std::string> m_vecRunTimeList;

	std::string m_strUrl;
	std::string m_strPage;
	std::string m_strVersionManifestPath;
	std::vector<std::string> m_basePackageUrls;
	std::string m_extractRootPrefix;

	int m_nTotalDownload{ 0 };
	int m_nCurrentDownload{ 0 };
	std::wstring m_strCurrentDownload;
	int m_nCurrentDownloadSize{ 0 };
	int m_nCurrentDownloadProgress{ 0 };

	HANDLE m_hGameProcess[2];

	// 
	DWORD m_dwGameProcessId[2];

	std::vector<HANDLE> m_hFileMappings;

	bool m_bUpdateSelf{ false };

	std::wstring m_strModulePath;
	std::wstring m_strModuleName;
	std::wstring m_strModuleDir;

	std::mutex m_mutex;
	std::mutex m_mutexUnzip;

	std::queue<DataBlock> dataQueue;
	std::mutex queueMutex;
	std::mutex archiveMutex;
	std::condition_variable dataCondition;

	std::string m_strLocalVersionMD5;
	std::string m_strCurrentDir;

	httplib::Client* m_client{ nullptr };

	HWND m_hMainWnd{ nullptr };

	std::unique_ptr<P2PClient> m_p2pClient;
	P2PSettings m_p2pSettings;
	mutable std::mutex m_p2pMutex;
};
