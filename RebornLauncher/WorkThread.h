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

// 鏁版嵁鍧楃粨鏋勶紝鐢ㄤ簬浼犻€掕В鍘嬫暟鎹埌宸ヤ綔绾跨▼
struct DataBlock {
	std::string filePath;
	long long fileOffset;  // 鏂囦欢鍦ㄥ綊妗ｄ腑鐨勪綅缃?
	struct archive_entry* entry;  // 褰掓。鏂囦欢淇℃伅
};

class WorkThread
{
public:
	WorkThread(HWND hWnd, const std::wstring& strModulePath,const std::wstring& strModuleName,const std::wstring& strModuleDir);
	~WorkThread();
public:
	static DWORD WINAPI ThreadProc(LPVOID lpParameter);
public:
	DWORD Run();
	void HandleError(const char* msg);
	// 瑙ｅ瘑
	std::string DecryptUrl(const std::string& ciphertext);
	// 瑙ｅ瘑 Version.dat鏂囦欢
	std::string DecryptVersionDat(const std::string& ciphertext);
	// 涓嬭浇RunTime鏂囦欢
	void DownloadRunTimeFile();

	// 鑾峰彇鎬讳笅杞?
	int GetTotalDownload() const;
	// 鑾峰彇褰撳墠涓嬭浇
	int GetCurrentDownload() const;
	// 褰撳墠鏂囦欢
	std::wstring GetCurrentDownloadFile();
	// 璁剧疆
	void SetCurrentDownloadFile(const std::wstring& strFile);

	int GetCurrentDownloadSize() const;
	int GetCurrentDownloadProgress() const;
	
	// 涓嬭浇鍩虹鍖?
	void DownloadBasePackage();

	void Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	void ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(const std::string& url, const std::string& file_path);

	// 鎶婃暟鎹啓鍏ユ槧灏勫唴瀛?	void WriteDataToMapping();

	// 缃戠粶璇锋眰绾跨▼
	void WebServiceThread();

	void Stop();

	// 鑾峰彇URL鍦板潃
	bool GetDownloadUrl();

	// 鑾峰彇杩滅▼鐗堟湰鏂囦欢淇℃伅
	bool GetRemoteVersionFile();

	// 鏇存柊 P2P 璁剧疆锛堢嚎绋嬪畨鍏級
	void UpdateP2PSettings(const P2PSettings& settings);
	P2PSettings GetP2PSettings() const;
private:

	BOOL m_bRun{ TRUE };

	// 杩涚▼鍚?
	LPCTSTR m_szProcessName = TEXT("MapleReborn.exe");

	// 褰撳墠鐗堟湰鍙?
	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	// 蹇呬笅鍒楄〃
	std::vector<std::string> m_vecRunTimeList;

	// 鏈嶅姟鍣℉OST
	std::string m_strUrl;
	std::string m_strPage;

	// 鎬诲叡闇€瑕佷笅杞界殑鏂囦欢鏁伴噺
	int m_nTotalDownload{ 0 };
	// 褰撳墠涓嬭浇鐨勬枃浠舵暟閲?
	int m_nCurrentDownload{ 0 };
	// 褰撳墠涓嬭浇鐨勬枃浠跺悕
	std::wstring m_strCurrentDownload;
	// 褰撳墠涓嬭浇鐨勬枃浠跺ぇ灏?
	int m_nCurrentDownloadSize{ 0 };
	// 褰撳墠涓嬭浇鐨勬枃浠惰繘搴?
	int m_nCurrentDownloadProgress{ 0 };

	// 鐩爣杩涚▼
	HANDLE m_hGameProcess[2]; // 鍙屽紑

	// 
	DWORD m_dwGameProcessId[2];

	std::vector<HANDLE> m_hFileMappings;

	bool m_bUpdateSelf{ false };

	// 鏈繘绋嬫ā鍧楀叏璺緞鍚?
	std::wstring m_strModulePath;
	// 鏈繘绋嬫枃浠跺悕
	std::wstring m_strModuleName;
	// 鏈繘绋嬬洰褰?
	std::wstring m_strModuleDir;

	// stl搴撶殑閿?tmux
	std::mutex m_mutex;
	// 瑙ｅ帇閿?
	std::mutex m_mutexUnzip;

	std::queue<DataBlock> dataQueue;
	std::mutex queueMutex;
	std::mutex archiveMutex;
	std::condition_variable dataCondition;

	// 鏈湴鐗堟湰鏂囦欢鐨凪D5
	std::string m_strLocalVersionMD5;
	// 褰撳墠鐩綍
	std::string m_strCurrentDir;

	httplib::Client* m_client{ nullptr };

	HWND m_hMainWnd{ nullptr };

	std::unique_ptr<P2PClient> m_p2pClient;
	P2PSettings m_p2pSettings;
	mutable std::mutex m_p2pMutex;
};
