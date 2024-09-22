#include "framework.h"
#include "LauncherMainDlg.h"
#include "resource.h"
#include <commctrl.h> // Add this include for STATUSCLASSNAME and SBARS_SIZEGRIP

void LauncherMainDlg::Create(HWND hParentWnd)
{
	// 创建一个窗口
	m_hWnd = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DLG_MAIN), hParentWnd, (DLGPROC)LauncherMainDlg::DlgProc);
	ShowWindow(m_hWnd, SW_SHOW);
	UpdateWindow(m_hWnd);
	CreateStatusBar();
	UpdateStatusBar(100, 50, 1, 10);
}

void LauncherMainDlg::CreateStatusBar()
{
	// 创建状态条在窗口上
	m_hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
	// 给状态条添加一个标签用以说明， 添加一个处理文件数量文字 比如 1/1000 添加一个总进度条 初始值为0 最大 100，添加一个进度条查看正在下载的文件进度
	int parts[] = { 200, 300, 500, -1 };
	SendMessage(m_hStatus, SB_SETPARTS, 4, (LPARAM)parts);
	SendMessage(m_hStatus, SB_SETTEXT, 0, (LPARAM)L"正在检查更新...");
	SendMessage(m_hStatus, SB_SETTEXT, 1, (LPARAM)L"1/1000");
	// 条状的那种进度条
	RECT rc;
	SendMessage(m_hStatus, SB_GETRECT, 2, (LPARAM)&rc); // 获取第3个部分的矩形区域
	m_hProgressTotal = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hStatus, NULL, GetModuleHandle(NULL), NULL);
	SendMessage(m_hProgressTotal, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(m_hProgressTotal, PBM_SETPOS, 0, 0);

	SendMessage(m_hStatus, SB_GETRECT, 3, (LPARAM)&rc); // 获取第4个部分的矩形区域

	m_hProgressCurrent = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hStatus, NULL, GetModuleHandle(NULL), NULL);
	SendMessage(m_hProgressCurrent, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(m_hProgressCurrent, PBM_SETPOS, 0, 0);
}

void LauncherMainDlg::UpdateStatusBar(int TotalProgress, int CurrentProgress, int CurrentFile, int TotalFiles)
{
	SendMessage(m_hProgressTotal, PBM_SETPOS, TotalProgress, 0);
	SendMessage(m_hProgressCurrent, PBM_SETPOS, CurrentProgress, 0);
	wchar_t buffer[256];
	swprintf_s(buffer, L"%d/%d", CurrentFile, TotalFiles);
	SendMessage(m_hStatus, SB_SETTEXT, 1, (LPARAM)buffer);
}

LRESULT LauncherMainDlg::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
	{
		// 设置窗口标题
		SetWindowText(hWnd, L"Reborn Launcher");
		return TRUE;
	}
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// 分析菜单选择:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, &LauncherMainDlg::About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	// 右上角的叉
	case WM_CLOSE:
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;
	}
	return 0;
}

INT_PTR CALLBACK LauncherMainDlg::About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}