#pragma once

#include "auth_ui_state.h"
#include "i18n.h"
#include "launcher_common.h"
#include "launcher_mouse.h"
#include "mods_ui_globals.h"
#include "profiles.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl.h>
#include <windows.foundation.h>
#include <windows.ui.core.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>

using Microsoft::WRL::ComPtr;
using i18n::Tr;
using i18n::TrFormat;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::UI::Core;

#include "mods_browser.h"

void ProcessAuthUiEvents();

namespace theme {
constexpr UINT32 kBg        = 0x0D0E10;
constexpr UINT32 kSurface   = 0x1B1D21;
constexpr UINT32 kRaised    = 0x2C2F35;
constexpr UINT32 kText      = 0xF4F2ED;
constexpr UINT32 kTextMuted = 0xA5A29C;
// the tan of the crate behind the raccoon on the app icon, lifted so it reads on a dark screen
constexpr UINT32 kAccent    = 0xE3A96B;
constexpr UINT32 kOk        = 0x7CC48D;
constexpr UINT32 kDanger    = 0xEC7462;
constexpr UINT32 kOnLight   = 0x121315;
}

class AuthScreenRenderer {
public:
    bool Initialize(ICoreWindow* window) {
        if (!window) return false;
        ResetDeviceResources();
        WriteLog(L"Auth screen Initialize started");
        window_ = window;

        Rect bounds = {};
        if (FAILED(window->get_Bounds(&bounds))) {
            bounds.Width = 1280;
            bounds.Height = 720;
        }
        width_ = bounds.Width > 0 ? bounds.Width : 1280;
        height_ = bounds.Height > 0 ? bounds.Height : 720;
        displayScale_ = ReadDisplayScale();
        renderWidthPx_ = ScaleToPixels(width_, displayScale_, 1280);
        renderHeightPx_ = ScaleToPixels(height_, displayScale_, 720);

        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (!CreateDeviceResources(false, level)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen IDXGIDevice query failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DXGI adapter failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIFactory2> dxgiFactory;
        hr = adapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DXGI factory failed hr=0x%08X", hr);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = renderWidthPx_;
        desc.Height = renderHeightPx_;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        hr = dxgiFactory->CreateSwapChainForCoreWindow(
            d3dDevice_.Get(),
            reinterpret_cast<IUnknown*>(window),
            &desc,
            nullptr,
            swapChain_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen swap chain failed hr=0x%08X", hr);
            return false;
        }

        if (!CreateTargetBitmap()) return false;

        WriteLogF(L"Auth screen initialized %.0fx%.0f view, %ux%u backbuffer, scale=%.3f driver=%s featureLevel=0x%X",
            width_, height_, renderWidthPx_, renderHeightPx_, displayScale_,
            DriverTypeName(d3dDriverType_), static_cast<unsigned int>(level));
        return true;
    }

    // renders into a bitmap for SaveFramePng, tools\ui-preview uses it to check layouts without an appx build
    bool InitializeOffscreen(float viewW, float viewH, float scale) {
        offscreen_ = true;
        width_ = viewW;
        height_ = viewH;
        displayScale_ = scale > 0.0f ? scale : 1.0f;
        renderWidthPx_ = ScaleToPixels(width_, displayScale_, 1280);
        renderHeightPx_ = ScaleToPixels(height_, displayScale_, 720);

        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (!CreateDeviceResources(true, level)) return false;

        const float dpi = 96.0f * displayScale_;
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi,
            dpi);
        const HRESULT hr = d2dContext_->CreateBitmap(
            D2D1::SizeU(renderWidthPx_, renderHeightPx_), nullptr, 0, &props, targetBitmap_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen offscreen target failed hr=0x%08X", hr);
            return false;
        }
        d2dContext_->SetTarget(targetBitmap_.Get());
        return true;
    }

    bool SaveFramePng(const std::wstring& path) {
        if (!targetBitmap_ || !wicFactory_) return false;

        const D2D1_BITMAP_PROPERTIES1 readProps = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        ComPtr<ID2D1Bitmap1> readable;
        HRESULT hr = d2dContext_->CreateBitmap(
            D2D1::SizeU(renderWidthPx_, renderHeightPx_), nullptr, 0, &readProps, readable.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = readable->CopyFromBitmap(nullptr, targetBitmap_.Get(), nullptr);
        if (FAILED(hr)) return false;

        D2D1_MAPPED_RECT mapped = {};
        hr = readable->Map(D2D1_MAP_OPTIONS_READ, &mapped);
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmap> pixels;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        // the png encoder picks its own format, usually 24bpp. WritePixels would read this 32bpp
        // buffer as 24bpp and smear every pixel, WriteSource converts
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
        hr = wicFactory_->CreateBitmapFromMemory(renderWidthPx_, renderHeightPx_, GUID_WICPixelFormat32bppBGR,
            mapped.pitch, mapped.pitch * renderHeightPx_, mapped.bits, pixels.GetAddressOf());
        if (SUCCEEDED(hr)) hr = wicFactory_->CreateStream(stream.GetAddressOf());
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = wicFactory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(renderWidthPx_, renderHeightPx_);
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(pixels.Get(), nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
        readable->Unmap();
        return SUCCEEDED(hr);
    }

    void Render(const AuthUiState& state) {
        if (deviceRecoveryNeeded_) {
            const auto window = window_;
            if (!Initialize(window.Get())) return;
            deviceRecoveryNeeded_ = false;
        }
        if (!d2dContext_ || (!swapChain_ && !offscreen_)) return;
        if (!EnsureRenderTargetSize()) return;
        if (!targetBitmap_ && !CreateTargetBitmap()) return;

        mainMenuRectCount_ = 0;
        hitRegions_.clear();
        BeginPaint();

        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(theme::kBg));

        if (state.showModsPage) {
            if (state.modsDetailOpen) DrawModDetail(state);
            else if (state.modsProfileOpen) DrawProfile(state);
            else DrawModsBrowser(state);
        } else if (state.showChangelog) {
            DrawChangelog(state);
        } else if (state.showSettings) {
            DrawSettings(state);
        } else if (state.showCrashScreen) {
            DrawCrash(state);
        } else if (state.showMainMenu) {
            DrawMainMenu(state);
        } else if (state.showRemoteFiles) {
            DrawRemoteFiles(state);
        } else if (!state.showDeviceCode) {
            DrawProgress(state);
        } else {
            DrawDeviceCode(state);
        }

        FinishDraw();
    }

private:
    // one frame's brushes. the raw pointers stay valid until the next BeginPaint or a device reset
    void BeginPaint() {
        brushPool_.clear();
        text_ = MakeBrush(theme::kText);
        muted_ = MakeBrush(theme::kTextMuted);
        line_ = MakeBrush(theme::kText, 0.10f);
        surface_ = MakeBrush(theme::kSurface, 0.94f);
        raised_ = MakeBrush(theme::kRaised, 0.96f);
        accent_ = MakeBrush(theme::kAccent);
        accentSoft_ = MakeBrush(theme::kAccent, 0.16f);
        danger_ = MakeBrush(theme::kDanger);
        dangerSoft_ = MakeBrush(theme::kDanger, 0.16f);
        ok_ = MakeBrush(theme::kOk);
        black_ = MakeBrush(0x000000);
        onLight_ = MakeBrush(theme::kOnLight);

        // keep inside the tv title-safe area, otherwise overscan clips the edges
        L_ = floorf(width_ * 0.05f);
        T_ = floorf(height_ * 0.065f);
        R_ = width_ - L_;
        B_ = height_ - T_;
    }

    ID2D1SolidColorBrush* MakeBrush(UINT32 rgb, float alpha = 1.0f) {
        ComPtr<ID2D1SolidColorBrush> brush;
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(rgb, alpha), brush.GetAddressOf());
        brushPool_.push_back(brush);
        return brush.Get();
    }

    void FinishDraw() {
        if (cursorVisible_) {
            const float cx = cursorX_;
            const float cy = cursorY_;
            const float arm = 14.0f;
            const float thick = 2.0f;
            const float box = 5.0f;
            black_->SetOpacity(0.4f);
            d2dContext_->FillRectangle(D2D1::RectF(cx - box, cy - box, cx + box, cy + box), black_);
            black_->SetOpacity(1.0f);
            d2dContext_->FillRectangle(D2D1::RectF(cx - arm, cy - thick, cx + arm, cy + thick), text_);
            d2dContext_->FillRectangle(D2D1::RectF(cx - thick, cy - arm, cx + thick, cy + arm), text_);
        }
        HRESULT hr = d2dContext_->EndDraw();
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen EndDraw failed hr=0x%08X", hr);
            d2dContext_->SetTarget(nullptr);
            targetBitmap_.Reset();
            deviceRecoveryNeeded_ = true;
            ProcessAuthUiEvents();
            return;
        }
        if (!swapChain_) return;
        hr = swapChain_->Present(1, 0);
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen Present failed hr=0x%08X", hr);
            deviceRecoveryNeeded_ = true;
        } else if (!presentOkLogged_) {
            presentOkLogged_ = true;
            WriteLogF(L"Auth screen Present OK driver=%s backbuffer=%ux%u",
                DriverTypeName(d3dDriverType_), renderWidthPx_, renderHeightPx_);
        }
        ProcessAuthUiEvents();
    }

    void DrawBackdrop(float cover) {
        const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, width_, height_);
        DrawScreenshots(full);
        d2dContext_->FillRectangle(full, MakeBrush(theme::kBg, cover));
    }

    void FillGradient(D2D1_RECT_F rect, UINT32 rgb, float alphaStart, float alphaEnd, bool vertical,
        float midPos = 0.5f, float midAlpha = -1.0f) {
        const D2D1_GRADIENT_STOP stops[] = {
            { 0.0f, D2D1::ColorF(rgb, alphaStart) },
            { midPos, D2D1::ColorF(rgb, midAlpha >= 0.0f ? midAlpha : (alphaStart + alphaEnd) * 0.5f) },
            { 1.0f, D2D1::ColorF(rgb, alphaEnd) },
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(d2dContext_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf()))) return;
        ComPtr<ID2D1LinearGradientBrush> brush;
        const D2D1_POINT_2F from = D2D1::Point2F(rect.left, rect.top);
        const D2D1_POINT_2F to = vertical ? D2D1::Point2F(rect.left, rect.bottom) : D2D1::Point2F(rect.right, rect.top);
        if (FAILED(d2dContext_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(from, to), collection.Get(), brush.GetAddressOf()))) return;
        d2dContext_->FillRectangle(rect, brush.Get());
    }

    float MeasureWidth(const wchar_t* text, IDWriteTextFormat* format) {
        if (!text || !format) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(
                text, static_cast<UINT32>(wcslen(text)), format, 4096.0f, 200.0f, layout.GetAddressOf()))) return 0.0f;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    }

    float FlowHeight(const std::wstring& text, IDWriteTextFormat* format, float w, float maxH) {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (text.empty() || FAILED(dwriteFactory_->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format, w, maxH, layout.GetAddressOf())) ||
            FAILED(layout->GetMetrics(&metrics))) {
            return 0.0f;
        }
        return (std::min)(metrics.height, maxH);
    }

    float DrawFlow(const wchar_t* text, IDWriteTextFormat* format, float x, float y, float w, float maxH, ID2D1Brush* brush) {
        if (!text || !*text || !format || !brush || w <= 0.0f) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(
                text, static_cast<UINT32>(wcslen(text)), format, w, maxH, layout.GetAddressOf()))) return 0.0f;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        d2dContext_->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + maxH), D2D1_ANTIALIAS_MODE_ALIASED);
        d2dContext_->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
        d2dContext_->PopAxisAlignedClip();
        return (std::min)(metrics.height, maxH);
    }

    void FillCircle(float cx, float cy, float radius, ID2D1Brush* brush) {
        if (brush) d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush);
    }

    void HLine(float x0, float x1, float y) {
        d2dContext_->FillRectangle(D2D1::RectF(x0, y, x1, y + 1.0f), line_);
    }

    void FocusRing(D2D1_RECT_F r, float radius) {
        const float off = 5.0f;
        StrokeRound(D2D1::RectF(r.left - off, r.top - off, r.right + off, r.bottom + off), text_, radius + off, 3.0f);
    }

    void DrawBrand(const wchar_t* label, ID2D1Brush* brush) {
        float x = L_;
        ComPtr<ID2D1Bitmap1> logo = GetCachedBitmap(GetExecutableDir() + L"\\Assets\\Square150x150Logo.png");
        if (logo) {
            const D2D1_RECT_F logoRect = D2D1::RectF(L_, T_, L_ + 44.0f, T_ + 44.0f);
            d2dContext_->DrawBitmap(logo.Get(), logoRect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            x = logoRect.right + 14.0f;
        }
        DrawText(label, overlineFormat_.Get(), D2D1::RectF(x, T_, x + 700.0f, T_ + 44.0f), brush);
    }

    D2D1_RECT_F DrawBackChip(float x, float top) {
        const D2D1_RECT_F chip = D2D1::RectF(x, top, x + 50.0f, top + 50.0f);
        FillRound(chip, raised_, 25.0f);
        DrawIcon(L"\uE72B", chip, text_);
        RegisterHit(launchhit::kBack, chip);
        return chip;
    }

    void DrawHeader(const wchar_t* titleText) {
        const D2D1_RECT_F back = DrawBackChip(L_, T_);
        DrawText(titleText, titleFormat_.Get(), D2D1::RectF(back.right + 20.0f, T_ - 4.0f, R_, T_ + 54.0f), text_);
    }

    void DrawFooter(const wchar_t* text, ID2D1Brush* brush = nullptr) {
        HLine(L_, R_, B_ - 46.0f);
        DrawText(text, smallMid_.Get(), D2D1::RectF(L_, B_ - 38.0f, R_, B_), brush ? brush : muted_);
    }

    void DrawRow(D2D1_RECT_F r, const wchar_t* glyph, const wchar_t* label, bool focused, bool active, bool hovered,
        bool large = false, ID2D1Brush* idle = nullptr) {
        ID2D1Brush* ink = idle ? idle : text_;
        if (focused) {
            FillRound(r, text_, 12.0f);
            ink = onLight_;
        } else if (active) {
            FillRound(r, raised_, 12.0f);
            FillRound(D2D1::RectF(r.left, r.top + 14.0f, r.left + 4.0f, r.bottom - 14.0f), accent_, 2.0f);
            ink = accent_;
        } else if (hovered) {
            FillRound(r, raised_, 12.0f);
        }
        DrawIcon(glyph, D2D1::RectF(r.left + 18.0f, r.top, r.left + 52.0f, r.bottom), ink, large);
        IDWriteTextFormat* format = large ? rowLargeFormat_.Get() : ((focused || active) ? rowStrongFormat_.Get() : rowFormat_.Get());
        DrawText(label, format, D2D1::RectF(r.left + 68.0f, r.top, r.right - 16.0f, r.bottom), ink);
    }

    enum class ButtonKind { Neutral, Primary, Danger, Muted };

    void DrawButton(D2D1_RECT_F r, const wchar_t* glyph, const wchar_t* label, bool focused, ButtonKind kind,
        ID2D1Brush* tint = nullptr) {
        ID2D1Brush* ink = tint ? tint : text_;
        if (kind == ButtonKind::Primary) {
            FillRound(r, accent_, 12.0f);
            ink = onLight_;
            if (focused) FocusRing(r, 12.0f);
        } else if (focused) {
            FillRound(r, text_, 12.0f);
            ink = onLight_;
        } else {
            FillRound(r, raised_, 12.0f);
            if (kind == ButtonKind::Danger) ink = danger_;
            if (kind == ButtonKind::Muted) ink = muted_;
        }
        float textLeft = r.left + 20.0f;
        if (glyph && *glyph) {
            DrawIcon(glyph, D2D1::RectF(r.left + 14.0f, r.top, r.left + 44.0f, r.bottom), ink);
            textLeft = r.left + 50.0f;
        }
        DrawText(label, buttonFormat_.Get(), D2D1::RectF(textLeft, r.top, r.right - 10.0f, r.bottom), ink);
    }

    void DrawToggle(float right, float midY, bool on) {
        const D2D1_RECT_F track = D2D1::RectF(right - 64.0f, midY - 17.0f, right, midY + 17.0f);
        FillRound(track, on ? accent_ : MakeBrush(theme::kText, 0.16f), 17.0f);
        FillCircle(on ? track.right - 17.0f : track.left + 17.0f, midY, 12.0f, on ? onLight_ : muted_);
    }

    void DrawIconTile(const std::wstring& path, D2D1_RECT_F rect, float radius, const wchar_t* fallbackGlyph) {
        ComPtr<ID2D1Bitmap1> icon = GetCachedBitmap(path);
        if (icon) {
            DrawBitmapCover(icon.Get(), rect, 1.0f, 1.0f, 0.0f, 0.0f);
            StrokeRound(rect, line_, radius, 1.0f);
        } else {
            FillRound(rect, MakeBrush(0x000000, 0.45f), radius);
            DrawIcon(fallbackGlyph, rect, muted_, true);
        }
    }

    void DrawMainMenu(const AuthUiState& state) {
        const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, width_, height_);
        DrawScreenshots(full);
        FillGradient(D2D1::RectF(0.0f, 0.0f, width_ * 0.80f, height_), theme::kBg, 0.96f, 0.0f, false, 0.42f, 0.86f);
        FillGradient(D2D1::RectF(0.0f, height_ * 0.72f, width_, height_), theme::kBg, 0.0f, 0.55f, true);

        const float menuRight = L_ + 520.0f;
        DrawBrand(L"BANDIT LAUNCHER", muted_);

        DrawText(state.title.c_str(), displayFormat_.Get(), D2D1::RectF(L_ - 3.0f, T_ + 64.0f, R_ - 200.0f, T_ + 148.0f), text_);

        float infoY = T_ + 158.0f;
        if (!state.status.empty()) {
            infoY += DrawFlow(state.status.c_str(), leadFormat_.Get(), L_, infoY, menuRight - L_ + 160.0f, 68.0f,
                state.isError ? danger_ : text_) + 4.0f;
        }
        if (!state.detail.empty()) {
            DrawFlow(state.detail.c_str(), smallFormat_.Get(), L_, infoY, menuRight - L_ + 160.0f, 60.0f, muted_);
        }

        const wchar_t* labels[] = {
            Tr(L"Play"), Tr(L"Mods"), Tr(L"Remote Files"), Tr(L"Settings"),
            state.updateAvailable ? Tr(L"Update available") : Tr(L"What's new"),
            Tr(L"Repair downloads"), Tr(L"Sign out") };
        const wchar_t* menuIcons[] = { L"\uE768", L"\uE74C", L"\uE838", L"\uE713", L"\uE946", L"\uE72C", L"\uE7E8" };

        float y = T_ + 276.0f;
        for (int i = 0; i < kMainMenuItems; ++i) {
            const bool play = i == 0;
            const float h = play ? 84.0f : 62.0f;
            const D2D1_RECT_F row = D2D1::RectF(L_, y, menuRight, y + h);
            mainMenuRects_[i] = row;
            const bool sel = i == state.selectedMenuIndex;
            if (play && !sel) {
                FillRound(row, accentSoft_, 12.0f);
            }
            DrawRow(row, menuIcons[i], labels[i], sel, false, false, play, play ? accent_ : nullptr);
            if (i == 4 && state.updateAvailable) {
                FillCircle(row.right - 30.0f, (row.top + row.bottom) * 0.5f, 7.0f, sel ? onLight_ : accent_);
            }
            y += h + (play ? 14.0f : 4.0f);
        }
        mainMenuRectCount_ = kMainMenuItems;
    }

    void DrawProgress(const AuthUiState& state) {
        const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, width_, height_);
        DrawScreenshots(full);
        d2dContext_->FillRectangle(full, MakeBrush(theme::kBg, state.showLaunchLog ? 0.80f : 0.50f));
        FillGradient(D2D1::RectF(0.0f, height_ * 0.45f, width_, height_), theme::kBg, 0.0f, 0.92f, true);

        const std::wstring title = state.title.empty() ? std::wstring(Tr(L"Microsoft sign-in")) : state.title;
        DrawBrand(title.c_str(), text_);

        const float barTop = B_ - 54.0f;
        const float barH = 8.0f;

        // the status block grows upward from the bar, a one line status should not float above a gap
        const float detailH = FlowHeight(state.detail, smallFormat_.Get(), R_ - L_, 62.0f);
        const float statusH = (std::max)(56.0f, FlowHeight(state.status, headlineFormat_.Get(), R_ - L_, 170.0f));
        const float detailTop = barTop - 26.0f - detailH;
        const float statusTop = detailTop - (detailH > 0.0f ? 8.0f : 0.0f) - statusH;
        DrawFlow(state.status.c_str(), headlineFormat_.Get(), L_, statusTop, R_ - L_, statusH, state.isError ? danger_ : text_);
        if (detailH > 0.0f) {
            DrawFlow(state.detail.c_str(), smallFormat_.Get(), L_, detailTop, R_ - L_, detailH, muted_);
        }

        if (state.showLaunchLog) {
            const D2D1_RECT_F logBox = D2D1::RectF(L_, T_ + 76.0f, R_, statusTop - 28.0f);
            if (logBox.bottom - logBox.top > 90.0f) {
                FillRound(logBox, MakeBrush(0x000000, 0.55f), 14.0f);
                DrawText(Tr(L"Live launch log"), overlineFormat_.Get(),
                    D2D1::RectF(logBox.left + 22.0f, logBox.top + 10.0f, logBox.right - 22.0f, logBox.top + 44.0f), accent_);
                const D2D1_RECT_F logTextRect = D2D1::RectF(logBox.left + 22.0f, logBox.top + 50.0f, logBox.right - 22.0f, logBox.bottom - 16.0f);
                d2dContext_->PushAxisAlignedClip(logTextRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                DrawText(state.launchLogText.empty() ? Tr(L"Waiting for pre-launch log output...") : state.launchLogText.c_str(),
                    monoFormat_.Get(), logTextRect, muted_);
                d2dContext_->PopAxisAlignedClip();
            }
        }

        const float trackW = R_ - L_;
        const D2D1_RECT_F track = D2D1::RectF(L_, barTop, R_, barTop + barH);
        FillRound(track, MakeBrush(theme::kText, 0.16f), barH * 0.5f);
        ID2D1Brush* barBrush = state.isError ? danger_ : accent_;
        if (state.progress >= 0.0f) {
            const float progress = (std::max)(0.0f, (std::min)(1.0f, state.progress));
            if (progress > 0.0f) {
                FillRound(D2D1::RectF(L_, barTop, L_ + (std::max)(barH, trackW * progress), barTop + barH), barBrush, barH * 0.5f);
            }
            wchar_t percent[32] = {};
            swprintf_s(percent, L"%d%%", static_cast<int>(progress * 100.0f + 0.5f));
            DrawText(percent, smallMid_.Get(), D2D1::RectF(L_, barTop + 16.0f, L_ + 200.0f, barTop + 48.0f), muted_);
        } else {
            const float phase = state.animation * 6.2831853f;
            const float pulse = 0.5f + 0.5f * std::sin(phase);
            const float fillWidth = trackW * (0.18f + 0.16f * pulse);
            const float fillLeft = L_ + (trackW - fillWidth) * (0.5f + 0.5f * std::sin(phase * 0.65f));
            FillRound(D2D1::RectF(fillLeft, barTop, fillLeft + fillWidth, barTop + barH), barBrush, barH * 0.5f);
            DrawText(Tr(L"Loading..."), smallMid_.Get(), D2D1::RectF(L_, barTop + 16.0f, L_ + 400.0f, barTop + 48.0f), muted_);
        }
    }

    void DrawDeviceCode(const AuthUiState& state) {
        DrawBackdrop(0.84f);
        DrawBrand(L"BANDIT LAUNCHER", muted_);

        const float colRight = L_ + (R_ - L_) * 0.52f;
        const std::wstring title = state.title.empty() ? std::wstring(Tr(L"Microsoft sign-in")) : state.title;
        DrawText(title.c_str(), displayFormat_.Get(), D2D1::RectF(L_ - 3.0f, T_ + 96.0f, colRight, T_ + 180.0f), text_);

        const std::wstring url = state.verificationUri.empty() ? L"microsoft.com/link" : state.verificationUri;
        DrawText(Tr(L"Enter this code at"), leadFormat_.Get(), D2D1::RectF(L_, T_ + 226.0f, colRight, T_ + 262.0f), muted_);
        DrawText(url.c_str(), titleFormat_.Get(), D2D1::RectF(L_, T_ + 262.0f, colRight, T_ + 320.0f), accent_);

        // one tile per character, the code is read off a tv and typed on a phone
        const float tilesTop = T_ + 356.0f;
        const float tileH = 104.0f;
        if (!state.userCode.empty()) {
            int glyphs = 0;
            int dashes = 0;
            for (const wchar_t ch : state.userCode) (ch == L'-' || ch == L' ') ? ++dashes : ++glyphs;
            const float gap = 10.0f;
            const float dashW = 34.0f;
            const float avail = colRight - L_ - 40.0f;
            float tileW = 76.0f;
            if (glyphs > 0) {
                tileW = (std::min)(76.0f, (avail - dashes * dashW - (glyphs - 1) * gap) / static_cast<float>(glyphs));
            }
            if (tileW < 34.0f) {
                const D2D1_RECT_F box = D2D1::RectF(L_, tilesTop, L_ + avail, tilesTop + tileH);
                FillRound(box, raised_, 14.0f);
                DrawText(state.userCode.c_str(), codeFormat_.Get(), box, text_);
            } else {
                float x = L_;
                for (const wchar_t ch : state.userCode) {
                    if (ch == L'-' || ch == L' ') {
                        d2dContext_->FillRectangle(
                            D2D1::RectF(x + 6.0f, tilesTop + tileH * 0.5f - 2.0f, x + dashW - 16.0f, tilesTop + tileH * 0.5f + 2.0f), muted_);
                        x += dashW;
                        continue;
                    }
                    const D2D1_RECT_F tile = D2D1::RectF(x, tilesTop, x + tileW, tilesTop + tileH);
                    FillRound(tile, raised_, 12.0f);
                    const wchar_t one[2] = { ch, 0 };
                    DrawText(one, codeFormat_.Get(), tile, text_);
                    x += tileW + gap;
                }
            }
        }

        float y = tilesTop + tileH + 40.0f;
        if (!state.status.empty()) {
            y += DrawFlow(state.status.c_str(), leadFormat_.Get(), L_, y, colRight - L_ - 40.0f, 110.0f,
                state.isError ? danger_ : text_) + 6.0f;
        }
        if (state.secondsRemaining > 0) {
            const std::wstring expires = TrFormat(L"Code expires in {0} seconds", { std::to_wstring(state.secondsRemaining) });
            y += DrawFlow(expires.c_str(), smallFormat_.Get(), L_, y, colRight - L_ - 40.0f, 60.0f, muted_) + 6.0f;
        }
        if (!state.detail.empty()) {
            DrawFlow(state.detail.c_str(), smallFormat_.Get(), L_, y, colRight - L_ - 40.0f, B_ - y, muted_);
        }

        const float qrSide = (std::min)((R_ - colRight) * 0.62f, (B_ - T_) * 0.56f);
        const float pad = 22.0f;
        const float cardSide = qrSide + pad * 2.0f;
        const float cardLeft = colRight + ((R_ - colRight) - cardSide) * 0.5f;
        const float cardTop = T_ + ((B_ - T_) - cardSide) * 0.44f;
        const D2D1_RECT_F card = D2D1::RectF(cardLeft, cardTop, cardLeft + cardSide, cardTop + cardSide);
        FillRound(card, MakeBrush(0xFFFFFF), 20.0f);
        DrawQr(state.qr, D2D1::RectF(card.left + pad, card.top + pad, card.right - pad, card.bottom - pad));
        DrawText(Tr(L"Scan QR"), centerFormat_.Get(), D2D1::RectF(card.left, card.bottom + 18.0f, card.right, card.bottom + 54.0f), muted_);
    }

    void DrawRemoteFiles(const AuthUiState& state) {
        DrawBackdrop(0.86f);
        DrawHeader(Tr(L"Remote Files"));

        // the address only exists inside the translated detail line, so it is picked back out for the qr code
        std::string url;
        {
            const size_t at = state.detail.find(L"http");
            if (at != std::wstring::npos) {
                for (size_t i = at; i < state.detail.size(); ++i) {
                    const wchar_t ch = state.detail[i];
                    if (ch <= L' ' || ch > 126) break;
                    url.push_back(static_cast<char>(ch));
                }
            }
        }
        if (url != remoteQrUrl_) {
            remoteQrUrl_ = url;
            remoteQr_ = url.empty() ? QrMatrix{} : GenerateLoginQrMatrix(url);
        }
        const bool haveQr = !remoteQr_.empty();

        const float top = T_ + 110.0f;
        const float qrCol = haveQr ? 420.0f : 0.0f;
        const float textRight = R_ - (haveQr ? qrCol + 56.0f : 0.0f);

        FillCircle(L_ + 9.0f, top + 19.0f, 7.0f, state.isError ? danger_ : ok_);
        DrawText(state.status.c_str(), rowStrongFormat_.Get(), D2D1::RectF(L_ + 30.0f, top, textRight, top + 38.0f),
            state.isError ? danger_ : text_);

        float y = top + 66.0f;
        if (!state.detail.empty()) {
            IDWriteTextFormat* detailFormat = state.isError ? leadFormat_.Get() : monoLargeFormat_.Get();
            const float textH = FlowHeight(state.detail, detailFormat, textRight - L_ - 60.0f, 320.0f);
            const D2D1_RECT_F box = D2D1::RectF(L_, y, textRight, y + textH + 52.0f);
            FillRound(box, surface_, 16.0f);
            DrawFlow(state.detail.c_str(), detailFormat, box.left + 30.0f, box.top + 26.0f,
                box.right - box.left - 60.0f, textH, text_);
            y = box.bottom + 30.0f;
        }
        DrawFlow(
            Tr(L"Open the URL and PIN in a browser on your PC or phone.\nImport and export worlds, mods, resource packs, and Modrinth .mrpack files for the active profile."),
            smallFormat_.Get(), L_, y, textRight - L_, 150.0f, muted_);

        if (haveQr) {
            const float pad = 20.0f;
            const D2D1_RECT_F card = D2D1::RectF(R_ - qrCol, top, R_, top + qrCol);
            FillRound(card, MakeBrush(0xFFFFFF), 20.0f);
            DrawQr(remoteQr_, D2D1::RectF(card.left + pad, card.top + pad, card.right - pad, card.bottom - pad));
            DrawText(Tr(L"Scan QR"), centerFormat_.Get(), D2D1::RectF(card.left, card.bottom + 16.0f, card.right, card.bottom + 50.0f), muted_);
        }

        DrawFooter(Tr(L"Select Back, or press B or Escape, to stop sharing and return to the launcher."));
    }

    void DrawSettings(const AuthUiState& state) {
        DrawHeader(Tr(L"Settings"));

        const float rowH = 88.0f;
        const float rowGap = 10.0f;
        float rowY = T_ + 104.0f;

        auto row = [&](int index, const wchar_t* label, const wchar_t* sub, float controlW) {
            const D2D1_RECT_F r = D2D1::RectF(L_, rowY, R_, rowY + rowH);
            const bool sel = state.settingsSelected == index;
            FillRound(r, sel ? raised_ : surface_, 14.0f);
            if (sel) FocusRing(r, 14.0f);
            const float textRight = r.right - controlW - 44.0f;
            DrawText(label, rowStrongFormat_.Get(), D2D1::RectF(r.left + 26.0f, r.top + 10.0f, textRight, r.top + 48.0f), text_);
            DrawText(sub, smallMid_.Get(), D2D1::RectF(r.left + 26.0f, r.top + 46.0f, textRight, r.bottom - 10.0f), muted_);
            RegisterHit(launchhit::kSettingsRowBase + index, r);
            rowY = r.bottom + rowGap;
            return r;
        };
        auto onOff = [&](const D2D1_RECT_F& r, bool on) {
            const float mid = (r.top + r.bottom) * 0.5f;
            DrawToggle(r.right - 26.0f, mid, on);
            DrawText(on ? Tr(L"On") : Tr(L"Off"), valueFormat_.Get(),
                D2D1::RectF(r.right - 250.0f, r.top, r.right - 106.0f, r.bottom), on ? text_ : muted_);
        };

        {
            const D2D1_RECT_F r = row(0, Tr(L"Send crash reports and usage stats"),
                Tr(L"Crash reports and basic launch counts use the same setting."), 180.0f);
            onOff(r, state.settingsReportingOn);
        }
        row(1, Tr(L"Reset the reporting id"), state.settingsInstallId.c_str(), 0.0f);
        {
            const bool curseForge = state.settingsModSource == ModSource::CurseForge;
            const std::wstring sub = curseForge
                ? (state.settingsCurseForgeKeyHint.empty()
                    ? std::wstring(Tr(L"No API key set. Add one in Remote Files or CurseForge stays empty."))
                    : TrFormat(L"API key saved ({0}).", { state.settingsCurseForgeKeyHint }))
                : std::wstring(Tr(L"Modrinth needs no key. Switch to CurseForge to browse there instead."));
            const float segW = 164.0f;
            const D2D1_RECT_F r = row(2, Tr(L"Mod download source"), sub.c_str(), segW * 2.0f + 8.0f);
            const D2D1_RECT_F box = D2D1::RectF(r.right - 26.0f - segW * 2.0f - 8.0f, r.top + 20.0f, r.right - 26.0f, r.bottom - 20.0f);
            FillRound(box, MakeBrush(0x000000, 0.35f), 12.0f);
            for (int i = 0; i < 2; ++i) {
                const D2D1_RECT_F seg = D2D1::RectF(box.left + 4.0f + i * segW, box.top + 4.0f, box.left + 4.0f + (i + 1) * segW, box.bottom - 4.0f);
                const bool on = (i == 1) == curseForge;
                if (on) FillRound(seg, accent_, 9.0f);
                DrawText(i == 0 ? L"Modrinth" : L"CurseForge", pillFormat_.Get(), seg, on ? onLight_ : muted_);
            }
        }
        {
            const D2D1_RECT_F r = row(3, Tr(L"Check for new versions"),
                Tr(L"Asks GitHub which version is newest when the launcher opens. Nothing about you is sent."), 180.0f);
            onOff(r, state.settingsUpdateCheckOn);
        }
        {
            const D2D1_RECT_F r = row(4, Tr(L"Language"), Tr(L"Anything that is not translated yet stays in English."), 300.0f);
            DrawIcon(L"\uE76C", D2D1::RectF(r.right - 56.0f, r.top, r.right - 22.0f, r.bottom), muted_);
            DrawText(state.settingsLanguageName.c_str(), valueFormat_.Get(),
                D2D1::RectF(r.right - 380.0f, r.top, r.right - 66.0f, r.bottom), text_);
        }

        DrawFlow(
            state.settingsConfigured
                ? Tr(L"Reports use a random id and include launcher and Minecraft versions, loader details, launch stage, memory totals and a mod list hash. Mod jar names and project ids can be sent when a crash needs them. A crash trace can include exception text and Java class names. The launcher does not add your account, gamertag, worlds or access token. Common user and app storage paths are scrubbed. The id stays the same until it is reset.")
                : Tr(L"No reporting server is configured on this install, so nothing can be sent either way."),
            captionFormat_.Get(), L_ + 4.0f, rowY + 14.0f, R_ - L_ - 8.0f, 130.0f, muted_);

        if (!state.settingsNote.empty()) {
            DrawText(state.settingsNote.c_str(), smallMid_.Get(),
                D2D1::RectF(L_, B_ - 92.0f, R_, B_ - 56.0f), accent_);
        }
        DrawFooter(Tr(L"Press B or select Back to return to the launcher."));
    }

    void DrawCrash(const AuthUiState& state) {

        FillCircle(L_ + 25.0f, T_ + 25.0f, 25.0f, dangerSoft_);
        DrawIcon(L"\uE7BA", D2D1::RectF(L_, T_ - 2.0f, L_ + 50.0f, T_ + 48.0f), danger_);
        DrawText(state.crashHeadline.c_str(), titleFormat_.Get(),
            D2D1::RectF(L_ + 70.0f, T_ - 4.0f, R_, T_ + 54.0f), text_);

        float y = T_ + 86.0f;
        if (!state.crashSuspectLine.empty()) {
            int lines = 1;
            for (const wchar_t ch : state.crashSuspectLine) {
                if (ch == L'\n') ++lines;
            }
            const float boxH = 36.0f + lines * 36.0f;
            const D2D1_RECT_F box = D2D1::RectF(L_, y, R_, y + boxH);
            FillRound(box, surface_, 14.0f);
            FillRound(D2D1::RectF(box.left, box.top + 16.0f, box.left + 4.0f, box.bottom - 16.0f), danger_, 2.0f);
            DrawText(state.crashSuspectLine.c_str(), leadFormat_.Get(),
                D2D1::RectF(box.left + 26.0f, box.top + 16.0f, box.right - 26.0f, box.bottom - 12.0f), text_);
            y = box.bottom + 22.0f;
        }

        if (state.crashAskConsent) {
            DrawText(Tr(L"Send this crash report"), rowStrongFormat_.Get(),
                D2D1::RectF(L_, y, R_, y + 36.0f), accent_);
            const float used = DrawFlow(
                Tr(L"Reporting is off. Send this crash once, turn reporting on, or keep it off. The preview includes the crash summary and trace that may be sent. The launcher does not add your account, gamertag or access token. Common user and app storage paths are scrubbed."),
                smallFormat_.Get(), L_, y + 40.0f, R_ - L_, 90.0f, muted_);
            y += 40.0f + used + 18.0f;
        }

        const float panelBottom = B_ - 104.0f;
        if (state.crashAskConsent || state.crashDetailsOpen) {
            const D2D1_RECT_F box = D2D1::RectF(L_, y, R_, panelBottom);
            FillRound(box, MakeBrush(0x000000, 0.60f), 14.0f);
            const D2D1_RECT_F clip = D2D1::RectF(box.left + 22.0f, box.top + 16.0f, box.right - 22.0f, box.bottom - 16.0f);
            const D2D1_RECT_F textRect = D2D1::RectF(
                clip.left, clip.top - state.crashDetailScroll * 22.0f, clip.right, clip.bottom + 4096.0f);
            d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const std::wstring& body = state.crashDetailsOpen ? state.crashTrace : state.crashConsentPayload;
            d2dContext_->DrawText(body.c_str(), static_cast<UINT32>(body.size()), monoFormat_.Get(), textRect, muted_);
            d2dContext_->PopAxisAlignedClip();
        }

        const float btnH = 60.0f;
        const float btnY = B_ - 84.0f;
        const float btnW = (R_ - L_ - 32.0f) / 3.0f;
        for (int i = 0; i < state.crashButtonCount && i < 3; ++i) {
            const float x = L_ + i * (btnW + 16.0f);
            const D2D1_RECT_F button = D2D1::RectF(x, btnY, x + btnW, btnY + btnH);
            DrawButton(button, nullptr, CrashButtonLabel(state, i), i == state.crashSelected, ButtonKind::Neutral);
            RegisterHit(launchhit::kCrashButtonBase + i, button);
        }

        if (!state.crashFootnote.empty()) {
            DrawText(state.crashFootnote.c_str(), smallMid_.Get(),
                D2D1::RectF(L_, B_ - 20.0f, R_, B_ + 14.0f), muted_);
        }
    }

    void DrawChangelog(const AuthUiState& state) {
        const bool offer = state.changelogOffer;

        float headX = L_;
        if (offer) {
            FillCircle(L_ + 25.0f, T_ + 25.0f, 25.0f, accentSoft_);
            DrawIcon(L"\uE896", D2D1::RectF(L_, T_, L_ + 50.0f, T_ + 50.0f), accent_);
            headX = L_ + 70.0f;
        } else {
            headX = DrawBackChip(L_, T_).right + 20.0f;
        }
        DrawText(state.changelogHeadline.c_str(), titleFormat_.Get(),
            D2D1::RectF(headX, T_ - 4.0f, R_, T_ + 54.0f), text_);
        if (!state.changelogSub.empty()) {
            DrawText(state.changelogSub.c_str(), smallMid_.Get(),
                D2D1::RectF(headX, T_ + 52.0f, R_, T_ + 84.0f), muted_);
        }

        const float qrColumn = offer ? 320.0f : 0.0f;
        const float bodyTop = T_ + 108.0f;
        const float bodyBottom = offer ? B_ - 146.0f : B_ - 66.0f;
        const D2D1_RECT_F bodyRect = D2D1::RectF(L_, bodyTop, R_ - (offer ? qrColumn + 40.0f : 0.0f), bodyBottom);
        FillRound(bodyRect, surface_, 16.0f);
        const D2D1_RECT_F inner = D2D1::RectF(
            bodyRect.left + 30.0f, bodyRect.top + 22.0f, bodyRect.right - 30.0f, bodyRect.bottom - 22.0f);
        const float lineStep = 30.0f;
        const float scrollPx = static_cast<float>(state.changelogScroll) * lineStep;

        const bool haveBody = !state.changelogBody.empty();
        const std::wstring bodyText = haveBody
            ? state.changelogBody
            : std::wstring(Tr(L"The changelog is not available right now."));
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                bodyText.c_str(), static_cast<UINT32>(bodyText.size()), smallFormat_.Get(),
                inner.right - inner.left, 100000.0f, layout.GetAddressOf()))) {
            if (haveBody) {
                for (const auto& b : state.changelogBold) {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ b.first, b.second });
                    layout->SetDrawingEffect(text_, DWRITE_TEXT_RANGE{ b.first, b.second });
                }
                for (const auto& hh : state.changelogHead) {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                    layout->SetFontSize(27.0f, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                    layout->SetDrawingEffect(accent_, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                }
            }
            DWRITE_TEXT_METRICS tm{};
            const float viewH = inner.bottom - inner.top;
            int maxScroll = 0;
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                maxScroll = tm.height > viewH ? static_cast<int>(ceilf((tm.height - viewH) / lineStep)) : 0;
                g_changelogMaxScroll.store(maxScroll);
            }
            d2dContext_->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            d2dContext_->DrawTextLayout(D2D1::Point2F(inner.left, inner.top - scrollPx), layout.Get(), muted_);
            d2dContext_->PopAxisAlignedClip();
            // text running under the bottom edge fades out instead of ending in half a line
            if (state.changelogScroll < maxScroll) {
                FillGradient(D2D1::RectF(inner.left, inner.bottom - 48.0f, inner.right, bodyRect.bottom - 4.0f), theme::kSurface, 0.0f, 1.0f, true, 0.6f, 0.9f);
            }
            if (maxScroll > 0) {
                DrawScrollThumb(bodyRect.right - 12.0f, inner.top, inner.bottom, viewH / tm.height,
                    static_cast<float>((std::min)(state.changelogScroll, maxScroll)) / static_cast<float>(maxScroll));
            }
        }

        if (offer) {
            const float pad = 16.0f;
            const D2D1_RECT_F card = D2D1::RectF(R_ - qrColumn, bodyTop, R_, bodyTop + qrColumn);
            FillRound(card, MakeBrush(0xFFFFFF), 18.0f);
            DrawQr(state.qr, D2D1::RectF(card.left + pad, card.top + pad, card.right - pad, card.bottom - pad));
            float qy = card.bottom + 18.0f;
            qy += DrawFlow(Tr(L"Scan to open the release page"), smallFormat_.Get(), card.left, qy, qrColumn, 60.0f, muted_) + 6.0f;
            DrawFlow(state.changelogUrl.c_str(), smallFormat_.Get(), card.left, qy, qrColumn, 90.0f, accent_);

            const float btnH = 60.0f;
            const float btnY = B_ - 122.0f;
            const float btnW = (R_ - L_ - 16.0f) / 2.0f;
            const wchar_t* buttonLabels[] = { Tr(L"Skip this version"), Tr(L"Remind me next time") };
            for (int i = 0; i < 2; ++i) {
                const float x = L_ + i * (btnW + 16.0f);
                const D2D1_RECT_F button = D2D1::RectF(x, btnY, x + btnW, btnY + btnH);
                DrawButton(button, nullptr, buttonLabels[i], i == state.changelogSelected, ButtonKind::Neutral);
                RegisterHit(launchhit::kChangelogButtonBase + i, button);
            }
        }

        DrawFooter(offer
            ? Tr(L"Install the new package from a PC. Skipping hides this notice until a newer version is out.")
            : Tr(L"Use up and down to scroll. Press B or select Back to return to the launcher."));
    }

    void DrawScrollThumb(float x, float top, float bottom, float visibleShare, float position) {
        const float trackH = bottom - top;
        const float thumbH = (std::max)(28.0f, trackH * (std::max)(0.0f, (std::min)(1.0f, visibleShare)));
        const float thumbY = top + (trackH - thumbH) * (std::max)(0.0f, (std::min)(1.0f, position));
        FillRound(D2D1::RectF(x, top, x + 4.0f, bottom), line_, 2.0f);
        FillRound(D2D1::RectF(x, thumbY, x + 4.0f, thumbY + thumbH), muted_, 2.0f);
    }

    void DrawModDetail(const AuthUiState& state) {
        const ModCard& card = state.modsDetailCard;

        const D2D1_RECT_F back = DrawBackChip(L_, T_);
        const float iconSide = 136.0f;
        const D2D1_RECT_F iconRect = D2D1::RectF(back.right + 22.0f, T_, back.right + 22.0f + iconSide, T_ + iconSide);
        DrawIconTile(card.iconPath, iconRect, 16.0f, ContentIcon(card.kind));

        const float btnW = 270.0f;
        const float btnH = 64.0f;
        const bool installing = g_installRunning.load();
        const D2D1_RECT_F installBtn = D2D1::RectF(R_ - btnW, T_ + 36.0f, R_, T_ + 36.0f + btnH);
        RegisterHit(launchhit::kDetailInstall, installBtn);
        if (installing) {
            DrawButton(installBtn, L"\uE895", Tr(L"Installing..."), false, ButtonKind::Muted);
        } else {
            DrawButton(installBtn, L"\uE896", card.kind == ContentKind::Modpack ? Tr(L"Install pack") : Tr(L"Install"),
                true, ButtonKind::Primary);
        }

        const float headLeft = iconRect.right + 28.0f;
        const float headRight = installBtn.left - 36.0f;
        DrawText(card.title.c_str(), titleFormat_.Get(),
            D2D1::RectF(headLeft, T_ - 6.0f, headRight, T_ + 50.0f), text_);
        {
            const wchar_t* kind = Tr(ContentLabel(card.kind));
            const float w = MeasureWidth(kind, overlineFormat_.Get()) + 24.0f;
            const D2D1_RECT_F pill = D2D1::RectF(headLeft, T_ + 54.0f, headLeft + w, T_ + 82.0f);
            FillRound(pill, accentSoft_, 14.0f);
            DrawText(kind, overlineFormat_.Get(), D2D1::RectF(pill.left + 12.0f, pill.top, pill.right, pill.bottom), accent_);
            const std::wstring metaLine = !state.modsDetailMeta.empty() ? state.modsDetailMeta : card.status;
            DrawText(metaLine.c_str(), smallMid_.Get(),
                D2D1::RectF(pill.right + 16.0f, T_ + 52.0f, headRight, T_ + 84.0f), muted_);
        }
        DrawText(card.description.c_str(), smallMid_.Get(),
            D2D1::RectF(headLeft, T_ + 94.0f, headRight, T_ + 128.0f), text_);

        const float under = iconRect.bottom + 6.0f;
        if (!state.status.empty()) {
            DrawText(state.status.c_str(), smallMid_.Get(),
                D2D1::RectF(L_, under + 6.0f, R_, under + 36.0f), state.isError ? danger_ : muted_);
        }

        const float bodyTop = CompatBannerBottom(state, L_, R_, under) + 16.0f;
        const D2D1_RECT_F bodyRect = D2D1::RectF(L_, bodyTop, R_, B_);
        FillRound(bodyRect, surface_, 16.0f);
        const D2D1_RECT_F bodyInner = D2D1::RectF(bodyRect.left + 30.0f, bodyRect.top + 22.0f, bodyRect.right - 30.0f, bodyRect.bottom - 22.0f);
        const float lineStep = 34.0f;
        const float scrollPx = static_cast<float>(state.modsDetailScroll) * lineStep;

        const std::wstring bodyText = (state.modsDetailLoading || state.modsDetailBody.empty())
            ? std::wstring(Tr(L"Loading description..."))
            : state.modsDetailBody;
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                bodyText.c_str(), static_cast<UINT32>(bodyText.size()), smallFormat_.Get(),
                bodyInner.right - bodyInner.left, 100000.0f, layout.GetAddressOf()))) {
            if (!state.modsDetailLoading && !state.modsDetailBody.empty()) {
                for (const auto& b : state.modsDetailBold) {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ b.first, b.second });
                    layout->SetDrawingEffect(text_, DWRITE_TEXT_RANGE{ b.first, b.second });
                }
                for (const auto& hh : state.modsDetailHead) {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                    layout->SetFontSize(27.0f, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                    layout->SetDrawingEffect(text_, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                }
            }
            DWRITE_TEXT_METRICS tm{};
            const float viewH = bodyInner.bottom - bodyInner.top;
            int maxScroll = 0;
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                maxScroll = tm.height > viewH ? static_cast<int>(ceilf((tm.height - viewH) / lineStep)) : 0;
                g_detailMaxScroll.store(maxScroll);
            }
            d2dContext_->PushAxisAlignedClip(bodyInner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            d2dContext_->DrawTextLayout(D2D1::Point2F(bodyInner.left, bodyInner.top - scrollPx), layout.Get(), muted_);
            d2dContext_->PopAxisAlignedClip();
            if (state.modsDetailScroll < maxScroll) {
                FillGradient(D2D1::RectF(bodyInner.left, bodyInner.bottom - 48.0f, bodyInner.right, bodyRect.bottom - 4.0f), theme::kSurface, 0.0f, 1.0f, true, 0.6f, 0.9f);
            }
            if (maxScroll > 0) {
                DrawScrollThumb(bodyRect.right - 12.0f, bodyInner.top, bodyInner.bottom, viewH / tm.height,
                    static_cast<float>((std::min)(state.modsDetailScroll, maxScroll)) / static_cast<float>(maxScroll));
            }
        }
    }

    void DrawProfile(const AuthUiState& state) {
        const bool isActive = !state.modsProfileId.empty() && state.modsProfileId == state.activeProfileId;

        const float btnW = 168.0f;
        const float btnH = 58.0f;
        const float btnGap = 10.0f;
        const float actionBarW = state.modsProfileBuiltin ? btnW : (btnW * 5.0f + btnGap * 4.0f);
        const float headRight = R_ - actionBarW - 30.0f;

        const std::wstring nameShown = state.modsRenaming ? (state.modsRenameText + L"_") : state.modsProfileName;
        const D2D1_RECT_F back = DrawBackChip(L_, T_);
        const float headX = back.right + 20.0f;
        DrawText(nameShown.c_str(), titleFormat_.Get(),
            D2D1::RectF(headX, T_ - 4.0f, headRight, T_ + 54.0f), state.modsRenaming ? accent_ : text_);
        const std::wstring modCount = std::to_wstring(state.modsProfileMods.size());
        const std::wstring sub = state.modsProfileBuiltin
            ? TrFormat(L"{0} - Pure vanilla, always available", { state.modsProfileTargetText })
            : state.modsProfileMods.size() == 1
                ? TrFormat(L"{0} - {1} mod installed", { state.modsProfileTargetText, modCount })
                : TrFormat(L"{0} - {1} mods installed", { state.modsProfileTargetText, modCount });
        DrawText(sub.c_str(), smallMid_.Get(), D2D1::RectF(headX, T_ + 52.0f, headRight, T_ + 84.0f), muted_);

        // play sits on the right edge and the rest run leftward from it, the order the d-pad walks them
        auto slot = [&](int fromRight) {
            const float right = R_ - fromRight * (btnW + btnGap);
            return D2D1::RectF(right - btnW, T_, right, T_ + btnH);
        };
        const D2D1_RECT_F playBtn = slot(0);
        RegisterHit(launchhit::kProfilePlay, playBtn);
        if (isActive) {
            DrawButton(playBtn, L"\uE768", Tr(L"Playing"), false, ButtonKind::Muted);
            if (state.modsProfileFocus == 0) FocusRing(playBtn, 12.0f);
        } else {
            DrawButton(playBtn, L"\uE768", Tr(L"Play"), state.modsProfileFocus == 0, ButtonKind::Primary);
        }

        if (!state.modsProfileBuiltin) {
            const D2D1_RECT_F controllerBtn = slot(1);
            RegisterHit(launchhit::kProfileController, controllerBtn);
            DrawButton(controllerBtn, L"\uE7FC", state.modsProfileControllerModEnabled ? Tr(L"Bandit On") : Tr(L"Bandit Off"),
                state.modsProfileFocus == 5, ButtonKind::Neutral, state.modsProfileControllerModEnabled ? accent_ : muted_);

            const D2D1_RECT_F exportBtn = slot(2);
            RegisterHit(launchhit::kProfileExport, exportBtn);
            DrawButton(exportBtn, L"\uE898", Tr(L"Export"), state.modsProfileFocus == 4, ButtonKind::Neutral);

            const D2D1_RECT_F backupBtn = slot(3);
            RegisterHit(launchhit::kProfileBackup, backupBtn);
            DrawButton(backupBtn, L"\uE74E", Tr(L"Backup"), state.modsProfileFocus == 3, ButtonKind::Neutral);

            const D2D1_RECT_F delBtn = slot(4);
            RegisterHit(launchhit::kProfileDelete, delBtn);
            DrawButton(delBtn, L"\uE74D", Tr(L"Delete"), state.modsProfileFocus == 1, ButtonKind::Danger);
        }

        const bool gridFocus = state.modsProfileFocus == 2;
        if (!state.status.empty()) {
            DrawText(state.status.c_str(), smallMid_.Get(),
                D2D1::RectF(L_, T_ + 96.0f, R_, T_ + 128.0f), state.isError ? danger_ : accent_);
        }

        const float bodyTop = T_ + 140.0f;
        const float bodyBottom = B_ - 62.0f;
        const D2D1_RECT_F bodyInner = D2D1::RectF(L_, bodyTop, R_, bodyBottom);
        const int total = static_cast<int>(state.modsProfileMods.size());

        if (total == 0) {
            g_profileRowsVisible.store(1);
            const D2D1_RECT_F empty = D2D1::RectF(L_, bodyTop, R_, bodyTop + 150.0f);
            FillRound(empty, surface_, 16.0f);
            DrawIcon(L"\uE74C", D2D1::RectF(empty.left + 30.0f, empty.top, empty.left + 70.0f, empty.bottom), muted_, true);
            DrawFlow(state.modsProfileBuiltin ? Tr(L"No mods. This is the clean vanilla game.")
                                              : Tr(L"No mods yet. Set this profile active, then install mods from the other tabs."),
                leadFormat_.Get(), empty.left + 92.0f, empty.top + 56.0f, empty.right - empty.left - 122.0f, 80.0f, muted_);
        } else {
            const float colGap = 12.0f;
            const float cardGap = 10.0f;
            const float cardW = (bodyInner.right - bodyInner.left - colGap) * 0.5f;
            const float cardH = 62.0f;
            const float gridH = bodyInner.bottom - bodyInner.top;
            const int rowsVisible = (std::max)(1, static_cast<int>((gridH + cardGap) / (cardH + cardGap)));
            g_profileRowsVisible.store(rowsVisible);
            const int totalRows = (total + 1) / 2;
            const int maxScroll = (std::max)(0, totalRows - rowsVisible);
            const int scroll = (std::min)((std::max)(0, state.modsProfileScroll), maxScroll);

            for (int row = scroll; row < scroll + rowsVisible && row < totalRows; ++row) {
                for (int col = 0; col < 2; ++col) {
                    const int i = row * 2 + col;
                    if (i >= total) break;
                    const float x = bodyInner.left + col * (cardW + colGap);
                    const float y = bodyInner.top + (row - scroll) * (cardH + cardGap);
                    const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                    RegisterHit(launchhit::kProfileGridBase + i, card);
                    const bool sel = gridFocus && i == state.modsProfileSel;
                    FillRound(card, sel ? text_ : surface_, 12.0f);
                    DrawIcon(L"\uE74C", D2D1::RectF(card.left + 14.0f, card.top, card.left + 48.0f, card.bottom), sel ? onLight_ : muted_);
                    std::wstring nm = state.modsProfileMods[static_cast<size_t>(i)];
                    if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, L".jar") == 0) nm = nm.substr(0, nm.size() - 4);
                    DrawText(nm.c_str(), smallMid_.Get(),
                        D2D1::RectF(card.left + 60.0f, card.top, card.right - 16.0f, card.bottom), sel ? onLight_ : text_);
                }
            }

            if (maxScroll > 0) {
                DrawScrollThumb(R_ + 12.0f, bodyInner.top, bodyInner.bottom,
                    static_cast<float>(rowsVisible) / static_cast<float>(totalRows),
                    static_cast<float>(scroll) / static_cast<float>(maxScroll));
                const std::wstring pos = TrFormat(L"row {0} / {1}", { std::to_wstring(scroll + 1), std::to_wstring(totalRows) });
                DrawText(pos.c_str(), valueFormat_.Get(), D2D1::RectF(R_ - 300.0f, B_ - 38.0f, R_, B_), muted_);
            }
        }

        std::wstring profileHint = Tr(L"B  Back");
        if (state.modsRenaming) {
            profileHint = Tr(L"Type a name, then close the keyboard to save");
        } else if (!state.modsProfileBuiltin) {
            if (gridFocus) profileHint = Tr(L"B  Back      X  Remove mod      Y  Rename");
            else if (state.modsProfileFocus == 4) profileHint = Tr(L"B  Back      A  Export .mrpack for PC");
            else if (state.modsProfileFocus == 5) profileHint = Tr(L"B  Back      A  Toggle Bandit controller mod      Y Rename");
            else profileHint = Tr(L"B  Back      A select      X Delete      Y Rename");
        }
        DrawFooter(profileHint.c_str(), state.modsRenaming ? accent_ : muted_);
    }

    void DrawModsBrowser(const AuthUiState& state) {
        const float railRight = L_ + 350.0f;
        const float mainLeft = railRight + 46.0f;

        const D2D1_RECT_F back = DrawBackChip(L_, T_);
        DrawText(Tr(L"Mods"), titleFormat_.Get(), D2D1::RectF(back.right + 20.0f, T_ - 4.0f, railRight, T_ + 54.0f), text_);

        const float sourceH = 50.0f;
        const float sourceY = T_ + 82.0f;
        {
            const D2D1_RECT_F box = D2D1::RectF(L_, sourceY, railRight, sourceY + sourceH);
            FillRound(box, surface_, 12.0f);
            const float segW = (railRight - L_ - 8.0f) * 0.5f;
            for (int i = 0; i < 2; ++i) {
                const float x = L_ + 4.0f + i * segW;
                const D2D1_RECT_F seg = D2D1::RectF(x, sourceY + 4.0f, x + segW, sourceY + sourceH - 4.0f);
                RegisterHit(launchhit::kSourceBase + i, seg);
                const bool active = (i == 1) == (state.modsSource == ModSource::CurseForge);
                const bool focused = state.modsFocus == 4 && active;
                const bool hovered = i == state.modsHoverSource;
                ID2D1Brush* ink = muted_;
                if (focused) {
                    FillRound(seg, text_, 9.0f);
                    ink = onLight_;
                } else if (active) {
                    FillRound(seg, raised_, 9.0f);
                    ink = accent_;
                } else if (hovered) {
                    FillRound(seg, raised_, 9.0f);
                    ink = text_;
                }
                DrawText(i == 0 ? L"Modrinth" : L"CurseForge", pillFormat_.Get(), seg, ink);
            }
        }

        const float tabsTop = sourceY + sourceH + 18.0f;
        const float tabStep = 64.0f;
        const float tabH = 58.0f;
        for (int i = 0; i < modstab::kCount; ++i) {
            const ModsTabInfo& info = ModsTabAt(i);
            const float y = tabsTop + i * tabStep;
            const D2D1_RECT_F tab = D2D1::RectF(L_, y, railRight, y + tabH);
            RegisterHit(launchhit::kTabBase + i, tab);
            const bool active = i == state.selectedModsTab;
            DrawRow(tab, info.icon, Tr(info.label), active && state.modsFocus == 0, active, i == state.modsHoverTab);
        }

        float railY = tabsTop + modstab::kCount * tabStep + 14.0f;
        HLine(L_, railRight, railY);
        railY += 18.0f;
        DrawText(Tr(L"INSTALLS GO TO"), overlineFormat_.Get(), D2D1::RectF(L_ + 4.0f, railY, railRight, railY + 24.0f), muted_);
        const std::wstring who = state.activeProfileName.empty() ? std::wstring(Tr(L"Vanilla")) : state.activeProfileName;
        DrawText(who.c_str(), rowStrongFormat_.Get(), D2D1::RectF(L_ + 4.0f, railY + 24.0f, railRight, railY + 62.0f), accent_);
        railY += 76.0f;

        if (!state.status.empty()) {
            DrawFlow(state.status.c_str(), smallFormat_.Get(), L_ + 4.0f, railY, railRight - L_ - 4.0f, B_ - railY,
                state.isError ? danger_ : muted_);
        }

        const float fieldH = 52.0f;
        const D2D1_RECT_F targetBox = D2D1::RectF(mainLeft, T_, R_, T_ + fieldH);
        RegisterHit(launchhit::kTarget, targetBox);
        const bool targetFocused = state.modsFocus == 3;
        FillRound(targetBox, targetFocused ? raised_ : surface_, 12.0f);
        if (targetFocused) FocusRing(targetBox, 12.0f);
        DrawIcon(L"\uE8EC", D2D1::RectF(targetBox.left + 12.0f, targetBox.top, targetBox.left + 46.0f, targetBox.bottom), targetFocused ? text_ : muted_);
        {
            const std::wstring targetText = TrFormat(L"Target: {0}", { TargetProfileText(CurrentModsTarget(state)) });
            DrawText(targetText.c_str(), smallMid_.Get(),
                D2D1::RectF(targetBox.left + 54.0f, targetBox.top, targetBox.right - 52.0f, targetBox.bottom), text_);
            DrawIcon(state.modsTargetOpen ? L"\uE70E" : L"\uE70D",
                D2D1::RectF(targetBox.right - 48.0f, targetBox.top, targetBox.right - 12.0f, targetBox.bottom),
                targetFocused ? text_ : muted_);
        }

        const D2D1_RECT_F search = D2D1::RectF(mainLeft, targetBox.bottom + 12.0f, R_, targetBox.bottom + 12.0f + fieldH);
        RegisterHit(launchhit::kSearch, search);
        const bool searchFocused = state.modsFocus == 1;
        FillRound(search, searchFocused ? raised_ : surface_, 12.0f);
        if (searchFocused) FocusRing(search, 12.0f);
        DrawIcon(L"\uE721", D2D1::RectF(search.left + 12.0f, search.top, search.left + 46.0f, search.bottom), searchFocused ? text_ : muted_);
        {
            const bool placeholder = state.modsSearchQuery.empty() && !state.modsSearchEditing;
            const std::wstring hint = TrFormat(L"Search {0}", { Tr(ContentNoun(ModsTabAt(state.selectedModsTab).kind, true)) });
            std::wstring shown = placeholder ? hint : state.modsSearchQuery;
            if (state.modsSearchEditing) shown += L"_";
            DrawText(shown.c_str(), smallMid_.Get(),
                D2D1::RectF(search.left + 54.0f, search.top, search.right - 16.0f, search.bottom),
                placeholder ? muted_ : text_);
        }

        const float gridTop = search.bottom + 22.0f;
        const float colGap = 16.0f;
        const float cardGap = 16.0f;
        const float cardW = (R_ - mainLeft - colGap) * 0.5f;
        const float gridH = B_ - gridTop;
        const float desiredCardH = 150.0f;
        int rowsVisible = static_cast<int>((gridH + cardGap) / (desiredCardH + cardGap) + 0.5f);
        if (rowsVisible < 1) rowsVisible = 1;
        const float cardH = (gridH - cardGap * (rowsVisible - 1)) / static_cast<float>(rowsVisible);
        g_modsRowsVisible.store(rowsVisible);

        const int count = static_cast<int>(state.modsCards.size());
        const int totalRows = (count + 1) / 2;
        const int maxScroll = totalRows > rowsVisible ? totalRows - rowsVisible : 0;
        int scroll = state.modsScrollRow;
        if (scroll < 0) scroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;

        for (int row = scroll; row < scroll + rowsVisible && row < totalRows; ++row) {
            for (int col = 0; col < 2; ++col) {
                const int i = row * 2 + col;
                if (i >= count) break;
                const ModCard& item = state.modsCards[static_cast<size_t>(i)];
                const float x = mainLeft + col * (cardW + colGap);
                const float y = gridTop + (row - scroll) * (cardH + cardGap);
                const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                RegisterHit(launchhit::kCardBase + i, card);
                const bool selected = state.modsFocus == 2 && i == state.selectedModIndex;

                FillRound(card, selected ? raised_ : surface_, 16.0f);
                if (selected) FocusRing(card, 16.0f);

                const float imageSide = (std::min)(cardH - 32.0f, 116.0f);
                const float imageTop = card.top + (cardH - imageSide) * 0.5f;
                const D2D1_RECT_F imageRect = D2D1::RectF(card.left + 16.0f, imageTop, card.left + 16.0f + imageSide, imageTop + imageSide);
                const wchar_t* glyph = state.selectedModsTab == modstab::kProfiles
                    ? (item.projectId == L"__new__" ? L"\uE710" : L"\uE8B7")
                    : ContentIcon(item.kind);
                DrawIconTile(item.iconPath, imageRect, 12.0f, glyph);

                const float textLeft = imageRect.right + 20.0f;
                const float textRight = card.right - 18.0f;
                DrawText(item.title.c_str(), cardTitleFormat_.Get(),
                    D2D1::RectF(textLeft, imageRect.top - 2.0f, textRight, imageRect.top + 32.0f), text_);
                DrawFlow(item.description.c_str(), captionFormat_.Get(), textLeft, imageRect.top + 36.0f,
                    textRight - textLeft, 50.0f, muted_);
                if (!item.status.empty()) {
                    DrawText(item.status.c_str(), captionMid_.Get(),
                        D2D1::RectF(textLeft, imageRect.bottom - 26.0f, textRight, imageRect.bottom + 2.0f),
                        item.installed ? ok_ : muted_);
                }
            }
        }

        if (maxScroll > 0) {
            DrawScrollThumb(R_ + 12.0f, gridTop, B_,
                static_cast<float>(rowsVisible) / static_cast<float>(totalRows),
                static_cast<float>(scroll) / static_cast<float>(maxScroll));
        }

        if (state.modsCards.empty()) {
            const std::wstring emptyText = state.selectedModsTab == modstab::kProfiles
                ? std::wstring(Tr(L"No installed mods"))
                : TrFormat(L"No {0} found", { Tr(ContentNoun(ModsTabAt(state.selectedModsTab).kind, true)) });
            const D2D1_RECT_F empty = D2D1::RectF(mainLeft, gridTop, R_, gridTop + 150.0f);
            FillRound(empty, surface_, 16.0f);
            DrawIcon(L"\uE721", D2D1::RectF(empty.left + 30.0f, empty.top, empty.left + 70.0f, empty.bottom), muted_, true);
            DrawText(emptyText.c_str(), rowFormat_.Get(),
                D2D1::RectF(empty.left + 92.0f, empty.top, empty.right - 30.0f, empty.bottom), muted_);
        }

        if (state.modsTargetOpen && !state.modsTargets.empty()) {
            const float rowH = 46.0f;
            const int n = static_cast<int>(state.modsTargets.size());
            const int visible = (std::min)(n, kModsTargetRowsVisible);
            int targetScroll = state.modsTargetScroll;
            if (targetScroll > n - visible) targetScroll = n - visible;
            if (targetScroll < 0) targetScroll = 0;
            const float dropTop = targetBox.bottom + 10.0f;
            const D2D1_RECT_F drop = D2D1::RectF(targetBox.left, dropTop, targetBox.right, dropTop + rowH * visible + 12.0f);
            FillRound(D2D1::RectF(drop.left - 2.0f, drop.top + 4.0f, drop.right + 2.0f, drop.bottom + 12.0f), MakeBrush(0x000000, 0.45f), 16.0f);
            FillRound(drop, MakeBrush(theme::kRaised), 14.0f);
            StrokeRound(drop, line_, 14.0f, 1.0f);
            for (int slot = 0; slot < visible; ++slot) {
                const int i = targetScroll + slot;
                const float ry = dropTop + 6.0f + slot * rowH;
                const D2D1_RECT_F row = D2D1::RectF(drop.left + 6.0f, ry, drop.right - 6.0f, ry + rowH - 4.0f);
                RegisterHit(launchhit::kTargetItemBase + i, row);
                const bool rowSel = i == state.modsTargetSel;
                const bool rowActive = state.modsTargets[static_cast<size_t>(i)].targetId == state.modsBrowseTargetId;
                if (rowSel) FillRound(row, text_, 9.0f);
                DrawText(TargetProfileText(state.modsTargets[static_cast<size_t>(i)]).c_str(), smallMid_.Get(),
                    D2D1::RectF(row.left + 16.0f, row.top, row.right - 40.0f, row.bottom),
                    rowSel ? onLight_ : text_);
                if (rowActive) {
                    DrawIcon(L"\uE73E", D2D1::RectF(row.right - 40.0f, row.top, row.right - 8.0f, row.bottom),
                        rowSel ? onLight_ : accent_);
                }
            }
            if (n > visible) {
                const float trackTop = dropTop + 8.0f;
                const float trackH = rowH * visible - 8.0f;
                const float thumbH = (std::max)(24.0f, trackH * visible / static_cast<float>(n));
                const float thumbY = trackTop + (trackH - thumbH) * targetScroll / static_cast<float>(n - visible);
                FillRound(D2D1::RectF(drop.right - 5.0f, thumbY, drop.right - 2.0f, thumbY + thumbH), muted_, 1.5f);
            }
        }
    }

    static const wchar_t* CrashButtonLabel(const AuthUiState& state, int index) {
        if (state.crashAskConsent) {
            switch (index) {
                case 0: return Tr(L"Send once");
                case 1: return Tr(L"Always send");
                case 2: return Tr(L"Never send");
                default: return L"";
            }
        }
        if (index == 0) return state.crashDetailsOpen ? Tr(L"Hide details") : Tr(L"View details");
        if (index == 1) return Tr(L"Dismiss");
        return L"";
    }

    ComPtr<ICoreWindow> window_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
public:
    bool NeedsRecovery() const { return deviceRecoveryNeeded_ || !targetBitmap_; }
    float Width() const { return width_; }
    float Height() const { return height_; }
    void SetCursor(float x, float y, bool visible) { cursorX_ = x; cursorY_ = y; cursorVisible_ = visible; }
    int MainMenuItemCount() const { return mainMenuRectCount_; }
    bool MainMenuItemRect(int index, D2D1_RECT_F& out) const {
        if (index < 0 || index >= mainMenuRectCount_) return false;
        out = mainMenuRects_[index];
        return true;
    }
    int HitTest(float x, float y) const {
        for (auto it = hitRegions_.rbegin(); it != hitRegions_.rend(); ++it) {
            if (x >= it->rect.left && x <= it->rect.right && y >= it->rect.top && y <= it->rect.bottom) {
                return it->id;
            }
        }
        return launchhit::kNone;
    }
private:
    void RegisterHit(int id, const D2D1_RECT_F& r) { hitRegions_.push_back(HitRegion{ r, id }); }
    struct HitRegion { D2D1_RECT_F rect; int id; };
    std::vector<HitRegion> hitRegions_;
    float cursorX_ = 0.0f;
    float cursorY_ = 0.0f;
    bool cursorVisible_ = false;
    D2D1_RECT_F mainMenuRects_[kMainMenuItems] = {};
    int mainMenuRectCount_ = 0;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<IDWriteTextFormat> codeFormat_;
    ComPtr<IDWriteTextFormat> smallFormat_;
    ComPtr<IDWriteTextFormat> cardTitleFormat_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    ComPtr<IDWriteTextFormat> captionFormat_;
    ComPtr<IDWriteTextFormat> smallMid_;
    ComPtr<IDWriteTextFormat> pillFormat_;
    ComPtr<IDWriteTextFormat> iconFormat_;
    ComPtr<IDWriteTextFormat> iconLgFormat_;
    ComPtr<IDWriteTextFormat> displayFormat_;
    ComPtr<IDWriteTextFormat> headlineFormat_;
    ComPtr<IDWriteTextFormat> leadFormat_;
    ComPtr<IDWriteTextFormat> rowFormat_;
    ComPtr<IDWriteTextFormat> rowStrongFormat_;
    ComPtr<IDWriteTextFormat> rowLargeFormat_;
    ComPtr<IDWriteTextFormat> buttonFormat_;
    ComPtr<IDWriteTextFormat> overlineFormat_;
    ComPtr<IDWriteTextFormat> captionMid_;
    ComPtr<IDWriteTextFormat> valueFormat_;
    ComPtr<IDWriteTextFormat> centerFormat_;
    ComPtr<IDWriteTextFormat> monoFormat_;
    ComPtr<IDWriteTextFormat> monoLargeFormat_;
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushPool_;
    ID2D1SolidColorBrush* text_ = nullptr;
    ID2D1SolidColorBrush* muted_ = nullptr;
    ID2D1SolidColorBrush* line_ = nullptr;
    ID2D1SolidColorBrush* surface_ = nullptr;
    ID2D1SolidColorBrush* raised_ = nullptr;
    ID2D1SolidColorBrush* accent_ = nullptr;
    ID2D1SolidColorBrush* accentSoft_ = nullptr;
    ID2D1SolidColorBrush* danger_ = nullptr;
    ID2D1SolidColorBrush* dangerSoft_ = nullptr;
    ID2D1SolidColorBrush* ok_ = nullptr;
    ID2D1SolidColorBrush* black_ = nullptr;
    ID2D1SolidColorBrush* onLight_ = nullptr;
    float L_ = 0.0f;
    float T_ = 0.0f;
    float R_ = 0.0f;
    float B_ = 0.0f;
    std::string remoteQrUrl_;
    QrMatrix remoteQr_;
    std::map<std::wstring, ComPtr<ID2D1Bitmap1>> bitmapCache_;
    std::vector<std::wstring> screenshotPaths_;
    ULONGLONG screenshotsScanTick_ = 0;
    float width_ = 1280.0f;
    float height_ = 720.0f;
    float displayScale_ = 1.0f;
    UINT renderWidthPx_ = 1280;
    UINT renderHeightPx_ = 720;
    D3D_DRIVER_TYPE d3dDriverType_ = D3D_DRIVER_TYPE_UNKNOWN;
    bool presentOkLogged_ = false;
    bool deviceRecoveryNeeded_ = false;
    bool offscreen_ = false;

    void ResetDeviceResources() {
        if (d2dContext_) d2dContext_->SetTarget(nullptr);
        bitmapCache_.clear();
        brushPool_.clear();
        targetBitmap_.Reset();
        d2dContext_.Reset();
        d2dDevice_.Reset();
        d2dFactory_.Reset();
        swapChain_.Reset();
        if (d3dContext_) { d3dContext_->ClearState(); d3dContext_->Flush(); }
        d3dContext_.Reset();
        d3dDevice_.Reset();
        wicFactory_.Reset();
        dwriteFactory_.Reset();
        codeFormat_.Reset();
        smallFormat_.Reset();
        cardTitleFormat_.Reset();
        titleFormat_.Reset();
        captionFormat_.Reset();
        smallMid_.Reset();
        pillFormat_.Reset();
        iconFormat_.Reset();
        iconLgFormat_.Reset();
        displayFormat_.Reset();
        headlineFormat_.Reset();
        leadFormat_.Reset();
        rowFormat_.Reset();
        rowStrongFormat_.Reset();
        rowLargeFormat_.Reset();
        buttonFormat_.Reset();
        overlineFormat_.Reset();
        captionMid_.Reset();
        valueFormat_.Reset();
        centerFormat_.Reset();
        monoFormat_.Reset();
        monoLargeFormat_.Reset();
        presentOkLogged_ = false;
    }

    static const wchar_t* DriverTypeName(D3D_DRIVER_TYPE type) {
        switch (type) {
        case D3D_DRIVER_TYPE_HARDWARE: return L"hardware";
        case D3D_DRIVER_TYPE_WARP: return L"warp";
        case D3D_DRIVER_TYPE_REFERENCE: return L"reference";
        default: return L"unknown";
        }
    }

    static float ReadDisplayScale() {
        wchar_t value[32] = {};
        const DWORD len = GetEnvironmentVariableW(L"MC_RAW_PIXELS_PER_VIEW_PIXEL", value, ARRAYSIZE(value));
        if (len > 0 && len < ARRAYSIZE(value)) {
            wchar_t* end = nullptr;
            const double parsed = wcstod(value, &end);
            if (parsed >= 0.25 && parsed <= 8.0) {
                return static_cast<float>(parsed);
            }
        }
        return 1.0f;
    }

    static UINT ScaleToPixels(float value, float scale, UINT fallback) {
        if (value <= 0.0f) return fallback;
        if (scale <= 0.0f) scale = 1.0f;
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        return scaled >= 1.0 ? static_cast<UINT>(scaled + 0.5) : 1;
    }

    bool ReadRenderMetrics(float& viewW, float& viewH, float& scale, UINT& pixelW, UINT& pixelH) {
        Rect bounds = {};
        if (!window_ || FAILED(window_->get_Bounds(&bounds))) {
            return false;
        }
        viewW = bounds.Width > 0 ? bounds.Width : width_;
        viewH = bounds.Height > 0 ? bounds.Height : height_;
        scale = ReadDisplayScale();
        pixelW = ScaleToPixels(viewW, scale, renderWidthPx_);
        pixelH = ScaleToPixels(viewH, scale, renderHeightPx_);
        return true;
    }

    bool EnsureRenderTargetSize() {
        float viewW = width_;
        float viewH = height_;
        float scale = displayScale_;
        UINT pixelW = renderWidthPx_;
        UINT pixelH = renderHeightPx_;
        if (!ReadRenderMetrics(viewW, viewH, scale, pixelW, pixelH)) {
            return true;
        }
        if (viewW == width_ && viewH == height_ &&
            scale == displayScale_ &&
            pixelW == renderWidthPx_ && pixelH == renderHeightPx_) {
            return true;
        }

        d2dContext_->SetTarget(nullptr);
        targetBitmap_.Reset();
        HRESULT hr = swapChain_->ResizeBuffers(0, pixelW, pixelH, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen ResizeBuffers failed hr=0x%08X target=%ux%u scale=%.3f", hr, pixelW, pixelH, scale);
            deviceRecoveryNeeded_ = hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
            return false;
        }

        width_ = viewW;
        height_ = viewH;
        displayScale_ = scale;
        renderWidthPx_ = pixelW;
        renderHeightPx_ = pixelH;
        bitmapCache_.clear();
        WriteLogF(L"Auth screen resized %.0fx%.0f view, %ux%u backbuffer, scale=%.3f",
            width_, height_, renderWidthPx_, renderHeightPx_, displayScale_);
        return CreateTargetBitmap();
    }

    bool CreateDeviceResources(bool preferWarp, D3D_FEATURE_LEVEL& level) {
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        HRESULT hr = E_FAIL;
        const D3D_DRIVER_TYPE driverOrder[] = {
            preferWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            preferWarp ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP
        };
        for (D3D_DRIVER_TYPE driverType : driverOrder) {
            hr = D3D11CreateDevice(
                nullptr,
                driverType,
                nullptr,
                flags,
                levels,
                ARRAYSIZE(levels),
                D3D11_SDK_VERSION,
                d3dDevice_.ReleaseAndGetAddressOf(),
                &level,
                d3dContext_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(hr)) {
                d3dDriverType_ = driverType;
                break;
            }
            WriteLogF(L"Auth screen D3D11CreateDevice %s failed hr=0x%08X",
                DriverTypeName(driverType), hr);
        }
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D3D11CreateDevice failed for all drivers hr=0x%08X", hr);
            return false;
        }

        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D factory failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen IDXGIDevice query failed hr=0x%08X", hr);
            return false;
        }

        hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D device failed hr=0x%08X", hr);
            return false;
        }

        hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D context failed hr=0x%08X", hr);
            return false;
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DWrite factory failed hr=0x%08X", hr);
            return false;
        }

        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen WIC factory failed hr=0x%08X", hr);
        }

        CreateTextFormats();
        return true;
    }

    bool CreateTargetBitmap() {
        ComPtr<IDXGISurface> backBuffer;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen back buffer failed hr=0x%08X", hr);
            return false;
        }

        const float dpi = 96.0f * (displayScale_ > 0.0f ? displayScale_ : 1.0f);
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi,
            dpi);
        hr = d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props, targetBitmap_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen target bitmap failed hr=0x%08X", hr);
            return false;
        }
        d2dContext_->SetTarget(targetBitmap_.Get());
        return true;
    }

    void CreateTextFormats() {
        if (!dwriteFactory_) return;
        MakeFormat(codeFormat_, L"Consolas", DWRITE_FONT_WEIGHT_SEMI_BOLD, 56.0f, DWRITE_TEXT_ALIGNMENT_CENTER, true, false);
        MakeFormat(monoFormat_, L"Consolas", DWRITE_FONT_WEIGHT_REGULAR, 19.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        MakeFormat(monoLargeFormat_, L"Consolas", DWRITE_FONT_WEIGHT_REGULAR, 36.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);

        MakeFormat(displayFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_LIGHT, 62.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, false);
        MakeFormat(titleFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 40.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, false);
        MakeFormat(headlineFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 40.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        MakeFormat(leadFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 25.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        MakeFormat(smallFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 21.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        MakeFormat(captionFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 18.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        MakeFormat(cardTitleFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 23.0f, DWRITE_TEXT_ALIGNMENT_LEADING, false, false);
        MakeFormat(rowLargeFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 32.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(rowFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 26.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(rowStrongFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 26.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(buttonFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 23.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(smallMid_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 21.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(captionMid_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 18.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(overlineFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 16.0f, DWRITE_TEXT_ALIGNMENT_LEADING, true, false);
        MakeFormat(valueFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 22.0f, DWRITE_TEXT_ALIGNMENT_TRAILING, true, false);
        MakeFormat(pillFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 21.0f, DWRITE_TEXT_ALIGNMENT_CENTER, true, false);
        MakeFormat(centerFormat_, L"Segoe UI", DWRITE_FONT_WEIGHT_REGULAR, 21.0f, DWRITE_TEXT_ALIGNMENT_CENTER, true, false);

        MakeFormat(iconFormat_, L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_REGULAR, 22.0f, DWRITE_TEXT_ALIGNMENT_CENTER, true, false);
        MakeFormat(iconLgFormat_, L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_REGULAR, 34.0f, DWRITE_TEXT_ALIGNMENT_CENTER, true, false);
    }

    void MakeFormat(ComPtr<IDWriteTextFormat>& out, const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
        DWRITE_TEXT_ALIGNMENT align, bool centered, bool wrap) {
        if (FAILED(dwriteFactory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", out.ReleaseAndGetAddressOf())) || !out) {
            return;
        }
        out->SetTextAlignment(align);
        out->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        out->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        // a long single line ends in an ellipsis, the default cuts it mid letter
        if (!wrap && align == DWRITE_TEXT_ALIGNMENT_LEADING) {
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(out.Get(), ellipsis.GetAddressOf()))) {
                const DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                out->SetTrimming(&trimming, ellipsis.Get());
            }
        }
    }

    void DrawText(const wchar_t* text, IDWriteTextFormat* format, D2D1_RECT_F rect, ID2D1Brush* brush) {
        if (!text || !format || !brush) return;
        d2dContext_->DrawText(
            text,
            static_cast<UINT32>(wcslen(text)),
            format,
            rect,
            brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void FillRound(D2D1_RECT_F r, ID2D1Brush* b, float radius) {
        if (b) d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
    }

    void StrokeRound(D2D1_RECT_F r, ID2D1Brush* b, float radius, float width) {
        if (b) d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b, width);
    }

    void DrawIcon(const wchar_t* glyph, D2D1_RECT_F rect, ID2D1Brush* brush, bool large = false) {
        DrawText(glyph, large ? iconLgFormat_.Get() : iconFormat_.Get(), rect, brush);
    }

    float CompatBannerBottom(const AuthUiState& state, float left, float right, float above) {
        if (!state.modsCompatWarn) return above + 26.0f;

        const D2D1_RECT_F rect = D2D1::RectF(left, above + 40.0f, right, above + 40.0f + 82.0f);
        FillRound(rect, dangerSoft_, 14.0f);
        DrawIcon(L"\uE7BA", D2D1::RectF(rect.left + 18.0f, rect.top, rect.left + 54.0f, rect.bottom), danger_, true);
        DrawText(state.modsCompatAcked ? Tr(L"Install it anyway") : Tr(L"Known not to work on this console"),
            rowStrongFormat_.Get(), D2D1::RectF(rect.left + 70.0f, rect.top + 8.0f, right - 18.0f, rect.top + 44.0f), danger_);
        DrawText(state.modsCompatNote.c_str(), smallMid_.Get(),
            D2D1::RectF(rect.left + 70.0f, rect.top + 42.0f, right - 18.0f, rect.bottom - 8.0f), text_);
        return rect.bottom;
    }

    ComPtr<ID2D1Bitmap1> GetCachedBitmap(const std::wstring& path) {
        if (path.empty()) return nullptr;
        auto found = bitmapCache_.find(path);
        if (found != bitmapCache_.end()) {
            return found->second;
        }

        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return nullptr;
        }

        ComPtr<ID2D1Bitmap1> bitmap;
        if (LoadBitmapFromFile(path, bitmap)) {
            bitmapCache_[path] = bitmap;
            return bitmap;
        }

        return nullptr;
    }

    bool LoadBitmapFromFile(const std::wstring& path, ComPtr<ID2D1Bitmap1>& out) {
        if (!wicFactory_ || !d2dContext_) return false;

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = wicFactory_->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap decoder failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap frame failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap converter create failed hr=0x%08X", hr);
            return false;
        }

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap converter init failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        hr = d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), &props, out.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap D2D bitmap failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        return true;
    }


    void ScanScreenshots() {
        screenshotPaths_.clear();
        const std::wstring dir = GetExecutableDir() + L"\\Assets\\screenshots";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                screenshotPaths_.push_back(dir + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(screenshotPaths_.begin(), screenshotPaths_.end());
    }


    void DrawBitmapCover(ID2D1Bitmap1* bitmap, D2D1_RECT_F rect, float opacity, float zoom, float panX, float panY) {
        if (!bitmap) return;

        const D2D1_SIZE_F size = bitmap->GetSize();
        const float srcW = size.width;
        const float srcH = size.height;
        if (srcW <= 0.0f || srcH <= 0.0f) return;

        const float destW = rect.right - rect.left;
        const float destH = rect.bottom - rect.top;
        if (destW <= 0.0f || destH <= 0.0f) return;

        const float destAspect = destW / destH;
        const float srcAspect = srcW / srcH;
        float cropW = srcW;
        float cropH = srcH;
        if (srcAspect > destAspect) {
            cropW = srcH * destAspect;
        } else {
            cropH = srcW / destAspect;
        }

        zoom = (std::max)(1.0f, zoom);
        cropW /= zoom;
        cropH /= zoom;

        const float maxX = (std::max)(0.0f, (srcW - cropW) * 0.5f);
        const float maxY = (std::max)(0.0f, (srcH - cropH) * 0.5f);
        const float centerX = srcW * 0.5f + maxX * (std::max)(-1.0f, (std::min)(1.0f, panX));
        const float centerY = srcH * 0.5f + maxY * (std::max)(-1.0f, (std::min)(1.0f, panY));
        const D2D1_RECT_F source = D2D1::RectF(
            centerX - cropW * 0.5f,
            centerY - cropH * 0.5f,
            centerX + cropW * 0.5f,
            centerY + cropH * 0.5f);

        d2dContext_->DrawBitmap(bitmap, rect, opacity, D2D1_INTERPOLATION_MODE_LINEAR, source);
    }

    void DrawScreenshots(D2D1_RECT_F rect) {
        const ULONGLONG nowMs = GetTickCount64();
        if (screenshotPaths_.empty() || nowMs - screenshotsScanTick_ > 10000) {
            ScanScreenshots();
            screenshotsScanTick_ = nowMs;
        }
        if (screenshotPaths_.empty()) return;

        const int total = static_cast<int>(screenshotPaths_.size());
        const double now = nowMs / 1000.0;
        const double hold = 6.0;
        const double t = now / hold;
        const long long base = static_cast<long long>(floor(t));
        const int idx = static_cast<int>(((base % total) + total) % total);
        const int nextIdx = (idx + 1) % total;
        const double frac = t - static_cast<double>(base);
        const double fade = frac > 0.82 ? (frac - 0.82) / 0.18 : 0.0;
        const float easedFade = static_cast<float>(fade * fade * (3.0 - 2.0 * fade));

        ComPtr<ID2D1Bitmap1> cur = GetCachedBitmap(screenshotPaths_[static_cast<size_t>(idx)]);
        if (cur) DrawBitmapCover(cur.Get(), rect, 1.0f, 1.0f, 0.0f, 0.0f);
        if (easedFade > 0.0f && total > 1) {
            ComPtr<ID2D1Bitmap1> nxt = GetCachedBitmap(screenshotPaths_[static_cast<size_t>(nextIdx)]);
            if (nxt) DrawBitmapCover(nxt.Get(), rect, easedFade, 1.0f, 0.0f, 0.0f);
        }
    }

    // the caller draws the white card, which is also the light border a qr code needs to scan
    void DrawQr(const QrMatrix& qr, D2D1_RECT_F rect) {
        if (qr.empty()) {
            DrawText(L"QR", codeFormat_.Get(), rect, MakeBrush(0x9A9A9A));
            return;
        }

        constexpr int quiet = 1;
        const float module = (std::min)(
            (rect.right - rect.left) / static_cast<float>(qr.size + quiet * 2),
            (rect.bottom - rect.top) / static_cast<float>(qr.size + quiet * 2));
        const float qrDraw = module * static_cast<float>(qr.size + quiet * 2);
        const float startX = rect.left + ((rect.right - rect.left) - qrDraw) * 0.5f + module * quiet;
        const float startY = rect.top + ((rect.bottom - rect.top) - qrDraw) * 0.5f + module * quiet;

        for (int y = 0; y < qr.size; ++y) {
            for (int x = 0; x < qr.size; ++x) {
                if (!qr.at(x, y)) continue;
                const D2D1_RECT_F moduleRect = D2D1::RectF(
                    startX + x * module,
                    startY + y * module,
                    startX + (x + 1) * module + 0.25f,
                    startY + (y + 1) * module + 0.25f);
                d2dContext_->FillRectangle(moduleRect, black_);
            }
        }
    }
};
