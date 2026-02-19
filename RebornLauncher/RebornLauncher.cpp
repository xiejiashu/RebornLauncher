// RebornLauncher main entry and message routing.

#include "framework.h"
#include "RebornLauncher.h"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <ShlDisp.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <objbase.h>
#include <objidl.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <process.h>
#include <string>
#include <vector>
#include <shellapi.h>
#include <gdiplus.h>

#undef min
#undef max

#include "WorkThread.h"
#include "LauncherP2PController.h"
#include "LauncherSplashRenderer.h"
#include "TrayIconManager.h"
#include <httplib.h>

#pragma comment (lib,"Comctl32.lib")
#pragma comment (lib,"Shlwapi.lib")
#pragma comment (lib,"Gdiplus.lib")

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool IsProcessRunning(DWORD dwProcessId);
void CreateDesktopLauncherShortcut(const std::filesystem::path& targetDir, const std::wstring& fileName);

HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;

POINT g_ptWindow = { 360, 180 };
constexpr SIZE g_szWindow = { 360, 280 };

std::wstring g_strCurrentModulePath;
std::wstring g_strCurrentExeName;
std::wstring g_strWorkPath;

bool g_bRendering = true;
WorkThread* g_workThreadPtr = nullptr;

constexpr UINT ID_TRAY_OPEN = 5005;
constexpr UINT ID_START_NEW_GAME = 5006;
constexpr UINT_PTR kAnimTimerId = 7001;
constexpr UINT kAnimIntervalMs = 90;
constexpr COLORREF kTransparentColorKey = RGB(1, 1, 1);

ULONG_PTR g_gdiplusToken = 0;
LauncherSplashRenderer g_splashRenderer;
LauncherP2PController g_p2pController;
TrayIconManager g_trayIconManager(g_bRendering);

HANDLE g_hSingleInstanceMutex = NULL;
constexpr const wchar_t* kLauncherSingleInstanceMutexName = L"Local\\MapleFireReborn.RebornLauncher.SingleInstance";
constexpr const wchar_t* kLauncherConfigSection = L"MapleFireReborn";
constexpr const wchar_t* kLauncherConfigKeyGamePath = L"GamePath";
constexpr const wchar_t* kCanonicalLauncherName = L"RebornLauncher.exe";
constexpr ULONGLONG kMinInstallFreeBytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;

namespace {

struct RelaunchArgs {
    DWORD cleanupPid{ 0 };
    std::wstring cleanupPath;
    std::wstring stage;
};

std::wstring QuoteCommandArg(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring NormalizePathForCompare(const std::filesystem::path& input) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(input, ec);
    if (ec) {
        normalized = input.lexically_normal();
    }
    std::wstring value = normalized.wstring();
    while (!value.empty() && (value.back() == L'\\' || value.back() == L'/')) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

bool PathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
    return NormalizePathForCompare(left) == NormalizePathForCompare(right);
}

std::wstring GetLauncherConfigPath() {
    static std::wstring path;
    if (!path.empty()) {
        return path;
    }

    wchar_t localAppData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::filesystem::path configDir = std::filesystem::path(localAppData) / L"MapleFireReborn";
        std::error_code ec;
        std::filesystem::create_directories(configDir, ec);
        path = (configDir / L"launcher.ini").wstring();
        return path;
    }

    path = (std::filesystem::path(g_strCurrentModulePath).parent_path() / L"launcher.ini").wstring();
    return path;
}

std::wstring ReadLauncherConfigString(const wchar_t* key, const wchar_t* defaultValue = L"") {
    wchar_t buf[MAX_PATH]{};
    const std::wstring configPath = GetLauncherConfigPath();
    GetPrivateProfileStringW(kLauncherConfigSection, key, defaultValue, buf, MAX_PATH, configPath.c_str());
    return buf;
}

void WriteLauncherConfigString(const wchar_t* key, const std::wstring& value) {
    const std::wstring configPath = GetLauncherConfigPath();
    WritePrivateProfileStringW(kLauncherConfigSection, key, value.c_str(), configPath.c_str());
}

bool StartsWith(const std::wstring& value, const wchar_t* prefix) {
    if (!prefix) {
        return false;
    }
    const size_t prefixLen = wcslen(prefix);
    return value.size() >= prefixLen && value.compare(0, prefixLen, prefix) == 0;
}

bool TryParseDword(const std::wstring& value, DWORD& outValue) {
    if (value.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0') {
        return false;
    }
    outValue = static_cast<DWORD>(parsed);
    return true;
}

RelaunchArgs ParseRelaunchArgsFromCommandLine() {
    RelaunchArgs args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return args;
    }

    for (int i = 1; i < argc; ++i) {
        const std::wstring token = argv[i] ? argv[i] : L"";
        if (StartsWith(token, L"--cleanup-pid=")) {
            DWORD pid = 0;
            if (TryParseDword(token.substr(wcslen(L"--cleanup-pid=")), pid)) {
                args.cleanupPid = pid;
            }
            continue;
        }
        if (StartsWith(token, L"--cleanup-path=")) {
            args.cleanupPath = token.substr(wcslen(L"--cleanup-path="));
            continue;
        }
        if (StartsWith(token, L"--stage=")) {
            args.stage = token.substr(wcslen(L"--stage="));
            continue;
        }
    }

    // Backward compatibility: old chain passed a single positional cleanup path.
    if (argc == 2 && argv[1] && !StartsWith(argv[1], L"--")) {
        args.cleanupPath = argv[1];
    }

    LocalFree(argv);
    return args;
}

std::wstring BuildRelaunchArgsString(const RelaunchArgs& args) {
    std::wstring result;
    if (args.cleanupPid != 0) {
        result += L"--cleanup-pid=" + std::to_wstring(args.cleanupPid);
    }
    if (!args.cleanupPath.empty()) {
        if (!result.empty()) {
            result += L" ";
        }
        result += L"--cleanup-path=" + QuoteCommandArg(args.cleanupPath);
    }
    if (!args.stage.empty()) {
        if (!result.empty()) {
            result += L" ";
        }
        result += L"--stage=" + args.stage;
    }
    return result;
}

bool EnsureDirectoryExists(const std::filesystem::path& dirPath) {
    if (dirPath.empty()) {
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(dirPath, ec)) {
        return std::filesystem::is_directory(dirPath, ec);
    }
    return std::filesystem::create_directories(dirPath, ec);
}

bool HasEnoughFreeSpace(const std::filesystem::path& dirPath, ULONGLONG requiredFreeBytes) {
    const std::filesystem::path root = dirPath.root_path();
    if (root.empty()) {
        return false;
    }
    ULARGE_INTEGER freeBytesAvailable{};
    if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytesAvailable, nullptr, nullptr)) {
        return false;
    }
    return freeBytesAvailable.QuadPart >= requiredFreeBytes;
}

bool DeleteFileWithRetry(const std::filesystem::path& filePath, int maxRetry = 25, DWORD delayMs = 80) {
    if (filePath.empty()) {
        return true;
    }
    for (int i = 0; i < maxRetry; ++i) {
        SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(filePath.c_str())) {
            return true;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        Sleep(delayMs);
    }
    return false;
}

bool CopySelfToTarget(const std::filesystem::path& targetPath) {
    if (targetPath.empty()) {
        return false;
    }
    const std::filesystem::path targetDir = targetPath.parent_path();
    if (!EnsureDirectoryExists(targetDir)) {
        return false;
    }
    DeleteFileWithRetry(targetPath);
    return CopyFileW(g_strCurrentModulePath.c_str(), targetPath.c_str(), FALSE) == TRUE;
}

bool LaunchProcess(const std::filesystem::path& exePath, const RelaunchArgs& relaunchArgs, const std::filesystem::path& workDir) {
    const std::wstring args = BuildRelaunchArgsString(relaunchArgs);
    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        exePath.c_str(),
        args.empty() ? nullptr : args.c_str(),
        workDir.c_str(),
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool IsInManagedInstallDir() {
    const std::filesystem::path currentDir = std::filesystem::path(g_strCurrentModulePath).parent_path();
    const std::wstring configuredPath = ReadLauncherConfigString(kLauncherConfigKeyGamePath);
    if (!configuredPath.empty() && PathsEqual(currentDir, std::filesystem::path(configuredPath))) {
        return true;
    }
    if (_wcsicmp(currentDir.filename().c_str(), L"MapleFireReborn") == 0) {
        WriteLauncherConfigString(kLauncherConfigKeyGamePath, currentDir.wstring());
        return true;
    }
    return false;
}

std::filesystem::path ResolveInstallDirectory() {
    const std::wstring configuredPath = ReadLauncherConfigString(kLauncherConfigKeyGamePath);
    if (!configuredPath.empty()) {
        const std::filesystem::path configuredDir(configuredPath);
        if (EnsureDirectoryExists(configuredDir)) {
            return configuredDir;
        }
    }

    constexpr std::array<const wchar_t*, 5> kPreferredDirs = {
        L"D:\\MapleFireReborn",
        L"E:\\MapleFireReborn",
        L"F:\\MapleFireReborn",
        L"G:\\MapleFireReborn",
        L"C:\\MapleFireReborn"
    };

    for (const wchar_t* candidate : kPreferredDirs) {
        const std::filesystem::path dir(candidate);
        if (HasEnoughFreeSpace(dir, kMinInstallFreeBytes) && EnsureDirectoryExists(dir)) {
            return dir;
        }
    }

    for (const wchar_t* candidate : kPreferredDirs) {
        const std::filesystem::path dir(candidate);
        if (EnsureDirectoryExists(dir)) {
            return dir;
        }
    }

    return {};
}

bool IsProcessPathMatching(DWORD processId, const std::filesystem::path& expectedPath) {
    if (processId == 0 || expectedPath.empty()) {
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) {
        return false;
    }
    wchar_t processPath[MAX_PATH]{};
    DWORD processPathLen = MAX_PATH;
    const BOOL queryOk = QueryFullProcessImageNameW(hProcess, 0, processPath, &processPathLen);
    CloseHandle(hProcess);
    if (!queryOk) {
        return false;
    }
    return PathsEqual(expectedPath, std::filesystem::path(processPath));
}

bool HandleCleanupRelay(const RelaunchArgs& relaunchArgs) {
    if (relaunchArgs.cleanupPath.empty() && relaunchArgs.cleanupPid == 0) {
        return true;
    }

    const std::filesystem::path cleanupPath(relaunchArgs.cleanupPath);
    if (cleanupPath.empty() || PathsEqual(cleanupPath, std::filesystem::path(g_strCurrentModulePath))) {
        return true;
    }

    if (relaunchArgs.cleanupPid != 0 &&
        IsProcessRunning(relaunchArgs.cleanupPid) &&
        IsProcessPathMatching(relaunchArgs.cleanupPid, cleanupPath)) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, relaunchArgs.cleanupPid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            WaitForSingleObject(hProc, 5000);
            CloseHandle(hProc);
        }
    }

    DeleteFileWithRetry(cleanupPath, 40, 100);

    if (_wcsicmp(g_strCurrentExeName.c_str(), kCanonicalLauncherName) != 0) {
        const std::filesystem::path canonicalPath = std::filesystem::path(g_strCurrentModulePath).parent_path() / kCanonicalLauncherName;
        if (!PathsEqual(canonicalPath, std::filesystem::path(g_strCurrentModulePath))) {
            if (!CopySelfToTarget(canonicalPath)) {
                std::wcout << __FILEW__ << L":" << __LINE__ << L" failed to promote launcher to canonical name: "
                    << canonicalPath.c_str() << std::endl;
                return false;
            }
            RelaunchArgs nextArgs;
            nextArgs.cleanupPid = _getpid();
            nextArgs.cleanupPath = g_strCurrentModulePath;
            nextArgs.stage = L"promote";
            if (!LaunchProcess(canonicalPath, nextArgs, canonicalPath.parent_path())) {
                std::wcout << __FILEW__ << L":" << __LINE__ << L" failed to relaunch canonical launcher: "
                    << canonicalPath.c_str() << std::endl;
                return false;
            }
            ExitProcess(0);
        }
    }

    return true;
}

bool RelocateLauncherIfNeeded() {
    if (IsInManagedInstallDir()) {
        return true;
    }

    const std::filesystem::path targetDir = ResolveInstallDirectory();
    if (targetDir.empty()) {
        return false;
    }

    const std::filesystem::path targetExe = targetDir / g_strCurrentExeName;
    if (PathsEqual(targetExe, std::filesystem::path(g_strCurrentModulePath))) {
        WriteLauncherConfigString(kLauncherConfigKeyGamePath, targetDir.wstring());
        return true;
    }

    if (!CopySelfToTarget(targetExe)) {
        std::wcout << __FILEW__ << L":" << __LINE__
            << L" failed to copy launcher to target: " << targetExe.c_str()
            << L" error=" << GetLastError() << std::endl;
        return false;
    }

    WriteLauncherConfigString(kLauncherConfigKeyGamePath, targetDir.wstring());
    CreateDesktopLauncherShortcut(targetDir, g_strCurrentExeName);

    RelaunchArgs nextArgs;
    nextArgs.cleanupPid = _getpid();
    nextArgs.cleanupPath = g_strCurrentModulePath;
    nextArgs.stage = L"relocate";
    if (!LaunchProcess(targetExe, nextArgs, targetDir)) {
        std::wcout << __FILEW__ << L":" << __LINE__ << L" failed to launch relocated launcher: "
            << targetExe.c_str() << std::endl;
        return false;
    }

    ExitProcess(0);
    return true;
}

} // namespace

void CreateShortcut(LPCTSTR lpShortcutPath, LPCTSTR lpTargetPath, const LPCTSTR lpFileName) {
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

void CreateDesktopLauncherShortcut(const std::filesystem::path& targetDir, const std::wstring& fileName) {
    TCHAR shortcutPath[MAX_PATH]{};
    LPITEMIDLIST pidlDesktop = nullptr;
    const HRESULT hrDesktop = SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &pidlDesktop);
    if (SUCCEEDED(hrDesktop) && pidlDesktop) {
        if (SHGetPathFromIDList(pidlDesktop, shortcutPath)) {
            swprintf(shortcutPath, TEXT("%s\\MapleFireReborn.lnk"), shortcutPath);
            CreateShortcut(shortcutPath, targetDir.c_str(), fileName.c_str());
        }
        CoTaskMemFree(pidlDesktop);
        pidlDesktop = nullptr;
        return;
    }

    std::wcout << __FILEW__ << TEXT(":") << __LINE__
        << TEXT(" SHGetSpecialFolderLocation(CSIDL_DESKTOP) failed, skip shortcut. err=")
        << hrDesktop << std::endl;
}

bool IsProcessRunning(DWORD dwProcessId) {
    if (dwProcessId == 0) {
        return false;
    }
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, dwProcessId);
    if (!hProcess) {
        return false;
    }
    const DWORD wait = WaitForSingleObject(hProcess, 0);
    CloseHandle(hProcess);
    return wait == WAIT_TIMEOUT;
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

bool RequestNewGameWithError(HWND owner) {
    if (RequestRunningLauncherRunClient()) {
        return true;
    }
    MessageBox(owner, TEXT("Failed to request new game launch."), TEXT("Error"), MB_OK | MB_ICONERROR);
	// 结束所有可能的僵尸实例
    
    return false;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    g_hInstance = hInstance;

    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileName(hInstance, modulePath, MAX_PATH);
    g_strCurrentModulePath = modulePath;
    g_strCurrentExeName = PathFindFileName(modulePath);
    g_strWorkPath = std::filesystem::path(modulePath).parent_path().wstring();
    if (!g_strWorkPath.empty()) {
        SetCurrentDirectoryW(g_strWorkPath.c_str());
    }

    g_splashRenderer.SetInstance(hInstance);
    g_splashRenderer.SetWindowPlacementContext(&g_ptWindow, &g_szWindow);
    g_p2pController.InitializePaths(g_strCurrentModulePath, g_strWorkPath);

#ifdef _DEBUG
    AllocConsole();
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
#endif

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
    const RelaunchArgs relaunchArgs = ParseRelaunchArgsFromCommandLine();
    if (!HandleCleanupRelay(relaunchArgs)) {
        MessageBox(nullptr, TEXT("Launcher cleanup/update relay failed."), TEXT("Error"), MB_OK | MB_ICONERROR);
        if (g_gdiplusToken != 0) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        CoUninitialize();
        return -1;
    }

    if (!RelocateLauncherIfNeeded()) {
        MessageBox(nullptr, TEXT("Launcher first-run relocation failed."), TEXT("Error"), MB_OK | MB_ICONERROR);
        if (g_gdiplusToken != 0) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        CoUninitialize();
        return -1;
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

    if (!InitInstance(hInstance, true)) {
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

    g_p2pController.ApplyP2PSettings();
    WorkThread workThread(g_hWnd, g_strCurrentModulePath, g_strCurrentExeName, g_strWorkPath, g_p2pController.GetP2PSettings());
    g_workThreadPtr = &workThread;
    g_p2pController.SetWorkThread(g_workThreadPtr);
    g_p2pController.ApplyP2PSettings();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REBORNLAUNCHER));

    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (!running) {
            break;
        }

        g_p2pController.UpdateProgressUi(g_hWnd, workThread, g_splashRenderer);
        Sleep(15);
    }

    std::cout << "Request stopped" << std::endl;
    workThread.Stop();
    g_workThreadPtr = nullptr;
    g_p2pController.SetWorkThread(nullptr);
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
    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    HWND hWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, szWindowClass, L"RebornLauncher",
                                WS_POPUP,
                                g_ptWindow.x, g_ptWindow.y, g_szWindow.cx, g_szWindow.cy, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) {
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

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static ULONGLONG s_lastIdleClickTick = 0;
    static POINT s_lastIdleClickPoint = { 0, 0 };

    switch (message)
    {
    case WM_CREATE:
        SetLayeredWindowAttributes(hWnd, kTransparentColorKey, 255, LWA_COLORKEY);
        g_p2pController.LoadStunServers();
        g_p2pController.ApplyP2PSettings();
        g_splashRenderer.EnsureAnimationFramesLoaded();
        SetTimer(hWnd, kAnimTimerId, kAnimIntervalMs, nullptr);
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = g_szWindow.cx;
        mmi->ptMinTrackSize.y = g_szWindow.cy;
        return 0;
    }
    case WM_SIZE:
        g_p2pController.LayoutMainControls(hWnd);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case WM_TIMER:
        if (wParam == kAnimTimerId) {
            g_splashRenderer.OnTimerTick(hWnd);
        }
        break;
    case WM_LBUTTONDOWN:
        if (g_splashRenderer.IsFollowingGameWindows()) {
            s_lastIdleClickTick = 0;
            return 0;
        }
    {
        POINT clickPoint{};
        clickPoint.x = static_cast<SHORT>(LOWORD(lParam));
        clickPoint.y = static_cast<SHORT>(HIWORD(lParam));
        ClientToScreen(hWnd, &clickPoint);

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = (s_lastIdleClickTick == 0) ? static_cast<ULONGLONG>(-1) : (now - s_lastIdleClickTick);
        const int maxDx = GetSystemMetrics(SM_CXDOUBLECLK);
        const int maxDy = GetSystemMetrics(SM_CYDOUBLECLK);
        const int dx = (clickPoint.x > s_lastIdleClickPoint.x)
            ? (clickPoint.x - s_lastIdleClickPoint.x)
            : (s_lastIdleClickPoint.x - clickPoint.x);
        const int dy = (clickPoint.y > s_lastIdleClickPoint.y)
            ? (clickPoint.y - s_lastIdleClickPoint.y)
            : (s_lastIdleClickPoint.y - clickPoint.y);
        const bool isDoubleClick =
            elapsed <= static_cast<ULONGLONG>(GetDoubleClickTime()) &&
            dx <= maxDx &&
            dy <= maxDy;

        s_lastIdleClickTick = now;
        s_lastIdleClickPoint = clickPoint;

        if (isDoubleClick) {
            s_lastIdleClickTick = 0;
            RequestNewGameWithError(hWnd);
            return 0;
        }
    }
        ReleaseCapture();
        SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    case WM_RBUTTONUP:
        if (g_splashRenderer.IsFollowingGameWindows()) {
            return 0;
        }
    {
        POINT pt{};
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, ID_START_NEW_GAME, L"Start New Game");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
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
        g_splashRenderer.DrawScene(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            g_trayIconManager.RestoreFromTray(hWnd);
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
        g_trayIconManager.MinimizeToTray(hWnd, g_hInstance);
        break;
    case WM_DELETE_TRAY:
        g_trayIconManager.RestoreFromTray(hWnd);
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            g_trayIconManager.MinimizeToTray(hWnd, g_hInstance);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (g_p2pController.HandleCommand(wParam, lParam)) {
            return 0;
        }
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case ID_TRAY_OPEN:
            g_trayIconManager.RestoreFromTray(hWnd);
            break;
        case ID_START_NEW_GAME:
            if (!g_splashRenderer.IsFollowingGameWindows()) {
                RequestNewGameWithError(hWnd);
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
        if (!g_splashRenderer.IsFollowingGameWindows()) {
            g_ptWindow.x = LOWORD(lParam);
            g_ptWindow.y = HIWORD(lParam);
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        KillTimer(hWnd, kAnimTimerId);
        g_trayIconManager.Delete();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
