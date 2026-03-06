#include "framework.h"
#include "LauncherUpdateCoordinator.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_set>

#include <httplib.h>

#include "Encoding.h"
#include "NetUtils.h"

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

void LauncherUpdateCoordinator::WebServiceThread()
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
		}
		LogUpdateInfoFmt(
			"UF-WS-DL-REQUEST",
			isAsync ? "LauncherUpdateCoordinator::WebServiceThread:/download-async"
			        : "LauncherUpdateCoordinator::WebServiceThread:/download",
			"Client requested file download (page={}, pid={}, async={})",
			strPage,
			requestPid,
			(isAsync ? "true" : "false"));
		m_downloadState.totalDownload = 1;
		m_downloadState.currentDownload = 0;

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
			LogUpdateErrorDetailsFmt(
				"UF-WS-NOTFOUND",
				isAsync ? "LauncherUpdateCoordinator::WebServiceThread:/download-async"
				        : "LauncherUpdateCoordinator::WebServiceThread:/download",
				"Requested file not found in manifest",
				"page={}, pid={}",
				strPage,
				requestPid);
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

		bool queuedForDeferred = false;
		if (DownloadWithResume(strRemotePage, resolvedPage, requestPid, isAsync, &queuedForDeferred)) {
			m_downloadState.currentDownload = 1;
			if (queuedForDeferred) {
				SetLauncherStatus(L"Processing queued async file update...");
			}
			else {
				SetLauncherStatus(isAsync
					? L"Queued async file update completed."
					: L"Client file request completed.");
			}
			MarkClientDownloadFinished(requestPid);
			outcome.status = queuedForDeferred ? 202 : 200;
			outcome.body = queuedForDeferred ? "DEFERRED" : "OK";
			outcome.success = true;
			outcome.resolvedPage = resolvedPage;
			LogUpdateInfoFmt(
				"UF-WS-DL-SUCCESS",
				isAsync ? "LauncherUpdateCoordinator::WebServiceThread:/download-async"
				        : "LauncherUpdateCoordinator::WebServiceThread:/download",
				"Client file download completed (page={}, pid={}, async={}, deferred={})",
				resolvedPage,
				requestPid,
				(isAsync ? "true" : "false"),
				(queuedForDeferred ? "true" : "false"));
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
		LogUpdateErrorDetailsFmt(
			"UF-WS-DOWNLOAD",
			isAsync ? "LauncherUpdateCoordinator::WebServiceThread:/download-async"
			        : "LauncherUpdateCoordinator::WebServiceThread:/download",
			"Client file download failed",
			"page={}, pid={}",
			resolvedPage,
			requestPid);
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
				LogUpdateErrorDetailsFmt(
					"UF-WS-ASYNC",
					"LauncherUpdateCoordinator::WebServiceThread:/download-async-worker",
					"Queued async file update failed",
					"page={}, pid={}, status={}",
					task.page,
					task.pid,
					outcome.status);
			}
			{
				std::lock_guard<std::mutex> lock(asyncQueueMutex);
				asyncQueuedPages.erase(queueKey);
			}
		}
	});

	while (m_runtimeState.run) {
		auto svr = std::make_shared<httplib::Server>();
		{
			std::lock_guard<std::mutex> lock(m_webServiceMutex);
			m_activeWebServer = svr;
		}
		svr->Get("/download", [this, &asyncQueueMutex, &asyncQueueCv, &asyncQueue, &asyncQueuedPages, &executeClientDownload](const httplib::Request& req, httplib::Response& res) {
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

		svr->Get("/RunClient", [this](const httplib::Request& req, httplib::Response& res) {
			(void)req;
			LogUpdateInfoFmt(
				"UF-WS-RUNCLIENT",
				"LauncherUpdateCoordinator::WebServiceThread:/RunClient",
				"RunClient request received");
			std::lock_guard<std::mutex> flowGuard(m_launchFlowMutex);
			SetLauncherStatus(L"RunClient requested: checking updates...");
			if (!RefreshRemoteVersionManifest()) {
				SetLauncherStatus(L"Failed: RunClient manifest refresh.");
				res.status = 502;
				res.set_content("Manifest refresh failed.", "text/plain");
				LogUpdateError(
					"UF-WS-MANIFEST",
					"LauncherUpdateCoordinator::WebServiceThread:/RunClient",
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
					"LauncherUpdateCoordinator::WebServiceThread:/RunClient",
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
					"LauncherUpdateCoordinator::WebServiceThread:/RunClient",
					"Game client launch failed after update",
					"RunClient request finished with launch failure.");
				return;
			}

			res.status = 200;
			res.set_content("OK", "text/plain");
			SetLauncherStatus(L"RunClient launch succeeded.");
		});

		svr->Get("/Stop", [this, svr](const httplib::Request& req, httplib::Response& res) {
			(void)req;
			TerminateAllGameProcesses();
			res.status = 200;
			res.set_content("OK", "text/plain");
			svr->stop();
		});

		const bool listening = svr->listen("localhost", 12345);
		{
			std::lock_guard<std::mutex> lock(m_webServiceMutex);
			if (m_activeWebServer.get() == svr.get()) {
				m_activeWebServer.reset();
			}
		}
		if (!m_runtimeState.run) {
			break;
		}
		const bool recoveryRequested = m_webServiceRecoveryRequested.exchange(false, std::memory_order_relaxed);
		if (recoveryRequested) {
			SetLauncherStatus(L"Restarting local HTTP service...");
			LogUpdateWarn(
				"UF-WS-RECOVER",
				"LauncherUpdateCoordinator::WebServiceThread",
				"Web service recovery requested; listener restart completed");
			Sleep(120);
			continue;
		}
		if (!listening) {
			SetLauncherStatus(L"Retrying local HTTP service...");
			LogUpdateError(
				"UF-WS-LISTEN",
				"LauncherUpdateCoordinator::WebServiceThread",
				"HTTP web service listen failed",
				"listen(localhost:12345) returned false.");
		}
		else {
			SetLauncherStatus(L"Restarting local HTTP service...");
			LogUpdateError(
				"UF-WS-RESTART",
				"LauncherUpdateCoordinator::WebServiceThread",
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
	LogUpdateInfoFmt(
		"UF-WS-STOP",
		"LauncherUpdateCoordinator::WebServiceThread",
		"Web service thread finished");
}
