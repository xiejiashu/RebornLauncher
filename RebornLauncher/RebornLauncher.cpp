// RebornLauncher.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "RebornLauncher.h"
#include "LauncherMainDlg.h"
#include <objidl.h>
#include <gdiplus.h>


#pragma comment (lib,"Gdiplus.lib")

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

// GDI+ 全局变量
ULONG_PTR g_gdiplusToken = 0;
HBITMAP g_hBitmap = NULL;
HWND g_hWnd = NULL;
HINSTANCE g_hInstance = NULL;

//猪的纹理 系列帧
Gdiplus::Bitmap *g_hPigBitmap[7] = { NULL };
Gdiplus::Bitmap* g_hBkBitmap = nullptr;

// 初始化 GDI+
void InitGDIPlus(ULONG_PTR& gdiplusToken) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 加载 PNG 图像
HBITMAP LoadImageWithAlpha(LPCWSTR filePath) {
    Bitmap* bitmap = new Bitmap((TCHAR*)filePath);
    HBITMAP hBitmap;
    bitmap->GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    delete bitmap;
    return hBitmap;
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

// 从资源中加载 PNG 到 GDI+ 的 Bitmap
Gdiplus::Bitmap* LoadPngFromResource(UINT resId) {
    Gdiplus::Bitmap* pBitmap = nullptr;

    // 1. 查找资源
    HRSRC hResource = FindResource(g_hInstance, MAKEINTRESOURCE(resId), L"PNG");
    if (!hResource) {
        return nullptr;  // 未找到资源
    }

    // 2. 获取资源大小并加载资源
    DWORD imageSize = SizeofResource(g_hInstance, hResource);
    HGLOBAL hGlobal = LoadResource(g_hInstance, hResource);
    if (!hGlobal) {
        return nullptr;
    }

    // 3. 锁定资源获取指向 PNG 数据的指针
    void* pResourceData = LockResource(hGlobal);
    if (!pResourceData) {
        return nullptr;
    }

    // 4. 将资源数据拷贝到全局内存
    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        memcpy_s(pBuffer,imageSize, pResourceData, imageSize);
        GlobalUnlock(hBuffer);

        // 5. 创建 IStream 以便 GDI+ 使用
        IStream* pStream = nullptr;
        if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
            // 6. 使用 GDI+ Bitmap 构造函数从流中加载 PNG
            pBitmap = new Gdiplus::Bitmap(pStream);
            if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
                delete pBitmap;
                pBitmap = nullptr;
            }
            pStream->Release();  // 释放 IStream
        }

        // 7. 释放全局内存
        GlobalFree(hBuffer);
    }

    return pBitmap;
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
    static int alphaDirection = 5;  // 控制透明度的增减方向
    const int maxAlpha = 255;  // 透明度最大值
    const int minAlpha = 150;   // 透明度最小值

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
    }
    // 半透明渲染
	ColorMatrix colorMatrix = {
		1
	};
	colorMatrix.m[3][3] = alpha / 255.0f; // 透明度
	ImageAttributes imageAttributes;
	imageAttributes.SetColorMatrix(&colorMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
	graphics.DrawImage(g_hBkBitmap, Rect(0, 0, nWidth, nHeight), 0, 0, nWidth, nHeight, UnitPixel, &imageAttributes);


	// 用GDI + 绘制猪的帧率 大概是0.1s一帧 
    static int frame = 0;    // 当前帧编号
    static DWORD lastTime = 0; // 记录上次帧绘制的时间

    // 如果时间间隔达到了0.1秒 (100ms)，就切换到下一帧
    if (currentTime - lastTime > 100) {
        frame++;
        frame %= 7; // 总共7帧，循环播放
        lastTime = currentTime;
    }

    if (g_hPigBitmap[frame]) {
        // 将当前帧绘制到窗口
        graphics.DrawImage(g_hPigBitmap[frame], 0, 0);  // 你可以指定 x, y 坐标
    }
}

void UpdateLoadingAnimation(HWND hWnd)
{
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
    RECT rect;
	GetWindowRect(GetDesktopWindow(), &rect);

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
    UpdateLayeredWindow(hWnd, hdcScreen, &ptPos, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

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
	int x = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
	int y = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2;
	POINT ptPos = { x, y };
    UpdateLayeredWindow(hwnd, hdcScreen, &ptPos, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    // 释放资源
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 在此处放置代码。
    // 调用GDI+库准备

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_REBORNLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    g_hInstance = hInstance;

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, true))
    {
        return FALSE;
    }

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

        UpdateLoadingAnimation( msg.hwnd );

        // 退出
		if (msg.message == WM_QUIT)
		{
			break;
		}
    }

    GdiplusShutdown(g_gdiplusToken);
    DeleteObject(g_hBitmap);
	for (int i = 0; i < sizeof g_hPigBitmap / sizeof g_hPigBitmap[0]; i++)
	{
		DeleteObject(g_hPigBitmap[i]);
	}

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
       WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,500,500, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   g_hWnd = hWnd;

   // SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

   // 置顶并移到最中间
   RECT rect;
   GetWindowRect(hWnd, &rect);
   int x = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
   int y = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2;
   SetWindowPos(hWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOMOVE | SWP_NOSIZE);


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
		for (int i = 0; i < sizeof g_hPigBitmap / sizeof g_hPigBitmap[0]; i++)
		{
			g_hPigBitmap[i] = LoadPngFromResource(IDB_PIG1 + i);
		}

		g_hBkBitmap = LoadPngFromResource(IDB_UI1);
		// 从导入资源加载ID_UI1 而不是文件 从资源加载图片

        // 设置分层窗口
        SetLayeredWindow(hWnd, g_hBitmap);
		break;
	}
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
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}