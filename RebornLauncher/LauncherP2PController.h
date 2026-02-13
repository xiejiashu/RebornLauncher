#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "framework.h"
#include "P2PClient.h"

class LauncherSplashRenderer;
class WorkThread;

class LauncherP2PController {
public:
    static constexpr UINT kIdCheckP2P = 5001;
    static constexpr UINT kIdStunList = 5002;
    static constexpr UINT kIdStunEdit = 5003;
    static constexpr UINT kIdStunAdd = 5004;

    void InitializePaths(const std::wstring& currentModulePath, const std::wstring& workPath);
    void SetWorkThread(WorkThread* workThread);

    void LoadStunServers();
    void ApplyP2PSettings();
    const P2PSettings& GetP2PSettings() const;

    void CreateMainControls(HWND hWnd, HINSTANCE hInst);
    void LayoutMainControls(HWND hWnd);
    void UpdateProgressUi(HWND hWnd, WorkThread& workThread, LauncherSplashRenderer& splashRenderer);
    bool HandleCommand(WPARAM wParam, LPARAM lParam);

private:
    struct UiHandles {
        HWND checkP2P{ nullptr };
        HWND stunList{ nullptr };
        HWND stunEdit{ nullptr };
        HWND addStunBtn{ nullptr };
        HWND statusText{ nullptr };
        HWND stunTitle{ nullptr };
        HWND fileLabel{ nullptr };
        HWND totalLabel{ nullptr };
        HWND totalProgress{ nullptr };
        HWND fileProgress{ nullptr };
    };

    std::filesystem::path GetStunConfigPath() const;
    void RefreshStunListUI();
    void SaveStunServers();
    void AddStunServerFromEdit();
    void RemoveSelectedStunServer();

    UiHandles m_ui;
    std::vector<std::wstring> m_stunServers;
    P2PSettings m_p2pSettings;
    std::wstring m_currentModulePath;
    std::wstring m_workPath;
    std::wstring m_animStatusText{ L"Updating resources..." };
    WorkThread* m_workThreadPtr{ nullptr };
    HINSTANCE m_hInst{ nullptr };
    static constexpr const wchar_t* kStunListFile = L"p2p_stun_servers.txt";
};
