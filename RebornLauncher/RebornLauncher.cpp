// RebornLauncher.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include <gdiplus.h>
#include "RebornLauncher.h"
#include "LauncherMainDlg.h"


// #pragma comment (lib,"Gdiplus.lib")

#define MAX_LOADSTRING 100
// using namespace Gdiplus;

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
ULONG_PTR gdiplusToken = 0;
HBITMAP hBitmap = NULL;

// 初始化 GDI+
void InitGDIPlus(ULONG_PTR& gdiplusToken) {
    //GdiplusStartupInput gdiplusStartupInput;
    //GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 加载 PNG 图像
HBITMAP LoadImageWithAlpha(LPCWSTR filePath) {
    // Bitmap* bitmap = new Bitmap((TCHAR*)filePath);
    // HBITMAP hBitmap;
    // bitmap->GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    // delete bitmap;
    return hBitmap;
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
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    // 更新分层窗口
    POINT ptSrc = { 0, 0 };
    POINT ptPos = { 100, 100 }; // 窗口显示的位置
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

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, true))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REBORNLAUNCHER));

    MSG msg;


    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
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
        InitGDIPlus(gdiplusToken);

        // 加载透明图像
        hBitmap = LoadImageWithAlpha(L"logo.png");

        // 设置分层窗口
        SetLayeredWindow(hWnd, hBitmap);
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
        // GdiplusShutdown(gdiplusToken);
        DeleteObject(hBitmap);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}