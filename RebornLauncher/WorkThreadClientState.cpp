#include "framework.h"
#include "WorkThread.h"

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

HWND WorkThread::FindGameWindowByProcessId(std::vector<std::shared_ptr<tagGameInfo>>& gameInfos, DWORD processId)
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

int WorkThread::GetTotalDownload() const
{
	return m_downloadState.totalDownload;
}

int WorkThread::GetCurrentDownload() const
{
	return m_downloadState.currentDownload;
}

std::wstring WorkThread::GetCurrentDownloadFile()
{
	std::lock_guard<std::mutex> lock(m_downloadState.mutex);
	return m_downloadState.currentFile;
}

void WorkThread::SetCurrentDownloadFile(const std::wstring& strFile)
{
	std::lock_guard<std::mutex> lock(m_downloadState.mutex);
	m_downloadState.currentFile = strFile;
}

int WorkThread::GetCurrentDownloadSize() const
{
	return m_downloadState.currentDownloadSize;
}

int WorkThread::GetCurrentDownloadProgress() const
{
	return m_downloadState.currentDownloadProgress;
}

std::vector<tagGameInfo> WorkThread::GetGameInfosSnapshot() const
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

void WorkThread::MarkClientDownloadStart(DWORD processId, const std::wstring& fileName)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [&](tagGameInfo& info) {
		info.downloading = true;
		info.downloadFile = fileName;
		info.downloadDoneBytes = 0;
		info.downloadTotalBytes = 0;
	});
}

void WorkThread::MarkClientDownloadProgress(DWORD processId, uint64_t downloaded, uint64_t total)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [&](tagGameInfo& info) {
		info.downloading = true;
		info.downloadDoneBytes = downloaded;
		info.downloadTotalBytes = total;
	});
}

void WorkThread::MarkClientDownloadFinished(DWORD processId)
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	UpdateClientDownloadState(m_runtimeState.gameInfos, processId, [](tagGameInfo& info) {
		info.downloading = false;
		info.downloadDoneBytes = 0;
		info.downloadTotalBytes = 0;
		info.downloadFile.clear();
	});
}

bool WorkThread::LaunchGameClient()
{
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi{};

	char currentDir[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, currentDir);

	char exePathBuf[MAX_PATH] = { 0 };
	std::string exePathStr = wstr2str(m_szProcessName);
	strncpy_s(exePathBuf, exePathStr.c_str(), MAX_PATH - 1);
	if (!CreateProcessA(NULL, exePathBuf, NULL, NULL, FALSE, 0, NULL, currentDir, &si, &pi)) {
		std::cerr << "CreateProcess failed, error: " << GetLastError() << std::endl;
		return false;
	}

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
	return true;
}

void WorkThread::CleanupExitedGameInfos()
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

bool WorkThread::HasRunningGameProcess()
{
	std::lock_guard<std::mutex> lock(m_runtimeState.gameInfosMutex);
	for (const auto& info : m_runtimeState.gameInfos) {
		if (info && info->dwProcessId != 0 && IsProcessRunning(info->dwProcessId)) {
			return true;
		}
	}
	return false;
}

void WorkThread::TerminateAllGameProcesses()
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

void WorkThread::Stop()
{
	httplib::Client cli("localhost", 12345);
	cli.Get("/Stop");
	m_runtimeState.run = FALSE;
	if (m_networkState.client) {
		m_networkState.client->stop();
		m_networkState.client.reset();
	}
}

void WorkThread::UpdateP2PSettings(const P2PSettings& settings)
{
	std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
	m_networkState.p2pSettings = settings;
}

P2PSettings WorkThread::GetP2PSettings() const
{
	std::lock_guard<std::mutex> lock(m_networkState.p2pMutex);
	return m_networkState.p2pSettings;
}
