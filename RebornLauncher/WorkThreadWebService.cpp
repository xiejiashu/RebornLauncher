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

} // namespace

void WorkThread::WebServiceThread()
{
	httplib::Server svr;
	svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
		g_bRendering = true;
		m_downloadState.totalDownload = 1;
		m_downloadState.currentDownload = 0;
		PostMessage(m_runtimeState.mainWnd, WM_DELETE_TRAY, 0, 0);
		// 计时
		DWORD dwTick = GetTickCount64();
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

		std::string keyPage = strPage;
		std::replace(keyPage.begin(), keyPage.end(), '/', '\\');
		auto it = m_versionState.files.find(keyPage);
		if (it == m_versionState.files.end()) {
			RefreshRemoteVersionManifest();
			it = m_versionState.files.find(keyPage);
		}
		if (it != m_versionState.files.end()) {
			const std::string strRemotePage = JoinUrlPath(
				m_networkState.page, std::to_string(it->second.m_qwTime) + "/" + strPage);
			m_downloadState.currentDownloadSize = static_cast<int>(it->second.m_qwSize);
			m_downloadState.currentDownloadProgress = 0;
			MarkClientDownloadProgress(requestPid, 0, static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
			if (DownloadWithResume(strRemotePage, strPage, requestPid)) {
				m_downloadState.currentDownload = 1;
				MarkClientDownloadFinished(requestPid);
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else {
				MarkClientDownloadFinished(requestPid);
				res.status = 502;
				res.set_content("Download Failed", "text/plain");
			}
		}
		else {
			MarkClientDownloadFinished(requestPid);
			res.status = 404;
			res.set_content("Not Found", "text/plain");
		}

		// 下载完把游戏窗口放到最前面
		SetForegroundWindow(FindGameWindowByProcessId(m_runtimeState.gameInfos, requestPid));
	});

	svr.Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {
		std::cout << "RunClient request" << std::endl;
		RefreshRemoteVersionManifest();
		if (!DownloadRunTimeFile()) {
			res.status = 502;
			res.set_content("Update download failed.", "text/plain");
			return;
		}

		CleanupExitedGameInfos();
		if (!LaunchGameClient()) {
			res.status = 500;
			res.set_content("Client launch failed.", "text/plain");
			return;
		}

		res.status = 200;
		res.set_content("OK", "text/plain");
	});

	svr.Get("/Stop", [&svr, this](const httplib::Request& req, httplib::Response& res) {
		TerminateAllGameProcesses();
		res.status = 200;
		res.set_content("OK", "text/plain");
		svr.stop();
	});

	svr.listen("localhost", 12345);
	std::cout << "Web service thread finished" << std::endl;
}
