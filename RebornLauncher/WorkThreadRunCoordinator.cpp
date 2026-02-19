#include "framework.h"
#include "WorkThreadRunCoordinator.h"

#include "WorkThread.h"

namespace workthread::runflow {

WorkThreadRunCoordinator::WorkThreadRunCoordinator(WorkThread& worker)
	: m_worker(worker) {
}

DWORD WorkThreadRunCoordinator::Execute() {
	m_worker.SetLauncherStatus(L"Initializing update environment...");
	if (!m_worker.InitializeDownloadEnvironment()) {
		m_worker.SetLauncherStatus(L"Failed: initialize update environment.");
		m_worker.LogUpdateError(
			"UF-INIT-001",
			"WorkThreadRunCoordinator::Execute",
			"InitializeDownloadEnvironment failed",
			"Bootstrap config fetch or network initialization failed.");
		FailWithMessage(L"Failed to fetch bootstrap config.");
		return 0;
	}

	m_worker.SetLauncherStatus(L"Checking base package...");
	if (!m_worker.EnsureBasePackageReady()) {
		m_worker.SetLauncherStatus(L"Failed: base package is unavailable.");
		m_worker.LogUpdateError(
			"UF-BASE-001",
			"WorkThreadRunCoordinator::Execute",
			"EnsureBasePackageReady failed",
			"Base package missing and download/extract failed.");
		FailWithMessage(L"Failed to download base package.");
		return 0;
	}

	m_worker.SetLauncherStatus(L"Loading local version state...");
	m_worker.LoadLocalVersionState();
	{
		std::lock_guard<std::mutex> flowGuard(m_worker.m_launchFlowMutex);
		m_worker.SetLauncherStatus(L"Refreshing remote manifest...");
		if (!m_worker.RefreshRemoteVersionManifest()) {
			m_worker.SetLauncherStatus(L"Failed: remote manifest refresh.");
			m_worker.LogUpdateError(
				"UF-MANIFEST-001",
				"WorkThreadRunCoordinator::Execute",
				"RefreshRemoteVersionManifest failed",
				"Cannot continue update because manifest refresh did not succeed.");
			FailWithMessage(L"Failed to refresh update manifest.");
			return 0;
		}

		m_worker.SetLauncherStatus(L"Applying runtime updates...");
		if (!m_worker.DownloadRunTimeFile()) {
			m_worker.SetLauncherStatus(L"Failed: runtime update download.");
			m_worker.LogUpdateError(
				"UF-RUNTIME-001",
				"WorkThreadRunCoordinator::Execute",
				"DownloadRunTimeFile failed",
				"Runtime update list execution failed.");
			FailWithMessage(L"Failed to download update files.");
			return 0;
		}

		if (m_worker.HandleSelfUpdateAndExit()) {
			return 0;
		}

		m_worker.SetLauncherStatus(L"Launching game client...");
		if (!m_worker.PublishMappingsAndLaunchInitialClient()) {
			m_worker.SetLauncherStatus(L"Failed: game client launch.");
			m_worker.LogUpdateError(
				"UF-LAUNCH-001",
				"WorkThreadRunCoordinator::Execute",
				"PublishMappingsAndLaunchInitialClient failed",
				"Initial game client launch failed after update.");
			FailWithMessage(L"Failed to launch game client.");
			return 0;
		}
	}

	m_worker.SetLauncherStatus(L"Game is running.");
	m_worker.MonitorClientsUntilShutdown();
	return 0;
}

bool WorkThreadRunCoordinator::FailWithMessage(const wchar_t* message) const {
	MessageBox(m_worker.m_runtimeState.mainWnd, message, L"Error", MB_OK);
	m_worker.Stop();
	return false;
}

} // namespace workthread::runflow
