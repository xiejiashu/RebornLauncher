#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <iostream>

#include <httplib.h>

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

} // namespace

void WorkThread::WebServiceThread()
{
	httplib::Server svr;
	svr.Get("/download", [this](const httplib::Request& req, httplib::Response& res) {
		g_bRendering = true;
		m_nTotalDownload = 1;
		m_nCurrentDownload = 0;
		PostMessage(m_hMainWnd, WM_DELETE_TRAY, 0, 0);
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
		auto it = m_mapFiles.find(keyPage);
		if (it == m_mapFiles.end()) {
			RefreshRemoteVersionManifest();
			it = m_mapFiles.find(keyPage);
		}
		if (it != m_mapFiles.end()) {
			const std::string strRemotePage = JoinUrlPath(
				m_strPage, std::to_string(it->second.m_qwTime) + "/" + strPage);
			m_nCurrentDownloadSize = static_cast<int>(it->second.m_qwSize);
			m_nCurrentDownloadProgress = 0;
			MarkClientDownloadProgress(requestPid, 0, static_cast<uint64_t>((std::max)(0, m_nCurrentDownloadSize)));
			if (DownloadWithResume(strRemotePage, strPage, requestPid)) {
				m_nCurrentDownload = 1;
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
