#include "framework.h"
#include "LauncherP2PController.h"

#include <algorithm>
#include <cmath>

#include <CommCtrl.h>

#include "Encoding.h"
#include "LauncherP2PConfig.h"
#include "LauncherSplashRenderer.h"
#include "WorkThread.h"

void LauncherP2PController::InitializePaths(const std::wstring& currentModulePath, const std::wstring& workPath) {
    m_currentModulePath = currentModulePath;
    m_workPath = workPath;
}

void LauncherP2PController::SetWorkThread(WorkThread* workThread) {
    m_workThreadPtr = workThread;
}

std::filesystem::path LauncherP2PController::GetStunConfigPath() const {
    if (!m_workPath.empty()) {
        return std::filesystem::path(m_workPath) / kStunListFile;
    }
    if (!m_currentModulePath.empty()) {
        return std::filesystem::path(m_currentModulePath).parent_path() / kStunListFile;
    }
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileName(nullptr, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path() / kStunListFile;
}

void LauncherP2PController::RefreshStunListUI() {
    if (!m_ui.stunList) {
        return;
    }
    SendMessage(m_ui.stunList, LB_RESETCONTENT, 0, 0);
    for (const auto& server : m_stunServers) {
        SendMessage(m_ui.stunList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(server.c_str()));
    }
}

void LauncherP2PController::SaveStunServers() {
    SaveStunServersToFile(GetStunConfigPath(), m_stunServers);
}

void LauncherP2PController::LoadStunServers() {
    m_stunServers = LoadStunServersFromFile(GetStunConfigPath());
    RefreshStunListUI();
}

void LauncherP2PController::ApplyP2PSettings() {
    m_p2pSettings.enabled = m_ui.checkP2P == nullptr
        ? true
        : (SendMessage(m_ui.checkP2P, BM_GETCHECK, 0, 0) == BST_CHECKED);
    std::string envStunRaw = ReadEnvVarTrimmed("P2P_STUN_SERVERS");
    if (envStunRaw.empty()) {
        envStunRaw = ReadEnvVarTrimmed("STUN_SERVERS");
    }
    m_p2pSettings.stunServers = BuildMergedStunServers(m_stunServers, envStunRaw);

    const std::filesystem::path workDir = m_workPath.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(m_workPath);
    m_p2pSettings.signalEndpoint = ResolveSignalEndpoint(workDir);
    m_p2pSettings.signalAuthToken = ResolveSignalAuthToken(workDir);

    if (m_ui.statusText) {
        std::wstring status = m_p2pSettings.enabled ? L"P2P: enabled" : L"P2P: disabled";
        if (!m_p2pSettings.signalEndpoint.empty()) {
            status += L" | Signal: custom";
        } else {
            status += L" | Signal: auto";
        }
        SetWindowTextW(m_ui.statusText, status.c_str());
    }

    m_animStatusText = m_p2pSettings.enabled
        ? L"Updating resources with P2P..."
        : L"Updating resources...";

    if (m_workThreadPtr) {
        m_workThreadPtr->UpdateP2PSettings(m_p2pSettings);
    }
}

const P2PSettings& LauncherP2PController::GetP2PSettings() const {
    return m_p2pSettings;
}

void LauncherP2PController::AddStunServerFromEdit() {
    if (!m_ui.stunEdit) {
        return;
    }
    wchar_t buffer[256]{};
    GetWindowTextW(m_ui.stunEdit, buffer, static_cast<int>(std::size(buffer)));
    std::wstring value = TrimWide(buffer);
    if (value.empty()) {
        return;
    }
    for (const auto& existing : m_stunServers) {
        if (_wcsicmp(existing.c_str(), value.c_str()) == 0) {
            return;
        }
    }
    m_stunServers.push_back(value);
    RefreshStunListUI();
    SaveStunServers();
    ApplyP2PSettings();
    SetWindowTextW(m_ui.stunEdit, L"");
}

void LauncherP2PController::RemoveSelectedStunServer() {
    if (!m_ui.stunList) {
        return;
    }
    int sel = static_cast<int>(SendMessage(m_ui.stunList, LB_GETCURSEL, 0, 0));
    if (sel != LB_ERR && sel < static_cast<int>(m_stunServers.size())) {
        m_stunServers.erase(m_stunServers.begin() + sel);
        RefreshStunListUI();
        SaveStunServers();
        ApplyP2PSettings();
    }
}

void LauncherP2PController::LayoutMainControls(HWND hWnd) {
    RECT rc{};
    GetClientRect(hWnd, &rc);
    const int clientW = rc.right - rc.left;
    const int clientH = rc.bottom - rc.top;

    const int margin = 16;
    const int gap = 24;
    const int leftW = (std::max)(320, clientW / 2 - gap);
    const int rightX = margin + leftW + gap;
    const int rightW = (std::max)(260, clientW - rightX - margin);

    int y = margin;
    MoveWindow(m_ui.statusText, margin, y, leftW, 24, TRUE);

    y += 28;
    MoveWindow(m_ui.checkP2P, margin, y, leftW, 24, TRUE);

    y += 32;
    MoveWindow(m_ui.stunTitle, margin, y, leftW, 20, TRUE);

    y += 20;
    const int editH = 24;
    const int addBtnW = 90;
    const int bottomY = clientH - margin - editH;
    const int listH = (std::max)(90, bottomY - 8 - y);
    MoveWindow(m_ui.stunList, margin, y, leftW, listH, TRUE);
    MoveWindow(m_ui.stunEdit, margin, bottomY, leftW - addBtnW - 8, editH, TRUE);
    MoveWindow(m_ui.addStunBtn, margin + leftW - addBtnW, bottomY, addBtnW, editH, TRUE);

    int ry = margin;
    MoveWindow(m_ui.totalLabel, rightX, ry, rightW, 20, TRUE);
    ry += 24;
    MoveWindow(m_ui.totalProgress, rightX, ry, rightW, 24, TRUE);
    ry += 38;
    MoveWindow(m_ui.fileLabel, rightX, ry, rightW, 20, TRUE);
    ry += 24;
    MoveWindow(m_ui.fileProgress, rightX, ry, rightW, 24, TRUE);
}

void LauncherP2PController::CreateMainControls(HWND hWnd, HINSTANCE hInst) {
    m_hInst = hInst;
    const int margin = 16;
    const int columnWidth = 320;
    int y = margin;

    m_ui.statusText = CreateWindowExW(
        0, L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
        margin, y, columnWidth, 24, hWnd, nullptr, hInst, nullptr);

    y += 28;
    m_ui.checkP2P = CreateWindowExW(
        0, L"BUTTON", L"Enable P2P", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin, y, columnWidth, 24, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCheckP2P)), hInst, nullptr);
    SendMessage(m_ui.checkP2P, BM_SETCHECK, BST_CHECKED, 0);

    y += 32;
    m_ui.stunTitle = CreateWindowExW(
        0, L"STATIC", L"STUN Servers (double click to remove)", WS_CHILD | WS_VISIBLE,
        margin, y, columnWidth, 20, hWnd, nullptr, hInst, nullptr);

    y += 20;
    m_ui.stunList = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        margin, y, columnWidth, 150, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStunList)), hInst, nullptr);

    y += 160;
    m_ui.stunEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        margin, y, columnWidth - 110, 24, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStunEdit)), hInst, nullptr);

    m_ui.addStunBtn = CreateWindowExW(
        0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
        margin + columnWidth - 100, y, 100, 24, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStunAdd)), hInst, nullptr);

    const int rightX = margin + columnWidth + 40;
    int ry = margin;
    m_ui.totalLabel = CreateWindowExW(
        0, L"STATIC", L"Total: 0/0", WS_CHILD | WS_VISIBLE,
        rightX, ry, 220, 20, hWnd, nullptr, hInst, nullptr);
    ry += 24;
    m_ui.totalProgress = CreateWindowExW(
        0, PROGRESS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
    SendMessage(m_ui.totalProgress, PBM_SETRANGE32, 0, 100);

    ry += 36;
    m_ui.fileLabel = CreateWindowExW(
        0, L"STATIC", L"File: idle", WS_CHILD | WS_VISIBLE,
        rightX, ry, 420, 20, hWnd, nullptr, hInst, nullptr);
    ry += 24;
    m_ui.fileProgress = CreateWindowExW(
        0, PROGRESS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE, rightX, ry, 420, 22, hWnd, nullptr, hInst, nullptr);
    SendMessage(m_ui.fileProgress, PBM_SETRANGE32, 0, 100);

    LayoutMainControls(hWnd);
}

void LauncherP2PController::UpdateProgressUi(HWND hWnd, WorkThread& workThread, LauncherSplashRenderer& splashRenderer) {
    splashRenderer.RefreshOverlayState(hWnd, workThread);

    const int totalCount = workThread.GetTotalDownload();
    const int currentCount = workThread.GetCurrentDownload();
    const int fileSize = workThread.GetCurrentDownloadSize();
    const int fileProgress = workThread.GetCurrentDownloadProgress();
    if (m_ui.totalProgress && m_ui.fileProgress) {
        SendMessage(m_ui.totalProgress, PBM_SETRANGE32, 0, (std::max)(1, totalCount));
        SendMessage(m_ui.totalProgress, PBM_SETPOS, currentCount, 0);
        SendMessage(m_ui.fileProgress, PBM_SETRANGE32, 0, (std::max)(1, fileSize));
        SendMessage(m_ui.fileProgress, PBM_SETPOS, fileProgress, 0);
    }

    std::wstring fileName = workThread.GetCurrentDownloadFile();
    if (!fileName.empty()) {
        m_animStatusText = L"Updating: " + fileName;
    }

    const int safeTotal = (std::max)(1, totalCount);
    const int safeCurrent = (std::max)(0, (std::min)(currentCount, safeTotal));
    double fileRatio = 0.0;
    if (fileSize > 0) {
        fileRatio = static_cast<double>((std::max)(0, fileProgress)) / static_cast<double>(fileSize);
        fileRatio = (std::max)(0.0, (std::min)(1.0, fileRatio));
    }
    double overall = (static_cast<double>(safeCurrent) + fileRatio) / static_cast<double>(safeTotal);
    overall = (std::max)(0.0, (std::min)(1.0, overall));
    int percent = static_cast<int>(std::round(overall * 100.0));
    if (safeCurrent >= safeTotal) {
        percent = 100;
    }

    splashRenderer.SetDownloadPercent(hWnd, percent);
}

bool LauncherP2PController::HandleCommand(WPARAM wParam, LPARAM lParam) {
    const int controlId = LOWORD(wParam);
    const int notifyCode = HIWORD(wParam);
    (void)lParam;

    switch (controlId) {
    case kIdCheckP2P:
        if (notifyCode == BN_CLICKED) {
            ApplyP2PSettings();
            return true;
        }
        break;
    case kIdStunAdd:
        if (notifyCode == BN_CLICKED) {
            AddStunServerFromEdit();
            return true;
        }
        break;
    case kIdStunList:
        if (notifyCode == LBN_DBLCLK) {
            RemoveSelectedStunServer();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}
