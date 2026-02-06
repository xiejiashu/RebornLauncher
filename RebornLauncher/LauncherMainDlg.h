#pragma once
class LauncherMainDlg
{
public:
	void Create(HWND hParentWnd);
	void CreateStatusBar();
	void UpdateStatusBar(int TotalProgress, int CurrentProgress, int CurrentFile, int TotalFiles);
private:
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
	HANDLE m_hClientProcess{ nullptr };
};