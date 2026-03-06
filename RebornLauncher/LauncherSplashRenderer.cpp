#include "framework.h"
#include "LauncherSplashRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <objidl.h>
#include <gdiplus.h>

#include "RebornLauncher.h"
#include "LauncherUpdateCoordinator.h"

namespace {

std::wstring FitStatusLabel(const std::wstring& raw, size_t maxChars) {
    if (raw.empty()) {
        return L"\u5904\u7406\u4e2d...";
    }
    if (raw.size() <= maxChars) {
        return raw;
    }
    if (maxChars < 4) {
        return raw.substr(0, maxChars);
    }
    return raw.substr(0, maxChars - 3) + L"...";
}

} // namespace

LauncherSplashRenderer::~LauncherSplashRenderer() = default;

void LauncherSplashRenderer::SetInstance(HINSTANCE hInstance) {
    m_hInstance = hInstance;
}

void LauncherSplashRenderer::SetWindowPlacementContext(POINT* savedWindowPos, const SIZE* defaultWindowSize) {
    m_savedWindowPos = savedWindowPos;
    m_defaultWindowSize = defaultWindowSize;
}

std::wstring LauncherSplashRenderer::GetDisplayFileName(const std::wstring& raw) const {
    if (raw.empty()) {
        return {};
    }
    const size_t slashPos = raw.find_last_of(L"\\/");
    std::wstring name = slashPos == std::wstring::npos ? raw : raw.substr(slashPos + 1);
    if (name.size() > 28) {
        return name.substr(0, 25) + L"...";
    }
    return name;
}

std::unique_ptr<Gdiplus::Bitmap> LauncherSplashRenderer::LoadPngFromResource(UINT resId) const {
    HINSTANCE instance = m_hInstance ? m_hInstance : GetModuleHandle(nullptr);
    HRSRC hResource = FindResourceW(instance, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hResource) {
        return {};
    }

    DWORD imageSize = SizeofResource(instance, hResource);
    HGLOBAL hGlobal = LoadResource(instance, hResource);
    if (!hGlobal || imageSize == 0) {
        return {};
    }

    void* resourceData = LockResource(hGlobal);
    if (!resourceData) {
        return {};
    }

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (!hBuffer) {
        return {};
    }

    void* pBuffer = GlobalLock(hBuffer);
    if (!pBuffer) {
        GlobalFree(hBuffer);
        return {};
    }
    memcpy_s(pBuffer, imageSize, resourceData, imageSize);
    GlobalUnlock(hBuffer);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hBuffer, TRUE, &stream) != S_OK) {
        GlobalFree(hBuffer);
        return {};
    }

    std::unique_ptr<Gdiplus::Bitmap> bitmap(new Gdiplus::Bitmap(stream));
    stream->Release();
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return {};
    }
    return bitmap;
}

bool LauncherSplashRenderer::LoadAnimationFramesFromResources() {
    const UINT frameIds[] = {
        IDB_PIG1,
        IDB_PIG2,
        IDB_PIG3,
        IDB_PIG4,
        IDB_PIG5,
        IDB_PIG6,
        IDB_PIG7,
    };

    std::vector<std::unique_ptr<Gdiplus::Bitmap>> loaded;
    loaded.reserve(sizeof(frameIds) / sizeof(frameIds[0]));
    for (UINT frameId : frameIds) {
        std::unique_ptr<Gdiplus::Bitmap> bitmap = LoadPngFromResource(frameId);
        if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok && bitmap->GetWidth() > 0 && bitmap->GetHeight() > 0) {
            loaded.push_back(std::move(bitmap));
        }
    }

    if (loaded.empty()) {
        return false;
    }

    m_animFrames = std::move(loaded);
    m_animFrameIndex = 0;
    return true;
}

void LauncherSplashRenderer::EnsureAnimationFramesLoaded() {
    if (!m_animFrames.empty()) {
        return;
    }
    LoadAnimationFramesFromResources();
}

POINT LauncherSplashRenderer::ComputeDockTargetPos(HWND hWnd) const {
    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea.left = 0;
        workArea.top = 0;
        workArea.right = GetSystemMetrics(SM_CXSCREEN);
        workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT wndRect{};
    GetWindowRect(hWnd, &wndRect);
    const int wndWRaw = static_cast<int>(wndRect.right - wndRect.left);
    const int wndHRaw = static_cast<int>(wndRect.bottom - wndRect.top);
    const int wndW = (std::max)(1, wndWRaw);
    const int wndH = (std::max)(1, wndHRaw);
    constexpr int kDockMarginX = 8;
    constexpr int kDockMarginY = 8;
    POINT target{};
    target.x = workArea.right - wndW - kDockMarginX;
    target.y = workArea.bottom - wndH - kDockMarginY;
    return target;
}

void LauncherSplashRenderer::RestartDockToCorner() {
    m_hasDockTargetOverride = false;
    m_dockToCornerEnabled = true;
    m_dockAnimationStarted = false;
    m_dockAnimationFinished = false;
    m_dockAnimationStartTick = 0;
}

void LauncherSplashRenderer::RestartDockToPosition(const POINT& target) {
    m_dockTargetOverride = target;
    m_hasDockTargetOverride = true;
    m_dockToCornerEnabled = true;
    m_dockAnimationStarted = false;
    m_dockAnimationFinished = false;
    m_dockAnimationStartTick = 0;
}

void LauncherSplashRenderer::CancelDockToCorner() {
    m_dockToCornerEnabled = false;
    m_dockAnimationStarted = false;
    m_dockAnimationFinished = true;
}

void LauncherSplashRenderer::UpdateDockAnimation(HWND hWnd) {
    if (!hWnd || !m_dockToCornerEnabled || !IsWindowVisible(hWnd)) {
        return;
    }

    RECT wndRect{};
    if (!GetWindowRect(hWnd, &wndRect)) {
        return;
    }

    const POINT latestTarget = m_hasDockTargetOverride
        ? m_dockTargetOverride
        : ComputeDockTargetPos(hWnd);

    if (!m_dockAnimationStarted) {
        m_dockAnimationStarted = true;
        m_dockAnimationFinished = false;
        m_dockAnimationStartTick = GetTickCount64();
        m_dockStartPos.x = wndRect.left;
        m_dockStartPos.y = wndRect.top;
        m_dockTargetPos = latestTarget;
    }

    if (latestTarget.x != m_dockTargetPos.x || latestTarget.y != m_dockTargetPos.y) {
        m_dockTargetPos = latestTarget;
        if (m_dockAnimationFinished) {
            m_dockStartPos.x = wndRect.left;
            m_dockStartPos.y = wndRect.top;
            m_dockAnimationStartTick = GetTickCount64();
            m_dockAnimationFinished = false;
        }
    }

    int newX = m_dockTargetPos.x;
    int newY = m_dockTargetPos.y;
    if (!m_dockAnimationFinished) {
        const ULONGLONG now = GetTickCount64();
        constexpr double kAnimMs = 900.0;
        double t = static_cast<double>(now - m_dockAnimationStartTick) / kAnimMs;
        t = (std::max)(0.0, (std::min)(1.0, t));
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        newX = static_cast<int>(std::round(
            static_cast<double>(m_dockStartPos.x) + static_cast<double>(m_dockTargetPos.x - m_dockStartPos.x) * eased));
        newY = static_cast<int>(std::round(
            static_cast<double>(m_dockStartPos.y) + static_cast<double>(m_dockTargetPos.y - m_dockStartPos.y) * eased));
        if (t >= 1.0) {
            m_dockAnimationFinished = true;
        }
    }

    SetWindowPos(
        hWnd,
        nullptr,
        newX,
        newY,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

    if (m_savedWindowPos) {
        m_savedWindowPos->x = newX;
        m_savedWindowPos->y = newY;
    }

    if (m_dockAnimationFinished) {
        m_dockToCornerEnabled = false;
        m_hasDockTargetOverride = false;
    }
}

void LauncherSplashRenderer::RefreshOverlayState(HWND hWnd, LauncherUpdateCoordinator& workThread) {
    if (!hWnd) {
        return;
    }

    m_globalStatusText = FitStatusLabel(workThread.GetLauncherStatus(), 54);
    m_globalFileText = GetDisplayFileName(workThread.GetCurrentDownloadFile());
    m_followingGameWindows = false;
    UpdateDockAnimation(hWnd);
}

void LauncherSplashRenderer::DrawFallbackPulse(Gdiplus::Graphics& graphics, int width, int height) const {
    const int cx = width / 2;
    const int cy = height / 2 - 12;
    const int baseRadius = (std::max)(16, (std::min)(width, height) / 22);
    for (int i = 0; i < 3; ++i) {
        const int phase = (m_animPulse + i * 8) % 24;
        const int alpha = 90 + (phase <= 12 ? phase * 10 : (24 - phase) * 10);
        const int radius = baseRadius + i * 16;
        Gdiplus::SolidBrush b(Gdiplus::Color((std::min)(255, alpha), 120, 166, 214));
        graphics.FillEllipse(&b, cx - radius, cy - radius, radius * 2, radius * 2);
    }
}

void LauncherSplashRenderer::DrawScene(HWND hWnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hWnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    Gdiplus::Graphics graphics(memDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(255, 1, 1, 1));

    EnsureAnimationFramesLoaded();
    int pigCenterX = width / 2;
    int pigTopY = (std::max)(0, height / 2 - 24);
    if (!m_animFrames.empty()) {
        auto* frame = m_animFrames[m_animFrameIndex % m_animFrames.size()].get();
        const double sx = static_cast<double>(width) * 0.36 / (std::max<UINT>)(1, frame->GetWidth());
        const double sy = static_cast<double>(height) * 0.54 / (std::max<UINT>)(1, frame->GetHeight());
        const double scale = (std::max)(0.2, (std::min)(sx, sy));
        const int drawW = static_cast<int>(frame->GetWidth() * scale);
        const int drawH = static_cast<int>(frame->GetHeight() * scale);
        const int drawX = (width - drawW) / 2;
        const int drawY = (height - drawH) / 2 - 20;
        graphics.DrawImage(frame, drawX, drawY, drawW, drawH);
        pigCenterX = drawX + drawW / 2;
        pigTopY = drawY;
    } else {
        DrawFallbackPulse(graphics, width, height);
    }

    const int percent = (std::max)(1, (std::min)(100, m_downloadPercent));
    const std::wstring percentText = std::to_wstring(percent) + L"%";
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font percentFont(&fontFamily, 38.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const float percentBoxW = 180.0f;
    const float percentX = (std::max)(0.0f, (std::min)(static_cast<float>(width) - percentBoxW, static_cast<float>(pigCenterX) - percentBoxW * 0.5f));
    const float percentY = (std::max)(6.0f, static_cast<float>(pigTopY - 56));
    Gdiplus::RectF textRect(percentX, percentY, percentBoxW, 56.0f);
    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(140, 0, 0, 0));
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(250, 255, 255, 255));
    Gdiplus::RectF shadowRect = textRect;
    shadowRect.X += 2.0f;
    shadowRect.Y += 2.0f;
    graphics.DrawString(percentText.c_str(), -1, &percentFont, shadowRect, &format, &shadowBrush);
    graphics.DrawString(percentText.c_str(), -1, &percentFont, textRect, &format, &textBrush);

    Gdiplus::Font statusFont(&fontFamily, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF statusRect(
        static_cast<float>((std::max)(0, pigCenterX - 210)),
        percentY + 58.0f,
        420.0f,
        26.0f);
    Gdiplus::RectF statusShadow = statusRect;
    statusShadow.X += 1.0f;
    statusShadow.Y += 1.0f;
    const std::wstring idleStatus = FitStatusLabel(m_globalStatusText, 54);
    graphics.DrawString(idleStatus.c_str(), -1, &statusFont, statusShadow, &format, &shadowBrush);
    graphics.DrawString(idleStatus.c_str(), -1, &statusFont, statusRect, &format, &textBrush);

    if (!m_globalFileText.empty()) {
        Gdiplus::Font fileFont(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        const std::wstring fileText = FitStatusLabel(m_globalFileText, 46);
        Gdiplus::RectF fileRect(
            static_cast<float>((std::max)(0, pigCenterX - 210)),
            percentY + 84.0f,
            420.0f,
            22.0f);
        Gdiplus::RectF fileShadow = fileRect;
        fileShadow.X += 1.0f;
        fileShadow.Y += 1.0f;
        graphics.DrawString(fileText.c_str(), -1, &fileFont, fileShadow, &format, &shadowBrush);
        graphics.DrawString(fileText.c_str(), -1, &fileFont, fileRect, &format, &textBrush);
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void LauncherSplashRenderer::OnTimerTick(HWND hWnd) {
    if (!m_animFrames.empty()) {
        m_animFrameIndex = (m_animFrameIndex + 1) % m_animFrames.size();
    }
    m_animPulse = (m_animPulse + 1) % 24;
    UpdateDockAnimation(hWnd);
    InvalidateRect(hWnd, nullptr, FALSE);
}

void LauncherSplashRenderer::SetDownloadPercent(HWND hWnd, int percent) {
    percent = (std::max)(1, (std::min)(100, percent));
    if (percent == m_downloadPercent || hWnd == nullptr) {
        return;
    }
    m_downloadPercent = percent;
    InvalidateRect(hWnd, nullptr, FALSE);
}

bool LauncherSplashRenderer::IsFollowingGameWindows() const {
    return m_followingGameWindows;
}

int LauncherSplashRenderer::GetDownloadPercent() const {
    return m_downloadPercent;
}
