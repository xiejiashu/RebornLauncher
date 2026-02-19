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
#include "WorkThread.h"

namespace {

std::wstring FitStatusLabel(const std::wstring& raw, size_t maxChars) {
    if (raw.empty()) {
        return L"Working...";
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

HWND LauncherSplashRenderer::FindTopTrackedGameWindow(const std::vector<PigOverlayState>& overlays) const {
    std::vector<HWND> tracked;
    tracked.reserve(overlays.size());
    for (const auto& pig : overlays) {
        if (pig.gameWindow && IsWindow(pig.gameWindow) && IsWindowVisible(pig.gameWindow)) {
            tracked.push_back(pig.gameWindow);
        }
    }
    if (tracked.empty()) {
        return nullptr;
    }

    for (HWND z = GetTopWindow(nullptr); z != nullptr; z = GetWindow(z, GW_HWNDNEXT)) {
        if (!IsWindowVisible(z)) {
            continue;
        }
        if (std::find(tracked.begin(), tracked.end(), z) != tracked.end()) {
            return z;
        }
    }
    return tracked.front();
}

void LauncherSplashRenderer::RefreshOverlayState(HWND hWnd, WorkThread& workThread) {
    if (!hWnd) {
        return;
    }

    const std::wstring globalStatus = FitStatusLabel(workThread.GetLauncherStatus(), 56);
    m_globalStatusText = globalStatus;
    const std::wstring globalFileRaw = workThread.GetCurrentDownloadFile();
    const std::wstring globalFileName = GetDisplayFileName(globalFileRaw);
    const int globalTotal = workThread.GetCurrentDownloadSize();
    const int globalProgress = workThread.GetCurrentDownloadProgress();
    const bool globalDownloading = !globalFileName.empty() && globalTotal > 0 && globalProgress >= 0 && globalProgress < globalTotal;

    auto gameInfos = workThread.GetGameInfosSnapshot();
    std::vector<PigOverlayState> overlays;
    overlays.reserve(gameInfos.size());

    RECT bounds{};
    bool hasBounds = false;
    for (const auto& info : gameInfos) {
        if (info.dwProcessId == 0 || info.hMainWnd == nullptr || !IsWindow(info.hMainWnd)) {
            continue;
        }
        RECT gameRect{};
        if (!GetWindowRect(info.hMainWnd, &gameRect)) {
            continue;
        }
        const int gameWidth = gameRect.right - gameRect.left;
        const int gameHeight = gameRect.bottom - gameRect.top;
        if (gameWidth <= 0 || gameHeight <= 0) {
            continue;
        }

        PigOverlayState state;
        state.processId = info.dwProcessId;
        state.gameWindow = info.hMainWnd;
        state.gameRect = gameRect;
        state.downloading = info.downloading;
        state.downloadedBytes = info.downloadDoneBytes;
        state.totalBytes = info.downloadTotalBytes;
        state.fileName = GetDisplayFileName(info.downloadFile);
        state.statusText = globalStatus;
        if (state.downloading && state.fileName.empty() && !globalFileName.empty()) {
            state.fileName = globalFileName;
        }
        if (state.downloading && state.totalBytes == 0 && globalTotal > 0) {
            state.totalBytes = static_cast<uint64_t>(globalTotal);
            state.downloadedBytes = static_cast<uint64_t>((std::max)(0, globalProgress));
        }
        if (!state.downloading && globalDownloading) {
            state.downloading = true;
            state.fileName = globalFileName;
            state.totalBytes = static_cast<uint64_t>(globalTotal);
            state.downloadedBytes = static_cast<uint64_t>((std::max)(0, globalProgress));
        }
        if (state.totalBytes > 0) {
            state.percent = static_cast<int>(std::round(
                static_cast<double>((std::min)(state.downloadedBytes, state.totalBytes)) * 100.0 /
                static_cast<double>(state.totalBytes)));
        }
        state.percent = (std::max)(0, (std::min)(100, state.percent));
        overlays.push_back(std::move(state));

        RECT drawArea{};
        drawArea.left = gameRect.left;
        drawArea.right = gameRect.right;
        drawArea.top = gameRect.top - 96;
        drawArea.bottom = gameRect.top - 4;
        if (!hasBounds) {
            bounds = drawArea;
            hasBounds = true;
        } else {
            bounds.left = (std::min)(bounds.left, drawArea.left);
            bounds.top = (std::min)(bounds.top, drawArea.top);
            bounds.right = (std::max)(bounds.right, drawArea.right);
            bounds.bottom = (std::max)(bounds.bottom, drawArea.bottom);
        }
    }

    m_overlayPigs = std::move(overlays);
    if (hasBounds) {
        const int margin = 6;
        bounds.left -= margin;
        bounds.top -= margin;
        bounds.right += margin;
        bounds.bottom += 2;
        m_overlayBoundsScreen = bounds;
        const int rawW = static_cast<int>(m_overlayBoundsScreen.right - m_overlayBoundsScreen.left);
        const int rawH = static_cast<int>(m_overlayBoundsScreen.bottom - m_overlayBoundsScreen.top);
        const int overlayW = (std::max)(1, rawW);
        const int overlayH = (std::max)(1, rawH);
        HWND anchorGame = FindTopTrackedGameWindow(m_overlayPigs);
        SetWindowPos(hWnd, anchorGame ? anchorGame : HWND_NOTOPMOST,
                     m_overlayBoundsScreen.left, m_overlayBoundsScreen.top,
                     overlayW, overlayH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_followingGameWindows = true;
        m_idleTopmost = false;
    } else {
        if (m_followingGameWindows) {
            m_overlayPigs.clear();
            m_overlayBoundsScreen = {};
            m_followingGameWindows = false;
        }
        if (!m_idleTopmost && m_savedWindowPos && m_defaultWindowSize) {
            SetWindowPos(hWnd, HWND_TOPMOST,
                         m_savedWindowPos->x, m_savedWindowPos->y,
                         m_defaultWindowSize->cx, m_defaultWindowSize->cy,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            m_idleTopmost = true;
        }
    }
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
    if (m_followingGameWindows && !m_overlayPigs.empty()) {
        Gdiplus::SolidBrush labelShadow(Gdiplus::Color(120, 0, 0, 0));
        Gdiplus::SolidBrush labelText(Gdiplus::Color(248, 255, 255, 255));
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font labelFont(&fontFamily, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat centerFormat;
        centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        for (const auto& pig : m_overlayPigs) {
            const int gameWidth = pig.gameRect.right - pig.gameRect.left;
            if (gameWidth <= 0) {
                continue;
            }
            auto toLocalX = [&](LONG x) {
                return static_cast<int>(x - m_overlayBoundsScreen.left);
            };
            auto toLocalY = [&](LONG y) {
                return static_cast<int>(y - m_overlayBoundsScreen.top);
            };

            int drawW = 54;
            int drawH = 42;
            if (!m_animFrames.empty()) {
                auto* frame = m_animFrames[m_animFrameIndex % m_animFrames.size()].get();
                const double scale = static_cast<double>(drawH) / static_cast<double>((std::max<UINT>)(1, frame->GetHeight()));
                drawW = static_cast<int>(frame->GetWidth() * scale);
            }

            const int runwayPadding = 10;
            const int runwayLeft = toLocalX(pig.gameRect.left + runwayPadding);
            int runwayRight = toLocalX(pig.gameRect.right - runwayPadding) - drawW;
            if (runwayRight < runwayLeft) {
                runwayRight = runwayLeft;
            }
            const double t = pig.downloading
                ? (std::max)(0.0, (std::min)(1.0, static_cast<double>(pig.percent) / 100.0))
                : 0.0;
            const int drawX = runwayLeft + static_cast<int>(std::round((runwayRight - runwayLeft) * t));
            const int drawY = toLocalY(pig.gameRect.top - drawH - 6);

            if (!m_animFrames.empty()) {
                auto* frame = m_animFrames[m_animFrameIndex % m_animFrames.size()].get();
                graphics.DrawImage(frame, drawX, drawY, drawW, drawH);
            } else {
                const int fallbackCx = drawX + drawW / 2;
                const int fallbackCy = drawY + drawH / 2;
                Gdiplus::SolidBrush fallbackBrush(Gdiplus::Color(210, 255, 220, 150));
                graphics.FillEllipse(&fallbackBrush, fallbackCx - 20, fallbackCy - 20, 40, 40);
            }

            std::wstring label = FitStatusLabel(pig.statusText, 42);
            if (pig.downloading) {
                std::wstring progressLabel = (pig.fileName.empty() ? L"updating" : pig.fileName);
                progressLabel += L" " + std::to_wstring(pig.percent) + L"%";
                label += L" | " + progressLabel;
            }
            const float labelW = static_cast<float>((std::max)(180, (std::min)(gameWidth - 24, 520)));
            float labelX = static_cast<float>(drawX + drawW / 2) - labelW * 0.5f;
            labelX = (std::max)(0.0f, (std::min)(static_cast<float>(width) - labelW, labelX));
            const float labelY = static_cast<float>((std::max)(2, drawY - 26));
            Gdiplus::RectF labelRect(labelX, labelY, labelW, 22.0f);
            Gdiplus::RectF shadowRect = labelRect;
            shadowRect.X += 1.0f;
            shadowRect.Y += 1.0f;
            graphics.DrawString(label.c_str(), -1, &labelFont, shadowRect, &centerFormat, &labelShadow);
            graphics.DrawString(label.c_str(), -1, &labelFont, labelRect, &centerFormat, &labelText);
        }
    } else {
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
