#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <iostream>

#include <httplib.h>

#include "Encoding.h"
#include "WorkThreadNetUtils.h"

extern bool g_bRendering;

namespace {

using workthread::netutils::JoinUrlPath;

std::map<std::string, VersionConfig>::iterator ResolveFileConfigByPage(
	std::map<std::string, VersionConfig>& files,
	const std::string& requestedPage) {
	auto it = files.find(requestedPage);
	if (it != files.end()) {
		return it;
	}

	std::string slashToBack = requestedPage;
	std::replace(slashToBack.begin(), slashToBack.end(), '/', '\\');
	it = files.find(slashToBack);
	if (it != files.end()) {
		return it;
	}

	std::string backToSlash = requestedPage;
	std::replace(backToSlash.begin(), backToSlash.end(), '\\', '/');
	it = files.find(backToSlash);
	if (it != files.end()) {
		return it;
	}

	return files.end();
}

} // namespace

void WorkThread::WebServiceThread()
{
	while (m_runtimeState.run) {
		httplib::Server svr;
		svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
			std::lock_guard<std::mutex> flowGuard(m_launchFlowMutex);
			SetLauncherStatus(L"Received file download request from client...");
			g_bRendering = true;
			m_downloadState.totalDownload = 1;
			m_downloadState.currentDownload = 0;
			PostMessage(m_runtimeState.mainWnd, WM_DELETE_TRAY, 0, 0);
			std::string strPage = req.get_param_value("page");
			const std::wstring pageW = str2wstr(strPage, static_cast<int>(strPage.length()));
			SetCurrentDownloadFile(pageW);
			DWORD requestPid = 0;
			if (req.has_param("pid")) {
				try {
					requestPid = static_cast<DWORD>(std::stoul(req.get_param_value("pid")));
				}
				catch (...) {
					requestPid = 0;
				}
			}
			MarkClientDownloadStart(requestPid, pageW);

			auto it = ResolveFileConfigByPage(m_versionState.files, strPage);
			if (it == m_versionState.files.end()) {
				RefreshRemoteVersionManifest();
				it = ResolveFileConfigByPage(m_versionState.files, strPage);
			}
			if (it != m_versionState.files.end()) {
				const std::string resolvedPage = it->first;
				const std::string strRemotePage = JoinUrlPath(
					m_networkState.page, std::to_string(it->second.m_qwTime) + "/" + resolvedPage);
				m_downloadState.currentDownloadSize = static_cast<int>(it->second.m_qwSize);
				m_downloadState.currentDownloadProgress = 0;
				MarkClientDownloadProgress(requestPid, 0, static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
				if (DownloadWithResume(strRemotePage, resolvedPage, requestPid)) {
					m_downloadState.currentDownload = 1;
					SetLauncherStatus(L"Client file request completed.");
					MarkClientDownloadFinished(requestPid);
					res.status = 200;
					res.set_content("OK", "text/plain");
				}
				else {
					SetLauncherStatus(L"Failed: client file request download.");
					MarkClientDownloadFinished(requestPid);
					res.status = 502;
					res.set_content("Download Failed", "text/plain");
					LogUpdateError(
						"UF-WS-DOWNLOAD",
						"WorkThread::WebServiceThread:/download",
						"Client file download failed",
						std::string("page=") + resolvedPage + ", pid=" + std::to_string(requestPid));
				}
			}
			else {
				SetLauncherStatus(L"Failed: requested client file not found.");
				MarkClientDownloadFinished(requestPid);
				res.status = 404;
				res.set_content("Not Found", "text/plain");
				LogUpdateError(
					"UF-WS-NOTFOUND",
					"WorkThread::WebServiceThread:/download",
					"Requested file not found in manifest",
					std::string("page=") + strPage + ", pid=" + std::to_string(requestPid));
			}

			// Bring the request owner's game window to foreground after download.
			SetForegroundWindow(FindGameWindowByProcessId(m_runtimeState.gameInfos, requestPid));
		});

		svr.Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {
			(void)req;
			std::cout << "RunClient request" << std::endl;
			std::lock_guard<std::mutex> flowGuard(m_launchFlowMutex);
			SetLauncherStatus(L"RunClient requested: checking updates...");
			if (!RefreshRemoteVersionManifest()) {
				SetLauncherStatus(L"Failed: RunClient manifest refresh.");
				res.status = 502;
				res.set_content("Manifest refresh failed.", "text/plain");
				LogUpdateError(
					"UF-WS-MANIFEST",
					"WorkThread::WebServiceThread:/RunClient",
					"Manifest refresh failed during RunClient request",
					"RunClient rejected before launch.");
				return;
			}
			if (!DownloadRunTimeFile()) {
				SetLauncherStatus(L"Failed: RunClient update download.");
				res.status = 502;
				res.set_content("Update download failed.", "text/plain");
				LogUpdateError(
					"UF-WS-RUNTIME",
					"WorkThread::WebServiceThread:/RunClient",
					"Runtime update failed during RunClient request",
					"RunClient rejected before launch.");
				return;
			}

			CleanupExitedGameInfos();
			SetLauncherStatus(L"RunClient: launching game...");
			if (!LaunchGameClient()) {
				SetLauncherStatus(L"Failed: RunClient launch.");
				res.status = 500;
				res.set_content("Client launch failed.", "text/plain");
				LogUpdateError(
					"UF-WS-LAUNCH",
					"WorkThread::WebServiceThread:/RunClient",
					"Game client launch failed after update",
					"RunClient request finished with launch failure.");
				return;
			}

			res.status = 200;
			res.set_content("OK", "text/plain");
			SetLauncherStatus(L"RunClient launch succeeded.");
		});

		svr.Get("/Stop", [&svr, this](const httplib::Request& req, httplib::Response& res) {
			(void)req;
			TerminateAllGameProcesses();
			res.status = 200;
			res.set_content("OK", "text/plain");
			svr.stop();
		});

		const bool listening = svr.listen("localhost", 12345);
		if (!m_runtimeState.run) {
			break;
		}
		if (!listening) {
			std::cout << "Web service listen failed, retrying..." << std::endl;
			SetLauncherStatus(L"Retrying local HTTP service...");
			LogUpdateError(
				"UF-WS-LISTEN",
				"WorkThread::WebServiceThread",
				"HTTP web service listen failed",
				"listen(localhost:12345) returned false.");
		}
		else {
			std::cout << "Web service stopped unexpectedly, restarting..." << std::endl;
			SetLauncherStatus(L"Restarting local HTTP service...");
			LogUpdateError(
				"UF-WS-RESTART",
				"WorkThread::WebServiceThread",
				"HTTP web service stopped unexpectedly",
				"Server loop exited while run flag is true; restarting listener.");
		}
		Sleep(400);
	}
	std::cout << "Web service thread finished" << std::endl;
}
