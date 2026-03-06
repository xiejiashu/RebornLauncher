#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <algorithm>
#include <iostream>

#include <httplib.h>

#include "Encoding.h"

extern bool IsProcessRunning(DWORD dwProcessId);

namespace {

template <typename Updater>
void UpdateClientDownloadState(std::vector<std::shared_ptr<tagGameInfo>>& gameInfos, DWORD processId, Updater&& updater)
{
	bool updated = false;
	for (const auto& info : gameInfos) {
		if (!info) {
			continue;
		}
		if (processId != 0 && info->dwProcessId != processId) {
			continue;
		}
		updater(*info);
		updated = true;
		if (processId != 0) {
			break;
		}
	}

	if (updated || processId == 0) {
		return;
	}

	for (const auto& info : gameInfos) {
		if (!info || info->dwProcessId == 0) {
			continue;
		}
		updater(*info);
	}
}
} // namespace

HWND LauncherUpdateCoordinator::FindGameWindowByProcessId(std::vector<std::shared_ptr<tagGameInfo>>& gameInfos, DWORD processId)
{
	HWND hWnd = nullptr;
	for (const auto& info : gameInfos) {
		if (info && info->dwProcessId == processId) {
			hWnd = info->hMainWnd;
			break;
		}
	}
	return hWnd;
}

int LauncherUpdateCoordinator::GetTotalDownload() const
{
	return m_downloadState.totalDownload;
}

int LauncherUpdateCoordinator::GetCurrentDownload() const
{
	return m_downloadState.currentDownload;
}

std::wstring LauncherUpdateCoordinator::GetCurrentDownloadFile()
{
	std::lock_guard<std::mutex> lock(m_downloadState.mutex);
	return m_downloadState.currentFile;
}

void LauncherUpdateCoordinator::SetCurrentDownloadFile(const std::wstring& strFile)
{
	std::lock_guard<std::mutex> lock(m_downloadState.mutex);
	m_downloadState.currentFile = strFile;
}

int LauncherUpdateCoordinator::GetCurrentDownloadSize() const
{
	return m_downloadState.currentDownloadSize;
}

int LauncherUpdateCoordinator::GetCurrentDownloadProgress() const
{
	return m_downloadState.currentDownloadProgress;
}

std::vector<tagGameInfo> LauncherUpdateCoordinator::GetGameInfosSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	std::vector<tagGameInfo> snapshot;
	snapshot.reserve(m_runtimeState.gameInfos.size());
	for (const auto& info : m_runtimeState.gameInfos) {
		if (!info) {
			continue;
		}
		snapshot.push_back(*info);
	}
	return snapshot;
}

void LauncherUpdateCoordinator::MarkClientDownloadStart(DWORD processId, const std::wstring& fileName)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [&](tagGameInfo& info) {
		info.downloading = true;
		info.downloadFile = fileName;
		info.downloadDoneBytes = 0;
		info.downloadTotalBytes = 0;
	});
}

void LauncherUpdateCoordinator::MarkClientDownloadProgress(DWORD processId, uint64_t downloaded, uint64_t total)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [&](tagGameInfo& info) {
		info.downloading = true;
		info.downloadDoneBytes = downloaded;
		info.downloadTotalBytes = total;
	});
}

void LauncherUpdateCoordinator::MarkClientDownloadFinished(DWORD processId)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [](tagGameInfo& info) {
		info.downloading = false;
		info.downloadDoneBytes = 0;
		info.downloadTotalBytes = 0;
		info.downloadFile.clear();
	});
}

bool LauncherUpdateCoordinator::LaunchGameClient()
{
	SetLauncherStatus(L"Launching game client...");
	const auto notifyLaunchFailureUi = [this]() {
		if (!m_runtimeState.mainWnd) {
			return;
		}
		PostMessage(m_runtimeState.mainWnd, WM_SHOW_FOR_DOWNLOAD, 0, 0);
		ShowWindow(m_runtimeState.mainWnd, SW_SHOWNORMAL);
		SetWindowPos(
			m_runtimeState.mainWnd,
			HWND_TOPMOST,
			0,
			0,
			0,
			0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow(m_runtimeState.mainWnd);
	};
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi{};

	char currentDir[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, currentDir);

	std::string exePathStr = std::string(currentDir) + "\\" + wstr2str(m_szProcessName);
	// Log resolved executable path for diagnostics.
	if (!CreateProcessA(NULL, &exePathStr[0], NULL, NULL, FALSE, 0, NULL, currentDir, &si, &pi)) {
		const DWORD lastError = GetLastError();
		std::cerr << "CreateProcess failed, error: " << lastError << std::endl;
		SetLauncherStatus(L"Failed: game launch process creation.");
		LogUpdateError(
			"UF-LAUNCH-CREATEPROCESS",
			"LauncherUpdateCoordinator::LaunchGameClient",
			"CreateProcess failed",
			std::string("exe_path=") + exePathStr,
			lastError);
		notifyLaunchFailureUi();
		return false;
	} 
	LogUpdateError(
		"UF-LAUNCH-SUCCESS",
		"LauncherUpdateCoordinator::LaunchGameClient",
		"Game client launched successfully",
		std::string("exe_path=") + exePathStr + ", pid=" + std::to_string(pi.dwProcessId));

	if (pi.hThread) {
		CloseHandle(pi.hThread);
	}

	auto gameInfo = std::make_shared<tagGameInfo>();
	gameInfo->hProcess = pi.hProcess;
	gameInfo->hMainWnd = nullptr;
	gameInfo->dwProcessId = pi.dwProcessId;

	{
		std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
		m_runtimeState.gameInfos.push_back(gameInfo);
	}

	std::cout << "Client launched, pid=" << pi.dwProcessId << std::endl;
	SetLauncherStatus(L"Game client launched.");
	const DWORD quickExitCheck = WaitForSingleObject(pi.hProcess, 100);
	if (quickExitCheck == WAIT_OBJECT_0) {
		DWORD exitCode = 0;
		if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
			std::cerr << "Client process exited quickly, pid=" << pi.dwProcessId
				<< ", exit code=" << exitCode << std::endl;
			SetLauncherStatus(L"Warning: game exited shortly after launch.");
			LogUpdateError(
				"UF-LAUNCH-QUICKEXIT",
				"LauncherUpdateCoordinator::LaunchGameClient",
				"Client exited shortly after launch",
				std::string("pid=") + std::to_string(pi.dwProcessId) + ", exit_code=" + std::to_string(exitCode));
		}
		else {
			std::cerr << "Client process exited quickly, pid=" << pi.dwProcessId
				<< ", exit code=<unknown>" << std::endl;
			SetLauncherStatus(L"Warning: game exited shortly after launch.");
			LogUpdateError(
				"UF-LAUNCH-QUICKEXIT",
				"LauncherUpdateCoordinator::LaunchGameClient",
				"Client exited shortly after launch",
				std::string("pid=") + std::to_string(pi.dwProcessId) + ", exit_code=<unknown>",
				GetLastError());
		}
		notifyLaunchFailureUi();
	}
	return true;
}

void LauncherUpdateCoordinator::CleanupExitedGameInfos()
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	m_runtimeState.gameInfos.erase(std::remove_if(m_runtimeState.gameInfos.begin(), m_runtimeState.gameInfos.end(),
		[](const std::shared_ptr<tagGameInfo>& info) {
			if (!info) {
				return true;
			}
			if (info->dwProcessId != 0 && IsProcessRunning(info->dwProcessId)) {
				return false;
			}
			if (info->hProcess) {
				CloseHandle(info->hProcess);
				info->hProcess = nullptr;
			}
			info->dwProcessId = 0;
			return true;
		}),
		m_runtimeState.gameInfos.end());
}

bool LauncherUpdateCoordinator::HasRunningGameProcess()
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	for (const auto& info : m_runtimeState.gameInfos) {
		if (info && info->dwProcessId != 0 && IsProcessRunning(info->dwProcessId)) {
			return true;
		}
	}
	return false;
}

void LauncherUpdateCoordinator::TerminateAllGameProcesses()
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	for (const auto& info : m_runtimeState.gameInfos) {
		if (!info) {
			continue;
		}
		if (info->hProcess) {
			TerminateProcess(info->hProcess, 0);
			CloseHandle(info->hProcess);
			info->hProcess = nullptr;
		}
		else if (info->dwProcessId != 0) {
			HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, info->dwProcessId);
			if (hProcess) {
				TerminateProcess(hProcess, 0);
				CloseHandle(hProcess);
			}
		}
		info->dwProcessId = 0;
	}
	m_runtimeState.gameInfos.clear();
}

void LauncherUpdateCoordinator::Stop()
{
	SetLauncherStatus(L"Stopping launcher services...");
	httplib::Client cli("localhost", 12345);
	cli.Get("/Stop");
	m_runtimeState.run = FALSE;
	if (m_networkState.client) {
		m_networkState.client->stop();
		m_networkState.client.reset();
	}
}

void LauncherUpdateCoordinator::UpdateP2PSettings(const P2PSettings& settings)
{
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
		const P2PSettings& previous = m_networkState.p2pSettings;
		changed =
			previous.enabled != settings.enabled ||
			previous.signalEndpoint != settings.signalEndpoint ||
			previous.signalAuthToken != settings.signalAuthToken ||
			previous.stunServers != settings.stunServers;
		m_networkState.p2pSettings = settings;
	}

	if (!changed) {
		return;
	}

	std::string details =
		"enabled=" + std::string(settings.enabled ? "true" : "false") +
		", signal_endpoint=" + (settings.signalEndpoint.empty() ? std::string("<empty>") : settings.signalEndpoint) +
		", stun_count=" + std::to_string(settings.stunServers.size()) +
		", auth_token=" + (settings.signalAuthToken.empty() ? std::string("<empty>") : std::string("<set>"));
	LogUpdateInfo(
		"UF-P2P-CONFIG",
		"LauncherUpdateCoordinator::UpdateP2PSettings",
		settings.enabled ? "P2P configuration enabled" : "P2P configuration disabled",
		details);
}

P2PSettings LauncherUpdateCoordinator::GetP2PSettings() const
{
	std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
	return m_networkState.p2pSettings;
}
