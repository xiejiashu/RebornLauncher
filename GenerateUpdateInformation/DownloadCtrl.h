#pragma once

#include <afxwin.h>
#include <afxcmn.h>

#define CLASS_NAME _T("DownloadCtrl")

// MFC���пؼ����ܼ�����ʹ�� https://blog.csdn.net/linyibin_123/article/details/134590629

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
    // ��ʾ�ļ���
    CStatic file_name_;
    CProgressCtrl progress_;
    CStatic value_; // ����ֵ   a/b
    int pos_{};
    int max_{};
};

