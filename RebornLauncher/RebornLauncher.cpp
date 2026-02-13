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
#include <filesystem>
#include <iostream>
#include <process.h>
#include <string>
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
constexpr UINT_PTR kAnimTimerId = 7001;
constexpr UINT kAnimIntervalMs = 90;
constexpr COLORREF kTransparentColorKey = RGB(1, 1, 1);

ULONG_PTR g_gdiplusToken = 0;
LauncherSplashRenderer g_splashRenderer;
LauncherP2PController g_p2pController;
TrayIconManager g_trayIconManager(g_bRendering);

HANDLE g_hSingleInstanceMutex = NULL;
constexpr const wchar_t* kLauncherSingleInstanceMutexName = L"Local\\MapleFireReborn.RebornLauncher.SingleInstance";

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

void MoveToDirectory(LPCTSTR lpTargetDir, LPCTSTR oldPath) {
    SetFileAttributes(oldPath, FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFile(oldPath)) {
        std::wcout << __FILEW__ << ":" << __LINE__
            << TEXT("Delete failed: ") << oldPath
            << TEXT(" err:") << GetLastError() << std::endl;
    }
    if (!CopyFile(g_strCurrentModulePath.c_str(), oldPath, TRUE)) {
        std::wcout << __FILEW__ << ":" << __LINE__ << g_strCurrentModulePath << TEXT("-->") << oldPath
            << TEXT(" Copy failed: ") << oldPath << TEXT(" err:") << GetLastError() << std::endl;
    }
    WriteProfileString(TEXT("MapleFireReborn"), TEXT("GamePath"), lpTargetDir);
}

bool IsInMapleFireRebornDir() {
    std::wstring str = g_strCurrentModulePath.c_str();
    std::filesystem::path currentPath = std::filesystem::path(g_strCurrentModulePath).parent_path();

    wchar_t desktopPath[MAX_PATH]{};
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktopPath);
    if (SUCCEEDED(hr)) {
        std::error_code ec;
        if (std::filesystem::equivalent(currentPath, std::filesystem::path(desktopPath), ec) && !ec) {
            return false;
        }
    }

    if (!currentPath.empty() && currentPath != currentPath.root_path()) {
        return true;
    }

    std::cout << "aaa" << std::endl;
    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__ << str << std::endl;
    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__
        << TEXT(" failed:") << __LINE__ << TEXT(" ") << str << std::endl;
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
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
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
    if (!IsInMapleFireRebornDir()) {
        do {
            LPCTSTR lpTargetDir = TEXT("C:\\MapleFireReborn");
            const TCHAR* dirs[] = { TEXT("D:\\MapleFireReborn"), TEXT("E:\\MapleFireReborn"), TEXT("F:\\MapleFireReborn"), TEXT("G:\\MapleFireReborn"), TEXT("C:\\MapleFireReborn") };
            for (int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
                CreateDirectory(dirs[i], NULL);
                if (GetFileAttributes(dirs[i]) != INVALID_FILE_ATTRIBUTES) {
                    lpTargetDir = dirs[i];
                    break;
                }
            }

            TCHAR szGamePath[MAX_PATH] = { 0 };
            GetProfileString(TEXT("MapleFireReborn"), TEXT("GamePath"), lpTargetDir, szGamePath, MAX_PATH);

            TCHAR oldPath[MAX_PATH];
            swprintf(oldPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());

            if (_tcsicmp(oldPath, g_strCurrentModulePath.c_str()) == 0) {
                break;
            }

            MoveToDirectory(szGamePath, oldPath);

            TCHAR shortcutPath[MAX_PATH]{};
            LPITEMIDLIST pidlDesktop = nullptr;
            HRESULT hrDesktop = SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidlDesktop);
            if (SUCCEEDED(hrDesktop) && pidlDesktop) {
                if (SHGetPathFromIDList(pidlDesktop, shortcutPath)) {
                    swprintf(shortcutPath, TEXT("%s\\MapleFireReborn.lnk"), shortcutPath);
                    CreateShortcut(shortcutPath, lpTargetDir, g_strCurrentExeName.c_str());
                }
                CoTaskMemFree(pidlDesktop);
                pidlDesktop = nullptr;
            } else {
                std::wcout << __FILEW__ << TEXT(":") << __LINE__
                    << TEXT(" SHGetSpecialFolderLocation(CSIDL_DESKTOP) failed, skip shortcut. err=")
                    << hrDesktop << std::endl;
            }

            if (!IsProcessRunning(oldPath)) {
                WriteProfileString(TEXT("MapleFireReborn"), TEXT("pid"), std::to_wstring(_getpid()).c_str());
                ShellExecute(NULL, TEXT("open"), oldPath, g_strCurrentModulePath.c_str(), lpTargetDir, SW_SHOWNORMAL);
                ExitProcess(0);
            }
            return 0;
        } while (false);
    } else {
        if (lpCmdLine) {
            DWORD pid = GetProfileInt(TEXT("MapleFireReborn"), TEXT("pid"), 0);
            if (pid && IsProcessRunning(pid)) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }

            SetFileAttributes(lpCmdLine, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(lpCmdLine);

            if (g_strCurrentExeName.compare(TEXT("RebornLauncher.exe")) != 0) {
                TCHAR newPath[MAX_PATH];
                swprintf(newPath, TEXT("%s\\RebornLauncher.exe"), g_strWorkPath.c_str());
                CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE);
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
            return 0;
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
