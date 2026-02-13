#include "framework.h"
#include "WorkThread.h"

extern bool IsProcessRunning(DWORD dwProcessId);

namespace {

struct EnumGameWindowContext {
	DWORD processId{ 0 };
	HWND found{ nullptr };
};

BOOL CALLBACK EnumGameWindowProc(HWND hWnd, LPARAM lParam) {
	auto* ctx = reinterpret_cast<EnumGameWindowContext*>(lParam);
	if (!ctx || !IsWindow(hWnd) || !IsWindowVisible(hWnd)) {
		return TRUE;
	}

	DWORD pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != ctx->processId) {
		return TRUE;
	}

	wchar_t className[128]{};
	if (GetClassNameW(hWnd, className, static_cast<int>(sizeof(className) / sizeof(className[0]))) <= 0) {
		return TRUE;
	}
	if (_wcsicmp(className, L"MapleStoryClass") != 0) {
		return TRUE;
	}
	if (GetWindow(hWnd, GW_OWNER) != nullptr) {
		return TRUE;
	}

	ctx->found = hWnd;
	return FALSE;
}

} // namespace

HWND WorkThread::FindGameWindowByProcessId(DWORD processId) const
{
	if (processId == 0) {
		return nullptr;
	}
	EnumGameWindowContext ctx{};
	ctx.processId = processId;
	EnumWindows(EnumGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.found;
}

void WorkThread::UpdateGameMainWindows()
{
	std::lock_guard<std::mutex> lock(m_gameInfosMutex);
	for (const auto& info : m_gameInfos) {
		if (!info || info->dwProcessId == 0) {
			continue;
		}
		if (!IsProcessRunning(info->dwProcessId)) {
			continue;
		}
		if (info->hMainWnd && IsWindow(info->hMainWnd)) {
			wchar_t className[128]{};
			DWORD pid = 0;
			GetWindowThreadProcessId(info->hMainWnd, &pid);
			if (pid == info->dwProcessId &&
				GetClassNameW(info->hMainWnd, className, static_cast<int>(sizeof(className) / sizeof(className[0]))) > 0 &&
				_wcsicmp(className, L"MapleStoryClass") == 0) {
				continue;
			}
		}
		info->hMainWnd = FindGameWindowByProcessId(info->dwProcessId);
	}
}
