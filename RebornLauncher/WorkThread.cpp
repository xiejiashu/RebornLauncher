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

#pragma comment(lib, "advapi32.lib")
extern bool g_bRendering;
extern bool IsProcessRunning(DWORD dwProcessId);
extern float g_fProgressTotal;

std::string wstr2str(const std::wstring& wstr) {
	std::string result;
	//��ȡ��������С��������ռ䣬��������С�°��ֽڼ����  
	int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
	char* buffer = new char[len + 1];
	if (!buffer)
		return "";

	memset(buffer, 0, len + 1);
	//���ֽڱ���ת���ɶ��ֽڱ���  
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
	buffer[len] = '\0';
	//ɾ��������������ֵ  
	result.append(buffer);
	delete[] buffer;
	return result;
}

std::wstring str2wstr(const std::string& str, int strLen) {
	int hLen = strLen;
	if (strLen < 0)
		hLen = str.length();
	std::wstring result;
	//��ȡ��������С��������ռ䣬��������С���ַ�����  
	int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), hLen, NULL, 0);
	WCHAR* buffer = new WCHAR[len + 1];
	if (!buffer)
		return L"";
	//���ֽڱ���ת���ɿ��ֽڱ���  
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), hLen, buffer, len);
	buffer[len] = '\0';             //����ַ�����β  
	//ɾ��������������ֵ  
	result.append(buffer);
	delete[] buffer;
	return result;
}

void WriteToFileWithSharedAccess(const std::wstring& strLocalFile, const std::string& data) {
	// ���ļ����������д
	HANDLE hFile = CreateFile(
		strLocalFile.c_str(),                // �ļ���
		GENERIC_READ | GENERIC_WRITE,        // ��д����
		FILE_SHARE_READ | FILE_SHARE_WRITE,  // �����д
		NULL,                                // Ĭ�ϰ�ȫ����
		OPEN_ALWAYS,                         // ����ļ��������򴴽�
		FILE_ATTRIBUTE_NORMAL,               // ��ͨ�ļ�����
		NULL                                 // ��ʹ��ģ���ļ�
	);

	if (hFile == INVALID_HANDLE_VALUE) {
		std::cerr << "�޷����ļ����������: " << GetLastError() << std::endl;
		return;
	}

	// д������
	DWORD dwBytesWritten = 0;
	if (!WriteFile(hFile, data.c_str(), data.size(), &dwBytesWritten, NULL)) {
		std::cerr << "�޷�д���ļ����������: " << GetLastError() << std::endl;
	}

	// �ر��ļ����
	CloseHandle(hFile);
}


WorkThread::WorkThread(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir)
	: m_hMainWnd(hWnd), m_strModulePath(strModulePath), m_strModuleName(strModuleName), m_strModuleDir(strModuleDir)
	, m_bUpdateSelf(false), m_nTotalDownload(0), m_nCurrentDownload(0), m_nCurrentDownloadSize(0), m_nCurrentDownloadProgress(0)
	, m_hGameProcess{ nullptr }
	, m_dwGameProcessId()
	, m_qwVersion(0)
	, m_bRun(TRUE)
	, m_strCurrentDownload(L"")
	, m_hThread(nullptr)
{
	// �����߳�
	DWORD dwThreadId = 0;
	// �� m_hFileMappings �ͷŵ�
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

WorkThread::~WorkThread()
{
}

DWORD __stdcall WorkThread::ThreadProc(LPVOID lpParameter)
{
	// �����������ֽ� MapleReborn.exe �Ľ���
	// 1. ��ȡ���н���
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(PROCESSENTRY32);
		if (Process32First(hSnapshot, &pe)) {
			do {
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
				if (hProcess) {
					TCHAR processPath[MAX_PATH];
					if (GetModuleFileNameEx(hProcess, NULL, processPath, MAX_PATH)) {
						std::wstring strProcessPath = processPath;
						if (strProcessPath.find(L"MapleReborn.exe") != std::string::npos){
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

DWORD WorkThread::Run()
{
	m_strCurrentDir = std::filesystem::current_path().string();
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	if (!GetDownloadUrl())
	{
		Stop();
		MessageBox(m_hMainWnd, L"��ȡ���ص�ַʧ��", L"����", MB_OK);
		return 0;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	m_client = new httplib::Client(m_strUrl);

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// ���������DataĿ¼,��ô���ػ�����
	if (!std::filesystem::exists("./Data"))
	{
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		DownloadBasePackage();
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	{
		std::cout << "9999999999999999999" << std::endl;
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

		if (!strLocalVersionDatContent.empty())
		{
			// ���� Version.dat�ļ�
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			// ����һ�����Ͻṹ��JSON����
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strLocalVersionDat, root)) {
				// ��ȡ�汾��
				m_qwVersion = root["time"].asInt64();
				// ��ȡ�ļ���Ϣ
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					m_mapFiles[config.m_strPage] = config;
					std::filesystem::path filePath = config.m_strPage;
					std::string strPath = filePath.parent_path().string();

					// �ж�Ŀ¼�Ƿ���ڣ������ڴ���
					if (!strPath.empty() && !std::filesystem::exists(strPath))
					{
						std::filesystem::create_directories(m_strCurrentDir + "\\" + strPath);
					}

					// �ж��ļ��Ƿ���ڣ������ڴ���һ���յ�
					if (std::filesystem::exists(m_strCurrentDir + "\\" + config.m_strPage) == false)
					{
						std::ofstream ofs(m_strCurrentDir + "\\" + config.m_strPage, std::ios::binary);
						ofs.close();
					}
				}

				// ��ȡ runtime �ļ��б�
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}

	GetRemoteVersionFile();
	// ����RunTime�ļ�
	DownloadRunTimeFile();

	if (m_bUpdateSelf)
	{
		Stop();
		// �����¸��µ� UpdateTemp.exe ��open����
		ShellExecute(NULL, L"open", L"UpdateTemp.exe", m_strModulePath.c_str(), m_strModuleDir.c_str(), SW_SHOWNORMAL);
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
		ExitProcess(0);
		return 0;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	unsigned long long dwTick = GetTickCount64();
	// ��MD5д��MAP��
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping ����ʱ��:" << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// ������Ϸ
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
	g_bRendering = false;

	// Create���̻���ʱ��
	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess ����ʱ��:" << dwNewTick - dwTick << std::endl;

	m_hGameProcess[0] = pi.hProcess;
	m_dwGameProcessId[0] = pi.dwProcessId;

	// д��PID
	TCHAR szPID[32] = { 0 };
	_itow_s(pi.dwProcessId, szPID, 10);
	WriteProfileString(TEXT("MapleReborn"), TEXT("Client1PID"), szPID);

	if (pi.hThread)
	{
		CloseHandle(pi.hThread);
	}

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

		// �������̶����ˣ��ָ�����
		if (bHaveGameRun == false && g_bRendering == false)
		{
			PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
			break;
		}

		Sleep(1);
	} while (m_bRun);

	// ��β����
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

	CloseHandle(m_hThread);
	m_hThread = nullptr;

	return 0;
}

void WorkThread::HandleError(const char* msg) {
	std::cerr << msg << " Error: " << GetLastError() << std::endl;
	exit(1);
}

std::string WorkThread::DecryptUrl(const std::string& ciphertext)
{
	HCRYPTPROV hProv;
	HCRYPTKEY hKey;
	HCRYPTHASH hHash;

	// ��ȡ�����ṩ������
	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	// ������ϣ����
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	// ��ϣ��Կ
	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	// ���� AES ��Կ
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

std::string WorkThread::DecryptVersionDat(const std::string& ciphertext)
{
	// ��Ӧ���ϼ��ܹ��������� ��
	std::string strDict = "KRu998Am";
	ZSTD_DDict* ddict = ZSTD_createDDict(strDict.c_str(), strDict.length());
	std::string strCompress = ciphertext;
	std::string strJson;
	ZSTD_DCtx* dctx = ZSTD_createDCtx();
	size_t decompressBound = ZSTD_getFrameContentSize(strCompress.c_str(), strCompress.length());
	strJson.resize(decompressBound);
	size_t decompressSize = ZSTD_decompress_usingDDict(dctx, &strJson[0], decompressBound, strCompress.c_str(), strCompress.length(), ddict);
	strJson.resize(decompressSize);
	ZSTD_freeDDict(ddict);
	ZSTD_freeDCtx(dctx);

	return strJson;
}

void WorkThread::DownloadRunTimeFile()
{
	m_nTotalDownload = m_vecRunTimeList.size();
	m_nCurrentDownload = 0;
	for (auto& download : m_vecRunTimeList)
	{
		// �Ȳ鿴�����Ƿ��������ļ�
		std::string strLocalFile = download;
		std::string strPage = m_strPage + "Update/" + std::to_string(m_mapFiles[download].m_qwTime) + "/" + download;
		std::replace(strPage.begin(), strPage.end(), '\\', '/');

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;
		SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_nCurrentDownloadSize = m_mapFiles[download].m_qwSize;
		m_nCurrentDownloadProgress = 0;

		// �Ա�MD5
		auto it = m_mapFiles.find(strLocalFile);
		if (it != m_mapFiles.end())
		{
			bool Md5Same = false;
			if (GetFileAttributesA(strLocalFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
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
		
		std::cout << __FILE__ << ":" << __LINE__<< " �ļ�:" << strLocalFile << std::endl;

		// ����ļ���������
		if (strLocalFile.find("RebornLauncher.exe") != std::string::npos)
		{
#ifdef _DEBUG
			continue;
#else
			// �ĸ�����
			strLocalFile = "UpdateTemp.exe";
			m_bUpdateSelf = true;
#endif
		}

		// ɾ�������ļ�
		// std::filesystem::remove(strLocalFile);
		// ȡ���ļ�ֻ������
		SetFileAttributesA(strLocalFile.c_str(), FILE_ATTRIBUTE_NORMAL);
		DeleteFileA(strLocalFile.c_str());

		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		// �������ļ�

		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		httplib::Progress progress([this](uint64_t current, uint64_t total)->bool {
			m_nCurrentDownloadProgress = current;
			m_nCurrentDownloadSize = total;
			return true;
		});
		auto res = m_client->Get(strPage,progress);
		std::cout << __FILE__ << ":" << __LINE__ << strPage << std::endl;
		if (res && res->status == 200) {
			std::ofstream ofs(strLocalFile, std::ios::binary);
			ofs.write(res->body.c_str(), res->body.size());
			ofs.close();
		}
		m_nCurrentDownload += 1;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
}

int WorkThread::GetTotalDownload() const
{
	return m_nTotalDownload;
}

int WorkThread::GetCurrentDownload() const
{
	return m_nCurrentDownload;
}

std::wstring WorkThread::GetCurrentDownloadFile()
{
	// �Զ���
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_strCurrentDownload;
}

void WorkThread::SetCurrentDownloadFile(const std::wstring& strFile)
{
	// �Զ���
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strCurrentDownload = strFile;
}

int WorkThread::GetCurrentDownloadSize() const
{
	return m_nCurrentDownloadSize;
}

int WorkThread::GetCurrentDownloadProgress() const
{
	return m_nCurrentDownloadProgress;
}

void WorkThread::DownloadBasePackage()
{
	m_nTotalDownload = 1;
	m_nCurrentDownload = 0;
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	while (!DownloadWithResume(m_strPage + "MapleReborn.7z", "MapleReborn.7z"));
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// ��ѹ�ļ�
	Extract7z("MapleReborn.7z", "./");
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// ɾ��ѹ����
	std::filesystem::remove("MapleReborn.7z");
}

void WorkThread::Extract7z(const std::string& filename, const std::string& destPath)
{
	std::vector<DataBlock> allFiles = ScanArchive(filename);

	// Total file count
	m_nCurrentDownloadSize = allFiles.size();
	m_nCurrentDownloadProgress = 0;

	// Determine the number of threads
	int numThreads = std::min<int>(std::thread::hardware_concurrency(), allFiles.size());
	size_t filesPerThread = allFiles.size() / numThreads;
	// size_t remainingFiles = allFiles.size() % numThreads;

	std::vector<std::thread> threads;
	for (int i = 0; i < numThreads; ++i) {
		size_t start = i * filesPerThread;
		size_t end = (i == numThreads - 1) ? allFiles.size() : (i + 1) * filesPerThread;
		//if (i == numThreads - 1) {
		//	end += remainingFiles; // Add the remaining files to the last thread
		//}
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

// Scan archive file and get list of file info
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
		DataBlock fileInfo;
		fileInfo.filePath = archive_entry_pathname(entry);
		fileInfo.fileOffset = archive_read_header_position(a);
		files.push_back(fileInfo);
		archive_read_data_skip(a); // Skip file data, only read header info
	}

	archive_read_close(a);
	archive_read_free(a);
	return files;
}

// Thread function to extract a group of files
void WorkThread::ExtractFiles(const std::string& archivePath, const std::string& outPath, const std::vector<DataBlock>& files) {
	struct archive* a;
	struct archive_entry* entry;
	a = archive_read_new();
	archive_read_support_format_7zip(a);
	archive_read_support_filter_all(a);
	do {

		if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
			std::cerr << "Failed to reopen archive for file: " << std::endl;
			break;
		}

		for (const auto& fileInfo : files) {
			// �ƶ����ļ�λ��
			while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
				if (fileInfo.filePath == archive_entry_pathname(entry)) {
					std::string outputPath = outPath + "/" + fileInfo.filePath;
					// ���û��Ŀ¼�ȴ���
					std::filesystem::path filePath = outputPath;
					if (!std::filesystem::exists(filePath.parent_path()))
					{
						std::filesystem::create_directories(filePath.parent_path());
					}
					std::ofstream outputFile(outputPath, std::ios::binary);

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

// �ϵ����� 
bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path) {

	std::string strUrl = m_strPage + "MapleReborn.7z"; 
	std::cout << __FILE__ << ":" << __LINE__ << "url:" << m_strUrl <<" " << "page:" << strUrl << std::endl;
	SetCurrentDownloadFile(str2wstr(file_path, file_path.length()));
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// �����ļ��ܴ�С
	auto res = m_client->Head(strUrl.c_str());
	if (res.value().status == 200)
	{
		m_nCurrentDownloadSize = std::stoull(res.value().get_header_value("Content-Length"));
	}
	else {
		std::cout << "Failed to get file size with status code: " << res.value().status << std::endl;
		return false;

	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// ����ļ��Ƿ��Ѿ���������
	std::ifstream existing_file(file_path, std::ios::binary | std::ios::ate);
	size_t existing_file_size = 0;
	if (existing_file.is_open()) {
		existing_file_size = existing_file.tellg();
		existing_file.close();
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	if (existing_file_size == m_nCurrentDownloadSize)
		return true;
	else if (existing_file_size > m_nCurrentDownloadSize)
	{
		std::filesystem::remove(file_path);
		existing_file_size = 0;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	httplib::Headers headers;
	if (existing_file_size > 0) {
		// ʹ�� Range ����ͷʵ�ֶϵ�����
		m_nCurrentDownloadProgress = existing_file_size;
		headers.insert({ "Range", "bytes=" + std::to_string(existing_file_size) + "-" + std::to_string(m_nCurrentDownloadSize) });
		std::cout << "Resuming download from byte: " << existing_file_size << std::endl;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// ���ļ���׷��д��
	std::ofstream file(file_path, std::ios::binary | std::ios::app);
	// ������������
	res = m_client->Get(url.c_str(), headers,[&](const char* data, size_t data_length) {
		file.write(data, data_length);
		file.flush();
		m_nCurrentDownloadProgress += data_length;
		return true; // ���� true �Լ�������
	});
	file.close();
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	if (res && res->status == 200) {
		std::cout << "Download completed!" << std::endl;
		return true;
	}
	else if (res && res->status == 206) {  // 206 Partial Content ״̬���ʾ�ɹ�����
		std::cout << "Download resumed and completed!" << std::endl;
		return true;
	}
	else {
		std::cerr << "Download failed with status code: " << res->status << std::endl;
		return false;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
}


void WorkThread::WriteDataToMapping()
{
	// m_mapFiles
	for (auto& [strPage, config] : m_mapFiles)
	{
		std::string strMemoryName = strPage;
		// ɾ�� "\"
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, config.m_strMd5.length() + 1, strMemoryName.c_str());
		if (hFileMapping)
		{
			// ��MD5д��ȥ
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				// �����ֹ��
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}

void WorkThread::WebServiceThread()
{
	// ������������
	httplib::Server svr;
	svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
		g_bRendering = true;
		// g_fProgressTotal = 0;
		m_nTotalDownload = 1;
		m_nCurrentDownload = 0;
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		std::string strPage = req.get_param_value("page");
		std::cout << "���������ļ�:" << strPage << std::endl;
		auto it = m_mapFiles.find(strPage);
		SetCurrentDownloadFile(str2wstr(strPage,strPage.length()));
		if (it != m_mapFiles.end())
		{
			std::cout << "��ʼ��֤�ļ�MD5:" << strPage << std::endl;
			std::string strLocalFile = it->first;
			bool Md5Same = false;
			if (GetFileAttributesA(strLocalFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				Md5Same = it->second.m_strMd5 == strLocalFileMd5;
			}

			if (Md5Same)
			{
				std::cout << "�ļ��Ѵ�����MD5��ͬ" << std::endl;
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else
			{
				std::cout << "��ʼ�����ļ�:" << strLocalFile << std::endl;
				// ����
				strPage = m_strPage + "Update/" + std::to_string(it->second.m_qwTime) + "/" + strPage;
				std::replace(strPage.begin(), strPage.end(), '\\', '/');
				httplib::Progress progress([this](uint64_t current, uint64_t total)->bool {
					m_nCurrentDownloadProgress = current;
					m_nCurrentDownloadSize = total;
					return true;
				});
				auto ret = m_client->Get(strPage, progress);
				if (ret && ret->status == 200)
				{
					// std::filesystem::create_directories(strLocalFile);
					// ��ȡĿ¼
					// std::filesystem::path filePath = strLocalFile;
					// std::filesystem::create_directories(filePath.parent_path());
					std::cout << "������ɿ�ʼ����:" << strLocalFile << std::endl;
					// �����д��

					std::ofstream ofs(strLocalFile, std::ios::binary);
					// ���д��ʧ�ܴ�ӡһ��
					if (!ofs)
					{
						std::cout << "д���ļ�ʧ��:" << GetLastError() << std::endl;
					}
					ofs.write(ret->body.c_str(), ret->body.size());

					ofs.close();
					res.status = 200;
					res.set_content("OK", "text/plain");
					std::cout << strLocalFile  <<"д���ļ��ɹ�" << std::endl;
				}
				else
				{
					res.status = 404;
					res.set_content("Not Found", "text/plain");
					std::cout << "�����ļ�ʧ��,״̬��:"<< ret->status << std::endl;
				}
			}
		}
		else {
			std::cout << "����˲���������ļ�" << strPage << std::endl;
			res.status = 404;
			res.set_content("404", "text/palin");
		}
		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

	svr.Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {

		std::cout << "��¼������������Ϸ" << std::endl;

		GetRemoteVersionFile();
		// ����RunTime�ļ�
		DownloadRunTimeFile();

		bool bRun = false;
		for (int i = 0; i < sizeof(m_dwGameProcessId) / sizeof(m_dwGameProcessId[0]); ++i)
		{
			if (m_dwGameProcessId[i] != 0 && IsProcessRunning(m_dwGameProcessId[i]))
			{
				std::cout << "����:" << m_dwGameProcessId[i] << "����" << std::endl;
				continue;
			}
			else
			{
				// ������Ϸ
				STARTUPINFO si = { sizeof(si) };
				PROCESS_INFORMATION pi;
				if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
					HandleError("CreateProcess failed");
				}
				m_hGameProcess[i] = pi.hProcess;
				m_dwGameProcessId[i] = pi.dwProcessId;
				// д��PID
				TCHAR szPID[32] = { 0 };
				_itow_s(pi.dwProcessId, szPID, 10);

				if (i == 0)
				{
					WriteProfileString(TEXT("MapleReborn"), TEXT("Client1PID"), szPID);
				}
				else if (i == 2)
				{
					WriteProfileString(TEXT("MapleReborn"), TEXT("Client2PID"), szPID);
				}

				if (pi.hThread)
				{
					CloseHandle(pi.hThread);
				}

				std::cout << "���гɹ�" << std::endl;
				res.status = 200;
				res.set_content("OK", "text/plain");
				bRun = true;
				break;
			}
		}

		if (bRun == false)
		{
			res.status = 404;
			res.set_content("һ��ֻ�����������ͻ���,û��λ������", "text/plain");
		}

		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

	// ��ֹ����
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


	std::cout << "�̻߳�����" << std::endl;
}

void WorkThread::Stop()
{
	// ֪ͨsvr ������
	httplib::Client cli("localhost",12345);
	cli.Get("/Stop");
	m_bRun = FALSE;
	m_client->stop();
	m_client = nullptr;
}

bool WorkThread::GetDownloadUrl()
{
	auto ExtractUrlParts = [](const std::string& url, std::string& baseUrl, std::string& page) {
		std::regex urlRegex(R"((https?://[^/]+)(/.*))");
		std::smatch match;
		if (std::regex_match(url, match, urlRegex)) {
			if (match.size() == 3) {
				baseUrl = match[1].str();
				page = match[2].str();
			}
		}
	};

	// ���ظ����ļ��ĵ�ַ 
	httplib::Client cli{ "https://gitee.com" };
	auto res = cli.Get("/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt");
	if (res && res->status == 200) {
		std::string ciphertext;
		for (size_t i = 0; i < res->body.size(); i += 2) {
			char hex[3] = { res->body[i], res->body[i + 1], 0 };
			ciphertext += (char)strtol(hex, NULL, 16);
		}
		std::string strVersionDatUrl = DecryptUrl(ciphertext);
		// ��ȡ�����Ķ��� ��ų����� https://vip.123pan.cn/1820268460/MapleStory
		// �� https://vip.123pan.cn ��һ����ȡ����
		ExtractUrlParts(strVersionDatUrl, m_strUrl, m_strPage);
		m_strPage.push_back('/');
		return true;
	}
	return false;
}

bool WorkThread::GetRemoteVersionFile()
{
	auto res = m_client->Get(m_strPage + "Version.dat");
	if (res && res->status == 200) {
		const std::string strVersionDatContent = DecryptVersionDat(res->body);
		// ����һ�����Ͻṹ��JSON����
		Json::Value root;
		Json::Reader reader;
		if (reader.parse(strVersionDatContent, root)) {
			// ��ȡԶ�̵�MD5
			std::string strRemoteVersionDatMd5 = FileHash::string_md5(res->body);
			// ��ȡ�汾��
			if (strRemoteVersionDatMd5 != m_strLocalVersionMD5)
			{
				// ���浽����
				std::ofstream ofs("Version.dat", std::ios::binary);
				ofs.write(res->body.data(), res->body.size());
				ofs.close();

				m_qwVersion = root["time"].asInt64();
				m_mapFiles.clear();
				// ��ȡ�ļ���Ϣ
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					m_mapFiles[config.m_strPage] = config;
					// ��ȡĿ¼ ɾ���ļ���
					std::filesystem::path filePath = config.m_strPage;
					std::string strPath = filePath.parent_path().string();
					// �ж�Ŀ¼�Ƿ���ڣ������ڴ���
					if (!strPath.empty() && !std::filesystem::exists(strPath))
					{
						std::filesystem::create_directories(m_strCurrentDir + "\\" + strPath);
					}

					// �ж��ļ��Ƿ���ڣ������ڴ���һ���յ�
					if (std::filesystem::exists(m_strCurrentDir + "\\" + config.m_strPage) == false)
					{
						std::ofstream ofs(m_strCurrentDir + "\\" + config.m_strPage, std::ios::binary);
						ofs.close();
					}
				}

				m_vecRunTimeList.clear();

				// ��ȡ runtime �ļ��б�
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}
	return true;
}