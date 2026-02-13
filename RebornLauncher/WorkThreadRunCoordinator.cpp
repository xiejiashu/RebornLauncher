#include "framework.h"
#include "WorkThreadRunCoordinator.h"

#include "WorkThread.h"

namespace workthread::runflow {

WorkThreadRunCoordinator::WorkThreadRunCoordinator(WorkThread& worker)
	: m_worker(worker) {
}

DWORD WorkThreadRunCoordinator::Execute() {
	if (!m_worker.InitializeDownloadEnvironment()) {
		FailWithMessage(L"Failed to fetch bootstrap config.");
		return 0;
	}

	if (!m_worker.EnsureBasePackageReady()) {
		FailWithMessage(L"Failed to download base package.");
		return 0;
	}

	m_worker.LoadLocalVersionState();
	m_worker.RefreshRemoteManifestIfChanged();

	if (!m_worker.DownloadRunTimeFile()) {
		FailWithMessage(L"Failed to download update files.");
		return 0;
	}

	if (m_worker.HandleSelfUpdateAndExit()) {
		return 0;
	}

	if (!m_worker.PublishMappingsAndLaunchInitialClient()) {
		FailWithMessage(L"Failed to launch game client.");
		return 0;
	}

	m_worker.MonitorClientsUntilShutdown();
	return 0;
}

bool WorkThreadRunCoordinator::FailWithMessage(const wchar_t* message) const {
	MessageBox(m_worker.m_runtimeState.mainWnd, message, L"Error", MB_OK);
	m_worker.Stop();
	return false;
}

} // namespace workthread::runflow
