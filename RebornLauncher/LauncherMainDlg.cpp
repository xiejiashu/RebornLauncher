#include "framework.h"
#include "LauncherMainDlg.h"
#include "resource.h"
#include <commctrl.h> // Add this include for STATUSCLASSNAME and SBARS_SIZEGRIP

void LauncherMainDlg::Create(HWND hParentWnd)
{
	// 鍒涘缓涓€涓獥鍙?
	m_hWnd = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DLG_MAIN), hParentWnd, (DLGPROC)LauncherMainDlg::DlgProc);
	ShowWindow(m_hWnd, SW_SHOW);
	UpdateWindow(m_hWnd);
	CreateStatusBar();
	UpdateStatusBar(100, 50, 1, 10);
}

void LauncherMainDlg::CreateStatusBar()
{
	// 鍒涘缓鐘舵€佹潯鍦ㄧ獥鍙ｄ笂
	m_hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
	// 缁欑姸鎬佹潯娣诲姞涓€涓爣绛剧敤浠ヨ鏄庯紝 娣诲姞涓€涓鐞嗘枃浠舵暟閲忔枃瀛?姣斿 1/1000 娣诲姞涓€涓€昏繘搴︽潯 鍒濆鍊间负0 鏈€澶?100锛屾坊鍔犱竴涓繘搴︽潯鏌ョ湅姝ｅ湪涓嬭浇鐨勬枃浠惰繘搴?
	int parts[] = { 200, 300, 500, -1 };
	SendMessage(m_hStatus, SB_SETPARTS, 4, (LPARAM)parts);
	SendMessage(m_hStatus, SB_SETTEXT, 0, (LPARAM)L"姝ｅ湪妫€鏌ユ洿鏂?..");
	SendMessage(m_hStatus, SB_SETTEXT, 1, (LPARAM)L"1/1000");
	// 鏉＄姸鐨勯偅绉嶈繘搴︽潯
	RECT rc;
	SendMessage(m_hStatus, SB_GETRECT, 2, (LPARAM)&rc); // 鑾峰彇绗?涓儴鍒嗙殑鐭╁舰鍖哄煙
	m_hProgressTotal = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hStatus, NULL, GetModuleHandle(NULL), NULL);
	SendMessage(m_hProgressTotal, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(m_hProgressTotal, PBM_SETPOS, 0, 0);

	SendMessage(m_hStatus, SB_GETRECT, 3, (LPARAM)&rc); // 鑾峰彇绗?涓儴鍒嗙殑鐭╁舰鍖哄煙

	m_hProgressCurrent = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hStatus, NULL, GetModuleHandle(NULL), NULL);
	SendMessage(m_hProgressCurrent, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessage(m_hProgressCurrent, PBM_SETPOS, 0, 0);
}

void LauncherMainDlg::UpdateStatusBar(int TotalProgress, int CurrentProgress, int CurrentFile, int TotalFiles)
{
	SendMessage(m_hProgressTotal, PBM_SETPOS, TotalProgress, 0);
	SendMessage(m_hProgressCurrent, PBM_SETPOS, CurrentProgress, 0);
	TCHAR buffer[256];
	swprintf_s(buffer, L"%d/%d", CurrentFile, TotalFiles);
	SendMessage(m_hStatus, SB_SETTEXT, 1, (LPARAM)buffer);
}

LRESULT LauncherMainDlg::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
	{
		// 璁剧疆绐楀彛鏍囬
		SetWindowText(hWnd, L"Reborn Launcher");
		return TRUE;
	}
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// 鍒嗘瀽鑿滃崟閫夋嫨:
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
	// 鍙充笂瑙掔殑鍙?
	case WM_DESTROY:
	{
		PostQuitMessage(0);
		break;
	}
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