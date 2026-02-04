// RebornLauncher.cpp : 瀹氫箟搴旂敤绋嬪簭鐨勫叆鍙ｇ偣銆?
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

#include "WorkThread.h"
#include "Encoding.h"
#include "P2PClient.h"
#include <httplib.h>

#pragma comment (lib,"Comctl32.lib")
#pragma comment (lib,"Shlwapi.lib")

#define MAX_LOADSTRING 100

// 鍏ㄥ眬鍙橀噺:
HINSTANCE hInst;                                // 褰撳墠瀹炰緥
WCHAR szTitle[MAX_LOADSTRING];                  // 鏍囬鏍忔枃鏈?
WCHAR szWindowClass[MAX_LOADSTRING];            // 涓荤獥鍙ｇ被鍚?

// 姝や唬鐮佹ā鍧椾腑鍖呭惈鐨勫嚱鏁扮殑鍓嶅悜澹版槑:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void InitTrayIcon(HWND hWnd);

HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;
NOTIFYICONDATA nid{};

// 绐楀彛褰撳墠浣嶇疆
POINT g_ptWindow = { 360, 180 };
// 绐楀彛澶у皬
constexpr SIZE g_szWindow = { 860, 520 };

// 褰撳墠鏂囦欢妯″潡璺緞鍖呭惈鏂囦欢鍚?
std::wstring g_strCurrentModulePath;
// 褰撳墠exe鐨勫悕瀛?
std::wstring g_strCurrentExeName;
// 褰撳墠璺緞锛屼笉鍖呭惈鏂囦欢鍚?
std::wstring g_strWorkPath;

// 娓叉煋寮€鍏筹紙鎵樼洏鍙鏍囪锛?
bool g_bRendering = true;

struct UiHandles {
	HWND checkP2P{ nullptr };
	HWND stunList{ nullptr };
	HWND stunEdit{ nullptr };
	HWND addStunBtn{ nullptr };
	HWND statusText{ nullptr };
	HWND fileLabel{ nullptr };
	HWND totalLabel{ nullptr };
	HWND totalProgress{ nullptr };
	HWND fileProgress{ nullptr };
} g_ui;

std::vector<std::wstring> g_stunServers;
P2PSettings g_p2pSettings;
WorkThread* g_workThreadPtr = nullptr;
constexpr const wchar_t* kStunListFile = L"p2p_stun_servers.txt";

// 鍒犻櫎鎵樼洏鍥炬爣
void DeleteTrayIcon() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
	DestroyIcon(nid.hIcon);
	// 鍒犻櫎鎵樼洏鍥炬爣
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
	// 瑙ｉ櫎绐楀彛閫忔槑
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
		SetWindowText(g_ui.statusText, g_p2pSettings.enabled ? L"P2P 宸插惎鐢?(WebRTC)" : L"P2P 宸插叧闂?);
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
	GetWindowText(g_ui.stunEdit, buffer, static_cast<int>(std::size(buffer)));
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
	SetWindowText(g_ui.stunEdit, L"");
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

void CreateMainControls(HWND hWnd) {
	const int margin = 16;
	const int columnWidth = 300;
	int y = margin;

	g_ui.statusText = CreateWindowEx(0, L"STATIC", L"涓嬭浇妯″紡", WS_CHILD | WS_VISIBLE,
		margin, y, columnWidth, 24, hWnd, nullptr, hInst, nullptr);

	y += 28;
	g_ui.checkP2P = CreateWindowEx(0, L"BUTTON", L"鍚敤 P2P (WebRTC)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		margin, y, columnWidth, 24, hWnd, reinterpret_cast<HMENU>(ID_CHECK_P2P), hInst, nullptr);

	y += 32;
	CreateWindowEx(0, L"STATIC", L"STUN 鏈嶅姟鍣?, WS_CHILD | WS_VISIBLE,
		margin, y, columnWidth, 20, hWnd, nullptr, hInst, nullptr);

	y += 20;
	g_ui.stunList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
		WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
		margin, y, columnWidth, 140, hWnd, reinterpret_cast<HMENU>(ID_STUN_LIST), hInst, nullptr);

	y += 150;
	g_ui.stunEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		margin, y, columnWidth - 110, 24, hWnd, reinterpret_cast<HMENU>(ID_STUN_EDIT), hInst, nullptr);

	g_ui.addStunBtn = CreateWindowEx(0, L"BUTTON", L"娣诲姞 STUN", WS_CHILD | WS_VISIBLE,
		margin + columnWidth - 100, y, 100, 24, hWnd, reinterpret_cast<HMENU>(ID_STUN_ADD), hInst, nullptr);

	const int rightX = margin + columnWidth + 40;
	int ry = margin;
	g_ui.totalLabel = CreateWindowEx(0, L"STATIC", L"鎬昏繘搴?, WS_CHILD | WS_VISIBLE,
		rightX, ry, 200, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.totalProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.totalProgress, PBM_SETRANGE32, 0, 100);

	ry += 36;
	g_ui.fileLabel = CreateWindowEx(0, L"STATIC", L"褰撳墠鏂囦欢: 鏃?, WS_CHILD | WS_VISIBLE,
		rightX, ry, 420, 20, hWnd, nullptr, hInst, nullptr);
	ry += 24;
	g_ui.fileProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
		WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
	SendMessage(g_ui.fileProgress, PBM_SETRANGE32, 0, 100);
}

void UpdateProgressUi(const WorkThread& workThread) {
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
		fileName = L"褰撳墠鏂囦欢: 鏃?;
	}
	else {
		fileName = L"褰撳墠鏂囦欢: " + fileName + L" " + std::to_wstring(fileProgress) + L"/" + std::to_wstring(std::max(1, fileSize));
	}
	if (g_ui.fileLabel) {
		SetWindowText(g_ui.fileLabel, fileName.c_str());
	}

	std::wstring totalText = L"鎬昏繘搴? " + std::to_wstring(currentCount) + L"/" + std::to_wstring(std::max(1, totalCount));
	if (g_ui.totalLabel) {
		SetWindowText(g_ui.totalLabel, totalText.c_str());
	}
}

// 鍒涘缓鍥炬爣
void CreateShortcut(LPCTSTR lpShortcutPath, LPCTSTR lpTargetPath,const LPCTSTR lpFileName) {
    IShellLink* pShellLink = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&pShellLink);
    if (SUCCEEDED(hr)) {
        // 鍚堝嚭涓€涓柊璺緞
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
    // 鎻愬彇鏂囦欢鍚?
	// TCHAR* FileName = PathFindFileName(g_strCurrentModulePath.c_str());
    TCHAR newPath[MAX_PATH];
    swprintf(newPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());
    // 娓呮竻闄ゆ枃浠剁殑鍙灞炴€?
	SetFileAttributes(newPath, FILE_ATTRIBUTE_NORMAL);
    
	// 鍏堟竻闄ゆ棫鏂囦欢
    if (!DeleteFile(newPath))
    {
		std::wcout <<__FILEW__<<":"<<__LINE__<<TEXT( "鍒犻櫎澶辫触:") << newPath << TEXT("err:")<<GetLastError()<<std::endl;
    }
    if (!CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE))
    {
        std::wcout << __FILEW__ << ":" << __LINE__ << g_strCurrentModulePath << TEXT("-->") << newPath << TEXT("绉诲姩澶辫触:") << newPath << TEXT("err:") << GetLastError() << std::endl;
    }

    // 璁剧疆娓告垙鐩綍鍐欓厤缃」
	WriteProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir);
}

bool IsInMapleRebornDir() {
    // 鑾峰彇褰撳墠妯″潡鐨勬枃浠惰矾寰?
    // TCHAR filePath[MAX_PATH];
    // GetModuleFileName(NULL, filePath, MAX_PATH);

    // 鑾峰彇鏂囦欢鍚?
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

    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__ << TEXT("澶辫触") << TEXT(":") << __LINE__ << TEXT(" ") << str << std::endl;

    //// 鍘绘帀鏂囦欢鍚嶏紝淇濈暀鐩綍璺緞
    //*fileName = '\0';
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //// 鑾峰彇鐖剁洰褰曞悕
    //PathRemoveBackslash(filePath);
    //PathRemoveFileSpec(filePath);
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //// 妫€鏌ョ埗鐩綍鍚嶆槸鍚︿负 "MapleReborn"
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
    // 璁剧疆鎵樼洏鍥炬爣鏁版嵁
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER)); // 浣跨敤浣犵殑鍥炬爣璧勬簮
	if (!nid.hIcon){
        std::cout << "LoadIcon failed errcode:" << GetLastError() << ":"<< g_hInstance << std::endl;
	}
    lstrcpy(nid.szTip, L"鏋彾閲嶇敓");
    // 娣诲姞鎵樼洏鍥炬爣
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
//    // 淇濆瓨鍘熷鐨勭紦鍐插尯鎸囬拡
//    std::wstreambuf* originalCoutBuffer = std::wcout.rdbuf();
//    // 灏?std::cout 鐨勭紦鍐插尯鎸囬拡閲嶅畾鍚戝埌鏂囦欢
//    std::wcout.rdbuf(outLog.rdbuf());
//
//	std::ofstream outLogA("Launcher.log");
//	std::streambuf* originalCoutBufferA = std::cout.rdbuf();
//	std::cout.rdbuf(outLogA.rdbuf());
//#endif

	DWORD dwPID = GetProfileInt(TEXT("MapleReborn"), TEXT("pid"),0);
    if (dwPID)
    {
        // 鏌ョ湅杩涚▼鏄惁娲荤潃
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPID);
        if (IsProcessRunning(dwPID))
        {
            // 鍒ゆ柇杩涚▼鏄惁娲荤潃鐨?
			//DWORD dwExitCode = 0;
   //         DWORD dwResult = WaitForSingleObject(hProcess, 0);
   //         if (dwResult != WAIT_TIMEOUT)
   //         {
   //             // 杩涚▼杩樻椿鐫€
   //             CloseHandle(hProcess);


   //         }
            httplib::Client cli("localhost", 12345);
            auto res = cli.Get("/RunClient");
            if (res && res->status == 200)
            {
                // 鍚姩鎴愬姛
            }
            else
            {
				MessageBox(NULL, TEXT("鏈€澶氬彧鑳藉惎鍔?涓鎴风"), TEXT("err"), MB_OK);
                // 鍚姩澶辫触
            }
            return 0;
        }
    }

    // 鎶婂綋鍓峆ID鍐欏叆
	DWORD dwCurrentPID = GetCurrentProcessId();
    TCHAR szPID[32] = { 0 };
	_itow_s(dwCurrentPID, szPID, 10);
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), szPID);

    // 缁撴潫 MapleReborn 涓嬬殑 Client1PID,Client2PID
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

    // TODO: 鍦ㄦ澶勬斁缃唬鐮併€?
    // 璋冪敤GDI+搴撳噯澶?

    // MessageBox(NULL, TEXT("0000000000000000"), TEXT("err"), MB_OK);
    std::wcout << TEXT("Start:") << lpCmdLine << std::endl;


    // 妗岀湅褰撳墠鏄笉鏄湪妗岄潰锛屾垨鏄湪C鐩樼洰褰曚笅 濡傛灉鏄闈紝灏辨妸鑷繁绉诲埌D鐩?娌℃湁D鐩樺氨E鐩橈紝渚濇鈥︹€?濡傛灉閮芥病鏈夛紝灏辩Щ鍒癈:\MapleReborn 鐩綍涓嬮潰銆?
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
            // 鍒涘缓鐩綍
			CreateDirectory(dirs[i], NULL);
			if (GetFileAttributes(dirs[i]) != INVALID_FILE_ATTRIBUTES) {
				// MoveToDirectory(dirs[i]);
				lpTargetDir = dirs[i];
				break;
			}
		}

        // 璇诲彇娓告垙鐩綍
        TCHAR szGamePath[MAX_PATH] = { 0 };
        GetProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir, szGamePath, MAX_PATH);
        MoveToDirectory(szGamePath);

        // MessageBox(NULL, TEXT("3333333333333333333"), TEXT("err"), MB_OK);

		// 鍒涘缓妗岄潰蹇嵎鏂瑰紡
		TCHAR shortcutPath[MAX_PATH];
        // 鑾峰彇妗岄潰璺緞
		LPITEMIDLIST pidlDesktop;
		HRESULT hr = SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidlDesktop);
		if (FAILED(hr)) {
			return -1;
            // MessageBox(NULL, TEXT("44444444444444444444444"), TEXT("err"), MB_OK);
		}
		SHGetPathFromIDList(pidlDesktop, shortcutPath);
		swprintf(shortcutPath, TEXT("%s\\MapleReborn.lnk"), shortcutPath);
        // 鑾峰彇褰撳墠妯″潡鐨勫悕瀛?
		// TCHAR currentPath[MAX_PATH];
		// GetModuleFileName(hInstance, currentPath, MAX_PATH);
        // 鎶婂悕瀛楁彁鍙栧嚭鏉ユ嫾锛屾妸璺緞鍘绘帀
		// TCHAR* fileName = PathFindFileName(g_strCurrentModulePath.c_str());
		CreateShortcut(shortcutPath, lpTargetDir, g_strCurrentExeName.c_str());
        // MessageBox(NULL, TEXT("55555555555555"), TEXT("err"), MB_OK);

        // 鏌ョ湅鐩爣杩涚▼鏄惁宸插惎鍔紝宸插惎鍔ㄥ氨涓嶅啀鍒涘缓
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
            // 鍙栨秷鏂囦欢鍙灞炴€?
			SetFileAttributes(lpCmdLine, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(lpCmdLine);

            // 濡傛灉褰撳墠鏂囦欢鍚嶄笉鏄?RebornLauncher.exe 閭ｅ鍒跺嚭涓€涓?RebornLauncher.exe
			if (g_strCurrentExeName != TEXT("RebornLauncher.exe"))
			{
				TCHAR newPath[MAX_PATH];
				swprintf(newPath, TEXT("%s\\RebornLauncher.exe"), g_strWorkPath.c_str());
				CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE);
				// 鍚姩鏂扮殑杩涚▼
				ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), g_strWorkPath.c_str(), SW_SHOWNORMAL);
                WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
				ExitProcess(0);
			}
        }
    }
#endif

    // 鍒濆鍖栧叏灞€瀛楃涓?    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    std::cout << "77777777777777" << std::endl;


    // 鎵ц搴旂敤绋嬪簭鍒濆鍖?
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

    // 涓绘秷鎭惊鐜?
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

    std::cout << "璇锋眰涓" << std::endl;
    workThread.Stop();
	// 鎶婇厤缃」鐨勮繘绋婭D娓呯┖
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
	g_workThreadPtr = nullptr;

    std::cout << "ooooooooooooooooooo" << std::endl;

    CoUninitialize();
    return (int) msg.wParam;
}

//
//  鍑芥暟: MyRegisterClass()
//
//  鐩爣: 娉ㄥ唽绐楀彛绫汇€?
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

//
//   鍑芥暟: InitInstance(HINSTANCE, int)
//
//   鐩爣: 淇濆瓨瀹炰緥鍙ユ焺骞跺垱寤轰富绐楀彛
//
//   娉ㄩ噴:
//
//        鍦ㄦ鍑芥暟涓紝鎴戜滑鍦ㄥ叏灞€鍙橀噺涓繚瀛樺疄渚嬪彞鏌勫苟
//        鍒涘缓鍜屾樉绀轰富绋嬪簭绐楀彛銆?
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 灏嗗疄渚嬪彞鏌勫瓨鍌ㄥ湪鍏ㄥ眬鍙橀噺涓?
   INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
   InitCommonControlsEx(&icc);

   HWND hWnd = CreateWindowExW(0, szWindowClass, L"鏋彾閲嶇敓",
       WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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

//
//  鍑芥暟: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  鐩爣: 澶勭悊涓荤獥鍙ｇ殑娑堟伅銆?
//
//  WM_COMMAND  - 澶勭悊搴旂敤绋嬪簭鑿滃崟
//  WM_PAINT    - 缁樺埗涓荤獥鍙?
//  WM_DESTROY  - 鍙戦€侀€€鍑烘秷鎭苟杩斿洖
//
//
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
    case WM_TRAYICON:
		// 鍙抽敭鑿滃崟
		if (lParam == WM_RBUTTONDOWN)
		{
			POINT pt;
			GetCursorPos(&pt);
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit");
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
        // 鑾峰彇绐楀彛褰撳墠浣嶇疆
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
