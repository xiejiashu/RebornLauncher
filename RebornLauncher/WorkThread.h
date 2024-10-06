#include "VersionConfig.h"
#include <map>
#include <vector>
#include <mutex>
#include <queue>
#include <memory>

namespace httplib
{
	class Client;
}

// 数据块结构，用于传递解压数据到工作线程
struct DataBlock {
	std::string filePath;
	long long fileOffset;  // 文件在归档中的位置
	struct archive_entry* entry;  // 归档文件信息
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
	// 解密
	std::string DecryptUrl(const std::string& ciphertext);
	// 解密 Version.dat文件
	std::string DecryptVersionDat(const std::string& ciphertext);
	// 下载RunTime文件
	void DownloadRunTimeFile();

	// 获取总下载
	int GetTotalDownload() const;
	// 获取当前下载
	int GetCurrentDownload() const;
	// 当前文件
	std::wstring GetCurrentDownloadFile();
	// 设置
	void SetCurrentDownloadFile(const std::wstring& strFile);

	int GetCurrentDownloadSize() const;
	int GetCurrentDownloadProgress() const;
	
	// 下载基础包
	void DownloadBasePackage();

	void Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	void ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(const std::string& url, const std::string& file_path);

	// 把数据写入映射内存
	void WriteDataToMapping();

	// 网络请求线程
	void WebServiceThread();

	void Stop();

	// 获取URL地址
	bool GetDownloadUrl();

	// 获取远程版本文件信息
	bool GetRemoteVersionFile();
private:

	BOOL m_bRun{ TRUE };

	// 进程名
	LPCTSTR m_szProcessName = TEXT("MapleReborn.exe");

	// 当前版本号
	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	// 必下列表
	std::vector<std::string> m_vecRunTimeList;

	// 服务器HOST
	std::string m_strUrl;
	std::string m_strPage;

	// 总共需要下载的文件数量
	int m_nTotalDownload{ 0 };
	// 当前下载的文件数量
	int m_nCurrentDownload{ 0 };
	// 当前下载的文件名
	std::wstring m_strCurrentDownload;
	// 当前下载的文件大小
	int m_nCurrentDownloadSize{ 0 };
	// 当前下载的文件进度
	int m_nCurrentDownloadProgress{ 0 };

	// 目标进程
	HANDLE m_hGameProcess[2]; // 双开

	// 
	DWORD m_dwGameProcessId[2];

	std::vector<HANDLE> m_hFileMappings;

	bool m_bUpdateSelf{ false };

	// 本进程模块全路径名
	std::wstring m_strModulePath;
	// 本进程文件名
	std::wstring m_strModuleName;
	// 本进程目录
	std::wstring m_strModuleDir;

	// stl库的锁 tmux
	std::mutex m_mutex;
	// 解压锁
	std::mutex m_mutexUnzip;

	std::queue<DataBlock> dataQueue;
	std::mutex queueMutex;
	std::mutex archiveMutex;
	std::condition_variable dataCondition;

	// 本地版本文件的MD5
	std::string m_strLocalVersionMD5;
	// 当前目录
	std::string m_strCurrentDir;

	httplib::Client* m_client{ nullptr };

	HWND m_hMainWnd{ nullptr };
};