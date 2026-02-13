#pragma once

#include "framework.h"
#include <shellapi.h>

class TrayIconManager {
public:
    explicit TrayIconManager(bool& renderingFlag);

    void Init(HWND hWnd, HINSTANCE hInstance);
    void Delete();
    void MinimizeToTray(HWND hWnd, HINSTANCE hInstance);
    void RestoreFromTray(HWND hWnd);

private:
    bool& m_renderingFlag;
    NOTIFYICONDATA m_nid{};
};
