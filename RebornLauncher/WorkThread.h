#include "VersionConfig.h"
#include <map>
#include <vector>
#include <mutex>
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
	// ����
	std::string DecryptUrl(const std::string& ciphertext);
	// ���� Version.dat�ļ�
	std::string DecryptVersionDat(const std::string& ciphertext);
	// ����RunTime�ļ�
	void DownloadRunTimeFile(const std::string& strHost,const short wPort);

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
	
	// ������д��ӳ���ڴ�
	void WriteDataToMapping();
private:
	// ��ǰ�汾��
	int64_t m_qwVersion{ 0 };
	std::map<std::string, VersionConfig> m_mapFiles;
	HANDLE m_hThread{ nullptr };
	// �����б�
	std::vector<std::string> m_vecRunTimeList;

	// ������HOST
	std::string m_strHost;
	// �������˿�
	short m_wPort{ 0 };

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
	HANDLE m_hGameProcess{ nullptr };

	std::vector<HANDLE> m_hFileMappings;

	// stl����� tmux
	std::mutex m_mutex;
};