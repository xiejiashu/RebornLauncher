#include "framework.h"
#include "TrayIconManager.h"

#include "RebornLauncher.h"
#include <cstdio>

TrayIconManager::TrayIconManager(bool& renderingFlag)
    : m_renderingFlag(renderingFlag) {
}

void TrayIconManager::Init(HWND hWnd, HINSTANCE hInstance) {
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_REBORNLAUNCHER));
    if (!m_nid.hIcon) {
        char message[192] = {};
        sprintf_s(
            message,
            sizeof(message),
            "[UF-TRAY-ICON] LoadIcon failed, error=%lu, instance=%p\n",
            static_cast<unsigned long>(GetLastError()),
            hInstance);
        OutputDebugStringA(message);
    }
    lstrcpy(m_nid.szTip, L"RebornLauncher");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void TrayIconManager::Delete() {
    if (m_nid.cbSize == 0) {
        m_renderingFlag = true;
        return;
    }
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
    if (m_nid.hIcon) {
        DestroyIcon(m_nid.hIcon);
        m_nid.hIcon = nullptr;
    }
    m_nid = {};
    m_renderingFlag = true;
}

void TrayIconManager::MinimizeToTray(HWND hWnd, HINSTANCE hInstance) {
    if (m_nid.cbSize == 0) {
        Init(hWnd, hInstance);
    }
    ShowWindow(hWnd, SW_HIDE);
    m_renderingFlag = false;
}

void TrayIconManager::RestoreFromTray(HWND hWnd) {
    Delete();
    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
    m_renderingFlag = true;
}
