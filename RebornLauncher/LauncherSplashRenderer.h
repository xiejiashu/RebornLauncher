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

class LauncherUpdateCoordinator;

class LauncherSplashRenderer {
public:
    LauncherSplashRenderer() = default;
    ~LauncherSplashRenderer();

    void SetInstance(HINSTANCE hInstance);
    void SetWindowPlacementContext(POINT* savedWindowPos, const SIZE* defaultWindowSize);
    void EnsureAnimationFramesLoaded();
    void OnTimerTick(HWND hWnd);
    void DrawScene(HWND hWnd, HDC hdc);
    void RefreshOverlayState(HWND hWnd, LauncherUpdateCoordinator& workThread);
    void SetDownloadPercent(HWND hWnd, int percent);
    bool IsFollowingGameWindows() const;
    int GetDownloadPercent() const;
    void RestartDockToCorner();
    void RestartDockToPosition(const POINT& target);
    void CancelDockToCorner();

private:
    std::wstring GetDisplayFileName(const std::wstring& raw) const;
    std::unique_ptr<Gdiplus::Bitmap> LoadPngFromResource(UINT resId) const;
    bool LoadAnimationFramesFromResources();
    void DrawFallbackPulse(Gdiplus::Graphics& graphics, int width, int height) const;
    POINT ComputeDockTargetPos(HWND hWnd) const;
    void UpdateDockAnimation(HWND hWnd);

    HINSTANCE m_hInstance{ nullptr };
    POINT* m_savedWindowPos{ nullptr };
    const SIZE* m_defaultWindowSize{ nullptr };
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> m_animFrames;
    size_t m_animFrameIndex{ 0 };
    int m_downloadPercent{ 1 };
    int m_animPulse{ 0 };
    std::wstring m_globalStatusText{ L"\u521d\u59cb\u5316\u4e2d..." };
    std::wstring m_globalFileText;
    bool m_dockAnimationStarted{ false };
    bool m_dockAnimationFinished{ false };
    ULONGLONG m_dockAnimationStartTick{ 0 };
    POINT m_dockStartPos{};
    POINT m_dockTargetPos{};
    POINT m_dockTargetOverride{};
    bool m_followingGameWindows{ false };
    bool m_dockToCornerEnabled{ true };
    bool m_hasDockTargetOverride{ false };
};
