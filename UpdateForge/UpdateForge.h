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
    void OnBrowse();
    void OnGenerate();
    void RunWorker(std::wstring root, std::wstring key, bool encrypt);
    void SetBusy(bool busy);
    void Log(const std::wstring& text);
    void AppendLogAsync(const std::wstring& text);
    void OnWorkerFinished(bool success, const std::wstring& message);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInst{};
    HWND m_hWnd{};
    HWND m_editPath{};
    HWND m_btnBrowse{};
    HWND m_editKey{};
    HWND m_chkEncrypt{};
    HWND m_btnGenerate{};
    HWND m_editLog{};
    HANDLE m_hWorker{};
};
