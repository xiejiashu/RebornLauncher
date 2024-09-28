#pragma once

#include <afxwin.h>
#include <afxcmn.h>

#define CLASS_NAME _T("DownloadCtrl")

// MFC所有控件介绍及基本使用 https://blog.csdn.net/linyibin_123/article/details/134590629

class DownloadCtrl :
    public CWnd
{
    virtual void PreSubclassWindow();
public:
    enum {
        WM_SET_FILE = (WM_USER + 100),
        WM_SET_MAX,
        WM_ADD_POS,
    };
    DownloadCtrl();
private:
    BOOL RegisterWindowClass();
public:
    DECLARE_MESSAGE_MAP()
    afx_msg LRESULT OnSetFile(WPARAM w, LPARAM l);
    afx_msg LRESULT OnSetMax(WPARAM w, LPARAM l);
    afx_msg LRESULT OnAddPos(WPARAM w, LPARAM l);

private:
    void update();
    // 显示文件名
    CStatic file_name_;
    CProgressCtrl progress_;
    CStatic value_; // 进度值   a/b
    int pos_{};
    int max_{};
};

