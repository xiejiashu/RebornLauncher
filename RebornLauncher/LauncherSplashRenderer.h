#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

#include "framework.h"

namespace Gdiplus {
class Bitmap;
class Graphics;
}

class WorkThread;

class LauncherSplashRenderer {
public:
    LauncherSplashRenderer() = default;
    ~LauncherSplashRenderer();

    void SetInstance(HINSTANCE hInstance);
    void SetWindowPlacementContext(POINT* savedWindowPos, const SIZE* defaultWindowSize);
    void EnsureAnimationFramesLoaded();
    void OnTimerTick(HWND hWnd);
    void DrawScene(HWND hWnd, HDC hdc);
    void RefreshOverlayState(HWND hWnd, WorkThread& workThread);
    void SetDownloadPercent(HWND hWnd, int percent);
    bool IsFollowingGameWindows() const;
    int GetDownloadPercent() const;

private:
    struct PigOverlayState {
        DWORD processId{ 0 };
        HWND gameWindow{ nullptr };
        RECT gameRect{};
        bool downloading{ false };
        uint64_t downloadedBytes{ 0 };
        uint64_t totalBytes{ 0 };
        int percent{ 0 };
        std::wstring fileName;
    };

    std::wstring GetDisplayFileName(const std::wstring& raw) const;
    std::unique_ptr<Gdiplus::Bitmap> LoadPngFromResource(UINT resId) const;
    bool LoadAnimationFramesFromResources();
    HWND FindTopTrackedGameWindow(const std::vector<PigOverlayState>& overlays) const;
    void DrawFallbackPulse(Gdiplus::Graphics& graphics, int width, int height) const;

    HINSTANCE m_hInstance{ nullptr };
    POINT* m_savedWindowPos{ nullptr };
    const SIZE* m_defaultWindowSize{ nullptr };
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> m_animFrames;
    size_t m_animFrameIndex{ 0 };
    int m_downloadPercent{ 1 };
    int m_animPulse{ 0 };
    std::vector<PigOverlayState> m_overlayPigs;
    RECT m_overlayBoundsScreen{};
    bool m_followingGameWindows{ false };
    bool m_idleTopmost{ false };
};
