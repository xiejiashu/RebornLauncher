// RebornLauncher main entry and UI logic / RebornLauncher 主入口与界面逻辑
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
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <shellapi.h>
#include <cwchar>

#undef min
#undef max

#include "WorkThread.h"
#include "Encoding.h"
#include "P2PClient.h"
#include <httplib.h>

#pragma comment (lib,"Comctl32.lib")
#pragma comment (lib,"Shlwapi.lib")

#define MAX_LOADSTRING 100

// Global process/window state / 全局进程与窗口状态
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

// Forward declarations / 前置声明
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void InitTrayIcon(HWND hWnd);

HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;
NOTIFYICONDATA nid{};

// Last saved window position / 记录窗口位置
POINT g_ptWindow = { 360, 180 };
// Default launcher size / 默认启动器尺寸
constexpr SIZE g_szWindow = { 980, 620 };

// Full path of current module / 当前模块完整路径
std::wstring g_strCurrentModulePath;
// Current executable filename / 当前可执行文件名
std::wstring g_strCurrentExeName;
// Working directory / 工作目录
std::wstring g_strWorkPath;

// Rendering visibility flag / 渲染可见性标记
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

// Remove tray icon safely / 安全移除托盘图标
void DeleteTrayIcon() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
	DestroyIcon(nid.hIcon);
	// Reset rendering flag when tray icon is removed.
    g_bRendering = true;
}

void MinimizeToTray(HWND hWnd) {
    ShowWindow(hWnd, SW_HIDE);
    InitTrayIcon(hWnd);
    g_bRendering = false;
	// SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
}

void RestoreFromTray(HWND hWnd) {
    ShowWindow(hWnd, SW_SHOW);
    DeleteTrayIcon();
	SetForegroundWindow(hWnd);
    g_bRendering = true;
	// Legacy layered-window path retained for fallback.
	// SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
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
	g_p2pSettings.enabled = g_ui.checkP2P && SendMessage(g_ui.checkP2P, BM_GETCHECK, 0, 0) == BST_CHECKED;
	g_p2pSettings.stunServers.clear();
	for (const auto& ws : g_stunServers) {
		g_p2pSettings.stunServers.push_back(wstr2str(ws));
	}

	if (g_ui.statusText) {
		const wchar_t* status = g_p2pSettings.enabled
			? L"P2P状态: 已启用(WebRTC) / P2P: enabled (WebRTC)"
			: L"P2P状态: 已禁用 / P2P: disabled";
		SetWindowTextW(g_ui.statusText, status);
	}

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
		0, L"STATIC", L"状态: 就绪 / Status: Ready", WS_CHILD | WS_VISIBLE,
		margin, y, columnWidth, 24, hWnd, nullptr, hInst, nullptr);

	y += 28;
	g_ui.checkP2P = CreateWindowExW(
		0, L"BUTTON", L"启用P2P / Enable P2P", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		margin, y, columnWidth, 24, hWnd, reinterpret_cast<HMENU>(ID_CHECK_P2P), hInst, nullptr);

	y += 32;
	g_ui.stunTitle = CreateWindowExW(
		0, L"STATIC", L"STUN服务器(双击删除) / STUN Servers", WS_CHILD | WS_VISIBLE,
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
		0, L"BUTTON", L"添加 / Add", WS_CHILD | WS_VISIBLE,
		margin + columnWidth - 100, y, 100, 24, hWnd, reinterpret_cast<HMENU>(ID_STUN_ADD), hInst, nullptr);

	const int rightX = margin + columnWidth + 40;
	int ry = margin;
	g_ui.totalLabel = CreateWindowExW(
		0, L"STATIC", L"总进度 / Total: 0/0", WS_CHILD | WS_VISIBLE,
		rightX, ry, 220, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.totalProgress = CreateWindowExW(
		0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.totalProgress, PBM_SETRANGE32, 0, 100);

	ry += 36;
	g_ui.fileLabel = CreateWindowExW(
		0, L"STATIC", L"当前文件 / File: idle", WS_CHILD | WS_VISIBLE,
		rightX, ry, 420, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.fileProgress = CreateWindowExW(
		0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.fileProgress, PBM_SETRANGE32, 0, 100);

	LayoutMainControls(hWnd);
}

void UpdateProgressUi(WorkThread& workThread) {
	if (!g_ui.totalProgress || !g_ui.fileProgress) {
		return;
	}

	const int totalCount = workThread.GetTotalDownload();
	const int currentCount = workThread.GetCurrentDownload();
	SendMessage(g_ui.totalProgress, PBM_SETRANGE32, 0, std::max(1, totalCount));
	SendMessage(g_ui.totalProgress, PBM_SETPOS, currentCount, 0);

	const int fileSize = workThread.GetCurrentDownloadSize();
	const int fileProgress = workThread.GetCurrentDownloadProgress();
	SendMessage(g_ui.fileProgress, PBM_SETRANGE32, 0, std::max(1, fileSize));
	SendMessage(g_ui.fileProgress, PBM_SETPOS, fileProgress, 0);

	std::wstring fileName = workThread.GetCurrentDownloadFile();
	if (fileName.empty()) {
		fileName = L"当前文件 / File: idle";
	}
	else {
		fileName = L"当前文件 / File: " + fileName + L" " + std::to_wstring(fileProgress) + L"/" + std::to_wstring(std::max(1, fileSize));
	}
	if (g_ui.fileLabel) {
		SetWindowTextW(g_ui.fileLabel, fileName.c_str());
	}

	std::wstring totalText = L"总进度 / Total: " + std::to_wstring(currentCount) + L"/" + std::to_wstring(std::max(1, totalCount));
	if (g_ui.totalLabel) {
		SetWindowTextW(g_ui.totalLabel, totalText.c_str());
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
			<< TEXT("删除失败 / Delete failed: ") << newPath
			<< TEXT(" err:") << GetLastError() << std::endl;
    }
    if (!CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE))
    {
        std::wcout << __FILEW__ << ":" << __LINE__ << g_strCurrentModulePath << TEXT("-->") << newPath
            << TEXT(" 复制失败 / Copy failed: ") << newPath << TEXT(" err:") << GetLastError() << std::endl;
    }

	WriteProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir);
}

bool IsInMapleRebornDir() {
    // TCHAR filePath[MAX_PATH];
    // GetModuleFileName(NULL, filePath, MAX_PATH);

    // TCHAR* fileName = PathFindFileName(filePath);

    std::cout << "2222222222222222222222222222" << std::endl;
	std::wstring str = g_strCurrentModulePath.c_str();
	if (str.find(TEXT("MapleReborn")) != std::string::npos
        || str.find(TEXT("RebornV")) != std::string::npos)
    {
        std::cout << "aaa" << std::endl;
        std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__ << str << std::endl;
        return true;
    }

    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__
        << TEXT(" 失败 / failed:") << __LINE__ << TEXT(" ") << str << std::endl;

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
    lstrcpy(nid.szTip, L"重生启动器 / RebornLauncher");
    Shell_NotifyIcon(NIM_ADD, &nid);
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

	DWORD dwPID = GetProfileInt(TEXT("MapleReborn"), TEXT("pid"),0);
    if (dwPID)
    {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPID);
        if (IsProcessRunning(dwPID))
        {
			//DWORD dwExitCode = 0;
   //         DWORD dwResult = WaitForSingleObject(hProcess, 0);
   //         if (dwResult != WAIT_TIMEOUT)
   //         {
   //             CloseHandle(hProcess);


   //         }
            httplib::Client cli("localhost", 12345);
            auto res = cli.Get("/RunClient");
            if (res && res->status == 200)
            {
                // success
            }
            else
            {
                MessageBox(NULL, TEXT("最多仅可同时运行 2 个客户端。 / Only up to 2 clients can run at the same time."), TEXT("错误 / Error"), MB_OK);
            }
            return 0;
        }
    }

	DWORD dwCurrentPID = GetCurrentProcessId();
    TCHAR szPID[32] = { 0 };
	_itow_s(dwCurrentPID, szPID, 10);
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), szPID);

	DWORD dwClient1PID = GetProfileInt(TEXT("MapleReborn"), TEXT("Client1PID"), 0);
	DWORD dwClient2PID = GetProfileInt(TEXT("MapleReborn"), TEXT("Client2PID"), 0);
	if (dwClient1PID)
	{
		HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, dwClient1PID);
		if (hProcess)
		{
			TerminateProcess(hProcess, 0);
			CloseHandle(hProcess);
		}
	}

    if (dwClient2PID)
    {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, dwClient1PID);
        if (hProcess)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
    }

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

    std::cout << __FILE__ << ":" << __LINE__ << std::endl;
#ifndef _DEBUG
    if (!IsInMapleRebornDir())
    {
       //  MessageBox(NULL, TEXT("22222222222222222222"), TEXT("err"), MB_OK);
		LPCTSTR lpTargetDir = TEXT("C:\\MapleReborn ");
        const TCHAR* dirs[] = { TEXT("D:\\MapleReborn"), TEXT("E:\\MapleReborn"), TEXT("C:\\MapleReborn") };
        for (int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
			CreateDirectory(dirs[i], NULL);
			if (GetFileAttributes(dirs[i]) != INVALID_FILE_ATTRIBUTES) {
				// MoveToDirectory(dirs[i]);
				lpTargetDir = dirs[i];
				break;
			}
		}

        TCHAR szGamePath[MAX_PATH] = { 0 };
        GetProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir, szGamePath, MAX_PATH);
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
		swprintf(shortcutPath, TEXT("%s\\MapleReborn.lnk"), shortcutPath);
		// TCHAR currentPath[MAX_PATH];
		// GetModuleFileName(hInstance, currentPath, MAX_PATH);
		// TCHAR* fileName = PathFindFileName(g_strCurrentModulePath.c_str());
		CreateShortcut(shortcutPath, lpTargetDir, g_strCurrentExeName.c_str());
        // MessageBox(NULL, TEXT("55555555555555"), TEXT("err"), MB_OK);

		TCHAR newPath[MAX_PATH];
		swprintf(newPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());
        if (!IsProcessRunning(newPath)) {
			ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), lpTargetDir, SW_SHOWNORMAL);
            WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
            ExitProcess(0);
        }
        return 0;
    }
    else
    {
        if (lpCmdLine)
        {
			SetFileAttributes(lpCmdLine, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(lpCmdLine);

			if (g_strCurrentExeName != TEXT("RebornLauncher.exe"))
			{
				TCHAR newPath[MAX_PATH];
				swprintf(newPath, TEXT("%s\\RebornLauncher.exe"), g_strWorkPath.c_str());
				CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE);
				ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), g_strWorkPath.c_str(), SW_SHOWNORMAL);
                WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
				ExitProcess(0);
			}
        }
    }
#endif

    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    std::cout << "77777777777777" << std::endl;


    if (!InitInstance (hInstance, true))
    {
        std::cout << "888888888888" << std::endl;
        return FALSE;
    }

    WorkThread workThread(g_hWnd, g_strCurrentModulePath, g_strCurrentExeName, g_strWorkPath);
    g_workThreadPtr = &workThread;
    ApplyP2PSettings();
    InitTrayIcon(g_hWnd);

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

    std::cout << "请求终止 / Request stopped" << std::endl;
    workThread.Stop();
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
	g_workThreadPtr = nullptr;

    std::cout << "ooooooooooooooooooo" << std::endl;

    CoUninitialize();
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
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_REBORNLAUNCHER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// Init app instance and create the main window.
// 初始化应用实例并创建主窗口。
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance;
   INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
   InitCommonControlsEx(&icc);

   HWND hWnd = CreateWindowExW(0, szWindowClass, L"重生启动器 / RebornLauncher",
       WS_OVERLAPPEDWINDOW,
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

// Main window procedure / 主窗口消息处理。
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
	case WM_CREATE:
	{
		CreateMainControls(hWnd);
		LoadStunServers();
		ApplyP2PSettings();
		break;
	}
	case WM_GETMINMAXINFO:
	{
		auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
		mmi->ptMinTrackSize.x = 900;
		mmi->ptMinTrackSize.y = 560;
		return 0;
	}
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED) {
			LayoutMainControls(hWnd);
		}
		break;
    case WM_TRAYICON:
		// Show tray context menu on right click.
		if (lParam == WM_RBUTTONDOWN)
		{
			POINT pt;
			GetCursorPos(&pt);
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"退出 / Exit");
			TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);

            RestoreFromTray(hWnd);
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
			int notify = HIWORD(wParam);
            switch (wmId)
            {
			case ID_CHECK_P2P:
				if (notify == BN_CLICKED) {
					ApplyP2PSettings();
				}
				break;
			case ID_STUN_ADD:
				if (notify == BN_CLICKED) {
					AddStunServerFromEdit();
				}
				break;
			case ID_STUN_LIST:
				if (notify == LBN_DBLCLK) {
					RemoveSelectedStunServer();
				}
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
        g_ptWindow.x = LOWORD(lParam);
        g_ptWindow.y = HIWORD(lParam);
        break;
    }
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
    case WM_DESTROY:
		DeleteTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
