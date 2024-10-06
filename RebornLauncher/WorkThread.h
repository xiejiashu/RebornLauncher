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

// ���ݿ�ṹ�����ڴ��ݽ�ѹ���ݵ������߳�
struct DataBlock {
	std::string filePath;
	long long fileOffset;  // �ļ��ڹ鵵�е�λ��
	struct archive_entry* entry;  // �鵵�ļ���Ϣ
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
	// ����
	std::string DecryptUrl(const std::string& ciphertext);
	// ���� Version.dat�ļ�
	std::string DecryptVersionDat(const std::string& ciphertext);
	// ����RunTime�ļ�
	void DownloadRunTimeFile();

	// ��ȡ������
	int GetTotalDownload() const;
	// ��ȡ��ǰ����
	int GetCurrentDownload() const;
	// ��ǰ�ļ�
	std::wstring GetCurrentDownloadFile();
	// ����
	void SetCurrentDownloadFile(const std::wstring& strFile);

	int GetCurrentDownloadSize() const;
	int GetCurrentDownloadProgress() const;
	
	// ���ػ�����
	void DownloadBasePackage();

	void Extract7z(const std::string& filename, const std::string& destPath);

	std::vector<DataBlock> ScanArchive(const std::string& archivePath);

	void ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files);

	bool DownloadWithResume(const std::string& url, const std::string& file_path);

	// ������д��ӳ���ڴ�
	void WriteDataToMapping();

	// ���������߳�
	void WebServiceThread();

	void Stop();

	// ��ȡURL��ַ
	bool GetDownloadUrl();

	// ��ȡԶ�̰汾�ļ���Ϣ
	bool GetRemoteVersionFile();
private:

	BOOL m_bRun{ TRUE };

	// ������
	LPCTSTR m_szProcessName = TEXT("MapleReborn.exe");

	// ��ǰ�汾��
	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	// �����б�
	std::vector<std::string> m_vecRunTimeList;

	// ������HOST
	std::string m_strUrl;
	std::string m_strPage;

	// �ܹ���Ҫ���ص��ļ�����
	int m_nTotalDownload{ 0 };
	// ��ǰ���ص��ļ�����
	int m_nCurrentDownload{ 0 };
	// ��ǰ���ص��ļ���
	std::wstring m_strCurrentDownload;
	// ��ǰ���ص��ļ���С
	int m_nCurrentDownloadSize{ 0 };
	// ��ǰ���ص��ļ�����
	int m_nCurrentDownloadProgress{ 0 };

	// Ŀ�����
	HANDLE m_hGameProcess[2]; // ˫��

	// 
	DWORD m_dwGameProcessId[2];

	std::vector<HANDLE> m_hFileMappings;

	bool m_bUpdateSelf{ false };

	// ������ģ��ȫ·����
	std::wstring m_strModulePath;
	// �������ļ���
	std::wstring m_strModuleName;
	// ������Ŀ¼
	std::wstring m_strModuleDir;

	// stl����� tmux
	std::mutex m_mutex;
	// ��ѹ��
	std::mutex m_mutexUnzip;

	std::queue<DataBlock> dataQueue;
	std::mutex queueMutex;
	std::mutex archiveMutex;
	std::condition_variable dataCondition;

	// ���ذ汾�ļ���MD5
	std::string m_strLocalVersionMD5;
	// ��ǰĿ¼
	std::string m_strCurrentDir;

	httplib::Client* m_client{ nullptr };

	HWND m_hMainWnd{ nullptr };
};