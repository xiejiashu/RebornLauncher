#pragma once
class LauncherMainDlg
{
public:
	// 创建一个DLG 参数一个父窗口
	void Create(HWND hParentWnd);
	// 创建一个状态条
	void CreateStatusBar();
	void UpdateStatusBar(int TotalProgress, int CurrentProgress, int CurrentFile, int TotalFiles);
private:
	// 对话框过程
	static LRESULT CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
private:
	HWND m_hParentWnd { nullptr };
	HWND m_hWnd{ nullptr };
	HWND m_hStatus{ nullptr };
	HWND m_hProgressTotal{ nullptr };
	HWND m_hProgressCurrent{ nullptr };
	DWORD m_CurrentFile{ 0 };
	DWORD m_TotalFiles{ 0 };
	// 目标进程HANDLE
	HANDLE m_hClientProcess{ nullptr };
};