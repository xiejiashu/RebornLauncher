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
#include "Encoding.h"

#pragma comment(lib, "advapi32.lib")
extern bool g_bRendering;
extern bool IsProcessRunning(DWORD dwProcessId);

void WriteToFileWithSharedAccess(const std::wstring& strLocalFile, const std::string& data) {
	// 鎵撳紑鏂囦欢锛屽厑璁稿叡浜鍐?
	HANDLE hFile = CreateFile(
		strLocalFile.c_str(),                // 鏂囦欢鍚?
		GENERIC_READ | GENERIC_WRITE,        // 璇诲啓璁块棶
		FILE_SHARE_READ | FILE_SHARE_WRITE,  // 鍏变韩璇诲啓
		NULL,                                // 榛樿瀹夊叏灞炴€?
		OPEN_ALWAYS,                         // 濡傛灉鏂囦欢涓嶅瓨鍦ㄥ垯鍒涘缓
		FILE_ATTRIBUTE_NORMAL,               // 鏅€氭枃浠跺睘鎬?
		NULL                                 // 涓嶄娇鐢ㄦā鏉挎枃浠?
	);

	if (hFile == INVALID_HANDLE_VALUE) {
		std::cerr << "鏃犳硶鎵撳紑鏂囦欢锛岄敊璇唬鐮? " << GetLastError() << std::endl;
		return;
	}

	// 鍐欏叆鏁版嵁
	DWORD dwBytesWritten = 0;
	if (!WriteFile(hFile, data.c_str(), data.size(), &dwBytesWritten, NULL)) {
		std::cerr << "鏃犳硶鍐欏叆鏂囦欢锛岄敊璇唬鐮? " << GetLastError() << std::endl;
	}

	// 鍏抽棴鏂囦欢鍙ユ焺
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
	, m_p2pClient(std::make_unique<P2PClient>())
{
	m_p2pSettings.enabled = false;
	m_p2pSettings.stunServers = { "stun:stun.l.google.com:19302", "stun:global.stun.twilio.com:3478", "stun:stun.cloudflare.com:3478" };
	// 鍚姩绾跨▼
	DWORD dwThreadId = 0;
	// 鎶?m_hFileMappings 閲婃斁鎺?
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
	// 缁撴潫鎵€鏈夊悕瀛楀彨 MapleReborn.exe 鐨勮繘绋?
	// 1. 鑾峰彇鎵€鏈夎繘绋?
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(PROCESSENTRY32);
		if (Process32First(hSnapshot, &pe)) {
			do {
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
				if (hProcess) {
					TCHAR processPath[MAX_PATH];
					DWORD dwSize = MAX_PATH;
					if (QueryFullProcessImageName(hProcess, 0, processPath, &dwSize))
					{
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
		MessageBox(m_hMainWnd, L"鑾峰彇涓嬭浇鍦板潃澶辫触", L"閿欒", MB_OK);
		Stop();
		return 0;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	{
		std::lock_guard<std::mutex> lock(m_p2pMutex);
		if (m_p2pSettings.signalEndpoint.empty()) {
			m_p2pSettings.signalEndpoint = m_strUrl + "/signal";
		}
		if (m_p2pClient) {
			m_p2pClient->UpdateSettings(m_p2pSettings);
		}
	}

	m_client = new httplib::Client(m_strUrl);

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// 濡傛灉涓嶅瓨鍦―ata鐩綍,閭ｄ箞涓嬭浇鍩虹鍖?
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
			// 瑙ｅ瘑 Version.dat鏂囦欢
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			// 杩欐槸涓€涓互涓婄粨鏋勭殑JSON鍐呭
			Json::Value root;
			Json::Reader reader;
			if (reader.parse(strLocalVersionDat, root)) {
				// 鑾峰彇鐗堟湰鍙?
				m_qwVersion = root["time"].asInt64();
				// 鑾峰彇鏂囦欢淇℃伅
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

					// 鍒ゆ柇鐩綍鏄惁瀛樺湪锛屼笉瀛樺湪鍒涘缓
					if (!strPath.empty() && !std::filesystem::exists(strPath))
					{
						std::filesystem::create_directories(m_strCurrentDir + "\\" + strPath);
					}

					// 鍒ゆ柇鏂囦欢鏄惁瀛樺湪锛屼笉瀛樺湪鍒涘缓涓€涓┖鐨?
					if (std::filesystem::exists(m_strCurrentDir + "\\" + config.m_strPage) == false)
					{
						std::ofstream ofs(m_strCurrentDir + "\\" + config.m_strPage, std::ios::binary);
						ofs.close();
					}
				}

				// 鑾峰彇 runtime 鏂囦欢鍒楄〃
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}

	GetRemoteVersionFile();
	// 涓嬭浇RunTime鏂囦欢
	DownloadRunTimeFile();

	if (m_bUpdateSelf)
	{
		Stop();
		// 鍚姩鏂版洿鏂扮殑 UpdateTemp.exe 鐢╫pen鍚姩
		ShellExecute(NULL, L"open", L"UpdateTemp.exe", m_strModulePath.c_str(), m_strModuleDir.c_str(), SW_SHOWNORMAL);
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
		ExitProcess(0);
		return 0;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	unsigned long long dwTick = GetTickCount64();
	// 鎶奙D5鍐欏埌MAP涓?
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping 鑺辫垂鏃堕棿:" << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// 鍚姩娓告垙
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		HandleError("CreateProcess failed");
	}

	PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
	g_bRendering = false;

	// Create杩涚▼鑺辫垂鏃堕棿
	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess 鑺辫垂鏃堕棿:" << dwNewTick - dwTick << std::endl;

	m_hGameProcess[0] = pi.hProcess;
	m_dwGameProcessId[0] = pi.dwProcessId;

	// 鍐欏叆PID
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

		// 涓や釜杩涚▼閮芥浜嗭紝鎭㈠绐楀彛
		if (bHaveGameRun == false && g_bRendering == false)
		{
			PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
			break;
		}

		Sleep(1);
	} while (m_bRun);

	// 鏀跺熬澶勭悊
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

	// 鑾峰彇鍔犲瘑鎻愪緵绋嬪簭鍙ユ焺
	if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		HandleError("CryptAcquireContext failed");
		return ciphertext;
	}

	// 鍒涘缓鍝堝笇瀵硅薄
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
		HandleError("CryptCreateHash failed");
		return ciphertext;
	}

	// 鍝堝笇瀵嗛挜
	const char* key = "cDds!ErF9sIe6u$B";
	if (!CryptHashData(hHash, (BYTE*)key, strlen(key), 0)) {
		HandleError("CryptHashData failed");
		return ciphertext;
	}

	// 娲剧敓 AES 瀵嗛挜
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
	// 瀵瑰簲浠ヤ笂鍔犲瘑杩囩▼鏉ヨВ瀵?瀛?
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
		// 鍏堟煡鐪嬫湰鍦版槸鍚﹀瓨鍦ㄨ繖涓枃浠?
		std::string strLocalFile = download;
		std::string strPage = m_strPage + "Update/" + std::to_string(m_mapFiles[download].m_qwTime) + "/" + download;
		std::replace(strPage.begin(), strPage.end(), '\\', '/');

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;
		SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_nCurrentDownloadSize = m_mapFiles[download].m_qwSize;
		m_nCurrentDownloadProgress = 0;

		// 瀵规瘮MD5
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
		
		std::cout << __FILE__ << ":" << __LINE__<< " 鏂囦欢:" << strLocalFile << std::endl;

		// 濡傛灉鏂囦欢鍚嶆槸鑷韩
		if (strLocalFile.find("RebornLauncher.exe") != std::string::npos)
		{
#ifdef _DEBUG
			continue;
#else
			// 鏀逛釜鍚嶅瓧
			strLocalFile = "UpdateTemp.exe";
			m_bUpdateSelf = true;
#endif
		}

		// 鍒犻櫎鏈湴鏂囦欢
		// std::filesystem::remove(strLocalFile);
		// 鍙栨秷鏂囦欢鍙灞炴€?
		SetFileAttributesA(strLocalFile.c_str(), FILE_ATTRIBUTE_NORMAL);
		DeleteFileA(strLocalFile.c_str());

		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		// 涓嬭浇鏂版枃浠?

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
	// 鑷姩閿?
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_strCurrentDownload;
}

void WorkThread::SetCurrentDownloadFile(const std::wstring& strFile)
{
	// 鑷姩閿?
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
	// 瑙ｅ帇鏂囦欢
	Extract7z("MapleReborn.7z", "./");
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// 鍒犻櫎鍘嬬缉鍖?
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
			// 绉诲姩鍒版枃浠朵綅缃?
			while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
				if (fileInfo.filePath == archive_entry_pathname(entry)) {
					std::string outputPath = outPath + "/" + fileInfo.filePath;
					// 濡傛灉娌℃湁鐩綍鍏堝垱寤?
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

// 鏂偣缁紶 
bool WorkThread::DownloadWithResume(const std::string& url, const std::string& file_path) {

	std::string strUrl = url;
	std::cout << __FILE__ << ":" << __LINE__ << " url:" << m_strUrl << " page:" << strUrl << std::endl;
	SetCurrentDownloadFile(str2wstr(file_path, static_cast<int>(file_path.length())));

	if (m_client == nullptr) {
		return false;
	}

	// 璇锋眰鏂囦欢鎬诲ぇ灏忥紙Range 鏂瑰紡閬垮厤棰濆 HEAD锛?	httplib::Result res;
	{
		httplib::Headers headers;
		headers.insert({ "Range", "bytes=0-0" });
		res = m_client->Get(strUrl.c_str(), headers);
		if (res && (res->status == 200 || res->status == 206)) {
			m_nCurrentDownloadSize = static_cast<int>(std::stoull(res.value().get_header_value("Content-Length")));
		}
		else {
			std::cout << "Failed to get file size, status code: " << (res ? res->status : -1) << std::endl;
			m_nCurrentDownloadSize = 0;
		}
	}

	// P2P 浼樺厛灏濊瘯
	{
		P2PSettings settingsCopy;
		{
			std::lock_guard<std::mutex> lock(m_p2pMutex);
			settingsCopy = m_p2pSettings;
		}
		if (settingsCopy.enabled && m_p2pClient) {
			m_p2pClient->UpdateSettings(settingsCopy);
			const bool p2pOk = m_p2pClient->TryDownload(strUrl, file_path, [this](uint64_t current, uint64_t total) {
				m_nCurrentDownloadProgress = static_cast<int>(current);
				m_nCurrentDownloadSize = static_cast<int>(total);
			});
			if (p2pOk) {
				m_nCurrentDownloadProgress = m_nCurrentDownloadSize;
				return true;
			}
		}
	}

	// 妫€鏌ユ枃浠舵槸鍚﹀凡缁忛儴鍒嗕笅杞?	std::ifstream existing_file(file_path, std::ios::binary | std::ios::ate);
	size_t existing_file_size = 0;
	if (existing_file.is_open()) {
		existing_file_size = existing_file.tellg();
		existing_file.close();
	}
	if (m_nCurrentDownloadSize > 0) {
		if (existing_file_size == static_cast<size_t>(m_nCurrentDownloadSize)) {
			return true;
		}
		else if (existing_file_size > static_cast<size_t>(m_nCurrentDownloadSize)) {
			std::filesystem::remove(file_path);
			existing_file_size = 0;
		}
	}

	httplib::Headers headers;
	if (existing_file_size > 0) {
		// 浣跨敤 Range 璇锋眰澶村疄鐜版柇鐐圭画浼?		m_nCurrentDownloadProgress = static_cast<int>(existing_file_size);
		headers.insert({ "Range", "bytes=" + std::to_string(existing_file_size) + "-" + std::to_string(m_nCurrentDownloadSize) });
		std::cout << "Resuming download from byte: " << existing_file_size << std::endl;
	}

	// 鎵撳紑鏂囦欢锛岃拷鍔犲啓鍏?	std::ofstream file(file_path, std::ios::binary | std::ios::app);
	if (!file.is_open()) {
		return false;
	}

	// 鍙戣捣涓嬭浇璇锋眰
	res = m_client->Get(strUrl.c_str(), headers, [&](const char* data, size_t data_length) {
		file.write(data, static_cast<std::streamsize>(data_length));
		file.flush();
		m_nCurrentDownloadProgress += static_cast<int>(data_length);
		return true; // 杩斿洖 true 浠ョ户缁笅杞?	});
	file.close();

	if (res && res->status == 200) {
		std::cout << "Download completed!" << std::endl;
		return true;
	}
	else if (res && res->status == 206) {  // 206 Partial Content 鐘舵€佺爜琛ㄧず鎴愬姛缁紶
		std::cout << "Download resumed and completed!" << std::endl;
		return true;
	}
	else {
		std::cerr << "Download failed with status code: " << (res ? res->status : -1) << std::endl;
		return false;
	}
}


void WorkThread::WriteDataToMapping()
{
	// m_mapFiles
	for (auto& [strPage, config] : m_mapFiles)
	{
		std::string strMemoryName = strPage;
		// 鍒犻櫎 "\"
		std::replace(strMemoryName.begin(), strMemoryName.end(), '\\', '_');
		HANDLE hFileMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, config.m_strMd5.length() + 1, strMemoryName.c_str());
		if (hFileMapping)
		{
			// 鎶奙D5鍐欒繘鍘?
			LPVOID lpBaseAddress = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
			if (lpBaseAddress)
			{
				memcpy(lpBaseAddress, config.m_strMd5.c_str(), config.m_strMd5.length());
				// 娣诲姞缁堟绗?
				((char*)lpBaseAddress)[config.m_strMd5.length()] = '\0';
				UnmapViewOfFile(lpBaseAddress);
			}
			m_hFileMappings.push_back(hFileMapping);
		}
	}
}

void WorkThread::WebServiceThread()
{
	// 鐩戝惉缃戠粶璇锋眰
	httplib::Server svr;
	svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
		g_bRendering = true;
		// g_fProgressTotal = 0;
		m_nTotalDownload = 1;
		m_nCurrentDownload = 0;
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		std::string strPage = req.get_param_value("page");
		std::cout << "璇锋眰涓嬭浇鏂囦欢:" << strPage << std::endl;
		auto it = m_mapFiles.find(strPage);
		SetCurrentDownloadFile(str2wstr(strPage,strPage.length()));
		if (it != m_mapFiles.end())
		{
			std::cout << "寮€濮嬮獙璇佹枃浠禡D5:" << strPage << std::endl;
			std::string strLocalFile = it->first;
			bool Md5Same = false;
			if (GetFileAttributesA(strLocalFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
				std::string strLocalFileMd5 = FileHash::file_md5(strLocalFile);
				Md5Same = it->second.m_strMd5 == strLocalFileMd5;
			}

			if (Md5Same)
			{
				std::cout << "鏂囦欢宸插瓨鍦ㄤ笖MD5鐩稿悓" << std::endl;
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else
			{
				std::cout << "寮€濮嬩笅杞芥枃浠?" << strLocalFile << std::endl;
				// 涓嬭浇
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
					// 鎻愬彇鐩綍
					// std::filesystem::path filePath = strLocalFile;
					// std::filesystem::create_directories(filePath.parent_path());
					std::cout << "涓嬭浇瀹屾垚寮€濮嬩繚瀛?" << strLocalFile << std::endl;
					// 鍏变韩璇诲啓鎵撳紑

					std::ofstream ofs(strLocalFile, std::ios::binary);
					// 濡傛灉鍐欏叆澶辫触鎵撳嵃涓€涓?
					if (!ofs)
					{
						std::cout << "鍐欏叆鏂囦欢澶辫触:" << GetLastError() << std::endl;
					}
					ofs.write(ret->body.c_str(), ret->body.size());

					ofs.close();
					res.status = 200;
					res.set_content("OK", "text/plain");
					std::cout << strLocalFile  <<"鍐欏叆鏂囦欢鎴愬姛" << std::endl;
				}
				else
				{
					res.status = 404;
					res.set_content("Not Found", "text/plain");
					std::cout << "涓嬭浇鏂囦欢澶辫触,鐘舵€佺爜:"<< ret->status << std::endl;
				}
			}
		}
		else {
			std::cout << "鏈嶅姟绔笉瀛樺湪杩欎釜鏂囦欢" << strPage << std::endl;
			res.status = 404;
			res.set_content("404", "text/palin");
		}
		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

	svr.Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {

		std::cout << "鐧诲綍鍣ㄨ姹傝繍琛屾父鎴? << std::endl;

		GetRemoteVersionFile();
		// 涓嬭浇RunTime鏂囦欢
		DownloadRunTimeFile();

		bool bRun = false;
		for (int i = 0; i < sizeof(m_dwGameProcessId) / sizeof(m_dwGameProcessId[0]); ++i)
		{
			if (m_dwGameProcessId[i] != 0 && IsProcessRunning(m_dwGameProcessId[i]))
			{
				std::cout << "杩涚▼:" << m_dwGameProcessId[i] << "瀛樺湪" << std::endl;
				continue;
			}
			else
			{
				// 鍚姩娓告垙
				STARTUPINFO si = { sizeof(si) };
				PROCESS_INFORMATION pi;
				if (!CreateProcess(m_szProcessName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
					HandleError("CreateProcess failed");
				}
				m_hGameProcess[i] = pi.hProcess;
				m_dwGameProcessId[i] = pi.dwProcessId;
				// 鍐欏叆PID
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

				std::cout << "杩愯鎴愬姛" << std::endl;
				res.status = 200;
				res.set_content("OK", "text/plain");
				bRun = true;
				break;
			}
		}

		if (bRun == false)
		{
			res.status = 404;
			res.set_content("涓€娆″彧鑳藉惎鍔ㄤ袱涓鎴风,娌℃湁浣嶇疆鍟︺€?, "text/plain");
		}

		PostMessage(m_hMainWnd, WM_MINIMIZE_TO_TRAY, 0, 0);
		g_bRendering = false;
	});

	// 涓璇锋眰
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


	std::cout << "绾跨▼浼氬畬鍚? << std::endl;
}

void WorkThread::Stop()
{
	// 閫氱煡svr 缁撴潫浜?	httplib::Client cli("localhost",12345);
	cli.Get("/Stop");
	m_bRun = FALSE;
	if (m_client) {
		m_client->stop();
		m_client = nullptr;
	}
}

void WorkThread::UpdateP2PSettings(const P2PSettings& settings)
{
	std::lock_guard<std::mutex> lock(m_p2pMutex);
	m_p2pSettings = settings;
}

P2PSettings WorkThread::GetP2PSettings() const
{
	std::lock_guard<std::mutex> lock(m_p2pMutex);
	return m_p2pSettings;
}

bool WorkThread::GetDownloadUrl()
{
	std::cout << __FILE__<<":"<<__LINE__ << std::endl;
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
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// 涓嬭浇鏇存柊鏂囦欢鐨勫湴鍧€ 
	httplib::Client cli{ "https://gitee.com" };
	auto res = cli.Get("/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt");
	if (res && res->status == 200) {
		std::string ciphertext;
		for (size_t i = 0; i < res->body.size(); i += 2) {
			char hex[3] = { res->body[i], res->body[i + 1], 0 };
			ciphertext += (char)strtol(hex, NULL, 16);
		}
		std::string strVersionDatUrl = DecryptUrl(ciphertext);
		// 鑾峰彇鍑烘潵鐨勪笢瑗?澶ф闀胯繖鏍?https://vip.123pan.cn/1820268460/MapleStory
		// 鎶?https://vip.123pan.cn 杩欎竴鎴彁鍙栧嚭鏉?
		ExtractUrlParts(strVersionDatUrl, m_strUrl, m_strPage);
		m_strPage.push_back('/');
		return true;
	}
	else {
		if (res) {
			std::cout << "鑾峰彇涓嬭浇鍦板潃澶辫触 鐘舵€佺爜:" << res->status << std::endl;
		}
		else {
			std::cout << "鑾峰彇涓嬭浇鍦板潃澶辫触 鏈煡鐨凥TTP閿欒:"<< res.error() << std::endl;
		}
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	return false;
}

bool WorkThread::GetRemoteVersionFile()
{
	auto res = m_client->Get(m_strPage + "Version.dat");
	if (res && res->status == 200) {
		const std::string strVersionDatContent = DecryptVersionDat(res->body);
		// 杩欐槸涓€涓互涓婄粨鏋勭殑JSON鍐呭
		Json::Value root;
		Json::Reader reader;
		if (reader.parse(strVersionDatContent, root)) {
			// 鑾峰彇杩滅▼鐨凪D5
			std::string strRemoteVersionDatMd5 = FileHash::string_md5(res->body);
			// 鑾峰彇鐗堟湰鍙?
			if (strRemoteVersionDatMd5 != m_strLocalVersionMD5)
			{
				// 淇濆瓨鍒版湰鍦?
				std::ofstream ofs("Version.dat", std::ios::binary);
				ofs.write(res->body.data(), res->body.size());
				ofs.close();

				m_qwVersion = root["time"].asInt64();
				m_mapFiles.clear();
				// 鑾峰彇鏂囦欢淇℃伅
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					m_mapFiles[config.m_strPage] = config;
					// 鎻愬彇鐩綍 鍒犻櫎鏂囦欢鍚?
					std::filesystem::path filePath = config.m_strPage;
					std::string strPath = filePath.parent_path().string();
					// 鍒ゆ柇鐩綍鏄惁瀛樺湪锛屼笉瀛樺湪鍒涘缓
					if (!strPath.empty() && !std::filesystem::exists(strPath))
					{
						std::filesystem::create_directories(m_strCurrentDir + "\\" + strPath);
					}

					// 鍒ゆ柇鏂囦欢鏄惁瀛樺湪锛屼笉瀛樺湪鍒涘缓涓€涓┖鐨?
					if (std::filesystem::exists(m_strCurrentDir + "\\" + config.m_strPage) == false)
					{
						std::ofstream ofs(m_strCurrentDir + "\\" + config.m_strPage, std::ios::binary);
						ofs.close();
					}
				}

				m_vecRunTimeList.clear();

				// 鑾峰彇 runtime 鏂囦欢鍒楄〃
				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
	}
	return true;
}
