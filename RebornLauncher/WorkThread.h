#include "VersionConfig.h"
#include <map>
#include <vector>
class WorkThread
{
public:
	WorkThread();
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
	void DownloadRunTimeFile(const std::string& strHost,const short wPort);

	// 获取总下载
	int GetTotalDownload() const;
	// 获取当前下载
	int GetCurrentDownload() const;
	// 把数据写入映射内存
	void WriteDataToMapping();
private:
	// 当前版本号
	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	// 必下列表
	std::vector<std::string> m_vecRunTimeList;

	// 服务器HOST
	std::string m_strHost;
	// 服务器端口
	short m_wPort{ 0 };

	// 总共需要下载的文件数量
	int m_nTotalDownload{ 0 };
	// 当前下载的文件数量
	int m_nCurrentDownload{ 0 };

	// 目标进程
	HANDLE m_hGameProcess{ nullptr };

	std::vector<HANDLE> m_hFileMappings;
};