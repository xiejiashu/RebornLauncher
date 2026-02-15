#pragma once

#include <windows.h>
#include <string>

class UpdateForgeApp
{
public:
    explicit UpdateForgeApp(HINSTANCE hInst);
    int Run(int nCmdShow);

private:
    void InitWindow(int nCmdShow);
    void CreateControls();
    void LayoutControls(int width, int height);
    void OnBrowse();
    void OnBrowseJsonFile();
    void OnEncryptJsonPayload();
    void OnDecryptJsonPayload();
    void OnOpenDiffPackDialog();
    void OnGenerate();
    void RunWorker(std::wstring root, std::wstring key, bool encrypt);
    void LoadCachedSettings();
    void SaveCachedSettings();
    void SyncEncryptUiState();
    void UpdateStatusText(const std::wstring& text);
    void UpdateProgress(int processed, int total);
    void AppendProgressAsync(int processed, int total);
    void SetBusy(bool busy);
    void Log(const std::wstring& text);
    void AppendLogAsync(const std::wstring& text);
    void OnWorkerFinished(bool success, const std::wstring& message);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInst{};
    HWND m_hWnd{};
    HWND m_lblPath{};
    HWND m_editPath{};
    HWND m_btnBrowse{};
    HWND m_lblKey{};
    HWND m_editKey{};
    HWND m_chkEncrypt{};
    HWND m_btnGenerate{};
    HWND m_lblUrlInput{};
    HWND m_editUrlInput{};
    HWND m_btnJsonBrowse{};
    HWND m_btnEncryptUrl{};
    HWND m_btnDecryptPayload{};
    HWND m_lblUrlOutput{};
    HWND m_editUrlOutput{};
    HWND m_lblLog{};
    HWND m_editLog{};
    HWND m_statusBar{};
    HWND m_statusProgress{};
    HWND m_statusProgressText{};
    int m_processedCount{};
    int m_totalCount{};
    HANDLE m_hWorker{};
    bool m_isBusy{};
};
