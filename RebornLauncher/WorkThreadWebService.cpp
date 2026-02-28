#include "framework.h"
#include "WorkThread.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <thread>
#include <unordered_set>

#include <httplib.h>

#include "Encoding.h"
#include "WorkThreadNetUtils.h"

extern bool g_bRendering;

namespace {

using workthread::netutils::JoinUrlPath;

struct DownloadRequestOutcome {
	int status = 500;
	std::string body = "Download Failed";
	bool success = false;
	std::string resolvedPage;
};

struct AsyncDownloadTask {
	std::string page;
	DWORD pid = 0;
};

std::string ToLowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		if (ch >= 'A' && ch <= 'Z') {
			return static_cast<char>(ch - 'A' + 'a');
		}
		return static_cast<char>(ch);
	});
	return value;
}

std::string NormalizeAsyncQueueKey(std::string page) {
	std::replace(page.begin(), page.end(), '\\', '/');
	return ToLowerAscii(page);
}

bool IsTruthyParam(const std::string& value) {
	const std::string lowered = ToLowerAscii(value);
	return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

bool IsAsyncDownloadRequest(const httplib::Request& req) {
	if (req.has_param("mode")) {
		const std::string mode = ToLowerAscii(req.get_param_value("mode"));
		if (mode == "async" || mode == "queue") {
			return true;
		}
	}
	if (req.has_param("async")) {
		return IsTruthyParam(req.get_param_value("async"));
	}
	return false;
}

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
	std::mutex asyncQueueMutex;
	std::condition_variable asyncQueueCv;
	std::deque<AsyncDownloadTask> asyncQueue;
	std::unordered_set<std::string> asyncQueuedPages;
	bool asyncWorkerStop = false;

	auto executeClientDownload = [this](
		const std::string& strPage,
		DWORD requestPid,
		bool isAsync) -> DownloadRequestOutcome {
		DownloadRequestOutcome outcome;
		std::lock_guard<std::mutex> flowGuard(m_launchFlowMutex);
		const std::wstring pageW = str2wstr(strPage, static_cast<int>(strPage.length()));
		SetCurrentDownloadFile(pageW);
		MarkClientDownloadStart(requestPid, pageW);

		if (isAsync) {
			SetLauncherStatus(L"Processing queued async file update...");
		}
		else {
			SetLauncherStatus(L"Received file download request from client...");
			g_bRendering = true;
			m_downloadState.totalDownload = 1;
			m_downloadState.currentDownload = 0;
		}

		auto it = ResolveFileConfigByPage(m_versionState.files, strPage);
		if (it == m_versionState.files.end()) {
			RefreshRemoteVersionManifest();
			it = ResolveFileConfigByPage(m_versionState.files, strPage);
		}
		if (it == m_versionState.files.end()) {
			SetLauncherStatus(L"Failed: requested client file not found.");
			MarkClientDownloadFinished(requestPid);
			outcome.status = 404;
			outcome.body = "Not Found";
			LogUpdateError(
				"UF-WS-NOTFOUND",
				isAsync ? "WorkThread::WebServiceThread:/download-async"
				        : "WorkThread::WebServiceThread:/download",
				"Requested file not found in manifest",
				std::string("page=") + strPage + ", pid=" + std::to_string(requestPid));
			return outcome;
		}

		const std::string resolvedPage = it->first;
		const std::string strRemotePage = JoinUrlPath(
			m_networkState.page, std::to_string(it->second.m_qwTime) + "/" + resolvedPage);
		m_downloadState.currentDownloadSize = static_cast<int>(it->second.m_qwSize);
		m_downloadState.currentDownloadProgress = 0;
		MarkClientDownloadProgress(
			requestPid,
			0,
			static_cast<uint64_t>((std::max)(0, m_downloadState.currentDownloadSize)));
		if (!isAsync) {
			PostMessage(m_runtimeState.mainWnd, WM_SHOW_FOR_DOWNLOAD, 0, 0);
		}

		if (DownloadWithResume(strRemotePage, resolvedPage, requestPid)) {
			m_downloadState.currentDownload = 1;
			SetLauncherStatus(isAsync
				? L"Queued async file update completed."
				: L"Client file request completed.");
			MarkClientDownloadFinished(requestPid);
			outcome.status = 200;
			outcome.body = "OK";
			outcome.success = true;
			outcome.resolvedPage = resolvedPage;
			if (!isAsync) {
				PostMessage(m_runtimeState.mainWnd, WM_HIDE_AFTER_DOWNLOAD, 0, 0);
				SetForegroundWindow(FindGameWindowByProcessId(m_runtimeState.gameInfos, requestPid));
			}
			return outcome;
		}

		SetLauncherStatus(isAsync
			? L"Failed: queued async file update."
			: L"Failed: client file request download.");
		MarkClientDownloadFinished(requestPid);
		if (!isAsync) {
			PostMessage(m_runtimeState.mainWnd, WM_HIDE_AFTER_DOWNLOAD, 0, 0);
		}
		outcome.status = 502;
		outcome.body = "Download Failed";
		LogUpdateError(
			"UF-WS-DOWNLOAD",
			isAsync ? "WorkThread::WebServiceThread:/download-async"
			        : "WorkThread::WebServiceThread:/download",
			"Client file download failed",
			std::string("page=") + resolvedPage + ", pid=" + std::to_string(requestPid));
		return outcome;
	};

	std::thread asyncWorker([&]() {
		for (;;) {
			AsyncDownloadTask task;
			std::string queueKey;
			{
				std::unique_lock<std::mutex> lock(asyncQueueMutex);
				asyncQueueCv.wait(lock, [&]() {
					return asyncWorkerStop || !asyncQueue.empty();
				});
				if (asyncWorkerStop && asyncQueue.empty()) {
					return;
				}
				task = asyncQueue.front();
				asyncQueue.pop_front();
				queueKey = NormalizeAsyncQueueKey(task.page);
			}

			DownloadRequestOutcome outcome = executeClientDownload(task.page, task.pid, true);
			if (!outcome.success) {
				LogUpdateError(
					"UF-WS-ASYNC",
					"WorkThread::WebServiceThread:/download-async-worker",
					"Queued async file update failed",
					std::string("page=") + task.page + ", pid=" + std::to_string(task.pid) +
					", status=" + std::to_string(outcome.status));
			}
			{
				std::lock_guard<std::mutex> lock(asyncQueueMutex);
				asyncQueuedPages.erase(queueKey);
			}
		}
	});

	while (m_runtimeState.run) {
		httplib::Server svr;
		svr.Get("/download", [this, &asyncQueueMutex, &asyncQueueCv, &asyncQueue, &asyncQueuedPages, &executeClientDownload](const httplib::Request& req, httplib::Response& res) {
			const std::string strPage = req.has_param("page")
				? req.get_param_value("page")
				: std::string();
			if (strPage.empty()) {
				res.status = 400;
				res.set_content("Missing page", "text/plain");
				return;
			}

			DWORD requestPid = 0;
			if (req.has_param("pid")) {
				try {
					requestPid = static_cast<DWORD>(std::stoul(req.get_param_value("pid")));
				}
				catch (...) {
					requestPid = 0;
				}
			}

			if (IsAsyncDownloadRequest(req)) {
				const std::string queueKey = NormalizeAsyncQueueKey(strPage);
				bool enqueued = false;
				{
					std::lock_guard<std::mutex> lock(asyncQueueMutex);
					if (asyncQueuedPages.insert(queueKey).second) {
						asyncQueue.push_back({ strPage, requestPid });
						enqueued = true;
					}
				}
				if (enqueued) {
					asyncQueueCv.notify_one();
				}
				res.status = 202;
				res.set_content(enqueued ? "QUEUED" : "ALREADY_QUEUED", "text/plain");
				return;
			}

			DownloadRequestOutcome outcome = executeClientDownload(strPage, requestPid, false);
			res.status = outcome.status;
			res.set_content(outcome.body, "text/plain");
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

	{
		std::lock_guard<std::mutex> lock(asyncQueueMutex);
		asyncWorkerStop = true;
	}
	asyncQueueCv.notify_all();
	if (asyncWorker.joinable()) {
		asyncWorker.join();
	}
	std::cout << "Web service thread finished" << std::endl;
}
