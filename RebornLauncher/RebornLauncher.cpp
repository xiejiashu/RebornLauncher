// RebornLauncher main entry and UI logic / RebornLauncher main UI
//

#include "framework.h"
#include "RebornLauncher.h"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <ShlDisp.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <process.h>
#include <string>
#include <algorithm>
#include <vector>
#include <memory>
#include <cmath>
#include <sstream>
#include <shellapi.h>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <gdiplus.h>

#undef min
#undef max

#include "WorkThread.h"
#include "Encoding.h"
#include "P2PClient.h"
#include <httplib.h>

#pragma comment (lib,"Comctl32.lib")
#pragma comment (lib,"Shlwapi.lib")
#pragma comment (lib,"Gdiplus.lib")

#define MAX_LOADSTRING 100

// Global process/window state.
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

// Forward declarations.
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void InitTrayIcon(HWND hWnd);

HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;
NOTIFYICONDATA nid{};

// Last saved window position.
POINT g_ptWindow = { 360, 180 };
// Default launcher size.
constexpr SIZE g_szWindow = { 360, 280 };

// Full path of current module.
std::wstring g_strCurrentModulePath;
// Current executable filename.
std::wstring g_strCurrentExeName;
// Working directory.
std::wstring g_strWorkPath;

// Rendering visibility flag.
bool g_bRendering = true;

struct UiHandles {
	HWND checkP2P{ nullptr };
	HWND stunList{ nullptr };
	HWND stunEdit{ nullptr };
	HWND addStunBtn{ nullptr };
	HWND statusText{ nullptr };
	HWND stunTitle{ nullptr };
	HWND fileLabel{ nullptr };
	HWND totalLabel{ nullptr };
	HWND totalProgress{ nullptr };
	HWND fileProgress{ nullptr };
} g_ui;

std::vector<std::wstring> g_stunServers;
P2PSettings g_p2pSettings;
WorkThread* g_workThreadPtr = nullptr;
constexpr const wchar_t* kStunListFile = L"p2p_stun_servers.txt";

constexpr UINT ID_TRAY_OPEN = 5005;
constexpr UINT_PTR kAnimTimerId = 7001;
constexpr UINT kAnimIntervalMs = 90;
constexpr COLORREF kTransparentColorKey = RGB(1, 1, 1);

ULONG_PTR g_gdiplusToken = 0;
std::vector<std::unique_ptr<Gdiplus::Bitmap>> g_animFrames;
size_t g_animFrameIndex = 0;
int g_downloadPercent = 1;
int g_animPulse = 0;
std::wstring g_animStatusText = L"Updating resources...";
struct PigOverlayState {
	DWORD processId{ 0 };
	HWND gameWindow{ nullptr };
	RECT gameRect{};
	bool downloading{ false };
	uint64_t downloadedBytes{ 0 };
	uint64_t totalBytes{ 0 };
	int percent{ 0 };
	std::wstring fileName;
};
std::vector<PigOverlayState> g_overlayPigs;
RECT g_overlayBoundsScreen{};
bool g_followingGameWindows = false;
HANDLE g_hSingleInstanceMutex = NULL;
constexpr const wchar_t* kLauncherSingleInstanceMutexName = L"Local\\MapleFireReborn.RebornLauncher.SingleInstance";

std::filesystem::path GetModuleDir() {
	if (!g_strCurrentModulePath.empty()) {
		return std::filesystem::path(g_strCurrentModulePath).parent_path();
	}
	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileName(nullptr, modulePath, MAX_PATH);
	return std::filesystem::path(modulePath).parent_path();
}

bool IsPngFile(const std::filesystem::path& path) {
	if (!path.has_extension()) {
		return false;
	}
	std::wstring ext = path.extension().wstring();
	std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
	return ext == L".png";
}

bool LoadAnimationFramesFrom(const std::filesystem::path& frameDir) {
	if (!std::filesystem::exists(frameDir) || !std::filesystem::is_directory(frameDir)) {
		return false;
	}

	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(frameDir)) {
		if (entry.is_regular_file() && IsPngFile(entry.path())) {
			files.push_back(entry.path());
		}
	}
	if (files.empty()) {
		return false;
	}

	std::sort(files.begin(), files.end());
	std::vector<std::unique_ptr<Gdiplus::Bitmap>> loaded;
	loaded.reserve(files.size());
	for (const auto& file : files) {
		auto bitmap = std::make_unique<Gdiplus::Bitmap>(file.c_str());
		if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok && bitmap->GetWidth() > 0 && bitmap->GetHeight() > 0) {
			loaded.push_back(std::move(bitmap));
		}
	}
	if (loaded.empty()) {
		return false;
	}

	g_animFrames = std::move(loaded);
	g_animFrameIndex = 0;
	return true;
}

std::vector<std::filesystem::path> BuildAnimationSearchPaths() {
	std::vector<std::filesystem::path> paths;
	const auto moduleDir = GetModuleDir();
	const auto cwd = std::filesystem::current_path();

	auto add = [&paths](const std::filesystem::path& p) {
		if (std::find(paths.begin(), paths.end(), p) == paths.end()) {
			paths.push_back(p);
		}
	};

	add(moduleDir / "Texture" / "PiaoPiaoPig");
	add(moduleDir / "RebornLauncher" / "Texture" / "PiaoPiaoPig");
	add(cwd / "Texture" / "PiaoPiaoPig");
	add(cwd / "RebornLauncher" / "Texture" / "PiaoPiaoPig");

	std::filesystem::path walk = moduleDir;
	for (int i = 0; i < 6; ++i) {
		add(walk / "Texture" / "PiaoPiaoPig");
		add(walk / "RebornLauncher" / "Texture" / "PiaoPiaoPig");
		if (!walk.has_parent_path()) {
			break;
		}
		const auto parent = walk.parent_path();
		if (parent == walk) {
			break;
		}
		walk = parent;
	}

	return paths;
}

void EnsureAnimationFramesLoaded() {
	if (!g_animFrames.empty()) {
		return;
	}
	for (const auto& path : BuildAnimationSearchPaths()) {
		if (LoadAnimationFramesFrom(path)) {
			return;
		}
	}
}

std::wstring GetDisplayFileName(const std::wstring& raw) {
	if (raw.empty()) {
		return L"";
	}
	const size_t slashPos = raw.find_last_of(L"\\/");
	std::wstring name = slashPos == std::wstring::npos ? raw : raw.substr(slashPos + 1);
	if (name.size() > 28) {
		return name.substr(0, 25) + L"...";
	}
	return name;
}

HWND FindTopTrackedGameWindow(const std::vector<PigOverlayState>& overlays) {
	std::vector<HWND> tracked;
	tracked.reserve(overlays.size());
	for (const auto& pig : overlays) {
		if (pig.gameWindow && IsWindow(pig.gameWindow) && IsWindowVisible(pig.gameWindow)) {
			tracked.push_back(pig.gameWindow);
		}
	}
	if (tracked.empty()) {
		return nullptr;
	}

	for (HWND z = GetTopWindow(nullptr); z != nullptr; z = GetWindow(z, GW_HWNDNEXT)) {
		if (!IsWindowVisible(z)) {
			continue;
		}
		if (std::find(tracked.begin(), tracked.end(), z) != tracked.end()) {
			return z;
		}
	}
	return tracked.front();
}

void RefreshPigOverlayState(WorkThread& workThread) {
	if (!g_hWnd) {
		return;
	}

	const std::wstring globalFileRaw = workThread.GetCurrentDownloadFile();
	const std::wstring globalFileName = GetDisplayFileName(globalFileRaw);
	const int globalTotal = workThread.GetCurrentDownloadSize();
	const int globalProgress = workThread.GetCurrentDownloadProgress();
	const bool globalDownloading = !globalFileName.empty() && globalTotal > 0 && globalProgress >= 0 && globalProgress < globalTotal;

	auto gameInfos = workThread.GetGameInfosSnapshot();
	std::vector<PigOverlayState> overlays;
	overlays.reserve(gameInfos.size());

	RECT bounds{};
	bool hasBounds = false;
	for (const auto& info : gameInfos) {
		if (info.dwProcessId == 0 || info.hMainWnd == nullptr || !IsWindow(info.hMainWnd)) {
			continue;
		}
		RECT gameRect{};
		if (!GetWindowRect(info.hMainWnd, &gameRect)) {
			continue;
		}
		const int gameWidth = gameRect.right - gameRect.left;
		const int gameHeight = gameRect.bottom - gameRect.top;
		if (gameWidth <= 0 || gameHeight <= 0) {
			continue;
		}

		PigOverlayState state;
		state.processId = info.dwProcessId;
		state.gameWindow = info.hMainWnd;
		state.gameRect = gameRect;
		state.downloading = info.downloading;
		state.downloadedBytes = info.downloadDoneBytes;
		state.totalBytes = info.downloadTotalBytes;
		state.fileName = GetDisplayFileName(info.downloadFile);
		if (state.downloading && state.fileName.empty() && !globalFileName.empty()) {
			state.fileName = globalFileName;
		}
		if (state.downloading && state.totalBytes == 0 && globalTotal > 0) {
			state.totalBytes = static_cast<uint64_t>(globalTotal);
			state.downloadedBytes = static_cast<uint64_t>((std::max)(0, globalProgress));
		}
		if (!state.downloading && globalDownloading) {
			state.downloading = true;
			state.fileName = globalFileName;
			state.totalBytes = static_cast<uint64_t>(globalTotal);
			state.downloadedBytes = static_cast<uint64_t>((std::max)(0, globalProgress));
		}
		if (state.totalBytes > 0) {
			state.percent = static_cast<int>(std::round(
				static_cast<double>((std::min)(state.downloadedBytes, state.totalBytes)) * 100.0 /
				static_cast<double>(state.totalBytes)));
		}
		state.percent = (std::max)(0, (std::min)(100, state.percent));
		overlays.push_back(std::move(state));

		RECT drawArea{};
		drawArea.left = gameRect.left;
		drawArea.right = gameRect.right;
		drawArea.top = gameRect.top - 96;
		drawArea.bottom = gameRect.top - 4;
		if (!hasBounds) {
			bounds = drawArea;
			hasBounds = true;
		}
		else {
			bounds.left = (std::min)(bounds.left, drawArea.left);
			bounds.top = (std::min)(bounds.top, drawArea.top);
			bounds.right = (std::max)(bounds.right, drawArea.right);
			bounds.bottom = (std::max)(bounds.bottom, drawArea.bottom);
		}
	}

	g_overlayPigs = std::move(overlays);
	if (hasBounds) {
		const int margin = 6;
		bounds.left -= margin;
		bounds.top -= margin;
		bounds.right += margin;
		bounds.bottom += 2;
		g_overlayBoundsScreen = bounds;
		const int rawW = static_cast<int>(g_overlayBoundsScreen.right - g_overlayBoundsScreen.left);
		const int rawH = static_cast<int>(g_overlayBoundsScreen.bottom - g_overlayBoundsScreen.top);
		const int overlayW = (std::max)(1, rawW);
		const int overlayH = (std::max)(1, rawH);
		HWND anchorGame = FindTopTrackedGameWindow(g_overlayPigs);
		SetWindowPos(g_hWnd, anchorGame ? anchorGame : HWND_NOTOPMOST,
			g_overlayBoundsScreen.left, g_overlayBoundsScreen.top,
			overlayW, overlayH,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
		g_followingGameWindows = true;
	}
	else if (g_followingGameWindows) {
		g_overlayPigs.clear();
		g_overlayBoundsScreen = {};
		g_followingGameWindows = false;
		SetWindowPos(g_hWnd, HWND_NOTOPMOST,
			g_ptWindow.x, g_ptWindow.y, g_szWindow.cx, g_szWindow.cy,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
}

void DrawFallbackPulse(Gdiplus::Graphics& graphics, int width, int height) {
	const int cx = width / 2;
	const int cy = height / 2 - 12;
	const int baseRadius = (std::max)(16, (std::min)(width, height) / 22);
	for (int i = 0; i < 3; ++i) {
		const int phase = (g_animPulse + i * 8) % 24;
		const int alpha = 90 + (phase <= 12 ? phase * 10 : (24 - phase) * 10);
		const int radius = baseRadius + i * 16;
		Gdiplus::SolidBrush b(Gdiplus::Color((std::min)(255, alpha), 120, 166, 214));
		graphics.FillEllipse(&b, cx - radius, cy - radius, radius * 2, radius * 2);
	}
}

void DrawSplashScene(HWND hWnd, HDC hdc) {
	RECT rc{};
	GetClientRect(hWnd, &rc);
	const int width = rc.right - rc.left;
	const int height = rc.bottom - rc.top;
	if (width <= 0 || height <= 0) {
		return;
	}

	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
	HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

	Gdiplus::Graphics graphics(memDC);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
	graphics.Clear(Gdiplus::Color(255, 1, 1, 1));

	EnsureAnimationFramesLoaded();
	if (g_followingGameWindows && !g_overlayPigs.empty()) {
		Gdiplus::SolidBrush labelShadow(Gdiplus::Color(120, 0, 0, 0));
		Gdiplus::SolidBrush labelText(Gdiplus::Color(248, 255, 255, 255));
		Gdiplus::FontFamily fontFamily(L"Segoe UI");
		Gdiplus::Font labelFont(&fontFamily, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::StringFormat centerFormat;
		centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
		centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		for (const auto& pig : g_overlayPigs) {
			const int gameWidth = pig.gameRect.right - pig.gameRect.left;
			if (gameWidth <= 0) {
				continue;
			}
			auto toLocalX = [](LONG x) {
				return static_cast<int>(x - g_overlayBoundsScreen.left);
			};
			auto toLocalY = [](LONG y) {
				return static_cast<int>(y - g_overlayBoundsScreen.top);
			};

			int drawW = 54;
			int drawH = 42;
			if (!g_animFrames.empty()) {
				auto* frame = g_animFrames[g_animFrameIndex % g_animFrames.size()].get();
				const double scale = static_cast<double>(drawH) / static_cast<double>((std::max<UINT>)(1, frame->GetHeight()));
				drawW = static_cast<int>(frame->GetWidth() * scale);
			}

			const int runwayPadding = 10;
			const int runwayLeft = toLocalX(pig.gameRect.left + runwayPadding);
			int runwayRight = toLocalX(pig.gameRect.right - runwayPadding) - drawW;
			if (runwayRight < runwayLeft) {
				runwayRight = runwayLeft;
			}
			const double t = pig.downloading
				? (std::max)(0.0, (std::min)(1.0, static_cast<double>(pig.percent) / 100.0))
				: 0.0;
			const int drawX = runwayLeft + static_cast<int>(std::round((runwayRight - runwayLeft) * t));
			const int drawY = toLocalY(pig.gameRect.top - drawH - 6);

			if (!g_animFrames.empty()) {
				auto* frame = g_animFrames[g_animFrameIndex % g_animFrames.size()].get();
				graphics.DrawImage(frame, drawX, drawY, drawW, drawH);
			}
			else {
				const int fallbackCx = drawX + drawW / 2;
				const int fallbackCy = drawY + drawH / 2;
				Gdiplus::SolidBrush fallbackBrush(Gdiplus::Color(210, 255, 220, 150));
				graphics.FillEllipse(&fallbackBrush, fallbackCx - 20, fallbackCy - 20, 40, 40);
			}

			if (pig.downloading) {
				const std::wstring label = (pig.fileName.empty() ? L"unknown" : pig.fileName) + L" " + std::to_wstring(pig.percent) + L"%";
				const float labelW = static_cast<float>((std::max)(160, (std::min)(gameWidth - 24, 420)));
				float labelX = static_cast<float>(drawX + drawW / 2) - labelW * 0.5f;
				labelX = (std::max)(0.0f, (std::min)(static_cast<float>(width) - labelW, labelX));
				const float labelY = static_cast<float>((std::max)(2, drawY - 26));
				Gdiplus::RectF labelRect(labelX, labelY, labelW, 22.0f);
				Gdiplus::RectF shadowRect = labelRect;
				shadowRect.X += 1.0f;
				shadowRect.Y += 1.0f;
				graphics.DrawString(label.c_str(), -1, &labelFont, shadowRect, &centerFormat, &labelShadow);
				graphics.DrawString(label.c_str(), -1, &labelFont, labelRect, &centerFormat, &labelText);
			}
		}
	}
	else {
		int pigCenterX = width / 2;
		int pigTopY = (std::max)(0, height / 2 - 24);
		if (!g_animFrames.empty()) {
			auto* frame = g_animFrames[g_animFrameIndex % g_animFrames.size()].get();
			const double sx = static_cast<double>(width) * 0.36 / (std::max<UINT>)(1, frame->GetWidth());
			const double sy = static_cast<double>(height) * 0.54 / (std::max<UINT>)(1, frame->GetHeight());
			const double scale = (std::max)(0.2, (std::min)(sx, sy));
			const int drawW = static_cast<int>(frame->GetWidth() * scale);
			const int drawH = static_cast<int>(frame->GetHeight() * scale);
			const int drawX = (width - drawW) / 2;
			const int drawY = (height - drawH) / 2 - 20;
			graphics.DrawImage(frame, drawX, drawY, drawW, drawH);
			pigCenterX = drawX + drawW / 2;
			pigTopY = drawY;
		}
		else {
			DrawFallbackPulse(graphics, width, height);
		}

		const int percent = (std::max)(1, (std::min)(100, g_downloadPercent));
		const std::wstring percentText = std::to_wstring(percent) + L"%";
		Gdiplus::FontFamily fontFamily(L"Segoe UI");
		Gdiplus::Font percentFont(&fontFamily, 38.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::StringFormat format;
		format.SetAlignment(Gdiplus::StringAlignmentCenter);
		format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		const float percentBoxW = 180.0f;
		const float percentX = (std::max)(0.0f, (std::min)(static_cast<float>(width) - percentBoxW, static_cast<float>(pigCenterX) - percentBoxW * 0.5f));
		const float percentY = (std::max)(6.0f, static_cast<float>(pigTopY - 56));
		Gdiplus::RectF textRect(percentX, percentY, percentBoxW, 56.0f);
		Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(140, 0, 0, 0));
		Gdiplus::SolidBrush textBrush(Gdiplus::Color(250, 255, 255, 255));
		Gdiplus::RectF shadowRect = textRect;
		shadowRect.X += 2.0f;
		shadowRect.Y += 2.0f;
		graphics.DrawString(percentText.c_str(), -1, &percentFont, shadowRect, &format, &shadowBrush);
		graphics.DrawString(percentText.c_str(), -1, &percentFont, textRect, &format, &textBrush);
	}

	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
}

// Remove tray icon safely.
void DeleteTrayIcon() {
	if (nid.cbSize == 0) {
		g_bRendering = true;
		return;
	}
	Shell_NotifyIcon(NIM_DELETE, &nid);
	if (nid.hIcon) {
		DestroyIcon(nid.hIcon);
		nid.hIcon = nullptr;
	}
	nid = {};
	// Reset rendering flag when tray icon is removed.
    g_bRendering = true;
}

void MinimizeToTray(HWND hWnd) {
	if (nid.cbSize == 0) {
		InitTrayIcon(hWnd);
	}
	ShowWindow(hWnd, SW_HIDE);
	g_bRendering = false;
}

void RestoreFromTray(HWND hWnd) {
	DeleteTrayIcon();
	ShowWindow(hWnd, SW_SHOW);
	SetForegroundWindow(hWnd);
	g_bRendering = true;
}

constexpr UINT ID_CHECK_P2P = 5001;
constexpr UINT ID_STUN_LIST = 5002;
constexpr UINT ID_STUN_EDIT = 5003;
constexpr UINT ID_STUN_ADD = 5004;

std::wstring Trim(const std::wstring& value) {
	constexpr wchar_t whitespace[] = L" \t\r\n";
	const auto start = value.find_first_not_of(whitespace);
	if (start == std::wstring::npos) {
		return L"";
	}
	const auto end = value.find_last_not_of(whitespace);
	return value.substr(start, end - start + 1);
}

std::string TrimAscii(std::string value) {
	constexpr const char* whitespace = " \t\r\n";
	const auto start = value.find_first_not_of(whitespace);
	if (start == std::string::npos) {
		return {};
	}
	const auto end = value.find_last_not_of(whitespace);
	return value.substr(start, end - start + 1);
}

std::string ReadEnvVar(const char* name) {
	char* raw = nullptr;
	size_t len = 0;
	if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) {
		return {};
	}
	std::string value(raw);
	free(raw);
	return TrimAscii(value);
}

std::string ReadOptionalTextFile(const std::filesystem::path& path) {
	if (!std::filesystem::exists(path)) {
		return {};
	}
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return {};
	}
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return TrimAscii(content);
}

std::vector<std::string> ParseStunServerList(const std::string& raw) {
	std::string normalized = raw;
	for (char& ch : normalized) {
		if (ch == ',' || ch == ';' || ch == '\r' || ch == '\t') {
			ch = '\n';
		}
	}

	std::vector<std::string> result;
	std::istringstream in(normalized);
	std::string line;
	while (std::getline(in, line)) {
		std::string value = TrimAscii(line);
		if (value.empty()) {
			continue;
		}
		std::string lower = value;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
		if (lower.rfind("stun:", 0) != 0 && lower.rfind("turn:", 0) != 0) {
			value = "stun:" + value;
			lower = "stun:" + lower;
		}
		if (std::find(result.begin(), result.end(), value) == result.end()) {
			result.push_back(value);
		}
	}
	return result;
}

std::filesystem::path GetStunConfigPath() {
	if (!g_strWorkPath.empty()) {
		return std::filesystem::path(g_strWorkPath) / kStunListFile;
	}
	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileName(nullptr, modulePath, MAX_PATH);
	return std::filesystem::path(modulePath).parent_path() / kStunListFile;
}

void RefreshStunListUI() {
	if (!g_ui.stunList) {
		return;
	}
	SendMessage(g_ui.stunList, LB_RESETCONTENT, 0, 0);
	for (const auto& server : g_stunServers) {
		SendMessage(g_ui.stunList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(server.c_str()));
	}
}

void SaveStunServers() {
	const auto path = GetStunConfigPath();
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return;
	}
	for (const auto& server : g_stunServers) {
		out << wstr2str(server) << "\n";
	}
}

void LoadStunServers() {
	g_stunServers.clear();
	const auto path = GetStunConfigPath();
	if (std::filesystem::exists(path)) {
		std::ifstream in(path, std::ios::binary);
		std::string line;
		while (std::getline(in, line)) {
			auto ws = Trim(str2wstr(line, static_cast<int>(line.size())));
			if (!ws.empty()) {
				g_stunServers.push_back(ws);
			}
		}
	}
	if (g_stunServers.empty()) {
		g_stunServers = {
			L"stun:stun.l.google.com:19302",
			L"stun:global.stun.twilio.com:3478",
			L"stun:stun.cloudflare.com:3478"
		};
	}
	RefreshStunListUI();
}

void ApplyP2PSettings() {
	g_p2pSettings.enabled = g_ui.checkP2P == nullptr
		? true
		: (SendMessage(g_ui.checkP2P, BM_GETCHECK, 0, 0) == BST_CHECKED);
	g_p2pSettings.stunServers.clear();
	for (const auto& ws : g_stunServers) {
		g_p2pSettings.stunServers.push_back(wstr2str(ws));
	}
	if (g_p2pSettings.stunServers.empty()) {
		g_p2pSettings.stunServers = {
			"stun:stun.l.google.com:19302",
			"stun:global.stun.twilio.com:3478",
			"stun:stun.cloudflare.com:3478"
		};
	}
	std::string envStunRaw = ReadEnvVar("P2P_STUN_SERVERS");
	if (envStunRaw.empty()) {
		envStunRaw = ReadEnvVar("STUN_SERVERS");
	}
	const auto envStuns = ParseStunServerList(envStunRaw);
	if (!envStuns.empty()) {
		std::vector<std::string> merged;
		merged.reserve(envStuns.size() + g_p2pSettings.stunServers.size());
		for (const auto& s : envStuns) {
			if (std::find(merged.begin(), merged.end(), s) == merged.end()) {
				merged.push_back(s);
			}
		}
		for (const auto& s : g_p2pSettings.stunServers) {
			if (std::find(merged.begin(), merged.end(), s) == merged.end()) {
				merged.push_back(s);
			}
		}
		g_p2pSettings.stunServers = std::move(merged);
	}

	const std::filesystem::path workDir = g_strWorkPath.empty()
		? std::filesystem::current_path()
		: std::filesystem::path(g_strWorkPath);
	std::string signalEndpoint = ReadOptionalTextFile(workDir / "p2p_signal_endpoint.txt");
	if (signalEndpoint.empty()) {
		signalEndpoint = ReadEnvVar("P2P_SIGNAL_ENDPOINT");
	}
	if (signalEndpoint.empty()) {
		signalEndpoint = ReadEnvVar("SIGNAL_ENDPOINT");
	}
	g_p2pSettings.signalEndpoint = signalEndpoint;

	std::string signalAuthToken = ReadOptionalTextFile(workDir / "p2p_signal_auth_token.txt");
	if (signalAuthToken.empty()) {
		signalAuthToken = ReadEnvVar("P2P_SIGNAL_AUTH_TOKEN");
	}
	if (signalAuthToken.empty()) {
		signalAuthToken = ReadEnvVar("SIGNAL_AUTH_TOKEN");
	}
	g_p2pSettings.signalAuthToken = signalAuthToken;

	if (g_ui.statusText) {
		std::wstring status = g_p2pSettings.enabled ? L"P2P: enabled" : L"P2P: disabled";
		if (!g_p2pSettings.signalEndpoint.empty()) {
			status += L" | Signal: custom";
		}
		else {
			status += L" | Signal: auto";
		}
		SetWindowTextW(g_ui.statusText, status.c_str());
	}
	g_animStatusText = g_p2pSettings.enabled
		? L"Updating resources with P2P..."
		: L"Updating resources...";

	if (g_workThreadPtr) {
		g_workThreadPtr->UpdateP2PSettings(g_p2pSettings);
	}
}

void AddStunServerFromEdit() {
	if (!g_ui.stunEdit) {
		return;
	}
	wchar_t buffer[256]{};
	GetWindowTextW(g_ui.stunEdit, buffer, static_cast<int>(std::size(buffer)));
	std::wstring value = Trim(buffer);
	if (value.empty()) {
		return;
	}
	for (const auto& existing : g_stunServers) {
		if (_wcsicmp(existing.c_str(), value.c_str()) == 0) {
			return;
		}
	}
	g_stunServers.push_back(value);
	RefreshStunListUI();
	SaveStunServers();
	ApplyP2PSettings();
	SetWindowTextW(g_ui.stunEdit, L"");
}

void RemoveSelectedStunServer() {
	if (!g_ui.stunList) {
		return;
	}
	int sel = static_cast<int>(SendMessage(g_ui.stunList, LB_GETCURSEL, 0, 0));
	if (sel != LB_ERR && sel < static_cast<int>(g_stunServers.size())) {
		g_stunServers.erase(g_stunServers.begin() + sel);
		RefreshStunListUI();
		SaveStunServers();
		ApplyP2PSettings();
	}
}

void LayoutMainControls(HWND hWnd) {
	RECT rc{};
	GetClientRect(hWnd, &rc);
	const int clientW = rc.right - rc.left;
	const int clientH = rc.bottom - rc.top;

	const int margin = 16;
	const int gap = 24;
	const int leftW = (std::max)(320, clientW / 2 - gap);
	const int rightX = margin + leftW + gap;
	const int rightW = (std::max)(260, clientW - rightX - margin);

	int y = margin;
	MoveWindow(g_ui.statusText, margin, y, leftW, 24, TRUE);

	y += 28;
	MoveWindow(g_ui.checkP2P, margin, y, leftW, 24, TRUE);

	y += 32;
	MoveWindow(g_ui.stunTitle, margin, y, leftW, 20, TRUE);

	y += 20;
	const int editH = 24;
	const int addBtnW = 90;
	const int bottomY = clientH - margin - editH;
	const int listH = (std::max)(90, bottomY - 8 - y);
	MoveWindow(g_ui.stunList, margin, y, leftW, listH, TRUE);
	MoveWindow(g_ui.stunEdit, margin, bottomY, leftW - addBtnW - 8, editH, TRUE);
	MoveWindow(g_ui.addStunBtn, margin + leftW - addBtnW, bottomY, addBtnW, editH, TRUE);

	int ry = margin;
	MoveWindow(g_ui.totalLabel, rightX, ry, rightW, 20, TRUE);
	ry += 24;
	MoveWindow(g_ui.totalProgress, rightX, ry, rightW, 24, TRUE);
	ry += 38;
	MoveWindow(g_ui.fileLabel, rightX, ry, rightW, 20, TRUE);
	ry += 24;
	MoveWindow(g_ui.fileProgress, rightX, ry, rightW, 24, TRUE);
}

void CreateMainControls(HWND hWnd) {
	const int margin = 16;
	const int columnWidth = 320;
	int y = margin;

	g_ui.statusText = CreateWindowExW(
		0, L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
		margin, y, columnWidth, 24, hWnd, nullptr, hInst, nullptr);

	y += 28;
	g_ui.checkP2P = CreateWindowExW(
		0, L"BUTTON", L"Enable P2P", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		margin, y, columnWidth, 24, hWnd, reinterpret_cast<HMENU>(ID_CHECK_P2P), hInst, nullptr);
	SendMessage(g_ui.checkP2P, BM_SETCHECK, BST_CHECKED, 0);

	y += 32;
	g_ui.stunTitle = CreateWindowExW(
		0, L"STATIC", L"STUN Servers (double click to remove)", WS_CHILD | WS_VISIBLE,
		margin, y, columnWidth, 20, hWnd, nullptr, hInst, nullptr);

	y += 20;
	g_ui.stunList = CreateWindowExW(
		WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
		WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
		margin, y, columnWidth, 150, hWnd, reinterpret_cast<HMENU>(ID_STUN_LIST), hInst, nullptr);

	y += 160;
	g_ui.stunEdit = CreateWindowExW(
		WS_EX_CLIENTEDGE, L"EDIT", nullptr,
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		margin, y, columnWidth - 110, 24, hWnd, reinterpret_cast<HMENU>(ID_STUN_EDIT), hInst, nullptr);

	g_ui.addStunBtn = CreateWindowExW(
		0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
		margin + columnWidth - 100, y, 100, 24, hWnd, reinterpret_cast<HMENU>(ID_STUN_ADD), hInst, nullptr);

	const int rightX = margin + columnWidth + 40;
	int ry = margin;
	g_ui.totalLabel = CreateWindowExW(
		0, L"STATIC", L"Total: 0/0", WS_CHILD | WS_VISIBLE,
		rightX, ry, 220, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.totalProgress = CreateWindowExW(
		0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.totalProgress, PBM_SETRANGE32, 0, 100);

	ry += 36;
	g_ui.fileLabel = CreateWindowExW(
		0, L"STATIC", L"File: idle", WS_CHILD | WS_VISIBLE,
		rightX, ry, 420, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.fileProgress = CreateWindowExW(
		0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.fileProgress, PBM_SETRANGE32, 0, 100);

	LayoutMainControls(hWnd);
}

void UpdateProgressUi(WorkThread& workThread) {
	RefreshPigOverlayState(workThread);

	const int totalCount = workThread.GetTotalDownload();
	const int currentCount = workThread.GetCurrentDownload();
	const int fileSize = workThread.GetCurrentDownloadSize();
	const int fileProgress = workThread.GetCurrentDownloadProgress();
	if (g_ui.totalProgress && g_ui.fileProgress) {
		SendMessage(g_ui.totalProgress, PBM_SETRANGE32, 0, (std::max)(1, totalCount));
		SendMessage(g_ui.totalProgress, PBM_SETPOS, currentCount, 0);
		SendMessage(g_ui.fileProgress, PBM_SETRANGE32, 0, (std::max)(1, fileSize));
		SendMessage(g_ui.fileProgress, PBM_SETPOS, fileProgress, 0);
	}

	std::wstring fileName = workThread.GetCurrentDownloadFile();
	if (!fileName.empty()) {
		g_animStatusText = L"Updating: " + fileName;
	}

	const int safeTotal = (std::max)(1, totalCount);
	const int safeCurrent = (std::max)(0, (std::min)(currentCount, safeTotal));
	double fileRatio = 0.0;
	if (fileSize > 0) {
		fileRatio = static_cast<double>((std::max)(0, fileProgress)) / static_cast<double>(fileSize);
		fileRatio = (std::max)(0.0, (std::min)(1.0, fileRatio));
	}
	double overall = (static_cast<double>(safeCurrent) + fileRatio) / static_cast<double>(safeTotal);
	overall = (std::max)(0.0, (std::min)(1.0, overall));
	int percent = static_cast<int>(std::round(overall * 100.0));
	if (safeCurrent >= safeTotal) {
		percent = 100;
	}
	percent = (std::max)(1, (std::min)(100, percent));
	if (percent != g_downloadPercent && g_hWnd) {
		g_downloadPercent = percent;
		InvalidateRect(g_hWnd, nullptr, FALSE);
	}
}
void CreateShortcut(LPCTSTR lpShortcutPath, LPCTSTR lpTargetPath,const LPCTSTR lpFileName) {
    IShellLink* pShellLink = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&pShellLink);
    if (SUCCEEDED(hr)) {
		TCHAR newPath[MAX_PATH];
		swprintf(newPath, TEXT("%s\\%s"), lpTargetPath, lpFileName);
        pShellLink->SetPath(newPath);
		pShellLink->SetIconLocation(newPath, 0);
		pShellLink->SetWorkingDirectory(lpTargetPath);
        IPersistFile* pPersistFile = NULL;
        hr = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
        if (SUCCEEDED(hr)) {
            pPersistFile->Save(lpShortcutPath, TRUE);
            pPersistFile->Release();
        }
        pShellLink->Release();
    }
}

bool IsProcessRunning(const TCHAR* exePath) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hSnapshot, &pe)) {
        CloseHandle(hSnapshot);
        return false;
    }

    do {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProcess) {
            TCHAR processPath[MAX_PATH];
            DWORD dwSize = MAX_PATH;
            if (QueryFullProcessImageName(hProcess, NULL, processPath, &dwSize)) {
                if (_tcsicmp(processPath, exePath) == 0) {
                    CloseHandle(hProcess);
                    CloseHandle(hSnapshot);
                    return true;
                }
            }
            CloseHandle(hProcess);
        }
    } while (Process32Next(hSnapshot, &pe));

    CloseHandle(hSnapshot);
    return false;
}

void MoveToDirectory(LPCTSTR lpTargetDir) {
    // TCHAR currentPath[MAX_PATH];
    // GetModuleFileName(g_hInstance, currentPath, MAX_PATH);
	// TCHAR* FileName = PathFindFileName(g_strCurrentModulePath.c_str());
    TCHAR newPath[MAX_PATH];
    swprintf(newPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());
	SetFileAttributes(newPath, FILE_ATTRIBUTE_NORMAL);

    if (!DeleteFile(newPath))
    {
		std::wcout << __FILEW__ << ":" << __LINE__
			<< TEXT("Delete failed: ") << newPath
			<< TEXT(" err:") << GetLastError() << std::endl;
    }
    if (!CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE))
    {
        std::wcout << __FILEW__ << ":" << __LINE__ << g_strCurrentModulePath << TEXT("-->") << newPath
            << TEXT(" Copy failed: ") << newPath << TEXT(" err:") << GetLastError() << std::endl;
    }

	WriteProfileString(TEXT("MapleFireReborn"), TEXT("GamePath"), lpTargetDir);
}

bool IsInMapleFireRebornDir() {
    // TCHAR filePath[MAX_PATH];
    // GetModuleFileName(NULL, filePath, MAX_PATH);

    // TCHAR* fileName = PathFindFileName(filePath);

    std::cout << "2222222222222222222222222222" << std::endl;
	std::wstring str = g_strCurrentModulePath.c_str();
	if (str.find(TEXT("MapleFireReborn")) != std::string::npos
        || str.find(TEXT("RebornV")) != std::string::npos)
    {
		// 如果不是处于硬盘根目录下，且目录下只有当前可执行文件，则认为是在正确目录下
		std::filesystem::path currentPath = std::filesystem::path(g_strCurrentModulePath).parent_path();
		if (currentPath.has_parent_path()) {
			bool onlyCurrentExe = true;
			for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
				if (entry.path().filename() == g_strCurrentExeName) {
					continue;
				}
				onlyCurrentExe = false;
				break;
			}
			if (onlyCurrentExe) {
				return true;
			}
		}
        std::cout << "aaa" << std::endl;
        std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__ << str << std::endl;
        return true;
    }

    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__
        << TEXT(" failed:") << __LINE__ << TEXT(" ") << str << std::endl;

    //*fileName = '\0';
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //PathRemoveBackslash(filePath);
    //PathRemoveFileSpec(filePath);
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //TCHAR* folderName = PathFindFileName(filePath);
    //MessageBox(NULL, folderName, TEXT("err"), MB_OK);
    return false;
}

bool IsProcessRunning(DWORD dwProcessId) {
    bool bRet = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (pe.th32ProcessID == dwProcessId) {
                    bRet = true;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return bRet;
}

void InitTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER));
	if (!nid.hIcon){
        std::cout << "LoadIcon failed errcode:" << GetLastError() << ":"<< g_hInstance << std::endl;
	}
    lstrcpy(nid.szTip, L"RebornLauncher");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

bool RequestRunningLauncherRunClient() {
	httplib::Client cli("localhost", 12345);
	for (int i = 0; i < 10; ++i) {
		auto res = cli.Get("/RunClient");
		if (res && res->status == 200) {
			return true;
		}
		Sleep(200);
	}
	return false;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    g_hInstance = hInstance;

	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileName(hInstance, modulePath, MAX_PATH);
	g_strCurrentModulePath = modulePath;
	g_strCurrentExeName = PathFindFileName(modulePath);
    wchar_t workPath[MAX_PATH]{};
	GetCurrentDirectory(MAX_PATH, workPath);
	g_strWorkPath = workPath;

//#ifndef _DEBUG
//	std::wofstream outLog(g_strCurrentExeName + TEXT(".log"));
//    std::wstreambuf* originalCoutBuffer = std::wcout.rdbuf();
//    std::wcout.rdbuf(outLog.rdbuf());
//
//	std::ofstream outLogA("Launcher.log");
//	std::streambuf* originalCoutBufferA = std::cout.rdbuf();
//	std::cout.rdbuf(outLogA.rdbuf());
//#endif

#ifdef _DEBUG
    AllocConsole();
    FILE* stream = NULL;
    freopen_s(&stream, "CONOUT$", "w", stdout); //CONOUT$
#endif


    // MessageBox(NULL, TEXT("0000000000000000"), TEXT("err"), MB_OK);
    std::wcout << TEXT("Start:") << lpCmdLine << std::endl;


    HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		return -1;
	}
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
		CoUninitialize();
		return -1;
	}

    std::cout << __FILE__ << ":" << __LINE__ << std::endl;
#ifndef _DEBUG
    if (!IsInMapleFireRebornDir())
    {
       //  MessageBox(NULL, TEXT("22222222222222222222"), TEXT("err"), MB_OK);
		LPCTSTR lpTargetDir = TEXT("C:\\MapleFireReborn ");
        const TCHAR* dirs[] = { TEXT("D:\\MapleFireReborn"), TEXT("E:\\MapleFireReborn"), TEXT("F:\\MapleFireReborn"),TEXT("G:\\MapleFireReborn"),TEXT("C:\\MapleFireReborn") };
        for (int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
			CreateDirectory(dirs[i], NULL);
			if (GetFileAttributes(dirs[i]) != INVALID_FILE_ATTRIBUTES) {
				// MoveToDirectory(dirs[i]);
				lpTargetDir = dirs[i];
				break;
			}
		}

        TCHAR szGamePath[MAX_PATH] = { 0 };
        GetProfileString(TEXT("MapleFireReborn"), TEXT("GamePath"), lpTargetDir, szGamePath, MAX_PATH);
        MoveToDirectory(szGamePath);

        // MessageBox(NULL, TEXT("3333333333333333333"), TEXT("err"), MB_OK);

		TCHAR shortcutPath[MAX_PATH];
		LPITEMIDLIST pidlDesktop;
		HRESULT hr = SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidlDesktop);
		if (FAILED(hr)) {
			return -1;
            // MessageBox(NULL, TEXT("44444444444444444444444"), TEXT("err"), MB_OK);
		}
		SHGetPathFromIDList(pidlDesktop, shortcutPath);
		swprintf(shortcutPath, TEXT("%s\\MapleFireReborn.lnk"), shortcutPath);
		// TCHAR currentPath[MAX_PATH];
		// GetModuleFileName(hInstance, currentPath, MAX_PATH);
		// TCHAR* fileName = PathFindFileName(g_strCurrentModulePath.c_str());
		CreateShortcut(shortcutPath, lpTargetDir, g_strCurrentExeName.c_str());
        // MessageBox(NULL, TEXT("55555555555555"), TEXT("err"), MB_OK);

		TCHAR newPath[MAX_PATH];
		swprintf(newPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());
        if (!IsProcessRunning(newPath)) {
			WriteProfileString(TEXT("MapleFireReborn"), TEXT("pid"), std::to_wstring(_getpid()).c_str());
			ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), lpTargetDir, SW_SHOWNORMAL);
            ExitProcess(0);
        }
        return 0;
    }
    else
    {
        if (lpCmdLine)
        {
			// 如果 lpCmdLine 进程存在则先结束掉进程再删除文件
			DWORD pid = GetProfileInt(TEXT("MapleFireReborn"), TEXT("pid"), 0);
			if (pid) {
				if (IsProcessRunning(pid)) {
					TerminateProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid), 0);
				}
			}

			// 删除掉旧文件(因为有可能在桌面)
			SetFileAttributes(lpCmdLine, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(lpCmdLine);

			// 修复启动器名称为 RebornLauncher.exe 因为可能更新自己了把Template.exe改名
			if (g_strCurrentExeName.compare(TEXT("RebornLauncher.exe")) != 0)
			{
				TCHAR newPath[MAX_PATH];
				swprintf(newPath, TEXT("%s\\RebornLauncher.exe"), g_strWorkPath.c_str());
				CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE);
				// 修正快捷方式
				WriteProfileString(TEXT("MapleFireReborn"), TEXT("pid"), std::to_wstring(_getpid()).c_str());
				ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), g_strWorkPath.c_str(), SW_SHOWNORMAL);
				ExitProcess(0);
			}
        }
    }
#endif

	g_hSingleInstanceMutex = CreateMutex(nullptr, FALSE, kLauncherSingleInstanceMutexName);
	if (g_hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
		if (!RequestRunningLauncherRunClient()) {
			MessageBox(nullptr, TEXT("Launcher is already running, but failed to request new game launch."), TEXT("Error"), MB_OK);
		}
		CloseHandle(g_hSingleInstanceMutex);
		g_hSingleInstanceMutex = NULL;
		return 0;
	}

    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    std::cout << "77777777777777" << std::endl;


    if (!InitInstance (hInstance, true))
    {
        std::cout << "888888888888" << std::endl;
		if (g_gdiplusToken != 0) {
			Gdiplus::GdiplusShutdown(g_gdiplusToken);
			g_gdiplusToken = 0;
		}
		CoUninitialize();
		if (g_hSingleInstanceMutex) {
			CloseHandle(g_hSingleInstanceMutex);
			g_hSingleInstanceMutex = NULL;
		}
        return FALSE;
    }

	ApplyP2PSettings();
    WorkThread workThread(g_hWnd, g_strCurrentModulePath, g_strCurrentExeName, g_strWorkPath, g_p2pSettings);
    g_workThreadPtr = &workThread;
	g_workThreadPtr->UpdateP2PSettings(g_p2pSettings);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REBORNLAUNCHER));

    MSG msg;

    bool running = true;
    while (running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (!running) {
            break;
        }

        UpdateProgressUi(workThread);
        Sleep(15);
    }

    std::cout << "Request stopped" << std::endl;
    workThread.Stop();
	g_workThreadPtr = nullptr;
	if (g_gdiplusToken != 0) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
		g_gdiplusToken = 0;
	}

    std::cout << "ooooooooooooooooooo" << std::endl;

	CoUninitialize();
	if (g_hSingleInstanceMutex) {
		CloseHandle(g_hSingleInstanceMutex);
		g_hSingleInstanceMutex = NULL;
	}
    return (int) msg.wParam;
}

//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// Init app instance and create the main window.
// Init app instance and create the main window.
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance;
   INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
   InitCommonControlsEx(&icc);

   HWND hWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, szWindowClass, L"RebornLauncher",
       WS_POPUP,
       g_ptWindow.x, g_ptWindow.y,g_szWindow.cx,g_szWindow.cy, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   g_hWnd = hWnd;

   g_ptWindow.x = (GetSystemMetrics(SM_CXSCREEN) - g_szWindow.cx) / 2;
   g_ptWindow.y = (GetSystemMetrics(SM_CYSCREEN) - g_szWindow.cy) / 2;
   SetWindowPos(hWnd, nullptr, g_ptWindow.x, g_ptWindow.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

// Main window procedure.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
	case WM_CREATE:
	{
		SetLayeredWindowAttributes(hWnd, kTransparentColorKey, 255, LWA_COLORKEY);
		LoadStunServers();
		ApplyP2PSettings();
		EnsureAnimationFramesLoaded();
		SetTimer(hWnd, kAnimTimerId, kAnimIntervalMs, nullptr);
		InvalidateRect(hWnd, nullptr, TRUE);
		break;
	}
	case WM_GETMINMAXINFO:
	{
		auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
		mmi->ptMinTrackSize.x = g_szWindow.cx;
		mmi->ptMinTrackSize.y = g_szWindow.cy;
		return 0;
	}
	case WM_SIZE:
		InvalidateRect(hWnd, nullptr, FALSE);
		break;
	case WM_TIMER:
		if (wParam == kAnimTimerId) {
			if (!g_animFrames.empty()) {
				g_animFrameIndex = (g_animFrameIndex + 1) % g_animFrames.size();
			}
			g_animPulse = (g_animPulse + 1) % 24;
			InvalidateRect(hWnd, nullptr, FALSE);
		}
		break;
	case WM_LBUTTONDOWN:
		if (g_followingGameWindows) {
			return 0;
		}
		ReleaseCapture();
		SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		return 0;
	case WM_RBUTTONUP:
		if (g_followingGameWindows) {
			return 0;
		}
	{
		POINT pt{};
		GetCursorPos(&pt);
		HMENU hMenu = CreatePopupMenu();
		AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");
		SetForegroundWindow(hWnd);
		TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
		DestroyMenu(hMenu);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps{};
		HDC hdc = BeginPaint(hWnd, &ps);
		DrawSplashScene(hWnd, hdc);
		EndPaint(hWnd, &ps);
		return 0;
	}
		break;
    case WM_TRAYICON:
		if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
			RestoreFromTray(hWnd);
			return 0;
		}
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			POINT pt{};
			GetCursorPos(&pt);
			HMENU hMenu = CreatePopupMenu();
			AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Open");
			AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");
			SetForegroundWindow(hWnd);
			TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
			DestroyMenu(hMenu);
			return 0;
		}
		break;
    case WM_MINIMIZE_TO_TRAY:
		MinimizeToTray(hWnd);
		break;
    case WM_DELETE_TRAY:
        RestoreFromTray(hWnd);
        break;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            MinimizeToTray(hWnd);
			return 0;
		}
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId)
            {
			case ID_TRAY_OPEN:
				RestoreFromTray(hWnd);
				break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_MOVE:
    {
        // Persist current window position.
		if (!g_followingGameWindows) {
			g_ptWindow.x = LOWORD(lParam);
			g_ptWindow.y = HIWORD(lParam);
		}
        break;
    }
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
    case WM_DESTROY:
		KillTimer(hWnd, kAnimTimerId);
		DeleteTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
