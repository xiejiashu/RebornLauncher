#include "DownloadCtrl.h"


void DownloadCtrl::PreSubclassWindow()
{
	// TODO: Add your specialized code here and/or call the base class

	//auto dummy = _T("abcdefghijklmnopqrstuvwxyzABCDEFabcdefghijklmnopqrstuvwxyz");
	auto dummy = _T("");
	auto ret = file_name_.Create(dummy, WS_CHILD | WS_VISIBLE | SS_RIGHT, CRect{ 2, 2, 530, 20 }, this);
	progress_.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect{2, 30, 350, 50}, this, 1);
	// dummy = _T("123456KB / 123456KB");
	value_.Create(dummy, WS_CHILD | WS_VISIBLE | SS_RIGHT, CRect{ 360, 30, 530, 50 }, this);
	//progress_.SetPos(90);
	CWnd::PreSubclassWindow();
}

DownloadCtrl::DownloadCtrl()
{
	RegisterWindowClass();
}

BOOL DownloadCtrl::RegisterWindowClass()
{
	WNDCLASS wndcls{};
	if (auto hInst = AfxGetInstanceHandle(); !GetClassInfo(hInst, CLASS_NAME, &wndcls)) {
		wndcls.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW ;
		wndcls.lpfnWndProc = ::DefWindowProc;
		wndcls.cbClsExtra = wndcls.cbWndExtra = 0;
		wndcls.hInstance = hInst;
		wndcls.hIcon = nullptr;
		wndcls.hCursor = AfxGetApp()->LoadStandardCursor(IDC_ARROW);
		wndcls.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
		wndcls.lpszMenuName = nullptr;
		wndcls.lpszClassName = CLASS_NAME;

		if (!AfxRegisterClass(&wndcls)) {
			AfxThrowResourceException();
			return FALSE;
		}
	}
	return TRUE;
}

BEGIN_MESSAGE_MAP(DownloadCtrl, CWnd)
	ON_MESSAGE(WM_SET_FILE, OnSetFile)
	ON_MESSAGE(WM_SET_MAX, OnSetMax)
	ON_MESSAGE(WM_ADD_POS, OnAddPos)
END_MESSAGE_MAP()


LRESULT DownloadCtrl::OnSetFile(WPARAM w, LPARAM l)
{
	LPCTSTR title = LPCTSTR(w);
	file_name_.SetWindowText(title);
	return 0;
}

LRESULT DownloadCtrl::OnSetMax(WPARAM w, LPARAM l)
{
	max_ = w;
	pos_ = 0;
	progress_.SetRange(0, w);
	update();
	return 0;
}

LRESULT DownloadCtrl::OnAddPos(WPARAM w, LPARAM l)
{
	pos_ += w;
	update();
	return 0;
}

void DownloadCtrl::update()
{
	progress_.SetPos(pos_);
	char buf[256];
	sprintf(buf, "%dKB / %dKB", pos_/1024, max_/1024);
	value_.SetWindowText(buf);
}
