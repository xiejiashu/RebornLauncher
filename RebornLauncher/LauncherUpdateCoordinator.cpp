#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <filesystem>
#include <httplib.h>
#include <iostream>
#include <random>
#include <shellapi.h>
#include <thread>

#include "LocalVersionLoader.h"
#include "NetUtils.h"
#include "ResumeDownload.h"
#include "RuntimeUpdater.h"
#include "RunCoordinator.h"

namespace {

using workthread::netutils::ExtractBaseAndPath;
using workthread::netutils::GetFileNameFromUrl;
using workthread::netutils::IsHttpUrl;
using workthread::netutils::JoinUrlPath;
using workthread::netutils::NormalizeRelativeUrlPath;
using workthread::netutils::TrimAscii;

std::wstring QuoteCommandArg(const std::wstring& value) {
	return L"\"" + value + L"\"";
}

std::wstring BuildRelaunchArgs(DWORD cleanupPid, const std::wstring& cleanupPath, const wchar_t* stage) {
	std::wstring args = L"--cleanup-pid=" + std::to_wstring(cleanupPid);
	if (!cleanupPath.empty()) {
		args += L" --cleanup-path=" + QuoteCommandArg(cleanupPath);
	}
	if (stage && stage[0] != L'\0') {
		args += L" --stage=" + std::wstring(stage);
	}
	return args;
}

std::string BuildSignalEndpoint(const std::string& baseUrl, const std::string& pagePath) {
	return baseUrl + JoinUrlPath(pagePath, "signal");
}

std::string ResolveBasePackageP2PResourcePath(const std::string& configuredPackageUrl, const std::string& absolutePackageUrl) {
	if (!configuredPackageUrl.empty() && !IsHttpUrl(configuredPackageUrl)) {
		return NormalizeRelativeUrlPath(configuredPackageUrl);
	}

	std::string baseUrl;
	std::string path;
	if (ExtractBaseAndPath(absolutePackageUrl, baseUrl, path)) {
		return NormalizeRelativeUrlPath(path);
	}
	if (ExtractBaseAndPath(configuredPackageUrl, baseUrl, path)) {
		return NormalizeRelativeUrlPath(path);
	}
	return {};
}

std::string NormalizeNoUpdatePathKey(std::string value) {
	value = TrimAscii(std::move(value));
	if (value.empty()) {
		return {};
	}

	std::replace(value.begin(), value.end(), '\\', '/');
	while (value.rfind("./", 0) == 0) {
		value.erase(0, 2);
	}
	while (!value.empty() && value.front() == '/') {
		value.erase(value.begin());
	}

	std::filesystem::path normalizedPath = std::filesystem::u8path(value).lexically_normal();
	std::string key = normalizedPath.generic_string();
	while (key.rfind("./", 0) == 0) {
		key.erase(0, 2);
	}
	while (!key.empty() && key.front() == '/') {
		key.erase(key.begin());
	}
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return key;
}

void MarkFileHidden(const std::string& path) {
	std::error_code ec;
	const auto fsPath = std::filesystem::u8path(path);
	if (!std::filesystem::exists(fsPath, ec)) {
		return;
	}
	const std::wstring pathW = fsPath.wstring();
	DWORD attrs = GetFileAttributesW(pathW.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return;
	}
	if ((attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
		SetFileAttributesW(pathW.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
	}
}

ULONGLONG BuildNextManifestPollTick(ULONGLONG nowTick) {
	static thread_local std::mt19937 rng{ static_cast<unsigned int>(GetTickCount()) };
	std::uniform_int_distribution<DWORD> intervalDist(5000, 10000);
	return nowTick + static_cast<ULONGLONG>(intervalDist(rng));
}

} // namespace


// Initialize the worker thread and background web service.
LauncherUpdateCoordinator::LauncherUpdateCoordinator(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings)
	: m_selfUpdateState{ false, strModulePath, strModuleDir }
{
	(void)strModuleName;
	m_runtimeState.mainWnd = hWnd;
	m_networkState.p2pClient = std::make_unique<P2PClient>();
	m_networkState.p2pSettings = initialP2PSettings;
	if (m_networkState.p2pSettings.stunServers.empty()) {
		m_networkState.p2pSettings.stunServers = {
			"stun:stun.l.google.com:19302",
			"stun:global.stun.twilio.com:3478",
			"stun:stun.cloudflare.com:3478",
			"stun:127.0.0.1:3478"
		};
	}
	DWORD dwThreadId = 0;
	for (auto hFileMapping : m_runtimeState.fileMappings)
	{
		CloseHandle(hFileMapping);
	}

	m_runtimeState.thread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	if (m_runtimeState.thread == NULL) {
		HandleError("CreateThread failed");
	}

	std::thread WebTr([this]() {
		while (m_runtimeState.run) {
			try {
				WebServiceThread();
			}
			catch (const std::exception& ex) {
				LogUpdateError(
					"UF-WS-EXCEPTION",
					"LauncherUpdateCoordinator::LauncherUpdateCoordinator:web-supervisor",
					"Web service thread threw std::exception",
					ex.what() ? ex.what() : "unknown");
			}
			catch (...) {
				LogUpdateError(
					"UF-WS-EXCEPTION",
					"LauncherUpdateCoordinator::LauncherUpdateCoordinator:web-supervisor",
					"Web service thread threw unknown exception");
			}

			if (!m_runtimeState.run) {
				break;
			}

			SetLauncherStatus(L"Web service stopped, restarting...");
			LogUpdateError(
				"UF-WS-SUPERVISOR",
				"LauncherUpdateCoordinator::LauncherUpdateCoordinator:web-supervisor",
				"Web service thread stopped unexpectedly",
				"Supervisor will restart WebServiceThread.");
			Sleep(500);
		}
	});
	WebTr.detach();
}

// Cleanup hook for LauncherUpdateCoordinator lifecycle.
LauncherUpdateCoordinator::~LauncherUpdateCoordinator()
{
}

// Thread entry point that terminates stray processes and runs the worker.
DWORD __stdcall LauncherUpdateCoordinator::ThreadProc(LPVOID lpParameter)
{
	LauncherUpdateCoordinator* pThis = (LauncherUpdateCoordinator*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

bool LauncherUpdateCoordinator::InitializeDownloadEnvironment()
{
	if (!FetchBootstrapConfig()) {
		return false;
	}

	bool p2pEnabled = false;
	std::string p2pSignalEndpoint;
	size_t stunCount = 0;
	{
		std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
		if (m_networkState.p2pSettings.signalEndpoint.empty()) {
			m_networkState.p2pSettings.signalEndpoint = BuildSignalEndpoint(m_networkState.url, m_networkState.page);
		}
		if (m_networkState.p2pClient) {
			m_networkState.p2pClient->UpdateSettings(m_networkState.p2pSettings);
		}
		p2pEnabled = m_networkState.p2pSettings.enabled;
		p2pSignalEndpoint = m_networkState.p2pSettings.signalEndpoint;
		stunCount = m_networkState.p2pSettings.stunServers.size();
	}
	LogUpdateInfo(
		"UF-P2P-INIT",
		"LauncherUpdateCoordinator::InitializeDownloadEnvironment",
		"P2P settings prepared",
		"enabled=" + std::string(p2pEnabled ? "true" : "false") +
		", signal_endpoint=" + (p2pSignalEndpoint.empty() ? std::string("<empty>") : p2pSignalEndpoint) +
		", stun_count=" + std::to_string(stunCount) +
		", stun_used_by_current_p2p_client=false");

	m_networkState.client = std::make_unique<httplib::Client>(m_networkState.url);
	m_networkState.client->set_follow_location(true);
	m_networkState.client->set_connection_timeout(8, 0);
	m_networkState.client->set_read_timeout(30, 0);
	m_networkState.client->set_write_timeout(15, 0);
	return true;
}

bool LauncherUpdateCoordinator::EnsureBasePackageReady()
{
	if (!std::filesystem::exists("./Data"))
	{
		if (!DownloadBasePackage()) {
			return false;
		}
	}
	return true;
}

void LauncherUpdateCoordinator::LoadLocalVersionState()
{
	workthread::versionload::LocalVersionLoader loader(*this);
	loader.Execute();
	LoadNoUpdateList();
}

void LauncherUpdateCoordinator::LoadNoUpdateList() {
	m_versionState.noUpdateFiles.clear();

	std::error_code ec;
	const std::filesystem::path listPath =
		std::filesystem::current_path(ec) / std::filesystem::u8path("NoUPdate.txt");
	if (ec) {
		return;
	}

	std::ifstream in(listPath, std::ios::binary);
	if (!in.is_open()) {
		return;
	}

	std::string line;
	size_t lineNumber = 0;
	while (std::getline(in, line)) {
		++lineNumber;
		if (lineNumber == 1 && line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3);
		}

		std::string trimmed = TrimAscii(line);
		if (trimmed.empty()) {
			continue;
		}
		if (trimmed[0] == '#' || trimmed[0] == ';') {
			continue;
		}

		const std::string key = NormalizeNoUpdatePathKey(trimmed);
		if (!key.empty()) {
			m_versionState.noUpdateFiles.insert(key);
		}
	}

	if (!m_versionState.noUpdateFiles.empty()) {
		std::cout << "NoUPdate.txt loaded, skip entries: " << m_versionState.noUpdateFiles.size() << std::endl;
	}
}

bool LauncherUpdateCoordinator::IsRuntimeUpdateSkipped(const std::string& localPath) const {
	const std::string key = NormalizeNoUpdatePathKey(localPath);
	if (key.empty()) {
		return false;
	}
	return m_versionState.noUpdateFiles.find(key) != m_versionState.noUpdateFiles.end();
}

void LauncherUpdateCoordinator::RefreshRemoteManifestIfChanged()
{
	std::string strRemoteVersionDatMD5;
	{
		std::string strVersionDatMD5 = m_versionState.manifestPath + ".md5";
		const auto fetchMd5Body = [](httplib::Client& client, const std::string& requestPath) -> std::string {
			client.set_follow_location(true);
			client.set_connection_timeout(2, 0);
			client.set_read_timeout(3, 0);
			client.set_write_timeout(2, 0);
			auto res = client.Get(requestPath.c_str());
			if (res && res->status == 200) {
				return TrimAscii(res->body);
			}
			return {};
		};
		if (IsHttpUrl(strVersionDatMD5)) {
			std::string baseUrl;
			std::string path;
			if (ExtractBaseAndPath(strVersionDatMD5, baseUrl, path)) {
				httplib::Client clientExtract(baseUrl.c_str());
				strRemoteVersionDatMD5 = fetchMd5Body(clientExtract, path);
			}
		}
		else
		{
			httplib::Client client(m_networkState.url);
			strRemoteVersionDatMD5 = fetchMd5Body(client, NormalizeRelativeUrlPath(strVersionDatMD5));
		}
	}

	if (strRemoteVersionDatMD5.empty()) {
		std::cout << "Version.dat.md5 unavailable, fallback to direct Version.dat fetch." << std::endl;
		if (!RefreshRemoteVersionManifest()) {
			std::cout << "Direct Version.dat fetch fallback failed." << std::endl;
		}
		return;
	}

	if (m_versionState.localVersionMD5 != strRemoteVersionDatMD5) {
		RefreshRemoteVersionManifest();
	}
}

bool LauncherUpdateCoordinator::HandleSelfUpdateAndExit()
{
	if (!m_selfUpdateState.updateSelf) {
		return false;
	}

	const std::wstring args = BuildRelaunchArgs(_getpid(), m_selfUpdateState.modulePath, L"selfupdate");
	const HINSTANCE launchResult = ShellExecuteW(
		nullptr,
		L"open",
		L"UpdateTemp.exe",
		args.c_str(),
		m_selfUpdateState.moduleDir.c_str(),
		SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(launchResult) <= 32) {
		SetLauncherStatus(L"Failed: launcher self-update relaunch.");
		LogUpdateError(
			"UF-SELFUPDATE-LAUNCH",
			"LauncherUpdateCoordinator::HandleSelfUpdateAndExit",
			"Failed to relaunch UpdateTemp.exe",
			"shell_execute=UpdateTemp.exe");
		m_selfUpdateState.updateSelf = false;
		return false;
	}

	Stop();
	PostMessage(m_runtimeState.mainWnd, WM_DELETE_TRAY, 0, 0);
	ExitProcess(0);
	return true;
}

bool LauncherUpdateCoordinator::PublishMappingsAndLaunchInitialClient()
{
	// unsigned long long dwTick = GetTickCount64();
	// WriteDataToMapping(); 不写入映射了，直接让客户端从文件读取，减少一次内存拷贝和映射资源占用

	// unsigned long long dwNewTick = GetTickCount64();
	// std::cout << "WriteDataToMapping elapsed ms: " << dwNewTick - dwTick << std::endl;
	// dwTick = dwNewTick;

	if (!LaunchGameClient()) {
		return false;
	}

	// dwNewTick = GetTickCount64();
	// std::cout << "CreateProcess elapsed ms: " << dwNewTick - dwTick << std::endl;
	return true;
}

void LauncherUpdateCoordinator::MonitorClientsUntilShutdown()
{
	ULONGLONG nextManifestPollTick = BuildNextManifestPollTick(GetTickCount64());
	ULONGLONG nextDeferredRetryTick = GetTickCount64() + 1000ULL;
	do
	{
		CleanupExitedGameInfos();
		UpdateGameMainWindows();
		const bool bHaveGameRun = HasRunningGameProcess();
		if (bHaveGameRun == false)
		{
			PostMessage(m_runtimeState.mainWnd, WM_DELETE_TRAY, 0, 0);
			break;
		}

		const ULONGLONG nowTick = GetTickCount64();
		if (nowTick >= nextManifestPollTick) {
			if (m_launchFlowMutex.try_lock()) {
				RefreshRemoteManifestIfChanged();
				m_launchFlowMutex.unlock();
			}
			nextManifestPollTick = BuildNextManifestPollTick(nowTick);
		}

		if (nowTick >= nextDeferredRetryTick) {
			if (m_launchFlowMutex.try_lock()) {
				ProcessDeferredFileUpdates();
				m_launchFlowMutex.unlock();
			}
			nextDeferredRetryTick = nowTick + 1000ULL;
		}

		Sleep(15);
	}
	while (m_runtimeState.run);

	TerminateAllGameProcesses();
	CloseHandle(m_runtimeState.thread);
	m_runtimeState.thread = nullptr;
}

DWORD LauncherUpdateCoordinator::Run()
{
	workthread::runflow::RunCoordinator coordinator(*this);
	return coordinator.Execute();
}

// Download runtime files listed in the manifest.
bool LauncherUpdateCoordinator::DownloadRunTimeFile()
{
	workthread::runtimeupdate::RuntimeUpdater updater(*this);
	return updater.Execute();
}

// Download and extract the base package archive.
bool LauncherUpdateCoordinator::DownloadBasePackage()
{
	m_downloadState.totalDownload = 1;
	m_downloadState.currentDownload = 0;

	if (m_versionState.basePackageUrls.empty()) {
		std::cout << "Bootstrap config missing base_package_urls/base_package_url." << std::endl;
		return false;
	}

	bool downloaded = false;
	std::string downloadedArchivePath;
	workthread::resume::ResumeDownloader resumeDownloader(m_networkState, m_downloadState);
	for (const auto& packageUrl : m_versionState.basePackageUrls) {
		std::string absolutePackageUrl = packageUrl;
		if (!IsHttpUrl(absolutePackageUrl)) {
			absolutePackageUrl = m_networkState.url + NormalizeRelativeUrlPath(packageUrl);
		}
		std::string localArchivePath = GetFileNameFromUrl(absolutePackageUrl);
		if (localArchivePath.empty()) {
			localArchivePath = "base_package.7z";
		}
		const std::string p2pResourcePath = ResolveBasePackageP2PResourcePath(packageUrl, absolutePackageUrl);
		m_downloadState.currentDownloadProgress = 0;
		m_downloadState.currentDownloadSize = 0;

		if (!p2pResourcePath.empty()) {
			downloaded = resumeDownloader.TryP2P(
				p2pResourcePath,
				localArchivePath,
				[](uint64_t, uint64_t) {});
			if (downloaded) {
				if (!VerifyArchiveReadable(localArchivePath)) {
					std::cout << "P2P base package failed archive verification: " << localArchivePath << std::endl;
					LogUpdateInfo(
						"UF-P2P-FALLBACK",
						"LauncherUpdateCoordinator::DownloadBasePackage",
						"P2P base package verification failed; falling back to HTTP",
						std::string("resource=") + p2pResourcePath + ", file=" + localArchivePath);
					std::error_code ec;
					std::filesystem::remove(std::filesystem::u8path(localArchivePath), ec);
					downloaded = false;
				}
				else {
					LogUpdateInfo(
						"UF-P2P-DOWNLOAD",
						"LauncherUpdateCoordinator::DownloadBasePackage",
						"P2P base package download succeeded",
						std::string("resource=") + p2pResourcePath + ", file=" + localArchivePath);
					downloadedArchivePath = localArchivePath;
					break;
				}
			}
			else {
				LogUpdateInfo(
					"UF-P2P-FALLBACK",
					"LauncherUpdateCoordinator::DownloadBasePackage",
					"P2P base package attempt failed; falling back to HTTP",
					std::string("resource=") + p2pResourcePath + ", file=" + localArchivePath);
			}
		}

		downloaded = DownloadFileChunkedWithResume(absolutePackageUrl, localArchivePath, 2);
		if (downloaded) {
			if (!VerifyArchiveReadable(localArchivePath)) {
				std::cout << "HTTP base package failed archive verification: " << localArchivePath << std::endl;
				std::error_code ec;
				std::filesystem::remove(std::filesystem::u8path(localArchivePath), ec);
				downloaded = false;
			}
			else {
				downloadedArchivePath = localArchivePath;
				break;
			}
		}
	}

	if (!downloaded) {
		std::cout << "Base package download failed for all candidates." << std::endl;
		return false;
	}

	if (!Extract7z(downloadedArchivePath, "./")) {
		std::cout << "Base package extraction failed: " << downloadedArchivePath << std::endl;
		LogUpdateError(
			"UF-BASE-EXTRACT",
			"LauncherUpdateCoordinator::DownloadBasePackage",
			"Base package extraction failed",
			std::string("archive=") + downloadedArchivePath);
		return false;
	}
	MarkFileHidden(downloadedArchivePath);
	return true;
}


