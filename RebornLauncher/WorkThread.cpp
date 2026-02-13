#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <json/json.h>
#include <shellapi.h>
#include <regex>
#include <sstream>

#include "FileHash.h"
#include "Encoding.h"

extern bool g_bRendering;

namespace {

std::string NormalizeRelativeUrlPath(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (path.empty()) {
		return {};
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return path;
}

std::string JoinUrlPath(const std::string& basePath, const std::string& childPath) {
	std::string base = basePath;
	std::string child = childPath;
	std::replace(base.begin(), base.end(), '\\', '/');
	std::replace(child.begin(), child.end(), '\\', '/');

	if (base.empty()) {
		return NormalizeRelativeUrlPath(child);
	}
	if (base.front() != '/') {
		base.insert(base.begin(), '/');
	}
	if (!base.empty() && base.back() != '/') {
		base.push_back('/');
	}
	while (!child.empty() && child.front() == '/') {
		child.erase(child.begin());
	}
	return base + child;
}

std::string BuildSignalEndpoint(const std::string& baseUrl, const std::string& pagePath) {
	return baseUrl + JoinUrlPath(pagePath, "signal");
}

bool IsHttpUrl(const std::string& value) {
	return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool ParseHttpUrl(const std::string& url, bool& useTls, std::string& host, int& port, std::string& path) {
	std::regex urlRegex(R"((https?)://([^/:]+)(?::(\d+))?(\/.*)?)");
	std::smatch match;
	if (!std::regex_match(url, match, urlRegex)) {
		return false;
	}
	useTls = match[1].str() == "https";
	host = match[2].str();
	port = match[3].matched ? std::stoi(match[3].str()) : (useTls ? 443 : 80);
	path = match[4].matched ? match[4].str() : "/";
	if (path.empty()) {
		path = "/";
	}
	if (path.front() != '/') {
		path.insert(path.begin(), '/');
	}
	return true;
}

bool ExtractBaseAndPath(const std::string& absoluteUrl, std::string& baseUrl, std::string& path) {
	bool useTls = false;
	std::string host;
	int port = 0;
	if (!ParseHttpUrl(absoluteUrl, useTls, host, port, path)) {
		return false;
	}
	const bool defaultPort = (useTls && port == 443) || (!useTls && port == 80);
	baseUrl = (useTls ? "https://" : "http://") + host;
	if (!defaultPort) {
		baseUrl += ":" + std::to_string(port);
	}
	return true;
}

std::string TrimAscii(std::string value) {
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
		value.erase(value.begin());
	}
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
		value.pop_back();
	}
	return value;
}

std::string GetFileNameFromUrl(std::string url) {
	const size_t hashPos = url.find('#');
	if (hashPos != std::string::npos) {
		url = url.substr(0, hashPos);
	}
	const size_t queryPos = url.find('?');
	if (queryPos != std::string::npos) {
		url = url.substr(0, queryPos);
	}
	std::replace(url.begin(), url.end(), '\\', '/');
	const size_t slash = url.find_last_of('/');
	if (slash == std::string::npos) {
		return url;
	}
	if (slash + 1 >= url.size()) {
		return {};
	}
	return url.substr(slash + 1);
}

} // namespace


// Initialize the worker thread and background web service.
WorkThread::WorkThread(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings)
	: m_hMainWnd(hWnd), m_strModulePath(strModulePath), m_strModuleName(strModuleName), m_strModuleDir(strModuleDir)
	, m_bUpdateSelf(false), m_nTotalDownload(0), m_nCurrentDownload(0), m_nCurrentDownloadSize(0), m_nCurrentDownloadProgress(0)
	, m_qwVersion(0)
	, m_bRun(TRUE)
	, m_strCurrentDownload(L"")
	, m_hThread(nullptr)
	, m_p2pClient(std::make_unique<P2PClient>())
{
	m_p2pSettings = initialP2PSettings;
	if (m_p2pSettings.stunServers.empty()) {
		m_p2pSettings.stunServers = {
			"stun:stun.l.google.com:19302",
			"stun:global.stun.twilio.com:3478",
			"stun:stun.cloudflare.com:3478",
			"stun:127.0.0.1:3478"
		};
	}
	DWORD dwThreadId = 0;
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

// Cleanup hook for WorkThread lifecycle.
WorkThread::~WorkThread()
{
}

// Thread entry point that terminates stray processes and runs the worker.
DWORD __stdcall WorkThread::ThreadProc(LPVOID lpParameter)
{
	// HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	// if (hSnapshot != INVALID_HANDLE_VALUE) {
	// 	PROCESSENTRY32 pe;
	// 	pe.dwSize = sizeof(PROCESSENTRY32);
	// 	if (Process32First(hSnapshot, &pe)) {
	// 		do {
	// 			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
	// 			if (hProcess) {
	// 				TCHAR processPath[MAX_PATH];
	// 				DWORD dwSize = MAX_PATH;
	// 				if (QueryFullProcessImageName(hProcess, 0, processPath, &dwSize))
	// 				{
	// 					std::wstring strProcessPath = processPath;
	// 					if (strProcessPath.find(L"MapleFireReborn.exe") != std::string::npos){
	// 						TerminateProcess(hProcess, 0);
	// 					}
	// 					if (strProcessPath.find(L"MapleStory.exe") != std::string::npos) {
	// 						TerminateProcess(hProcess, 0);
	// 					}
	// 				}
	// 				CloseHandle(hProcess);
	// 			}
	// 		} while (Process32Next(hSnapshot, &pe));
	// 	}
	// 	CloseHandle(hSnapshot);
	// }

	WorkThread* pThis = (WorkThread*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

DWORD WorkThread::Run()
{
	// Record current working directory.
	m_strCurrentDir = std::filesystem::current_path().string();
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// Fetch bootstrap config (download roots + P2P settings).
	if (!FetchBootstrapConfig())
	{
		MessageBox(m_hMainWnd, L"Failed to fetch bootstrap config.", L"Error", MB_OK);
		Stop();
		return 0;
	}
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	// Fill default signal endpoint and sync P2P client settings.
	{
		std::lock_guard<std::mutex> lock(m_p2pMutex);
		if (m_p2pSettings.signalEndpoint.empty()) {
			m_p2pSettings.signalEndpoint = BuildSignalEndpoint(m_strUrl, m_strPage);
		}
		if (m_p2pClient) {
			m_p2pClient->UpdateSettings(m_p2pSettings);
		}
	}

	// Initialize primary HTTP client.
	m_client = new httplib::Client(m_strUrl);

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	// Download and extract base package when Data folder is missing.
	if (!std::filesystem::exists("./Data"))
	{
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
		if (!DownloadBasePackage()) {
			MessageBox(m_hMainWnd, L"Failed to download base package.", L"Error", MB_OK);
			Stop();
			return 0;
		}
		std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	{
		std::cout << "9999999999999999999" << std::endl;
		// Read local Version.dat and compute MD5.
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

		// Parse local manifest and initialize local file/runtime lists.
		if (!strLocalVersionDatContent.empty())
		{
			std::string strLocalVersionDat = DecryptVersionDat(strLocalVersionDatContent);
			std::string strLocalManifestJson;
			Json::Value root;
			Json::Reader reader;
			bool parsedLocalManifest = false;

			if (!strLocalVersionDat.empty() && reader.parse(strLocalVersionDat, root)) {
				strLocalManifestJson = strLocalVersionDat;
				parsedLocalManifest = true;
			}
			else if (reader.parse(strLocalVersionDatContent, root)) {
				strLocalManifestJson = strLocalVersionDatContent;
				parsedLocalManifest = true;
			}

			if (parsedLocalManifest) {
				WriteVersionToMapping(strLocalManifestJson);
				m_qwVersion = root["time"].asInt64();
				Json::Value filesJson = root["file"];
				for (auto& fileJson : filesJson) {
					VersionConfig config;
					config.m_strMd5 = fileJson["md5"].asString();
					config.m_qwTime = fileJson["time"].asInt64();
					config.m_qwSize = fileJson["size"].asInt64();
					config.m_strPage = fileJson["page"].asString();
					if (config.m_strPage.empty()) {
						continue;
					}
					m_mapFiles[config.m_strPage] = config;
					// Ensure local placeholder file exists for each manifest entry.
					try {
						const std::filesystem::path localPath =
							std::filesystem::current_path() / std::filesystem::u8path(config.m_strPage);
						std::error_code ec;
						const auto parent = localPath.parent_path();
						if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
							std::filesystem::create_directories(parent, ec);
						}
						ec.clear();
						if (!std::filesystem::exists(localPath, ec)) {
							std::ofstream ofs(localPath, std::ios::binary);
							ofs.close();
						}
					}
					catch (...) {
						std::cout << "Skip invalid local page path: " << config.m_strPage << std::endl;
						m_mapFiles.erase(config.m_strPage);
					}
				}

				Json::Value downloadList = root["runtime"];
				for (auto& download : downloadList) {
					m_vecRunTimeList.push_back(download.asString());
				}
			}
		}
		// Fetch remote Version.dat MD5 from Version.dat.md5 when available.
		std::string strRemoteVersionDatMD5;
		{
			std::string strVersionDatMD5 = m_strVersionManifestPath + ".md5";
			// Absolute URL path.
			if (IsHttpUrl(strVersionDatMD5)) {
				httplib::Client clientExtract("");
				std::string baseUrl;
				std::string path;
				if (ExtractBaseAndPath(strVersionDatMD5, baseUrl, path)) {
					clientExtract = httplib::Client(baseUrl.c_str());
					auto res = clientExtract.Get(path.c_str());
					if (res && res->status == 200) {
						strRemoteVersionDatMD5 = TrimAscii(res->body);
					}
				}
			}
			else
			{
				httplib::Client client(m_strUrl);
				auto res = client.Get("/" + strVersionDatMD5);
				if (res && res->status == 200) {
					strRemoteVersionDatMD5 = TrimAscii(res->body);
				}
			}
		}

		// Refresh remote manifest when local and remote Version.dat MD5 differ.
		if (!strRemoteVersionDatMD5.empty() && m_strLocalVersionMD5 != strRemoteVersionDatMD5) {
			RefreshRemoteVersionManifest();
		}
	}

	if (!DownloadRunTimeFile())
	{
		MessageBox(m_hMainWnd, L"Failed to download update files.", L"Error", MB_OK);
		Stop();
		return 0;
	}

	// Start self-update helper then exit launcher process.
	if (m_bUpdateSelf)
	{
		Stop();
		WriteProfileString(TEXT("MapleFireReborn"), TEXT("pid"), std::to_wstring(_getpid()).c_str());
		ShellExecute(NULL, L"open", L"UpdateTemp.exe", m_strModulePath.c_str(), m_strModuleDir.c_str(), SW_SHOWNORMAL);
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
		ExitProcess(0);
		return 0;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	unsigned long long dwTick = GetTickCount64();
	// Publish file hashes into shared memory for the game client.
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping elapsed ms: " << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	// Launch first game client instance.
	if (!LaunchGameClient()) {
		MessageBox(m_hMainWnd, L"Failed to launch game client.", L"Error", MB_OK);
		Stop();
		return 0;
	}

	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess elapsed ms: " << dwNewTick - dwTick << std::endl;

	// Poll game process list until shutdown.
	do
	{
		CleanupExitedGameInfos();
		UpdateGameMainWindows();
		// Close tray and stop when no client remains.
		const bool bHaveGameRun = HasRunningGameProcess();
		if (bHaveGameRun == false)
		{
			PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
			break;
		}

		Sleep(1);
	}
	while (m_bRun);

	// Cleanup remaining game processes before worker exits.
	TerminateAllGameProcesses();
	// Close worker thread handle.
	CloseHandle(m_hThread);
	m_hThread = nullptr;

	return 0;
}

// Download runtime files listed in the manifest.
bool WorkThread::DownloadRunTimeFile()
{
	m_nTotalDownload = m_vecRunTimeList.size();
	m_nCurrentDownload = 0;
	for (auto& download : m_vecRunTimeList)
	{
		std::string strLocalFile = download;
		const std::string strPage = JoinUrlPath(
			m_strPage, std::to_string(m_mapFiles[download].m_qwTime) + "/" + download);

		std::cout <<__FUNCTION__<<":" << strPage << std::endl;
		SetCurrentDownloadFile(str2wstr(strLocalFile, strLocalFile.length()));
		m_nCurrentDownloadSize = m_mapFiles[download].m_qwSize;
		m_nCurrentDownloadProgress = 0;

		auto it = m_mapFiles.find(strLocalFile);
		if (it != m_mapFiles.end())
		{
			bool Md5Same = false;
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::u8path(strLocalFile), ec)) {
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
		std::cout << __FILE__ << ":" << __LINE__ << " File: " << strLocalFile << std::endl;

		if (strLocalFile.find("RebornLauncher.exe") != std::string::npos)
		{
#ifdef _DEBUG
			continue;
#else
			strLocalFile = "UpdateTemp.exe";
			m_bUpdateSelf = true;
#endif
		}

		const std::wstring strLocalFileW = str2wstr(strLocalFile);
		if (!strLocalFileW.empty()) {
			SetFileAttributesW(strLocalFileW.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(strLocalFileW.c_str());
		}
		else {
			std::error_code ec;
			std::filesystem::remove(std::filesystem::u8path(strLocalFile), ec);
		}

		if (!DownloadWithResume(strPage, strLocalFile)) {
			std::cout << "Download failed for runtime file: " << strPage << std::endl;
			return false;
		}
		m_nCurrentDownload += 1;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	return true;
}

// Download and extract the base package archive.
bool WorkThread::DownloadBasePackage()
{
	m_nTotalDownload = 1;
	m_nCurrentDownload = 0;
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;

	if (m_basePackageUrls.empty()) {
		std::cout << "Bootstrap config missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	bool downloaded = false;
	std::string downloadedArchivePath;
	for (const auto& packageUrl : m_basePackageUrls) {
		std::string absolutePackageUrl = packageUrl;
		if (!IsHttpUrl(absolutePackageUrl)) {
			absolutePackageUrl = m_strUrl + NormalizeRelativeUrlPath(packageUrl);
		}
		std::string localArchivePath = GetFileNameFromUrl(absolutePackageUrl);
		if (localArchivePath.empty()) {
			localArchivePath = "base_package.7z";
		}

		for (int attempt = 0; attempt < 2; ++attempt) {
			m_nCurrentDownloadProgress = 0;
			m_nCurrentDownloadSize = 0;
			downloaded = DownloadFileChunkedWithResume(absolutePackageUrl, localArchivePath, 2);
			if (downloaded) {
				downloadedArchivePath = localArchivePath;
				break;
			}
		}
		if (downloaded) {
			break;
		}
	}

	if (!downloaded) {
		std::cout << "Base package download failed for all candidates." << std::endl;
		return false;
	}

	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	Extract7z(downloadedArchivePath, "./");
	std::cout << __FILE__ << ":" << __LINE__ << std::endl;
	std::filesystem::remove(downloadedArchivePath);
	return true;
}


