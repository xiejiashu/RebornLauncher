#include "framework.h"
#include "WorkThread.h"

#include <filesystem>
#include <httplib.h>
#include <iostream>
#include <shellapi.h>
#include <thread>

#include "WorkThreadLocalVersionLoader.h"
#include "WorkThreadNetUtils.h"
#include "WorkThreadResumeDownload.h"
#include "WorkThreadRuntimeUpdater.h"
#include "WorkThreadRunCoordinator.h"

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

} // namespace


// Initialize the worker thread and background web service.
WorkThread::WorkThread(HWND hWnd, const std::wstring& strModulePath, const std::wstring& strModuleName, const std::wstring& strModuleDir, const P2PSettings& initialP2PSettings)
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
	WorkThread* pThis = (WorkThread*)lpParameter;
	if (pThis) {
		return pThis->Run();
	}
	return 0;
}

bool WorkThread::InitializeDownloadEnvironment()
{
	if (!FetchBootstrapConfig()) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
		if (m_networkState.p2pSettings.signalEndpoint.empty()) {
			m_networkState.p2pSettings.signalEndpoint = BuildSignalEndpoint(m_networkState.url, m_networkState.page);
		}
		if (m_networkState.p2pClient) {
			m_networkState.p2pClient->UpdateSettings(m_networkState.p2pSettings);
		}
	}

	m_networkState.client = std::make_unique<httplib::Client>(m_networkState.url);
	m_networkState.client->set_follow_location(true);
	m_networkState.client->set_connection_timeout(8, 0);
	m_networkState.client->set_read_timeout(30, 0);
	m_networkState.client->set_write_timeout(15, 0);
	return true;
}

bool WorkThread::EnsureBasePackageReady()
{
	if (!std::filesystem::exists("./Data"))
	{
		if (!DownloadBasePackage()) {
			return false;
		}
	}
	return true;
}

void WorkThread::LoadLocalVersionState()
{
	workthread::versionload::WorkThreadLocalVersionLoader loader(*this);
	loader.Execute();
}

void WorkThread::RefreshRemoteManifestIfChanged()
{
	std::string strRemoteVersionDatMD5;
	{
		std::string strVersionDatMD5 = m_versionState.manifestPath + ".md5";
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
			httplib::Client client(m_networkState.url);
			auto res = client.Get("/" + strVersionDatMD5);
			if (res && res->status == 200) {
				strRemoteVersionDatMD5 = TrimAscii(res->body);
			}
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

bool WorkThread::HandleSelfUpdateAndExit()
{
	if (!m_selfUpdateState.updateSelf) {
		return false;
	}

	Stop();
	const std::wstring args = BuildRelaunchArgs(_getpid(), m_selfUpdateState.modulePath, L"selfupdate");
	ShellExecuteW(nullptr, L"open", L"UpdateTemp.exe", args.c_str(), m_selfUpdateState.moduleDir.c_str(), SW_SHOWNORMAL);
	PostMessage(m_runtimeState.mainWnd, WM_DELETE_TRAY, 0, 0);
	ExitProcess(0);
	return true;
}

bool WorkThread::PublishMappingsAndLaunchInitialClient()
{
	unsigned long long dwTick = GetTickCount64();
	WriteDataToMapping();

	unsigned long long dwNewTick = GetTickCount64();
	std::cout << "WriteDataToMapping elapsed ms: " << dwNewTick - dwTick << std::endl;
	dwTick = dwNewTick;

	if (!LaunchGameClient()) {
		return false;
	}

	dwNewTick = GetTickCount64();
	std::cout << "CreateProcess elapsed ms: " << dwNewTick - dwTick << std::endl;
	return true;
}

void WorkThread::MonitorClientsUntilShutdown()
{
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

		Sleep(1);
	}
	while (m_runtimeState.run);

	TerminateAllGameProcesses();
	CloseHandle(m_runtimeState.thread);
	m_runtimeState.thread = nullptr;
}

DWORD WorkThread::Run()
{
	workthread::runflow::WorkThreadRunCoordinator coordinator(*this);
	return coordinator.Execute();
}

// Download runtime files listed in the manifest.
bool WorkThread::DownloadRunTimeFile()
{
	workthread::runtimeupdate::WorkThreadRuntimeUpdater updater(*this);
	return updater.Execute();
}

// Download and extract the base package archive.
bool WorkThread::DownloadBasePackage()
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
				downloadedArchivePath = localArchivePath;
				break;
			}
		}

		downloaded = DownloadFileChunkedWithResume(absolutePackageUrl, localArchivePath, 2);
		if (downloaded) {
			downloadedArchivePath = localArchivePath;
			break;
		}
	}

	if (!downloaded) {
		std::cout << "Base package download failed for all candidates." << std::endl;
		return false;
	}

	Extract7z(downloadedArchivePath, "./");
	MarkFileHidden(downloadedArchivePath);
	return true;
}


