#pragma once

#include "auth_ui_state.h"
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
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::UI::Core;

#include "mods_browser.h"

void ProcessAuthUiEvents();

namespace theme {
constexpr UINT32 kBg        = 0x0B0C0E;
constexpr UINT32 kPanel     = 0x111316;
constexpr UINT32 kSurface   = 0x17191D;
constexpr UINT32 kCard      = 0x1A1C20;
constexpr UINT32 kText      = 0xF2F4F5;
constexpr UINT32 kTextMuted = 0x9BA1A6;
constexpr UINT32 kAccent    = 0x70C486;
constexpr UINT32 kDanger    = 0xE36A5C;
constexpr UINT32 kHairline  = 0xFFFFFF;
constexpr float  kHairlineA = 0.08f;
constexpr float  kSurfaceA  = 0.80f;
constexpr float  kPanelA    = 0.96f;
}

class AuthScreenRenderer {
public:
    bool Initialize(ICoreWindow* window) {
        if (!window) return false;
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

        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = E_FAIL;
        const bool preferWarp = false;
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
        WriteLogF(L"Auth screen initialized %.0fx%.0f view, %ux%u backbuffer, scale=%.3f driver=%s featureLevel=0x%X",
            width_, height_, renderWidthPx_, renderHeightPx_, displayScale_,
            DriverTypeName(d3dDriverType_), static_cast<unsigned int>(level));
        return true;
    }

    void Render(const AuthUiState& state) {
        if (!d2dContext_ || !swapChain_) return;
        if (!EnsureRenderTargetSize()) return;

        mainMenuRectCount_ = 0;
        hitRegions_.clear();
        ComPtr<ID2D1SolidColorBrush> white;
        ComPtr<ID2D1SolidColorBrush> muted;
        ComPtr<ID2D1SolidColorBrush> panel;
        ComPtr<ID2D1SolidColorBrush> accent;
        ComPtr<ID2D1SolidColorBrush> danger;
        ComPtr<ID2D1SolidColorBrush> black;
        ComPtr<ID2D1SolidColorBrush> softEdge;
        ComPtr<ID2D1SolidColorBrush> surfaceFill;
        ComPtr<ID2D1SolidColorBrush> accentSoft;

        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kText), white.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kTextMuted), muted.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kCard), panel.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kAccent), accent.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kDanger), danger.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x000000), black.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kHairline, theme::kHairlineA), softEdge.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kSurface, theme::kSurfaceA), surfaceFill.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kAccent, 0.14f), accentSoft.GetAddressOf());

        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(theme::kBg));

        // keep inside tv title-safe area, otherwise overscan clips the panel edges
        const float marginX = width_ * 0.045f;
        const float marginY = height_ * 0.06f;
        const D2D1_RECT_F frame = D2D1::RectF(marginX, marginY, width_ - marginX, height_ - marginY);
        {
            ComPtr<ID2D1SolidColorBrush> surface, surfaceEdge;
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kPanel, theme::kPanelA), surface.GetAddressOf());
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kHairline, theme::kHairlineA), surfaceEdge.GetAddressOf());
            FillRound(frame, surface.Get(), 22.0f);
            StrokeRound(frame, surfaceEdge.Get(), 22.0f, 1.5f);
        }

        auto finishDraw = [&]() {
            if (cursorVisible_) {
                const float cx = cursorX_;
                const float cy = cursorY_;
                const float arm = 14.0f;
                const float thick = 2.0f;
                const float box = 5.0f;
                black->SetOpacity(0.4f);
                d2dContext_->FillRectangle(D2D1::RectF(cx - box, cy - box, cx + box, cy + box), black.Get());
                black->SetOpacity(1.0f);
                d2dContext_->FillRectangle(D2D1::RectF(cx - arm, cy - thick, cx + arm, cy + thick), white.Get());
                d2dContext_->FillRectangle(D2D1::RectF(cx - thick, cy - arm, cx + thick, cy + arm), white.Get());
            }
            HRESULT hr = d2dContext_->EndDraw();
            if (FAILED(hr)) {
                WriteLogF(L"Auth screen EndDraw failed hr=0x%08X", hr);
            }
            hr = swapChain_->Present(1, 0);
            if (FAILED(hr)) {
                WriteLogF(L"Auth screen Present failed hr=0x%08X", hr);
            } else if (!presentOkLogged_) {
                presentOkLogged_ = true;
                WriteLogF(L"Auth screen Present OK driver=%s backbuffer=%ux%u",
                    DriverTypeName(d3dDriverType_), renderWidthPx_, renderHeightPx_);
            }
            ProcessAuthUiEvents();
        };

        const std::wstring title = state.title.empty() ? L"Microsoft sign-in" : state.title;
        if (state.showModsPage) {
            if (state.modsDetailOpen) {
                const ModCard& card = state.modsDetailCard;
                const float left = frame.left + 40.0f;
                const float right = frame.right - 40.0f;
                const float top = frame.top + 34.0f;

                const float iconSide = 132.0f;
                const D2D1_RECT_F iconRect = D2D1::RectF(left, top, left + iconSide, top + iconSide);
                ComPtr<ID2D1Bitmap1> icon = GetCachedBitmap(card.iconPath);
                if (icon) {
                    DrawBitmapCover(icon.Get(), iconRect, 1.0f, 1.0f, 0.0f, 0.0f);
                    StrokeRound(iconRect, softEdge.Get(), 14.0f, 1.0f);
                } else {
                    ComPtr<ID2D1SolidColorBrush> ph;
                    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x05080B), ph.GetAddressOf());
                    FillRound(iconRect, ph.Get(), 14.0f);
                    StrokeRound(iconRect, softEdge.Get(), 14.0f, 1.0f);
                    DrawIcon(card.isModpack ? L"\uE7B8" : L"\uE74C", iconRect, muted.Get(), true);
                }

                const float headLeft = iconRect.right + 24.0f;
                DrawText(card.title.c_str(), titleFormat_.Get(),
                    D2D1::RectF(headLeft, top, right, top + 48.0f), white.Get());
                DrawText(card.isModpack ? L"Modpack" : L"Mod", captionFormat_.Get(),
                    D2D1::RectF(headLeft, top + 50.0f, headLeft + 200.0f, top + 74.0f), accent.Get());
                const std::wstring metaLine = !state.modsDetailMeta.empty() ? state.modsDetailMeta : card.status;
                DrawText(metaLine.c_str(), smallFormat_.Get(),
                    D2D1::RectF(headLeft, top + 76.0f, right, top + 102.0f), muted.Get());
                DrawText(card.description.c_str(), smallFormat_.Get(),
                    D2D1::RectF(headLeft, top + 104.0f, right, top + iconSide), muted.Get());

                const float btnW = 240.0f;
                const float btnH = 54.0f;
                const bool installing = g_installRunning.load();
                const D2D1_RECT_F installBtn = D2D1::RectF(right - btnW, iconRect.bottom + 16.0f, right, iconRect.bottom + 16.0f + btnH);
                RegisterHit(launchhit::kDetailInstall, installBtn);
                if (!installing) GlowSelect(installBtn, 12.0f);
                FillRound(installBtn, installing ? panel.Get() : accent.Get(), 12.0f);
                StrokeRound(installBtn, accent.Get(), 12.0f, 2.0f);
                DrawIcon(installing ? L"\uE895" : L"\uE896", D2D1::RectF(installBtn.left + 18.0f, installBtn.top, installBtn.left + 46.0f, installBtn.bottom), installing ? muted.Get() : black.Get());
                DrawText(installing ? L"Installing..." : (card.isModpack ? L"Install pack" : L"Install"),
                    bodyMid_.Get(), D2D1::RectF(installBtn.left + 52.0f, installBtn.top, installBtn.right - 8.0f, installBtn.bottom), installing ? muted.Get() : black.Get());

                RegisterHit(launchhit::kBack, D2D1::RectF(left, iconRect.bottom + 22.0f, left + 220.0f, iconRect.bottom + 62.0f));
                DrawIcon(L"\uE72B", D2D1::RectF(left, iconRect.bottom + 26.0f, left + 26.0f, iconRect.bottom + 26.0f + 30.0f), muted.Get());
                DrawText(L"Back", smallFormat_.Get(),
                    D2D1::RectF(left + 30.0f, iconRect.bottom + 30.0f, left + 220.0f, iconRect.bottom + 30.0f + 28.0f), muted.Get());

                if (!state.status.empty()) {
                    DrawText(state.status.c_str(), smallFormat_.Get(),
                        D2D1::RectF(left, installBtn.bottom + 6.0f, right, installBtn.bottom + 32.0f),
                        state.isError ? danger.Get() : muted.Get());
                }

                const float bodyTop = installBtn.bottom + 42.0f;
                const D2D1_RECT_F bodyRect = D2D1::RectF(left, bodyTop, right, frame.bottom - 30.0f);
                FillRound(bodyRect, surfaceFill.Get(), 16.0f);
                StrokeRound(bodyRect, softEdge.Get(), 16.0f, 1.0f);
                const float padb = 16.0f;
                const D2D1_RECT_F bodyInner = D2D1::RectF(bodyRect.left + padb, bodyRect.top + padb, bodyRect.right - padb, bodyRect.bottom - padb);
                const float lineStep = 34.0f;
                const float scrollPx = static_cast<float>(state.modsDetailScroll) * lineStep;

                const std::wstring bodyText = (state.modsDetailLoading || state.modsDetailBody.empty())
                    ? std::wstring(L"Loading description...")
                    : state.modsDetailBody;
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                        bodyText.c_str(), static_cast<UINT32>(bodyText.size()), smallFormat_.Get(),
                        bodyInner.right - bodyInner.left, 100000.0f, layout.GetAddressOf()))) {
                    if (!state.modsDetailLoading && !state.modsDetailBody.empty()) {
                        for (const auto& b : state.modsDetailBold) {
                            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{ b.first, b.second });
                            layout->SetDrawingEffect(white.Get(), DWRITE_TEXT_RANGE{ b.first, b.second });
                        }
                        for (const auto& hh : state.modsDetailHead) {
                            layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                            layout->SetFontSize(27.0f, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                            layout->SetDrawingEffect(white.Get(), DWRITE_TEXT_RANGE{ hh.first, hh.second });
                        }
                    }
                    DWRITE_TEXT_METRICS tm{};
                    const float viewH = bodyInner.bottom - bodyInner.top;
                    if (SUCCEEDED(layout->GetMetrics(&tm))) {
                        const int maxScroll = tm.height > viewH ? static_cast<int>(ceilf((tm.height - viewH) / lineStep)) : 0;
                        g_detailMaxScroll.store(maxScroll);
                    }
                    d2dContext_->PushAxisAlignedClip(bodyInner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    d2dContext_->DrawTextLayout(D2D1::Point2F(bodyInner.left, bodyInner.top - scrollPx), layout.Get(), muted.Get());
                    d2dContext_->PopAxisAlignedClip();
                }

                finishDraw();
                return;
            }
            if (state.modsProfileOpen) {
                const float left = frame.left + 40.0f;
                const float right = frame.right - 40.0f;
                const float top = frame.top + 34.0f;
                const bool isActive = !state.modsProfileId.empty() && state.modsProfileId == state.activeProfileId;

                const float btnW = 148.0f;
                const float btnH = 54.0f;
                const float btnGap = 12.0f;
                const float actionBarW = state.modsProfileBuiltin ? btnW : (btnW * 4.0f + btnGap * 3.0f);

                const std::wstring nameShown = state.modsRenaming ? (state.modsRenameText + L"_") : state.modsProfileName;
                const D2D1_RECT_F profileBack = DrawBackChip(left, top, surfaceFill.Get(), softEdge.Get(), muted.Get());
                DrawText(nameShown.c_str(), titleFormat_.Get(),
                    D2D1::RectF(profileBack.right + 16.0f, top, right - actionBarW - 28.0f, top + 48.0f), state.modsRenaming ? accent.Get() : white.Get());
                const std::wstring sub = state.modsProfileBuiltin
                    ? (state.modsProfileTargetText + L" - Pure vanilla, always available")
                    : (state.modsProfileTargetText + L" - " + std::to_wstring(state.modsProfileMods.size()) +
                        (state.modsProfileMods.size() == 1 ? L" mod installed" : L" mods installed"));
                DrawText(sub.c_str(), captionFormat_.Get(),
                    D2D1::RectF(left, top + 50.0f, right - actionBarW - 28.0f, top + 74.0f), muted.Get());

                const D2D1_RECT_F playBtn = D2D1::RectF(right - btnW, top, right, top + btnH);
                RegisterHit(launchhit::kProfilePlay, playBtn);
                const bool playFocus = state.modsProfileFocus == 0;
                if (playFocus) GlowSelect(playBtn, 12.0f);
                FillRound(playBtn, isActive ? panel.Get() : accent.Get(), 12.0f);
                StrokeRound(playBtn, accent.Get(), 12.0f, playFocus ? 3.0f : 2.0f);
                DrawIcon(L"\uE768", D2D1::RectF(playBtn.left + 14.0f, playBtn.top, playBtn.left + 40.0f, playBtn.bottom), isActive ? muted.Get() : black.Get());
                DrawText(isActive ? L"Playing" : L"Play", bodyMid_.Get(),
                    D2D1::RectF(playBtn.left + 44.0f, playBtn.top, playBtn.right - 8.0f, playBtn.bottom), isActive ? muted.Get() : black.Get());

                if (!state.modsProfileBuiltin) {
                    const D2D1_RECT_F exportBtn = D2D1::RectF(right - btnW * 2.0f - btnGap, top, right - btnW - btnGap, top + btnH);
                    RegisterHit(launchhit::kProfileExport, exportBtn);
                    const bool exportFocus = state.modsProfileFocus == 4;
                    if (exportFocus) GlowSelect(exportBtn, 12.0f);
                    FillRound(exportBtn, surfaceFill.Get(), 12.0f);
                    StrokeRound(exportBtn, accent.Get(), 12.0f, exportFocus ? 3.0f : 2.0f);
                    DrawIcon(L"\uE898", D2D1::RectF(exportBtn.left + 14.0f, exportBtn.top, exportBtn.left + 40.0f, exportBtn.bottom), accent.Get());
                    DrawText(L"Export", bodyMid_.Get(),
                        D2D1::RectF(exportBtn.left + 44.0f, exportBtn.top, exportBtn.right - 8.0f, exportBtn.bottom), accent.Get());

                    const D2D1_RECT_F backupBtn = D2D1::RectF(right - btnW * 3.0f - btnGap * 2.0f, top, right - btnW * 2.0f - btnGap * 2.0f, top + btnH);
                    RegisterHit(launchhit::kProfileBackup, backupBtn);
                    const bool backupFocus = state.modsProfileFocus == 3;
                    if (backupFocus) GlowSelect(backupBtn, 12.0f);
                    FillRound(backupBtn, surfaceFill.Get(), 12.0f);
                    StrokeRound(backupBtn, accent.Get(), 12.0f, backupFocus ? 3.0f : 2.0f);
                    DrawIcon(L"\uE74E", D2D1::RectF(backupBtn.left + 14.0f, backupBtn.top, backupBtn.left + 40.0f, backupBtn.bottom), accent.Get());
                    DrawText(L"Backup", bodyMid_.Get(),
                        D2D1::RectF(backupBtn.left + 44.0f, backupBtn.top, backupBtn.right - 8.0f, backupBtn.bottom), accent.Get());

                    const D2D1_RECT_F delBtn = D2D1::RectF(right - btnW * 4.0f - btnGap * 3.0f, top, right - btnW * 3.0f - btnGap * 3.0f, top + btnH);
                    RegisterHit(launchhit::kProfileDelete, delBtn);
                    const bool delFocus = state.modsProfileFocus == 1;
                    if (delFocus) GlowSelect(delBtn, 12.0f);
                    FillRound(delBtn, surfaceFill.Get(), 12.0f);
                    StrokeRound(delBtn, danger.Get(), 12.0f, delFocus ? 3.0f : 2.0f);
                    DrawIcon(L"\uE74D", D2D1::RectF(delBtn.left + 14.0f, delBtn.top, delBtn.left + 40.0f, delBtn.bottom), danger.Get());
                    DrawText(L"Delete", bodyMid_.Get(),
                        D2D1::RectF(delBtn.left + 44.0f, delBtn.top, delBtn.right - 8.0f, delBtn.bottom), danger.Get());
                }

                const bool gridFocus = state.modsProfileFocus == 2;
                const float hintTop = top + btnH + 12.0f;
                DrawText(state.modsRenaming
                            ? L"Type a name, then close the keyboard to save"
                            : (state.modsProfileBuiltin
                                ? L"B  Back"
                                : (gridFocus ? L"B  Back      X  Remove mod      Y  Rename"
                                             : (state.modsProfileFocus == 4
                                                 ? L"B  Back      A  Export .mrpack for PC"
                                                 : L"B  Back      A select      X Delete      Y Rename"))),
                    smallFormat_.Get(),
                    D2D1::RectF(left, hintTop, right, hintTop + 28.0f),
                    state.modsRenaming ? accent.Get() : muted.Get());

                if (!state.status.empty()) {
                    DrawText(state.status.c_str(), smallFormat_.Get(),
                        D2D1::RectF(left, hintTop + 30.0f, right, hintTop + 56.0f),
                        state.isError ? danger.Get() : accent.Get());
                }

                const float bodyTop = top + btnH + 78.0f;
                const D2D1_RECT_F bodyRect = D2D1::RectF(left, bodyTop, right, frame.bottom - 30.0f);
                FillRound(bodyRect, surfaceFill.Get(), 16.0f);
                StrokeRound(bodyRect, softEdge.Get(), 16.0f, 1.0f);
                const float padb = 16.0f;
                const D2D1_RECT_F bodyInner = D2D1::RectF(bodyRect.left + padb, bodyRect.top + padb, bodyRect.right - padb, bodyRect.bottom - padb);
                const int total = static_cast<int>(state.modsProfileMods.size());

                if (total == 0) {
                    g_profileMaxScroll.store(0);
                    g_profileRowsVisible.store(1);
                    DrawText(state.modsProfileBuiltin ? L"No mods. This is the clean vanilla game."
                                                      : L"No mods yet. Set this profile active, then install mods from the other tabs.",
                        smallFormat_.Get(), D2D1::RectF(bodyInner.left, bodyInner.top, bodyInner.right, bodyInner.top + 28.0f), muted.Get());
                } else {
                    const float colGap = 14.0f;
                    const float cardGap = 12.0f;
                    const float cardW = (bodyInner.right - bodyInner.left - colGap) * 0.5f;
                    const float cardH = 58.0f;
                    const float gridH = bodyInner.bottom - bodyInner.top;
                    const int rowsVisible = (std::max)(1, static_cast<int>((gridH + cardGap) / (cardH + cardGap)));
                    g_profileRowsVisible.store(rowsVisible);
                    const int totalRows = (total + 1) / 2;
                    const int maxScroll = (std::max)(0, totalRows - rowsVisible);
                    g_profileMaxScroll.store(maxScroll);
                    const int scroll = (std::min)((std::max)(0, state.modsProfileScroll), maxScroll);

                    d2dContext_->PushAxisAlignedClip(bodyInner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    for (int row = scroll; row < scroll + rowsVisible && row < totalRows; ++row) {
                        for (int col = 0; col < 2; ++col) {
                            const int i = row * 2 + col;
                            if (i >= total) break;
                            const float x = bodyInner.left + col * (cardW + colGap);
                            const float y = bodyInner.top + (row - scroll) * (cardH + cardGap);
                            const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                            RegisterHit(launchhit::kProfileGridBase + i, card);
                            const bool sel = gridFocus && i == state.modsProfileSel;
                            if (sel) GlowSelect(card, 12.0f);
                            FillRound(card, panel.Get(), 12.0f);
                            StrokeRound(card, sel ? accent.Get() : softEdge.Get(), 12.0f, sel ? 3.0f : 1.0f);
                            DrawIcon(L"\uE74C", D2D1::RectF(card.left + 10.0f, card.top, card.left + 44.0f, card.bottom), sel ? accent.Get() : muted.Get());
                            std::wstring nm = state.modsProfileMods[static_cast<size_t>(i)];
                            if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, L".jar") == 0) nm = nm.substr(0, nm.size() - 4);
                            DrawText(nm.c_str(), smallMid_.Get(),
                                D2D1::RectF(card.left + 48.0f, card.top, card.right - 12.0f, card.bottom), white.Get());
                        }
                    }
                    d2dContext_->PopAxisAlignedClip();

                    if (maxScroll > 0) {
                        const std::wstring pos = L"row " + std::to_wstring(scroll + 1) + L" / " + std::to_wstring(totalRows);
                        DrawText(pos.c_str(), smallFormat_.Get(),
                            D2D1::RectF(bodyRect.right - 140.0f, bodyRect.top - 26.0f, bodyRect.right, bodyRect.top), muted.Get());
                    }
                }

                finishDraw();
                return;
            }
            const float left = frame.left + 36.0f;
            const float tabsRight = frame.left + (frame.right - frame.left) * 0.22f;
            const float cardsLeft = tabsRight + 34.0f;
            const float cardsRight = frame.right - 36.0f;
            const float top = frame.top + 34.0f;
            const float buttonH = 58.0f;
            const float buttonGap = 22.0f;
            const wchar_t* tabs[] = { L"Profiles", L"Popular", L"Latest", L"Recommended", L"Modpacks" };

            const D2D1_RECT_F modsBack = DrawBackChip(left, top, surfaceFill.Get(), softEdge.Get(), muted.Get());
            DrawText(L"Mods", titleFormat_.Get(), D2D1::RectF(modsBack.right + 16.0f, top, tabsRight, top + 48.0f), white.Get());

            const wchar_t* tabIcons[] = { L"\uE8B7", L"\uE735", L"\uE823", L"\uEB52", L"\uE7B8" };
            for (int i = 0; i < 5; ++i) {
                const float y = top + 76.0f + i * (buttonH + buttonGap);
                const D2D1_RECT_F tab = D2D1::RectF(left, y, tabsRight, y + buttonH);
                RegisterHit(launchhit::kTabBase + i, tab);
                const bool selected = i == state.selectedModsTab && state.modsFocus == 0;
                const bool active = i == state.selectedModsTab;
                const bool hovered = i == state.modsHoverTab;
                const bool emphasized = selected || hovered;
                if (emphasized) GlowSelect(tab, 14.0f);
                FillRound(tab, active ? accentSoft.Get() : surfaceFill.Get(), 14.0f);
                StrokeRound(tab, (emphasized || active) ? accent.Get() : softEdge.Get(), 14.0f, emphasized ? 3.0f : (active ? 2.0f : 1.0f));
                DrawIcon(tabIcons[i], D2D1::RectF(tab.left + 8.0f, tab.top, tab.left + 46.0f, tab.bottom), (active || emphasized) ? accent.Get() : muted.Get());
                DrawText(tabs[i], bodyMid_.Get(),
                    D2D1::RectF(tab.left + 52.0f, tab.top, tab.right - 10.0f, tab.bottom),
                    (active || emphasized) ? accent.Get() : white.Get());
            }

            {
                const float infoY = top + 76.0f + 5 * (buttonH + buttonGap) + 8.0f;
                const D2D1_RECT_F infoBox = D2D1::RectF(left, infoY, tabsRight, infoY + 74.0f);
                FillRound(infoBox, surfaceFill.Get(), 12.0f);
                StrokeRound(infoBox, softEdge.Get(), 12.0f, 1.0f);
                DrawIcon(L"\uE768", D2D1::RectF(infoBox.left + 8.0f, infoBox.top + 6.0f, infoBox.left + 34.0f, infoBox.top + 30.0f), muted.Get());
                DrawText(L"INSTALLS GO TO", captionFormat_.Get(),
                    D2D1::RectF(infoBox.left + 36.0f, infoBox.top + 9.0f, infoBox.right - 8.0f, infoBox.top + 30.0f), muted.Get());
                const std::wstring who = state.activeProfileName.empty() ? std::wstring(L"Vanilla") : state.activeProfileName;
                DrawText(who.c_str(), bodyFormat_.Get(),
                    D2D1::RectF(infoBox.left + 12.0f, infoBox.top + 32.0f, infoBox.right - 8.0f, infoBox.bottom - 6.0f), accent.Get());
            }

            if (!state.status.empty()) {
                DrawText(state.status.c_str(), smallFormat_.Get(),
                    D2D1::RectF(left, frame.bottom - 112.0f, tabsRight, frame.bottom - 30.0f),
                    state.isError ? danger.Get() : muted.Get());
            }

            const D2D1_RECT_F list = D2D1::RectF(cardsLeft, top, cardsRight, frame.bottom - 34.0f);
            FillRound(list, surfaceFill.Get(), 16.0f);
            StrokeRound(list, softEdge.Get(), 16.0f, 1.0f);

            const float pad = 16.0f;
            const D2D1_RECT_F inner = D2D1::RectF(list.left + pad, list.top + pad, list.right - pad, list.bottom - pad);

            const float searchH = 46.0f;
            const D2D1_RECT_F targetBox = D2D1::RectF(inner.left, inner.top, inner.right, inner.top + searchH);
            RegisterHit(launchhit::kTarget, targetBox);
            const bool targetFocused = state.modsFocus == 3;
            if (targetFocused) GlowSelect(targetBox, 12.0f);
            FillRound(targetBox, targetFocused ? accentSoft.Get() : panel.Get(), 12.0f);
            StrokeRound(targetBox, targetFocused ? accent.Get() : softEdge.Get(), 12.0f, targetFocused ? 3.0f : 1.0f);
            DrawIcon(L"\uE8EC", D2D1::RectF(targetBox.left + 8.0f, targetBox.top, targetBox.left + 40.0f, targetBox.bottom), targetFocused ? accent.Get() : muted.Get());
            {
                const std::wstring targetText = L"Target: " + TargetProfileText(CurrentModsTarget(state));
                DrawText(targetText.c_str(), smallMid_.Get(),
                    D2D1::RectF(targetBox.left + 44.0f, targetBox.top, targetBox.right - 48.0f, targetBox.bottom),
                    white.Get());
                DrawText(state.modsTargetOpen ? L"\uE70E" : L"\uE70D", iconFormat_.Get(),
                    D2D1::RectF(targetBox.right - 42.0f, targetBox.top, targetBox.right - 10.0f, targetBox.bottom),
                    targetFocused ? accent.Get() : muted.Get());
            }

            const D2D1_RECT_F search = D2D1::RectF(inner.left, targetBox.bottom + 10.0f, inner.right, targetBox.bottom + 10.0f + searchH);
            RegisterHit(launchhit::kSearch, search);
            const bool searchFocused = state.modsFocus == 1;
            if (searchFocused) GlowSelect(search, 12.0f);
            FillRound(search, searchFocused ? accentSoft.Get() : panel.Get(), 12.0f);
            StrokeRound(search, searchFocused ? accent.Get() : softEdge.Get(), 12.0f, searchFocused ? 3.0f : 1.0f);
            DrawIcon(L"\uE721", D2D1::RectF(search.left + 8.0f, search.top, search.left + 40.0f, search.bottom), searchFocused ? accent.Get() : muted.Get());
            {
                const bool placeholder = state.modsSearchQuery.empty() && !state.modsSearchEditing;
                const wchar_t* hint = state.selectedModsTab == 4 ? L"Search modpacks" : L"Search mods";
                std::wstring shown = placeholder ? std::wstring(hint) : state.modsSearchQuery;
                if (state.modsSearchEditing) shown += L"_";
                DrawText(shown.c_str(), smallMid_.Get(),
                    D2D1::RectF(search.left + 44.0f, search.top, search.right - 12.0f, search.bottom),
                    placeholder ? muted.Get() : white.Get());
            }

            const float gridTop = search.bottom + 16.0f;
            const float colGap = 16.0f;
            const float cardGap = 16.0f;
            const float cardW = (inner.right - inner.left - colGap) * 0.5f;
            const float gridH = inner.bottom - gridTop;
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
                    const float x = inner.left + col * (cardW + colGap);
                    const float y = gridTop + (row - scroll) * (cardH + cardGap);
                    const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                    RegisterHit(launchhit::kCardBase + i, card);
                    const bool selected = state.modsFocus == 2 && i == state.selectedModIndex;

                    if (selected) GlowSelect(card, 14.0f);
                    FillRound(card, panel.Get(), 14.0f);
                    StrokeRound(card, selected ? accent.Get() : softEdge.Get(), 14.0f, selected ? 3.0f : 1.0f);

                    const float imageSide = (std::min)(cardH - 24.0f, 112.0f);
                    const float imageTop = card.top + (cardH - imageSide) * 0.5f;
                    const D2D1_RECT_F imageRect = D2D1::RectF(card.left + 14.0f, imageTop, card.left + 14.0f + imageSide, imageTop + imageSide);
                    ComPtr<ID2D1Bitmap1> icon = GetCachedBitmap(state.modsCards[i].iconPath);
                    if (icon) {
                        DrawBitmapCover(icon.Get(), imageRect, 1.0f, 1.0f, 0.0f, 0.0f);
                        StrokeRound(imageRect, softEdge.Get(), 8.0f, 1.0f);
                    } else {
                        ComPtr<ID2D1SolidColorBrush> ph;
                        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x05080B), ph.GetAddressOf());
                        FillRound(imageRect, ph.Get(), 8.0f);
                        StrokeRound(imageRect, softEdge.Get(), 8.0f, 1.0f);
                        const wchar_t* g = state.selectedModsTab == 0
                            ? (state.modsCards[i].projectId == L"__new__" ? L"\uE710" : L"\uE8B7")
                            : (state.modsCards[i].isModpack ? L"\uE7B8" : L"\uE74C");
                        DrawIcon(g, imageRect, muted.Get(), true);
                    }

                    const float textLeft = imageRect.right + 16.0f;
                    const float textRight = card.right - 14.0f;
                    DrawText(state.modsCards[i].title.c_str(), cardTitleFormat_.Get(),
                        D2D1::RectF(textLeft, card.top + 12.0f, textRight, card.top + 44.0f),
                        white.Get());
                    DrawText(state.modsCards[i].description.c_str(), captionFormat_.Get(),
                        D2D1::RectF(textLeft, card.top + 44.0f, textRight, card.bottom - 40.0f),
                        muted.Get());
                    if (!state.modsCards[i].status.empty()) {
                        DrawText(state.modsCards[i].status.c_str(), smallMid_.Get(),
                            D2D1::RectF(textLeft, card.bottom - 40.0f, textRight, card.bottom - 6.0f),
                            state.modsCards[i].installed ? accent.Get() : muted.Get());
                    }
                }
            }

            if (maxScroll > 0) {
                const D2D1_RECT_F track = D2D1::RectF(inner.right - 6.0f, gridTop, inner.right - 2.0f, inner.bottom);
                FillRound(track, panel.Get(), 2.0f);
                const float trackH = track.bottom - track.top;
                const float thumbH = trackH * (static_cast<float>(rowsVisible) / static_cast<float>(totalRows));
                const float thumbY = track.top + (trackH - thumbH) * (static_cast<float>(scroll) / static_cast<float>(maxScroll));
                FillRound(D2D1::RectF(track.left, thumbY, track.right, thumbY + thumbH), accent.Get(), 2.0f);
            }

            if (state.modsCards.empty()) {
                const std::wstring emptyText = state.selectedModsTab == 0
                    ? L"No installed mods"
                    : (state.selectedModsTab == 4 ? L"No modpacks found" : L"No mods found");
                DrawText(emptyText.c_str(), bodyFormat_.Get(),
                    D2D1::RectF(inner.left + 8.0f, gridTop + 8.0f, inner.right - 8.0f, gridTop + 70.0f),
                    muted.Get());
            }

            if (state.modsTargetOpen && !state.modsTargets.empty()) {
                const float rowH = 40.0f;
                const int n = static_cast<int>(state.modsTargets.size());
                const float dropTop = targetBox.bottom + 4.0f;
                const D2D1_RECT_F drop = D2D1::RectF(targetBox.left, dropTop, targetBox.right, dropTop + rowH * n + 12.0f);
                FillRound(drop, surfaceFill.Get(), 12.0f);
                StrokeRound(drop, accent.Get(), 12.0f, 2.0f);
                for (int i = 0; i < n; ++i) {
                    const float ry = dropTop + 6.0f + i * rowH;
                    const D2D1_RECT_F row = D2D1::RectF(drop.left + 6.0f, ry, drop.right - 6.0f, ry + rowH - 4.0f);
                    RegisterHit(launchhit::kTargetItemBase + i, row);
                    const bool rowSel = i == state.modsTargetSel;
                    const bool rowActive = state.modsTargets[static_cast<size_t>(i)].targetId == state.modsBrowseTargetId;
                    if (rowSel) FillRound(row, accentSoft.Get(), 8.0f);
                    DrawText(TargetProfileText(state.modsTargets[static_cast<size_t>(i)]).c_str(), smallMid_.Get(),
                        D2D1::RectF(row.left + 14.0f, row.top, row.right - 32.0f, row.bottom),
                        rowSel ? accent.Get() : white.Get());
                    if (rowActive) {
                        DrawText(L"\uE73E", iconFormat_.Get(),
                            D2D1::RectF(row.right - 30.0f, row.top, row.right - 6.0f, row.bottom),
                            accent.Get());
                    }
                }
            }

            finishDraw();
            return;
        }

        if (state.showMainMenu) {
            const float left = frame.left + 36.0f;
            const float menuRight = frame.left + (frame.right - frame.left) * 0.34f;
            const float previewLeft = menuRight + 34.0f;
            const float previewRight = frame.right - 36.0f;
            const float top = frame.top + 34.0f;
            const float buttonH = 62.0f;
            const float buttonGap = 24.0f;
            const wchar_t* labels[] = { L"Play", L"Mods", L"Remote Files", L"Repair downloads", L"Sign out" };

            DrawText(title.c_str(), titleFormat_.Get(), D2D1::RectF(left, top, menuRight, top + 48.0f), white.Get());

            const wchar_t* menuIcons[] = { L"\uE768", L"\uE74C", L"\uE838", L"\uE72C", L"\uE7E8" };
            for (int i = 0; i < 5; ++i) {
                const float y = top + 76.0f + i * (buttonH + buttonGap);
                const D2D1_RECT_F button = D2D1::RectF(left, y, menuRight, y + buttonH);
                if (i < 5) mainMenuRects_[i] = button;
                const bool sel = i == state.selectedMenuIndex;
                if (sel) GlowSelect(button, 14.0f);
                FillRound(button, sel ? accentSoft.Get() : surfaceFill.Get(), 14.0f);
                StrokeRound(button, sel ? accent.Get() : softEdge.Get(), 14.0f, sel ? 3.0f : 1.0f);
                DrawIcon(menuIcons[i], D2D1::RectF(button.left + 16.0f, button.top, button.left + 50.0f, button.bottom), sel ? accent.Get() : white.Get());
                const D2D1_RECT_F textRect = D2D1::RectF(button.left + 56.0f, button.top, button.right - 12.0f, button.bottom);
                DrawText(labels[i], bodyMid_.Get(), textRect, sel ? accent.Get() : white.Get());
            }
            mainMenuRectCount_ = 5;

            if (!state.status.empty()) {
                const D2D1_RECT_F statusRect = D2D1::RectF(left, frame.bottom - 88.0f, menuRight, frame.bottom - 28.0f);
                DrawText(state.status.c_str(), smallFormat_.Get(), statusRect, state.isError ? danger.Get() : muted.Get());
            }

            const D2D1_RECT_F preview = D2D1::RectF(previewLeft, top, previewRight, frame.bottom - 34.0f);
            FillRound(preview, black.Get(), 16.0f);
            DrawScreenshots(preview, 16.0f);
            StrokeRound(preview, softEdge.Get(), 16.0f, 1.0f);

            if (!state.detail.empty()) {
                const D2D1_RECT_F detailRect = D2D1::RectF(preview.left + 26.0f, preview.bottom - 82.0f, preview.right - 26.0f, preview.bottom - 24.0f);
                DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
            }

            finishDraw();
            return;
        }

        if (state.showRemoteFiles) {
            const float left = frame.left + 54.0f;
            const float right = frame.right - 54.0f;
            const float top = frame.top + 58.0f;
            const D2D1_RECT_F rfBack = DrawBackChip(left, top + 6.0f, surfaceFill.Get(), softEdge.Get(), muted.Get());
            DrawText(L"Remote Files", titleFormat_.Get(), D2D1::RectF(rfBack.right + 16.0f, top, right, top + 58.0f), white.Get());
            DrawText(state.status.c_str(), bodyFormat_.Get(), D2D1::RectF(left, top + 86.0f, right, top + 128.0f), accent.Get());

            const D2D1_RECT_F box = D2D1::RectF(left, top + 152.0f, right, top + 336.0f);
            FillRound(box, surfaceFill.Get(), 14.0f);
            StrokeRound(box, softEdge.Get(), 14.0f, 1.0f);
            DrawText(state.detail.c_str(), bodyMid_.Get(), D2D1::RectF(box.left + 24.0f, box.top + 20.0f, box.right - 24.0f, box.top + 92.0f), white.Get());
            DrawText(
                L"Open the URL and PIN in a browser on your PC or phone.\nImport and export worlds, mods, resource packs, and Modrinth .mrpack files for the active profile.",
                smallFormat_.Get(),
                D2D1::RectF(box.left + 24.0f, box.top + 104.0f, box.right - 24.0f, box.bottom - 22.0f),
                muted.Get());

            const D2D1_RECT_F hint = D2D1::RectF(left, frame.bottom - 92.0f, right, frame.bottom - 36.0f);
            DrawText(L"Select Back, or press B or Escape, to stop sharing and return to the launcher.", smallFormat_.Get(), hint, muted.Get());
            finishDraw();
            return;
        }

        if (!state.showDeviceCode) {
            const float left = frame.left + 54.0f;
            const float right = frame.right - 54.0f;
            const D2D1_RECT_F titleRect = D2D1::RectF(left, frame.top + 72.0f, right, frame.top + 130.0f);
            DrawText(title.c_str(), titleFormat_.Get(), titleRect, white.Get());

            const D2D1_RECT_F statusRect = D2D1::RectF(left, frame.top + 178.0f, right, frame.top + 240.0f);
            DrawText(state.status.c_str(), bodyFormat_.Get(), statusRect, state.isError ? danger.Get() : white.Get());

            if (!state.detail.empty()) {
                const D2D1_RECT_F detailRect = D2D1::RectF(left, frame.top + 248.0f, right, frame.top + 306.0f);
                DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
            }

            if (state.showLaunchLog) {
                const float logTop = frame.top + 326.0f;
                const float logBottom = frame.bottom - 158.0f;
                const D2D1_RECT_F logBox = D2D1::RectF(left, logTop, right, logBottom);
                FillRound(logBox, surfaceFill.Get(), 14.0f);
                StrokeRound(logBox, softEdge.Get(), 14.0f, 1.0f);
                DrawText(L"Live launch log", captionFormat_.Get(),
                    D2D1::RectF(logBox.left + 18.0f, logBox.top + 12.0f, logBox.right - 18.0f, logBox.top + 40.0f),
                    accent.Get());
                const D2D1_RECT_F logTextRect = D2D1::RectF(logBox.left + 18.0f, logBox.top + 46.0f, logBox.right - 18.0f, logBox.bottom - 16.0f);
                d2dContext_->PushAxisAlignedClip(logTextRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                DrawText(state.launchLogText.empty() ? L"Waiting for pre-launch log output..." : state.launchLogText.c_str(),
                    smallFormat_.Get(), logTextRect, muted.Get());
                d2dContext_->PopAxisAlignedClip();
            }

            {
                const float barTop = frame.bottom - 130.0f;
                const float barHeight = 18.0f;
                const float trackWidth = right - left;
                const D2D1_RECT_F track = D2D1::RectF(left, barTop, right, barTop + barHeight);

                if (state.progress >= 0.0f) {
                    const float progress = (std::max)(0.0f, (std::min)(1.0f, state.progress));
                    const D2D1_RECT_F fill = D2D1::RectF(left, barTop, left + trackWidth * progress, barTop + barHeight);
                    FillRound(track, panel.Get(), 9.0f);
                    FillRound(fill, state.isError ? danger.Get() : accent.Get(), 9.0f);

                    wchar_t percent[32] = {};
                    swprintf_s(percent, L"%d%%", static_cast<int>(progress * 100.0f + 0.5f));
                    const D2D1_RECT_F percentRect = D2D1::RectF(left, barTop + 28.0f, left + 140.0f, barTop + 68.0f);
                    DrawText(percent, smallFormat_.Get(), percentRect, muted.Get());
                } else {
                    const float phase = state.animation * 6.2831853f;
                    const float pulse = 0.5f + 0.5f * std::sin(phase);
                    const float fillWidth = trackWidth * (0.28f + 0.34f * pulse);
                    const float fillLeft = left + (trackWidth - fillWidth) * (0.5f + 0.5f * std::sin(phase * 0.65f));
                    const D2D1_RECT_F fill = D2D1::RectF(fillLeft, barTop, fillLeft + fillWidth, barTop + barHeight);
                    FillRound(track, panel.Get(), 9.0f);
                    FillRound(fill, state.isError ? danger.Get() : accent.Get(), 9.0f);

                    const D2D1_RECT_F percentRect = D2D1::RectF(left, barTop + 28.0f, left + 180.0f, barTop + 68.0f);
                    DrawText(L"Loading...", smallFormat_.Get(), percentRect, muted.Get());
                }
            }

            finishDraw();
            return;
        }

        const float dividerX = frame.left + (frame.right - frame.left) * 0.52f;
        d2dContext_->DrawLine(
            D2D1::Point2F(dividerX, frame.top + 32.0f),
            D2D1::Point2F(dividerX, frame.bottom - 32.0f),
            softEdge.Get(),
            1.5f);

        const D2D1_RECT_F titleRect = D2D1::RectF(frame.left + 42.0f, frame.top + 34.0f, dividerX - 42.0f, frame.top + 86.0f);
        DrawText(title.c_str(), titleFormat_.Get(), titleRect, white.Get());

        const D2D1_RECT_F codeBox = D2D1::RectF(frame.left + 42.0f, frame.top + 102.0f, dividerX - 42.0f, frame.top + 190.0f);
        FillRound(codeBox, accentSoft.Get(), 14.0f);
        StrokeRound(codeBox, accent.Get(), 14.0f, 2.0f);
        if (!state.userCode.empty()) {
            DrawText(state.userCode.c_str(), codeFormat_.Get(), codeBox, white.Get());
        }

        std::wstring instruction = L"Enter this code at";
        std::wstring url = state.verificationUri.empty() ? L"microsoft.com/link" : state.verificationUri;
        const D2D1_RECT_F bodyRect = D2D1::RectF(frame.left + 46.0f, frame.top + 218.0f, dividerX - 48.0f, frame.top + 326.0f);
        DrawText((instruction + L"\n" + url).c_str(), bodyFormat_.Get(), bodyRect, white.Get());

        std::wstring status = state.status;
        if (state.secondsRemaining > 0) {
            status += L"\nCode expires in " + std::to_wstring(state.secondsRemaining) + L" seconds";
        }
        const D2D1_RECT_F statusRect = D2D1::RectF(frame.left + 46.0f, frame.bottom - 116.0f, dividerX - 48.0f, frame.bottom - 38.0f);
        DrawText(status.c_str(), smallFormat_.Get(), statusRect, state.isError ? danger.Get() : muted.Get());

        if (!state.detail.empty()) {
            const D2D1_RECT_F detailRect = D2D1::RectF(frame.left + 46.0f, frame.bottom - 160.0f, dividerX - 48.0f, frame.bottom - 118.0f);
            DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
        }

        const float qrSide = (std::min)((frame.right - dividerX) * 0.55f, (frame.bottom - frame.top) * 0.58f);
        const float qrLeft = dividerX + ((frame.right - dividerX) - qrSide) * 0.5f;
        const float qrTop = frame.top + ((frame.bottom - frame.top) - qrSide) * 0.43f;
        const D2D1_RECT_F qrRect = D2D1::RectF(qrLeft, qrTop, qrLeft + qrSide, qrTop + qrSide);
        DrawQr(state.qr, qrRect, white.Get(), black.Get(), muted.Get());

        const D2D1_RECT_F qrLabel = D2D1::RectF(qrLeft, qrRect.bottom + 18.0f, qrLeft + qrSide, qrRect.bottom + 54.0f);
        DrawText(L"Scan QR", smallFormat_.Get(), qrLabel, muted.Get());

        finishDraw();
    }

private:
    ComPtr<ICoreWindow> window_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
public:
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
    D2D1_RECT_F mainMenuRects_[5] = {};
    int mainMenuRectCount_ = 0;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<IDWriteTextFormat> codeFormat_;
    ComPtr<IDWriteTextFormat> bodyFormat_;
    ComPtr<IDWriteTextFormat> smallFormat_;
    ComPtr<IDWriteTextFormat> cardTitleFormat_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    ComPtr<IDWriteTextFormat> captionFormat_;
    ComPtr<IDWriteTextFormat> bodyMid_;
    ComPtr<IDWriteTextFormat> smallMid_;
    ComPtr<IDWriteTextFormat> iconFormat_;
    ComPtr<IDWriteTextFormat> iconLgFormat_;
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
            if (parsed >= 0.5 && parsed <= 8.0) {
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
        dwriteFactory_->CreateTextFormat(
            L"Consolas", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 48.0f, L"en-US", codeFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-US", bodyFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 21.0f, L"en-US", smallFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-US", cardTitleFormat_.GetAddressOf());

        if (codeFormat_) {
            codeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            codeFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (bodyFormat_) {
            bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        if (smallFormat_) {
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        if (cardTitleFormat_) {
            cardTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            cardTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            cardTitleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 40.0f, L"en-US", titleFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-US", captionFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-US", iconFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"en-US", iconLgFormat_.GetAddressOf());
        if (titleFormat_) {
            titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (captionFormat_) {
            captionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            captionFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-US", bodyMid_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 21.0f, L"en-US", smallMid_.GetAddressOf());
        for (IDWriteTextFormat* mf : { bodyMid_.Get(), smallMid_.Get() }) {
            if (!mf) continue;
            mf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            mf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            mf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        for (IDWriteTextFormat* icf : { iconFormat_.Get(), iconLgFormat_.Get() }) {
            if (!icf) continue;
            icf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            icf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            icf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
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

    D2D1_RECT_F DrawBackChip(float x, float top, ID2D1Brush* fill, ID2D1Brush* edge, ID2D1Brush* glyph) {
        const D2D1_RECT_F chip = D2D1::RectF(x, top, x + 46.0f, top + 46.0f);
        FillRound(chip, fill, 12.0f);
        StrokeRound(chip, edge, 12.0f, 1.0f);
        DrawIcon(L"\uE72B", chip, glyph);
        RegisterHit(launchhit::kBack, chip);
        return chip;
    }

    void GlowSelect(D2D1_RECT_F r, float radius) {
        ComPtr<ID2D1SolidColorBrush> g1, g2;
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kAccent, 0.10f), g1.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(theme::kAccent, 0.20f), g2.GetAddressOf());
        if (g1) d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - 10.0f, r.top - 10.0f, r.right + 10.0f, r.bottom + 10.0f), radius + 9.0f, radius + 9.0f), g1.Get());
        if (g2) d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - 5.0f, r.top - 5.0f, r.right + 5.0f, r.bottom + 5.0f), radius + 5.0f, radius + 5.0f), g2.Get());
    }

    void FillVerticalGradient(D2D1_RECT_F r, UINT32 topColor, UINT32 bottomColor) {
        D2D1_GRADIENT_STOP stops[2];
        stops[0].position = 0.0f; stops[0].color = D2D1::ColorF(topColor);
        stops[1].position = 1.0f; stops[1].color = D2D1::ColorF(bottomColor);
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(d2dContext_->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
        props.startPoint = D2D1::Point2F(r.left, r.top);
        props.endPoint = D2D1::Point2F(r.left, r.bottom);
        ComPtr<ID2D1LinearGradientBrush> br;
        if (FAILED(d2dContext_->CreateLinearGradientBrush(props, coll.Get(), br.GetAddressOf()))) return;
        d2dContext_->FillRectangle(r, br.Get());
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

    void DrawScreenshots(D2D1_RECT_F rect, float cornerRadius = 0.0f) {
        const ULONGLONG nowMs = GetTickCount64();
        if (screenshotPaths_.empty() || nowMs - screenshotsScanTick_ > 10000) {
            ScanScreenshots();
            screenshotsScanTick_ = nowMs;
        }

        ComPtr<ID2D1Layer> clipLayer;
        bool usedLayer = false;
        if (cornerRadius > 0.0f) {
            ComPtr<ID2D1RoundedRectangleGeometry> geo;
            if (SUCCEEDED(d2dFactory_->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(rect, cornerRadius, cornerRadius), geo.GetAddressOf())) &&
                SUCCEEDED(d2dContext_->CreateLayer(nullptr, clipLayer.GetAddressOf()))) {
                D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(
                    D2D1::InfiniteRect(), geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                d2dContext_->PushLayer(&lp, clipLayer.Get());
                usedLayer = true;
            }
        }
        if (!usedLayer) {
            d2dContext_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
        }
        auto popClip = [&]() {
            if (usedLayer) d2dContext_->PopLayer();
            else d2dContext_->PopAxisAlignedClip();
        };

        if (screenshotPaths_.empty()) {
            ComPtr<ID2D1SolidColorBrush> dim, hint;
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x070A0E), dim.GetAddressOf());
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x5A6168), hint.GetAddressOf());
            d2dContext_->FillRectangle(rect, dim.Get());
            DrawText(L"No images in Assets\\screenshots", smallMid_.Get(),
                D2D1::RectF(rect.left + 28.0f, rect.top, rect.right - 28.0f, rect.bottom), hint.Get());
            popClip();
            return;
        }

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
        popClip();
    }

    void DrawQr(const QrMatrix& qr, D2D1_RECT_F rect, ID2D1Brush* white, ID2D1Brush* black, ID2D1Brush* muted) {
        d2dContext_->FillRectangle(rect, white);
        if (qr.empty()) {
            DrawText(L"QR", codeFormat_.Get(), rect, muted);
            return;
        }

        constexpr int quiet = 4;
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
                d2dContext_->FillRectangle(moduleRect, black);
            }
        }
    }
};
