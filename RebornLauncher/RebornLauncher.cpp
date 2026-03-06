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
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <format>
#include <process.h>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <shellapi.h>
#include <gdiplus.h>

#undef min
#undef max

#include "LauncherUpdateCoordinator.h"
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
LauncherUpdateCoordinator* g_updateCoordinatorPtr = nullptr;
bool g_manualTrayMode = false;
bool g_trayShownForDownload = false;
bool g_hasSavedWindowPos = false;
POINT g_savedWindowPos = { 0, 0 };
std::atomic<bool> g_runClientRequestInFlight{ false };

constexpr UINT ID_TRAY_OPEN = 5005;
constexpr UINT ID_START_NEW_GAME = 5006;
constexpr UINT ID_HIDE_TO_TRAY = 5007;
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
constexpr const wchar_t* kLauncherConfigKeyWindowX = L"WindowX";
constexpr const wchar_t* kLauncherConfigKeyWindowY = L"WindowY";
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

void DebugLog(const std::wstring& message) {
    std::wstring line = message;
    if (line.empty() || line.back() != L'\n') {
        line.push_back(L'\n');
    }
    OutputDebugStringW(line.c_str());
}

template <typename... Args>
void DebugLogFmt(std::wstring_view fmt, Args&&... args) {
    DebugLog(std::vformat(fmt, std::make_wformat_args(std::forward<Args>(args)...)));
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

bool TryParseInt(const std::wstring& value, int& outValue) {
    if (value.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    long parsed = wcstol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0') {
        return false;
    }
    outValue = static_cast<int>(parsed);
    return true;
}

POINT ClampWindowPositionToWorkArea(const POINT& pos) {
    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea.left = 0;
        workArea.top = 0;
        workArea.right = GetSystemMetrics(SM_CXSCREEN);
        workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    POINT clamped = pos;
    const int workLeft = static_cast<int>(workArea.left);
    const int workTop = static_cast<int>(workArea.top);
    const int workRight = static_cast<int>(workArea.right);
    const int workBottom = static_cast<int>(workArea.bottom);
    int maxX = workRight - static_cast<int>(g_szWindow.cx);
    int maxY = workBottom - static_cast<int>(g_szWindow.cy);
    if (maxX < workLeft) {
        maxX = workLeft;
    }
    if (maxY < workTop) {
        maxY = workTop;
    }
    if (clamped.x < workLeft) {
        clamped.x = workLeft;
    }
    else if (clamped.x > maxX) {
        clamped.x = maxX;
    }
    if (clamped.y < workTop) {
        clamped.y = workTop;
    }
    else if (clamped.y > maxY) {
        clamped.y = maxY;
    }
    return clamped;
}

bool LoadSavedLauncherWindowPosition(POINT& outPos) {
    const std::wstring xValue = ReadLauncherConfigString(kLauncherConfigKeyWindowX);
    const std::wstring yValue = ReadLauncherConfigString(kLauncherConfigKeyWindowY);
    int x = 0;
    int y = 0;
    if (!TryParseInt(xValue, x) || !TryParseInt(yValue, y)) {
        return false;
    }

    outPos.x = x;
    outPos.y = y;
    outPos = ClampWindowPositionToWorkArea(outPos);
    return true;
}

void SaveLauncherWindowPosition(const POINT& pos) {
    const POINT clamped = ClampWindowPositionToWorkArea(pos);
    WriteLauncherConfigString(kLauncherConfigKeyWindowX, std::to_wstring(clamped.x));
    WriteLauncherConfigString(kLauncherConfigKeyWindowY, std::to_wstring(clamped.y));
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

struct LauncherWindowSearchContext {
    DWORD selfPid{ 0 };
    std::filesystem::path launcherPath;
    HWND found{ nullptr };
};

BOOL CALLBACK EnumRunningLauncherWindowProc(HWND hWnd, LPARAM lParam) {
    auto* context = reinterpret_cast<LauncherWindowSearchContext*>(lParam);
    if (!context) {
        return TRUE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);
    if (processId == 0 || processId == context->selfPid) {
        return TRUE;
    }
    if (!IsProcessPathMatching(processId, context->launcherPath)) {
        return TRUE;
    }
    if ((GetWindowLongPtrW(hWnd, GWL_STYLE) & WS_CHILD) != 0) {
        return TRUE;
    }

    context->found = hWnd;
    return FALSE;
}

HWND FindRunningLauncherWindow() {
    LauncherWindowSearchContext context;
    context.selfPid = static_cast<DWORD>(_getpid());
    context.launcherPath = std::filesystem::path(g_strCurrentModulePath);
    EnumWindows(EnumRunningLauncherWindowProc, reinterpret_cast<LPARAM>(&context));
    return context.found;
}

void NotifyRunningLauncherStartRequest() {
    const HWND targetWindow = FindRunningLauncherWindow();
    if (!targetWindow) {
        DebugLog(L"No running launcher window found for foreground/start notification.");
        return;
    }

    DWORD_PTR result = 0;
    const LRESULT sendOk = SendMessageTimeoutW(
        targetWindow,
        WM_EXTERNAL_RUNCLIENT_REQUEST,
        0,
        0,
        SMTO_ABORTIFHUNG,
        500,
        &result);
    if (!sendOk) {
        DebugLogFmt(
            L"SendMessageTimeout(WM_EXTERNAL_RUNCLIENT_REQUEST) failed. err={}",
            GetLastError());
    }
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
                std::wstringstream ss;
                ss << __FILEW__ << L":" << __LINE__ << L" failed to promote launcher to canonical name: "
                    << canonicalPath.c_str();
                DebugLog(ss.str());
                return false;
            }
            RelaunchArgs nextArgs;
            nextArgs.cleanupPid = _getpid();
            nextArgs.cleanupPath = g_strCurrentModulePath;
            nextArgs.stage = L"promote";
            if (!LaunchProcess(canonicalPath, nextArgs, canonicalPath.parent_path())) {
                std::wstringstream ss;
                ss << __FILEW__ << L":" << __LINE__ << L" failed to relaunch canonical launcher: "
                    << canonicalPath.c_str();
                DebugLog(ss.str());
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
        std::wstringstream ss;
        ss << __FILEW__ << L":" << __LINE__
            << L" failed to copy launcher to target: " << targetExe.c_str()
            << L" error=" << GetLastError();
        DebugLog(ss.str());
        return false;
    }

    WriteLauncherConfigString(kLauncherConfigKeyGamePath, targetDir.wstring());
    CreateDesktopLauncherShortcut(targetDir, g_strCurrentExeName);

    RelaunchArgs nextArgs;
    nextArgs.cleanupPid = _getpid();
    nextArgs.cleanupPath = g_strCurrentModulePath;
    nextArgs.stage = L"relocate";
    if (!LaunchProcess(targetExe, nextArgs, targetDir)) {
        std::wstringstream ss;
        ss << __FILEW__ << L":" << __LINE__ << L" failed to launch relocated launcher: "
            << targetExe.c_str();
        DebugLog(ss.str());
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

    std::wstringstream ss;
    ss << __FILEW__ << TEXT(":") << __LINE__
        << TEXT(" SHGetSpecialFolderLocation(CSIDL_DESKTOP) failed, skip shortcut. err=")
        << hrDesktop;
    DebugLog(ss.str());
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

bool RequestRunningLauncherRunClient(
    int maxAttempts = 1,
    DWORD retryDelayMs = 120,
    int* outHttpStatus = nullptr,
    int* outHttpError = nullptr) {
    if (outHttpStatus) {
        *outHttpStatus = 0;
    }
    if (outHttpError) {
        *outHttpError = 0;
    }
    if (maxAttempts < 1) {
        maxAttempts = 1;
    }

    int lastHttpStatus = 0;
    int lastHttpError = 0;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        httplib::Client cli("localhost", 12345);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(2, 0);
        cli.set_write_timeout(1, 0);

        auto res = cli.Get("/RunClient");
        if (res) {
            lastHttpStatus = res->status;
            lastHttpError = static_cast<int>(res.error());
            if (res->status == 200) {
                if (outHttpStatus) {
                    *outHttpStatus = lastHttpStatus;
                }
                if (outHttpError) {
                    *outHttpError = lastHttpError;
                }
                return true;
            }

            DebugLogFmt(
                L"RunClient request failed (attempt={}/{}, status={}, error={})",
                attempt,
                maxAttempts,
                lastHttpStatus,
                lastHttpError);
        }
        else {
            lastHttpStatus = 0;
            lastHttpError = static_cast<int>(res.error());
            DebugLogFmt(
                L"RunClient request failed (attempt={}/{}, no response, error={})",
                attempt,
                maxAttempts,
                lastHttpError);
        }

        if (attempt < maxAttempts && retryDelayMs > 0) {
            Sleep(retryDelayMs);
        }
    }

    if (outHttpStatus) {
        *outHttpStatus = lastHttpStatus;
    }
    if (outHttpError) {
        *outHttpError = lastHttpError;
    }
    return false;
}

bool TerminateOtherLauncherProcesses() {
    const DWORD selfPid = static_cast<DWORD>(_getpid());
    const std::filesystem::path currentPath(g_strCurrentModulePath);
    bool terminatedAny = false;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        DebugLogFmt(L"CreateToolhelp32Snapshot failed while terminating stale launcher instances. err={}", GetLastError());
        return false;
    }

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == selfPid) {
                continue;
            }
            if (_wcsicmp(pe.szExeFile, g_strCurrentExeName.c_str()) != 0) {
                continue;
            }
            if (!IsProcessPathMatching(pe.th32ProcessID, currentPath)) {
                continue;
            }

            HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (!hProc) {
                DebugLogFmt(
                    L"OpenProcess(PROCESS_TERMINATE) failed for stale launcher pid={}. err={}",
                    pe.th32ProcessID,
                    GetLastError());
                continue;
            }

            if (!TerminateProcess(hProc, 0)) {
                DebugLogFmt(
                    L"TerminateProcess failed for stale launcher pid={}. err={}",
                    pe.th32ProcessID,
                    GetLastError());
                CloseHandle(hProc);
                continue;
            }

            WaitForSingleObject(hProc, 5000);
            CloseHandle(hProc);
            terminatedAny = true;
            DebugLogFmt(L"Terminated stale launcher process. pid={}", pe.th32ProcessID);
        } while (Process32Next(hSnap, &pe));
    }
    else {
        DebugLogFmt(L"Process32First failed while terminating stale launcher instances. err={}", GetLastError());
    }

    CloseHandle(hSnap);
    return terminatedAny;
}

bool AcquireSingleInstanceMutexWithRetry(int maxAttempts, DWORD retryDelayMs) {
    if (maxAttempts < 1) {
        maxAttempts = 1;
    }

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (g_hSingleInstanceMutex) {
            CloseHandle(g_hSingleInstanceMutex);
            g_hSingleInstanceMutex = NULL;
        }

        SetLastError(ERROR_SUCCESS);
        g_hSingleInstanceMutex = CreateMutex(nullptr, FALSE, kLauncherSingleInstanceMutexName);
        const DWORD createErr = GetLastError();
        if (g_hSingleInstanceMutex && createErr != ERROR_ALREADY_EXISTS) {
            DebugLogFmt(
                L"Single-instance mutex acquired after recovery. attempt={}, err={}",
                attempt,
                createErr);
            return true;
        }

        if (!g_hSingleInstanceMutex) {
            DebugLogFmt(
                L"CreateMutex failed while acquiring single-instance mutex. attempt={}, err={}",
                attempt,
                createErr);
        }
        else {
            DebugLogFmt(
                L"Single-instance mutex still owned by another instance. attempt={}, err={}",
                attempt,
                createErr);
        }

        if (attempt < maxAttempts && retryDelayMs > 0) {
            Sleep(retryDelayMs);
        }
    }

    if (g_hSingleInstanceMutex) {
        CloseHandle(g_hSingleInstanceMutex);
        g_hSingleInstanceMutex = NULL;
    }
    return false;
}

bool RequestNewGameWithError(HWND owner) {
    int httpStatus = 0;
    int httpError = 0;
    if (RequestRunningLauncherRunClient(2, 150, &httpStatus, &httpError)) {
        return true;
    }

    bool attemptedRecovery = false;
    if (g_updateCoordinatorPtr) {
        attemptedRecovery = true;
        DebugLogFmt(
            L"RunClient request failed in current launcher (status={}, error={}); requesting web service recovery.",
            httpStatus,
            httpError);
        g_updateCoordinatorPtr->RequestWebServiceRecovery();
        Sleep(220);
        if (RequestRunningLauncherRunClient(3, 180, &httpStatus, &httpError)) {
            DebugLog(L"RunClient request succeeded after web service recovery.");
            return true;
        }
    }

    std::wstring errorMessage = std::format(
        L"Failed to request new game launch.\nHTTP status: {}\nHTTP error: {}",
        httpStatus,
        httpError);
    if (attemptedRecovery) {
        errorMessage += L"\nTried HTTP service recovery.";
    }
    HWND ownerWnd = (owner && IsWindow(owner)) ? owner : nullptr;
    MessageBox(ownerWnd, errorMessage.c_str(), TEXT("Error"), MB_OK | MB_ICONERROR);

    return false;
}

void RequestNewGameAsync(HWND owner) {
    if (g_runClientRequestInFlight.exchange(true, std::memory_order_acq_rel)) {
        DebugLog(L"RunClient request ignored: previous request still in progress.");
        return;
    }

    std::thread([owner]() {
        RequestNewGameWithError(owner);
        g_runClientRequestInFlight.store(false, std::memory_order_release);
    }).detach();
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
    g_hasSavedWindowPos = LoadSavedLauncherWindowPosition(g_savedWindowPos);

#ifdef _DEBUG
    AllocConsole();
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
#endif

    DebugLog(std::wstring(TEXT("Start:")) + lpCmdLine);

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        return -1;
    }
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        CoUninitialize();
        return -1;
    }

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
        NotifyRunningLauncherStartRequest();

        int httpStatus = 0;
        int httpError = 0;
        if (RequestRunningLauncherRunClient(2, 150, &httpStatus, &httpError)) {
            CloseHandle(g_hSingleInstanceMutex);
            g_hSingleInstanceMutex = NULL;
            return 0;
        }

        if (httpStatus != 0) {
            std::wstring message = std::format(
                L"Launcher is already running, but /RunClient returned error.\nHTTP status: {}\nHTTP error: {}",
                httpStatus,
                httpError);
            MessageBox(nullptr, message.c_str(), TEXT("Error"), MB_OK | MB_ICONERROR);
            CloseHandle(g_hSingleInstanceMutex);
            g_hSingleInstanceMutex = NULL;
            return 0;
        }

        DebugLogFmt(
            L"Existing launcher instance did not respond to /RunClient (status={}, error={}). Attempting stale-instance recovery.",
            httpStatus,
            httpError);
        const bool terminatedAny = TerminateOtherLauncherProcesses();
        if (!AcquireSingleInstanceMutexWithRetry(10, 200)) {
            std::wstring message = std::format(
                L"Launcher is already running, but /RunClient failed.\nHTTP status: {}\nHTTP error: {}",
                httpStatus,
                httpError);
            if (terminatedAny) {
                message += L"\nStale launcher process was terminated, but mutex takeover failed.";
            }
            else {
                message += L"\nFailed to terminate stale launcher process.";
            }
            MessageBox(nullptr, message.c_str(), TEXT("Error"), MB_OK | MB_ICONERROR);
            if (g_hSingleInstanceMutex) {
                CloseHandle(g_hSingleInstanceMutex);
                g_hSingleInstanceMutex = NULL;
            }
            return 0;
        }

        DebugLog(L"Recovered single-instance ownership after stale launcher termination.");
    }

    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, true)) {
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
    LauncherUpdateCoordinator updateCoordinator(g_hWnd, g_strCurrentModulePath, g_strCurrentExeName, g_strWorkPath, g_p2pController.GetP2PSettings());
    g_updateCoordinatorPtr = &updateCoordinator;
    g_p2pController.SetUpdateCoordinator(g_updateCoordinatorPtr);
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

        g_p2pController.UpdateProgressUi(g_hWnd, updateCoordinator, g_splashRenderer);
        Sleep(15);
    }

    if (g_runClientRequestInFlight.load(std::memory_order_acquire)) {
        DebugLog(L"Waiting for in-flight RunClient request before shutdown.");
        while (g_runClientRequestInFlight.load(std::memory_order_acquire)) {
            Sleep(20);
        }
    }

    updateCoordinator.Stop();
    g_updateCoordinatorPtr = nullptr;
    g_p2pController.SetUpdateCoordinator(nullptr);
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }

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

    HWND hWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, szWindowClass, L"枫火岛重生",
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
    static bool s_draggingWindow = false;
    static POINT s_dragStartCursor = { 0, 0 };
    static POINT s_dragStartWindow = { 0, 0 };

    switch (message)
    {
    case WM_CREATE:
        SetLayeredWindowAttributes(hWnd, kTransparentColorKey, 255, LWA_COLORKEY);
        g_p2pController.LoadStunServers();
        g_p2pController.ApplyP2PSettings();
        g_splashRenderer.EnsureAnimationFramesLoaded();
        if (g_hasSavedWindowPos) {
            g_splashRenderer.RestartDockToPosition(g_savedWindowPos);
        }
        else {
            g_splashRenderer.RestartDockToCorner();
        }
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
    case WM_EXTERNAL_RUNCLIENT_REQUEST:
    {
        g_manualTrayMode = false;
        g_trayShownForDownload = false;
        if (!IsWindowVisible(hWnd)) {
            g_trayIconManager.RestoreFromTray(hWnd);
        }
        else {
            ShowWindow(hWnd, SW_RESTORE);
            ShowWindow(hWnd, SW_SHOW);
        }

        // Force z-order promotion without permanently pinning topmost.
        SetWindowPos(
            hWnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(
            hWnd,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hWnd);

        if (g_updateCoordinatorPtr) {
            g_updateCoordinatorPtr->SetLauncherStatus(L"启动中...");
        }
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (g_splashRenderer.IsFollowingGameWindows()) {
            s_lastIdleClickTick = 0;
            return 0;
        }

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
            RequestNewGameAsync(hWnd);
            return 0;
        }

        g_splashRenderer.CancelDockToCorner();
        s_draggingWindow = true;
        GetCursorPos(&s_dragStartCursor);
        RECT dragRect{};
        if (GetWindowRect(hWnd, &dragRect)) {
            s_dragStartWindow.x = dragRect.left;
            s_dragStartWindow.y = dragRect.top;
        }
        else {
            s_dragStartWindow = g_ptWindow;
        }
        SetCapture(hWnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (s_draggingWindow) {
            if ((wParam & MK_LBUTTON) == 0) {
                s_draggingWindow = false;
                ReleaseCapture();
                return 0;
            }
            POINT dragCursor{};
            GetCursorPos(&dragCursor);
            const int newX = s_dragStartWindow.x + (dragCursor.x - s_dragStartCursor.x);
            const int newY = s_dragStartWindow.y + (dragCursor.y - s_dragStartCursor.y);
            SetWindowPos(hWnd, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (s_draggingWindow) {
            s_draggingWindow = false;
            ReleaseCapture();
            RECT dragRect{};
            if (GetWindowRect(hWnd, &dragRect)) {
                g_ptWindow.x = dragRect.left;
                g_ptWindow.y = dragRect.top;
                SaveLauncherWindowPosition(g_ptWindow);
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        s_draggingWindow = false;
        break;
    case WM_RBUTTONUP:
        if (g_splashRenderer.IsFollowingGameWindows()) {
            return 0;
        }
    {
        POINT pt{};
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, ID_START_NEW_GAME, L"\u5f00\u59cb\u65b0\u6e38\u620f");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_HIDE_TO_TRAY, L"\u9690\u85cf\u5230\u6258\u76d8");
        AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"\u9000\u51fa");
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
            g_manualTrayMode = false;
            g_trayShownForDownload = false;
            g_trayIconManager.RestoreFromTray(hWnd);
            return 0;
        }
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt{};
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"\u6253\u5f00");
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"\u9000\u51fa");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
            return 0;
        }
        break;
    case WM_MINIMIZE_TO_TRAY:
        g_manualTrayMode = true;
        g_trayShownForDownload = false;
        g_trayIconManager.MinimizeToTray(hWnd, g_hInstance);
        break;
    case WM_DELETE_TRAY:
        if (!g_manualTrayMode) {
            g_trayShownForDownload = false;
            g_trayIconManager.RestoreFromTray(hWnd);
        }
        break;
    case WM_SHOW_FOR_DOWNLOAD:
        if (g_manualTrayMode) {
            g_trayIconManager.RestoreFromTray(hWnd);
            g_trayShownForDownload = true;
        }
        else if (!IsWindowVisible(hWnd)) {
            g_trayIconManager.RestoreFromTray(hWnd);
            g_trayShownForDownload = false;
        }
        break;
    case WM_HIDE_AFTER_DOWNLOAD:
        if (g_manualTrayMode && g_trayShownForDownload) {
            g_trayIconManager.MinimizeToTray(hWnd, g_hInstance);
            g_trayShownForDownload = false;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            g_manualTrayMode = true;
            g_trayShownForDownload = false;
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
            g_manualTrayMode = false;
            g_trayShownForDownload = false;
            g_trayIconManager.RestoreFromTray(hWnd);
            break;
        case ID_START_NEW_GAME:
            if (!g_splashRenderer.IsFollowingGameWindows()) {
                RequestNewGameAsync(hWnd);
            }
            break;
        case ID_HIDE_TO_TRAY:
            g_manualTrayMode = true;
            g_trayShownForDownload = false;
            g_trayIconManager.MinimizeToTray(hWnd, g_hInstance);
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
            g_ptWindow.x = static_cast<SHORT>(LOWORD(lParam));
            g_ptWindow.y = static_cast<SHORT>(HIWORD(lParam));
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        KillTimer(hWnd, kAnimTimerId);
        g_trayIconManager.Delete();
        SaveLauncherWindowPosition(g_ptWindow);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
