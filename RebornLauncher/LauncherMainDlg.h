#pragma once
class LauncherMainDlg
{
public:
	// 鍒涘缓涓€涓狣LG 鍙傛暟涓€涓埗绐楀彛
	void Create(HWND hParentWnd);
	// 鍒涘缓涓€涓姸鎬佹潯
	void CreateStatusBar();
	void UpdateStatusBar(int TotalProgress, int CurrentProgress, int CurrentFile, int TotalFiles);
private:
	// 瀵硅瘽妗嗚繃绋?
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
	// 鐩爣杩涚▼HANDLE
	HANDLE m_hClientProcess{ nullptr };
};