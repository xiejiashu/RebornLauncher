// RebornLauncher.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "RebornLauncher.h"

#include <corecrt_math.h>

#include "LauncherMainDlg.h"
#include <objidl.h>
#include <gdiplus.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ShlDisp.h>
#include <Shlwapi.h>
#include <iostream>

#include <TlHelp32.h>
#include <Psapi.h>

#include "Frame.h"
#include "Unit.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "ResourceManager.h"
#include "WorkThread.h"
#include <httplib.h>


#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Shlwapi.lib")

#define MAX_LOADSTRING 100

using namespace Gdiplus;

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void InitTrayIcon(HWND hWnd);

// GDI+ 全局变量
ULONG_PTR g_gdiplusToken = 0;
HBITMAP g_hBitmap = NULL;
HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;
NOTIFYICONDATA nid;

// 窗口当前位置
POINT g_ptWindow = { 831, 455 };
// 窗口大小
constexpr SIZE g_szWindow = { 1024, 500 };

// 进度条进度
float g_fProgressTotal = 0.0;
// 当前进度条
float g_fProgressCurrent = 0.0;
// 当前文件总大小
int g_nCurrentFileSize = 0;
// 当前文件已下载大小
int g_nCurrentFileDownloaded = 0;
// 当前文件名
std::wstring g_strCurrentFile;

std::shared_ptr<Sprite> g_pigSprite = nullptr;

// 当前文件模块路径包含文件名
std::wstring g_strCurrentModulePath;
// 当前exe的名字
std::wstring g_strCurrentExeName;
// 当前路径，不包含文件名
std::wstring g_strWorkPath;

// 进度条开始位置
constexpr POINT g_ptProgress = { 0, 20 };

//猪的纹理 系列帧
// Gdiplus::Bitmap *g_hPigBitmap[7] = { NULL };
Gdiplus::Bitmap* g_hBkBitmap = nullptr;

SpriteManager* g_pSpriteMgr = nullptr;
ResourceManager* g_pResMgr = nullptr;

// 渲染开关
bool g_bRendering = true;

// 初始化 GDI+
void InitGDIPlus(ULONG_PTR& gdiplusToken) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 加载 PNG 图像
HBITMAP LoadImageWithAlpha(LPCSTR filePath) {

    Bitmap* bitmap = new Bitmap((TCHAR*)filePath);
    HBITMAP hBitmap;
    bitmap->GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    delete bitmap;
    return hBitmap;
}

// 删除托盘图标
void DeleteTrayIcon() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
	DestroyIcon(nid.hIcon);
	// 删除托盘图标
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
	// 解除窗口透明
	// SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
}

// 从导入资源加载图像，而不是文件
HBITMAP LoadImageFromResource(UINT resId) {
    HBITMAP hBitmap = NULL;
    // 找到 PNG 资源
    HRSRC hResource = FindResource(g_hInstance, MAKEINTRESOURCE(resId), L"PNG");
    if (hResource) {
        DWORD imageSize = SizeofResource(g_hInstance, hResource);  // 获取资源大小
        HGLOBAL hGlobal = LoadResource(g_hInstance, hResource);    // 加载资源
        if (hGlobal) {
            void* pResourceData = LockResource(hGlobal);         // 锁定资源
            if (pResourceData) {
                // 创建 IStream 以便于 GDI+ 使用
                HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
                if (hBuffer) {
                    void* pBuffer = GlobalLock(hBuffer);
                    memcpy_s(pBuffer, imageSize, pResourceData, imageSize);
                    GlobalUnlock(hBuffer);

                    IStream* pStream = NULL;
                    if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
                        Gdiplus::Image* image = new Gdiplus::Image(pStream);
                        if (image && image->GetLastStatus() == Gdiplus::Ok) {
                            Gdiplus::Bitmap* bitmap = static_cast<Gdiplus::Bitmap*>(image);
                            bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);  // 获取 HBITMAP
                            delete image;
                        }
                        pStream->Release();
                    }
                    GlobalFree(hBuffer);
                }
            }
        }
    }
    return hBitmap;
}



void OnDraw(HDC hdc,const RECT &rect) 
{
    // 先绘制背景 绘制到屏幕最中间
    Graphics graphics(hdc);
    int nWidth = g_hBkBitmap->GetWidth();
    int nHeight = g_hBkBitmap->GetHeight();
	// graphics.DrawImage(g_hBkBitmap, (INT)(rect.right - nWidth) / 2, (INT)(rect.bottom - nHeight) / 2);
        // 透明度参数
    static int alpha = 0;
    static int colorChange = 0;
    static int alphaDirection = 5;  // 控制透明度的增减方向
    const int maxAlpha = 255;  // 透明度最大值
    const int minAlpha = 150;   // 透明度最小值
    static int colorDirection = 5;  // 控制颜色变化的增减方向

    // 进度条的参数
    // static float progress = 0.0f;    // 当前进度条的进度（从0%到100%）
    const float progressSpeed = 0.00005f;  // 控制进度条前进的速度

    // 更新进度条进度
    // progress += progressSpeed;
    // if (progress > 1.0f) progress = 1.0f;  // 进度最大为100%

    // 每1秒一次透明度变化
    static DWORD lastAlphaTime = 0;
    DWORD currentTime = GetTickCount64() % UINT_MAX;
    if (currentTime - lastAlphaTime > 100) {
        lastAlphaTime = currentTime;
        if (alpha > maxAlpha)
        {
            alphaDirection = -5;
        }
        if (alpha < minAlpha)
        {
            alphaDirection = 5;
        }
        alpha += alphaDirection;

        if (alpha > 255)
        {
            alpha = 255;
        }

        // 更新颜色变化
        if (colorChange > 255) {
            colorDirection = -5;
        }
        if (colorChange < 0) {
            colorDirection = 5;
        }
        colorChange += colorDirection;
    }

    // 左边已经停止呼吸的区域
    // int progressBarWidth = (int)(rect.right * progress);  // 计算已经不呼吸的区域的宽度
    int progressBarWidth = (int)(rect.right * g_fProgressTotal);  // 计算已经不呼吸的区域的宽度
    GraphicsPath path;
    path.AddRectangle(Rect(0, 0, progressBarWidth, rect.bottom)); // 左侧不呼吸区域
    Region nonBreathingRegion(&path);  // 非呼吸区域
	int curProgressBarWidth = (int)(rect.right * g_fProgressCurrent);  // 计算已经不呼吸的区域的宽度
    
    // 半透明和颜色渐变的渲染
    ColorMatrix colorMatrixBreathing = {
        1, 0, 0, 0, 0,
        0, 1, 0, 0, 0,
        0, 0, 1, 0, 0,
        0, 0, 0, alpha / 255.0f, 0,
        0, 0, 0, 0, 1
    };

    ColorMatrix colorMatrixNonBreathing = {
        1, 0, 0, 0, 0,
        0, 1, 0, 0, 0,
        0, 0, 1, 0, 0,
        0, 0, 0, maxAlpha / 255.0f, 0, // 完全不透明
        0, 0, 0, 0, 1
    };
    
    // 为RGB颜色增加渐变效果
    colorMatrixBreathing.m[0][0] = (255 - colorChange) / 255.0f; // Red
    colorMatrixBreathing.m[1][1] = (colorChange) / 255.0f;       // Green
    colorMatrixBreathing.m[2][2] = (128 + colorChange) / 255.0f; // Blue
    
    // 设置呼吸和非呼吸的颜色矩阵
    ImageAttributes imageAttributesBreathing;
    imageAttributesBreathing.SetColorMatrix(&colorMatrixBreathing, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);


    ImageAttributes imageAttributesNonBreathing;
    imageAttributesNonBreathing.SetColorMatrix(&colorMatrixNonBreathing, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    // 绘制已经停止呼吸的区域
    graphics.SetClip(&nonBreathingRegion, CombineModeReplace); // 限制绘制区域
    graphics.DrawImage(g_hBkBitmap, Rect(0, 0, nWidth, nHeight), 0, 0, nWidth, nHeight, UnitPixel, &imageAttributesNonBreathing);
    
    // 绘制仍然在呼吸的区域
    Region breathingRegion(Rect(progressBarWidth, 0, rect.right - progressBarWidth, rect.bottom)); // 剩下的呼吸区域
    graphics.SetClip(&breathingRegion, CombineModeReplace); // 切换到呼吸区域
    graphics.DrawImage(g_hBkBitmap, Rect(0, 0, nWidth, nHeight), 0, 0, nWidth, nHeight, UnitPixel, &imageAttributesBreathing);
    // 清除剪辑
    graphics.ResetClip();

    /*
	// 用GDI + 绘制猪的帧率 大概是0.1s一帧 
    static int frame = 0;    // 当前帧编号
    static DWORD lastTime = 0; // 记录上次帧绘制的时间

    // 如果时间间隔达到了0.1秒 (100ms)，就切换到下一帧
    if (currentTime - lastTime > 100) {
        frame++;
        frame %= 7; // 总共7帧，循环播放
        lastTime = currentTime;
    }

    // 猪的旋转相关
    static float angle = 0.0f;   // 当前的旋转角度
    const float radius = 100.0f; // 旋转半径，设定为100
    const float centerX = (rect.right - nWidth) / 2.0f;  // 中心点 X
    const float centerY = (rect.bottom - nHeight) / 2.0f; // 中心点 Y

    // 每帧增加角度
    const float rotationSpeed = 0.01f;  // 调整这个值控制旋转速度，数值越小速度越慢
    angle += rotationSpeed;  
    if (angle >= 360.0f) angle -= 360.0f;

    // 计算猪的当前位置 (x, y)
    float pigX = centerX + radius * cosf(angle * 3.14159265f / 180.0f);
    float pigY = centerY + radius * sinf(angle * 3.14159265f / 180.0f) - 100;

    if (g_hPigBitmap[frame]) {
        // 将当前帧绘制到窗口
        graphics.DrawImage(g_hPigBitmap[frame], pigX,pigY);  // 你可以指定 x, y 坐标
    }*/

    // 绘制深蓝色进度条
    LinearGradientBrush brush(Rect(0, 0, curProgressBarWidth, rect.bottom), Color(255, 0, 0, 255), Color(255, 0, 0, 128), LinearGradientModeHorizontal);
    graphics.FillRectangle(&brush, 0, rect.bottom - g_ptProgress.y, curProgressBarWidth, rect.bottom);
    // 绘制光晕效果
    Pen pen(Color(128, 0, 0, 255), 10);
    pen.SetLineJoin(LineJoinRound);
    graphics.DrawRectangle(&pen, 0, rect.bottom - g_ptProgress.y, curProgressBarWidth, rect.bottom);

    // 绘制透明区域
    SolidBrush transparentBrush(Color(128, 255, 255, 0));
    graphics.FillRectangle(&transparentBrush, curProgressBarWidth, rect.bottom - g_ptProgress.y, rect.right - curProgressBarWidth, rect.bottom);

    // 在进度条上渲染当下载文件的名字 当前量/总量
	FontFamily fontFamily(L"Arial");
	Font font(&fontFamily, 16, FontStyleRegular, UnitPixel);
	SolidBrush solidBrush(Color(255, 128, 255, 255));
	std::wstring str = g_strCurrentFile + TEXT(" ") + std::to_wstring(g_nCurrentFileDownloaded) + TEXT("/") + std::to_wstring(g_nCurrentFileSize);
    // 渲染到进度条中间
	graphics.DrawString(str.c_str(), str.length(), &font, PointF((rect.right - str.length() * 16) / 2, rect.bottom - g_ptProgress.y - 30), &solidBrush);
    // graphics.DrawString(str.c_str(), str.length(), &font, PointF(0, rect.bottom - g_ptProgress.y - 30), &solidBrush);

    // 设置猪的位置
	g_pigSprite->SetX(g_ptProgress.x + g_fProgressCurrent * rect.right);
	g_pigSprite->SetY(rect.bottom - g_ptProgress.y - 30);
    
	// 绘制猪
	g_pSpriteMgr->Draw(graphics);

    // auto brush1 = SolidBrush(Color(255, 0, 0, 0));
    // auto brush2 = SolidBrush(Color(255, 255, 0, 0));
    // graphics.FillRectangle(&brush1, 0, 0, progressBarWidth, rect.bottom);
    // graphics.FillRectangle(&brush2, progressBarWidth, 0, rect.right - progressBarWidth, rect.bottom);
}

void UpdateLoadingAnimation(HWND hWnd)
{
    if (g_bRendering == false)
        return;
	//HDC hdc = GetDC(hWnd);
	//HDC hdcMem = CreateCompatibleDC(hdc);
	//RECT rect;
	//GetClientRect(hWnd, &rect);
	//HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
	//SelectObject(hdcMem, hBitmap);
	//// 绘制
	//OnDraw(hdcMem);
	//BitBlt(hdc, 0, 0, rect.right, rect.bottom, hdcMem, 0, 0, SRCCOPY);
	//// 释放资源
	//DeleteObject(hBitmap);
	//DeleteDC(hdcMem);
	//ReleaseDC(hWnd, hdc);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

	// RECT rect = { 0,0,1000,300 };
    //GetClientRect(hWnd, &rect);
    // 获取当前窗口在桌面的位置
	//RECT windowRect;
 //   GetWindowRect(GetDesktopWindow(), &windowRect);
    RECT rect = { 0, 0, g_szWindow.cx, g_szWindow.cy };

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, rect.right, rect.bottom);
    SelectObject(hdcMem, hBitmap);

    // 使用 GDI+ 绘制猪的动画帧
    OnDraw(hdcMem,rect);

    // 获取图像尺寸
    BITMAP bitmap;
    GetObject(hBitmap, sizeof(BITMAP), &bitmap);
    SIZE size = { bitmap.bmWidth, bitmap.bmHeight };

    // 定义混合函数
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    // 中心化窗口位置
    POINT ptPos = { (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2,
                    (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2 };
    POINT ptSrc = { 0, 0 };

    // 使用 UpdateLayeredWindow 来更新整个窗口
    UpdateLayeredWindow(hWnd, hdcScreen, &g_ptWindow, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    // 释放资源
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// 设置窗口为带透明度的分层窗口
void SetLayeredWindow(HWND hwnd, HBITMAP hBitmap) {
    HDC hdcScreen = GetDC(NULL); // 获取屏幕的设备上下文
    HDC hdcMem = CreateCompatibleDC(hdcScreen); // 创建内存设备上下文
    SelectObject(hdcMem, hBitmap); // 选择图像到内存设备上下文

    // 获取图像尺寸
    BITMAP bitmap;
    GetObject(hBitmap, sizeof(BITMAP), &bitmap);
    SIZE size = { bitmap.bmWidth, bitmap.bmHeight };

    // 定义混合函数
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 0, AC_SRC_ALPHA };

    // 更新分层窗口
    POINT ptSrc = { 0, 0 };
    // POINT ptPos = { 100, 100 }; // 窗口显示的位置
    // 窗口移到最中间
	RECT rect;
	GetWindowRect(hwnd, &rect);
	//g_ptWindow.x = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
	//g_ptWindow.y = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2;
    UpdateLayeredWindow(hwnd, hdcScreen, &g_ptWindow, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    // 释放资源
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// 创建图标
void CreateShortcut(LPCTSTR lpShortcutPath, LPCTSTR lpTargetPath,const LPCTSTR lpFileName) {
    IShellLink* pShellLink = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&pShellLink);
    if (SUCCEEDED(hr)) {
        // 合出一个新路径
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
    // 提取文件名
	// TCHAR* FileName = PathFindFileName(g_strCurrentModulePath.c_str());
    TCHAR newPath[MAX_PATH];
    swprintf(newPath, TEXT("%s\\%s"), lpTargetDir, g_strCurrentExeName.c_str());
    // 清清除文件的只读属性
	SetFileAttributes(newPath, FILE_ATTRIBUTE_NORMAL);
    
	// 先清除旧文件
    if (!DeleteFile(newPath))
    {
		std::wcout <<__FILEW__<<":"<<__LINE__<<TEXT( "删除失败:") << newPath << TEXT("err:")<<GetLastError()<<std::endl;
    }
    if (!CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE))
    {
        std::wcout << __FILEW__ << ":" << __LINE__ << g_strCurrentModulePath << TEXT("-->") << newPath << TEXT("移动失败:") << newPath << TEXT("err:") << GetLastError() << std::endl;
    }

    // 设置游戏目录写配置项
	WriteProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir);
}

bool IsInMapleRebornDir() {
    // 获取当前模块的文件路径
    // TCHAR filePath[MAX_PATH];
    // GetModuleFileName(NULL, filePath, MAX_PATH);

    // 获取文件名
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

    std::wcout << __FILEW__ << TEXT(":") << __FUNCTIONW__ << TEXT("失败") << TEXT(":") << __LINE__ << TEXT(" ") << str << std::endl;

    //// 去掉文件名，保留目录路径
    //*fileName = '\0';
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //// 获取父目录名
    //PathRemoveBackslash(filePath);
    //PathRemoveFileSpec(filePath);
    //MessageBox(NULL, filePath, TEXT("err"), MB_OK);
    //// 检查父目录名是否为 "MapleReborn"
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
    // 设置托盘图标数据
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER)); // 使用你的图标资源
	if (!nid.hIcon){
        std::cout << "LoadIcon failed errcode:" << GetLastError() << ":"<< g_hInstance << std::endl;
	}
    lstrcpy(nid.szTip, L"枫叶重生");
    // 添加托盘图标
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

	g_strCurrentModulePath.resize(MAX_PATH);
	GetModuleFileName(hInstance, g_strCurrentModulePath.data(), MAX_PATH);
	g_strCurrentExeName = PathFindFileName(g_strCurrentModulePath.c_str());
    g_strWorkPath.resize(MAX_PATH);
	GetCurrentDirectory(MAX_PATH, (LPTSTR)g_strWorkPath.data());

//#ifndef _DEBUG
//	std::wofstream outLog(g_strCurrentExeName + TEXT(".log"));
//    // 保存原始的缓冲区指针
//    std::wstreambuf* originalCoutBuffer = std::wcout.rdbuf();
//    // 将 std::cout 的缓冲区指针重定向到文件
//    std::wcout.rdbuf(outLog.rdbuf());
//
//	std::ofstream outLogA("Launcher.log");
//	std::streambuf* originalCoutBufferA = std::cout.rdbuf();
//	std::cout.rdbuf(outLogA.rdbuf());
//#endif

	DWORD dwPID = GetProfileInt(TEXT("MapleReborn"), TEXT("pid"),0);
    if (dwPID)
    {
        // 查看进程是否活着
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPID);
        if (IsProcessRunning(dwPID))
        {
            // 判断进程是否活着的
			//DWORD dwExitCode = 0;
   //         DWORD dwResult = WaitForSingleObject(hProcess, 0);
   //         if (dwResult != WAIT_TIMEOUT)
   //         {
   //             // 进程还活着
   //             CloseHandle(hProcess);


   //         }
            httplib::Client cli("localhost", 12345);
            auto res = cli.Get("/RunClient");
            if (res && res->status == 200)
            {
                // 启动成功
            }
            else
            {
				MessageBox(NULL, TEXT("最多只能启动2个客户端"), TEXT("err"), MB_OK);
                // 启动失败
            }
            return 0;
        }
    }

    // 把当前PID写入
	DWORD dwCurrentPID = GetCurrentProcessId();
    TCHAR szPID[32] = { 0 };
	_itow_s(dwCurrentPID, szPID, 10);
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), szPID);

    // 结束 MapleReborn 下的 Client1PID,Client2PID
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

    // TODO: 在此处放置代码。
    // 调用GDI+库准备

    // MessageBox(NULL, TEXT("0000000000000000"), TEXT("err"), MB_OK);
    std::wcout << TEXT("Start:") << lpCmdLine << std::endl;


    // 桌看当前是不是在桌面，或是在C盘目录下 如果是桌面，就把自己移到D盘(没有D盘就E盘，依次…… 如果都没有，就移到C:\MapleReborn 目录下面。
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
            // 创建目录
			CreateDirectory(dirs[i], NULL);
			if (GetFileAttributes(dirs[i]) != INVALID_FILE_ATTRIBUTES) {
				// MoveToDirectory(dirs[i]);
				lpTargetDir = dirs[i];
				break;
			}
		}

        // 读取游戏目录
        TCHAR szGamePath[MAX_PATH] = { 0 };
        GetProfileString(TEXT("MapleReborn"), TEXT("GamePath"), lpTargetDir, szGamePath, MAX_PATH);
        MoveToDirectory(szGamePath);

        // MessageBox(NULL, TEXT("3333333333333333333"), TEXT("err"), MB_OK);

		// 创建桌面快捷方式
		TCHAR shortcutPath[MAX_PATH];
        // 获取桌面路径
		LPITEMIDLIST pidlDesktop;
		HRESULT hr = SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidlDesktop);
		if (FAILED(hr)) {
			return -1;
            // MessageBox(NULL, TEXT("44444444444444444444444"), TEXT("err"), MB_OK);
		}
		SHGetPathFromIDList(pidlDesktop, shortcutPath);
		swprintf(shortcutPath, TEXT("%s\\MapleReborn.lnk"), shortcutPath);
        // 获取当前模块的名字
		// TCHAR currentPath[MAX_PATH];
		// GetModuleFileName(hInstance, currentPath, MAX_PATH);
        // 把名字提取出来拼，把路径去掉
		// TCHAR* fileName = PathFindFileName(g_strCurrentModulePath.c_str());
		CreateShortcut(shortcutPath, lpTargetDir, g_strCurrentExeName.c_str());
        // MessageBox(NULL, TEXT("55555555555555"), TEXT("err"), MB_OK);

        // 查看目标进程是否已启动，已启动就不再创建
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
            // 取消文件只读属性
			SetFileAttributes(lpCmdLine, FILE_ATTRIBUTE_NORMAL);
            DeleteFile(lpCmdLine);

            // 如果当前文件名不是 RebornLauncher.exe 那复制出一个 RebornLauncher.exe
			if (g_strCurrentExeName != TEXT("RebornLauncher.exe"))
			{
				TCHAR newPath[MAX_PATH];
				swprintf(newPath, TEXT("%s\\RebornLauncher.exe"), g_strWorkPath.c_str());
				CopyFile(g_strCurrentModulePath.c_str(), newPath, TRUE);
				// 启动新的进程
				ShellExecute(NULL, TEXT("open"), newPath, g_strCurrentModulePath.c_str(), g_strWorkPath.c_str(), SW_SHOWNORMAL);
                WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));
				ExitProcess(0);
			}
        }
    }
#endif

    g_pSpriteMgr = new SpriteManager();
	g_pResMgr = new ResourceManager(hInstance);

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    std::cout << "77777777777777" << std::endl;


    // 执行应用程序初始化:
    if (!InitInstance (hInstance, true))
    {
        std::cout << "888888888888" << std::endl;
        return FALSE;
    }

    WorkThread workThread(g_hWnd, g_strCurrentModulePath, g_strCurrentExeName, g_strWorkPath);
    InitTrayIcon(g_hWnd);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REBORNLAUNCHER));

    MSG msg;

	SetTimer(g_hWnd, 1, 100, NULL);

    // 主消息循环:
    while (true)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (workThread.GetTotalDownload() > 0)
        {
			g_fProgressTotal = workThread.GetCurrentDownload() * 1.f / workThread.GetTotalDownload() * 1.f;
        }

        if (workThread.GetCurrentDownloadSize() > 0)
        {
            g_nCurrentFileSize = workThread.GetCurrentDownloadSize();
            g_nCurrentFileDownloaded = workThread.GetCurrentDownloadProgress();

            g_fProgressCurrent = g_nCurrentFileDownloaded * 1.f / g_nCurrentFileSize * 1.f;
			g_strCurrentFile = workThread.GetCurrentDownloadFile();
        }

        g_pSpriteMgr->Update(GetTickCount64() % UINT_MAX);

        UpdateLoadingAnimation( msg.hwnd );

        // 退出
		// if (msg.message == WM_QUIT)
		// {
        //     std::cout << "eeeeeeeeeeeeeeeeeeee" << std::endl;
        //     workThread.Stop();
		// 	break;
		// }
        // 
        // if (msg.message == WM_COMMAND)
        // {
        //     int wmId = LOWORD(msg.wParam);
		// 	if (wmId == IDM_EXIT)
		// 	{
		// 		workThread.Stop();
		// 	}
        // }
    }

    std::cout << "请求中止" << std::endl;
    workThread.Stop();
	// 把配置项的进程ID清空
	WriteProfileString(TEXT("MapleReborn"), TEXT("pid"), TEXT("0"));

    std::cout << "ooooooooooooooooooo" << std::endl;

    GdiplusShutdown(g_gdiplusToken);
    DeleteObject(g_hBitmap);

    delete g_pSpriteMgr;
    delete g_pResMgr;

    CoUninitialize();
    return (int) msg.wParam;
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
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
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowExW(WS_EX_LAYERED, szWindowClass, L"枫叶重生",
       WS_POPUP, g_ptWindow.x, g_ptWindow.y,g_szWindow.cx,g_szWindow.cy, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   g_hWnd = hWnd;

   // SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

   // 置顶并移到最中间
   g_ptWindow.x = GetSystemMetrics(SM_CXSCREEN) / 3;
   g_ptWindow.y = GetSystemMetrics(SM_CYSCREEN) / 5;
   SetWindowPos(hWnd, HWND_TOPMOST, g_ptWindow.x, g_ptWindow.y, 0, 0, SWP_NOMOVE | SWP_NOSIZE);


   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCHITTEST:
    {
		LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
		if (hit == HTCLIENT)
            return HTCAPTION;
		return hit;
    }
	case WM_CREATE:
	{
		// 创建一个DLG
		// LauncherMainDlg dlg;
		// dlg.Create(hWnd);
		// 创建一个状态条
		// dlg.CreateStatusBar();
		// 更新状态条
		// dlg.UpdateStatusBar(100, 50, 1, 10);

                // 初始化 GDI+
        InitGDIPlus(g_gdiplusToken);

        // 加载透明图像
        g_hBitmap = LoadImageFromResource(IDB_UI1);
        // 加载猪
		//for (int i = 0; i < sizeof g_hPigBitmap / sizeof g_hPigBitmap[0]; i++)
		//{
		//	g_hPigBitmap[i] = LoadPngFromResource(IDB_PIG1 + i);
		//}

		g_hBkBitmap = ResourceManager::LoadPngFromResource(IDB_UI1,g_hInstance);
		// 从导入资源加载ID_UI1 而不是文件 从资源加载图片

		//g_pSpriteMgr->CreateSprite([](std::shared_ptr<Sprite> pSprite) {
		//	pSprite->SetX(100);
		//	pSprite->SetY(100);
		//	pSprite->SetWidth(100);
		//	pSprite->SetHeight(100);
		//	pSprite->SetJumpHeight(100);
		//	pSprite->SetJumpSpeed(10);
		//	pSprite->SetJumpAcceleration(1);
		//	pSprite->SetMoveSpeed(5);

  //          Frame* pFrame = new Frame();
		//	for (int i = 0; i < 3; i++)
		//	{
		//		pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_LSL_STAND0 + i));
		//	}

		//	pSprite->SetBitmapFrame(SpriteState::Stand, pFrame);

		//	pFrame = new Frame();
		//	for (int i = 0; i < 7; i++)
		//	{
		//		pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_LSL_MOVE0 + i));
		//	}

		//	pSprite->SetBitmapFrame(SpriteState::Move, pFrame);

		//	pFrame = new Frame();

		//	for (int i = 0; i < 1; i++)
		//	{
		//		pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_LSL_JUMP0 + i));
		//	}

		//	pSprite->SetBitmapFrame(SpriteState::Jump, pFrame);

		//	return true;
		//});

        g_pSpriteMgr->CreateSprite([](std::shared_ptr<Sprite> pSprite) {
			pSprite->SetX(300);
			pSprite->SetY(100);
			pSprite->SetWidth(100);
			pSprite->SetHeight(100);
			pSprite->SetJumpHeight(100);
			pSprite->SetJumpSpeed(10);
			pSprite->SetJumpAcceleration(1);
            pSprite->SetMoveSpeed(1);

            // 
			Frame* pFrame = new Frame();
            for (int i = 0; i < 3; i++)
            {
                pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_PIG1 + i));
            }
			pSprite->SetBitmapFrame(SpriteState::Stand, pFrame);

			pFrame = new Frame();
			for (int i = 0; i < 7; i++)
			{
				pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_PIG1 + i));
			}
			pSprite->SetBitmapFrame(SpriteState::Move, pFrame);

            pFrame = new Frame();
			for (int i = 0; i < 1; i++)
			{
				pFrame->AddBitmap(g_pResMgr->GetBitmap(IDB_PIG1 + i));
			}

			pSprite->SetBitmapFrame(SpriteState::Jump, pFrame);

            g_pigSprite = pSprite;
            g_pigSprite->MoveRight();

            return true;
        });

        // 设置分层窗口
        SetLayeredWindow(hWnd, g_hBitmap);
		break;
	}
    case WM_TRAYICON:
        //if (lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONDOWN) {
        //    // 处理托盘图标的点击事件，例如显示窗口
        //    // ShowWindow(hWnd, SW_RESTORE);
        //    // SetForegroundWindow(hWnd);
        //    // DeleteTrayIcon();
        //    RestoreFromTray(hWnd);
        //}

		// 右键菜单
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
		// 最小化到托盘
		MinimizeToTray(hWnd);
		break;
    case WM_DELETE_TRAY:
        RestoreFromTray(hWnd);
        break;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_MINIMIZE) {
			// 最小化到托盘
            MinimizeToTray(hWnd);
			return 0;
		}
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            // 最小化到托盘
            // MinimizeToTray(hWnd);
            
            // 退出进程
            DeleteTrayIcon();
			PostQuitMessage(0);
        }
		break;
        // 窗口被最小化
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: 在此处添加使用 hdc 的任何绘图代码...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_MOVE:
    {
        // 获取窗口当前位置
        g_ptWindow.x = LOWORD(lParam);
        g_ptWindow.y = HIWORD(lParam);
        break;
    }
    // 从任务栏叉掉时
	case WM_CLOSE:
		// 最小化到托盘
		MinimizeToTray(hWnd);
		break;
    case WM_DESTROY:
        // 删除托盘图标
        DeleteTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}