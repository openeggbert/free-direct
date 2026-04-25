/**
 * @file DirectDraw.cpp
 * @brief SDL3-based internal implementation of DirectDraw subset.
 * @note Status: IMPLEMENTED (Minimal backend mapping)
 */
#include "ddraw.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace {
    class DirectDrawImpl;
    class DirectDrawSurfaceImpl;

    class DirectDrawPaletteImpl final : public IDirectDrawPalette {
    public:
        DirectDrawPaletteImpl(DWORD dwFlags, LPPALETTEENTRY lpColorTable) : refCount_(1), flags_(dwFlags) {
            if (lpColorTable) {
                for (int i = 0; i < 256; ++i) {
                    entries_[i] = lpColorTable[i];
                }
            } else {
                for (auto & entrie : entries_) {
                    entrie = {0, 0, 0, 0};
                }
            }
        }

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            (void)riid; (void)ppvObject; return DDERR_UNSUPPORTED;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

        HRESULT WINAPI GetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) override {
            (void)dwFlags;
            if (!lpEntries || dwBase + dwNumEntries > 256) return DDERR_INVALIDPARAMS;
            for (DWORD i = 0; i < dwNumEntries; ++i) {
                lpEntries[i] = entries_[dwBase + i];
            }
            return DD_OK;
        }

        HRESULT WINAPI SetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) override {
            (void)dwFlags;
            if (!lpEntries || dwBase + dwNumEntries > 256) return DDERR_INVALIDPARAMS;
            for (DWORD i = 0; i < dwNumEntries; ++i) {
                entries_[dwBase + i] = lpEntries[i];
            }
            return DD_OK;
        }

    private:
        std::atomic<ULONG> refCount_;
        DWORD flags_;
        PALETTEENTRY entries_[256];
    };

    class DirectDrawClipperImpl final : public IDirectDrawClipper {
    public:
        DirectDrawClipperImpl() : refCount_(1), hwnd_(NULL) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            (void)riid; (void)ppvObject; return DDERR_UNSUPPORTED;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

        HRESULT WINAPI SetHWnd(DWORD dwFlags, HWND hWnd) override {
            (void)dwFlags;
            hwnd_ = hWnd;
            return DD_OK;
        }

    private:
        std::atomic<ULONG> refCount_;
        HWND hwnd_;
    };

    class DirectDrawSurfaceImpl final : public IDirectDrawSurface {
    public:
        enum class SurfaceType {
            Primary,
            Offscreen
        };

        DirectDrawSurfaceImpl(DirectDrawImpl* owner, SurfaceType type, int width, int height, int bpp);
        ~DirectDrawSurfaceImpl() override;

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override;
        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;
        HRESULT WINAPI Blt(LPRECT lpDestRect,
                           LPDIRECTDRAWSURFACE lpDDSrcSurface,
                           LPRECT lpSrcRect,
                           DWORD dwFlags,
                           LPDDBLTFX lpDDBltFx) override;
        HRESULT WINAPI BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
        HRESULT WINAPI Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) override;
        HRESULT WINAPI SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) override;
        HRESULT WINAPI SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) override;
        HRESULT WINAPI IsLost() override;
        HRESULT WINAPI Restore() override;
        HRESULT WINAPI GetDC(HDC* lphDC) override;
        HRESULT WINAPI ReleaseDC(HDC hDC) override;
        HRESULT WINAPI GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc) override;
        HRESULT WINAPI Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) override;
        HRESULT WINAPI Unlock(LPVOID lpSurfaceData) override;
        HRESULT WINAPI SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) override;

        [[nodiscard]] SurfaceType GetType() const { return type_; }
        [[nodiscard]] int GetWidth() const { return width_; }
        [[nodiscard]] int GetHeight() const { return height_; }
        [[nodiscard]] int GetBPP() const { return bpp_; }
        [[nodiscard]] const std::vector<uint8_t>& GetPixels() const { return pixels_; }

        HRESULT FillColor(const RECT* destRect, DWORD fillColor);
        HRESULT BlitFrom(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect, bool useSrcColorKey);

    private:
        friend class DirectDrawImpl;
        std::atomic<ULONG> refCount_;
        DirectDrawImpl* owner_;
        SurfaceType type_;
        int width_;
        int height_;
        int bpp_;
        std::vector<uint8_t> pixels_;
        SDL_Texture* texture_;
        LPDIRECTDRAWPALETTE palette_ = nullptr;
        LPDIRECTDRAWCLIPPER clipper_ = nullptr;
        DDCOLORKEY colorKey_ = {0, 0};
        bool hasSrcColorKey_ = false;
    };

    class DirectDrawImpl final : public IDirectDraw {
    public:
        DirectDrawImpl();
        ~DirectDrawImpl() override;

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override;
        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;
        HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwFlags) override;
        HRESULT WINAPI CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                     LPDIRECTDRAWSURFACE* lplpDDSurface,
                                     IUnknown* pUnkOuter) override;
        HRESULT WINAPI SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP) override;
        HRESULT WINAPI CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) override;
        HRESULT WINAPI CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) override;

        HRESULT PresentSurface(DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect, float rotationAngle = 0.0f);

    private:
        friend class DirectDrawSurfaceImpl;
        std::atomic<ULONG> refCount_;
        HWND hwnd_;
        SDL_Window* sdlWindow_;
        SDL_Renderer* renderer_;
    };

    RECT GetFullRect(const int width, const int height)
    {
        RECT rect{};
        rect.left = 0;
        rect.top = 0;
        rect.right = width;
        rect.bottom = height;
        return rect;
    }

    RECT ClampRect(const RECT& input, const int width, const int height)
    {
        RECT rect{};
        rect.left = std::clamp(input.left, static_cast<LONG>(0), static_cast<LONG>(width));
        rect.top = std::clamp(input.top, static_cast<LONG>(0), static_cast<LONG>(height));
        rect.right = std::clamp(input.right, rect.left, static_cast<LONG>(width));
        rect.bottom = std::clamp(input.bottom, rect.top, static_cast<LONG>(height));
        return rect;
    }

    int RectWidth(const RECT& rect)
    {
        return static_cast<int>(rect.right - rect.left);
    }

    int RectHeight(const RECT& rect)
    {
        return static_cast<int>(rect.bottom - rect.top);
    }


    DirectDrawSurfaceImpl::DirectDrawSurfaceImpl(DirectDrawImpl* owner, const SurfaceType type, const int width, const int height, const int bpp)
        : refCount_(1),
          owner_(owner),
          type_(type),
          width_(width),
          height_(height),
          bpp_(bpp),
          texture_(nullptr)
    {
        if (type_ == SurfaceType::Offscreen) {
            pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * (bpp_ / 8u), 0);
        }
    }

    DirectDrawSurfaceImpl::~DirectDrawSurfaceImpl()
    {
        if (texture_) {
            SDL_DestroyTexture(texture_);
        }
        if (palette_) palette_->Release();
        if (clipper_) clipper_->Release();
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::QueryInterface(const GUID& riid, void** ppvObject)
    {
        (void)riid;
        (void)ppvObject;
        return DDERR_UNSUPPORTED;
    }

    ULONG WINAPI DirectDrawSurfaceImpl::AddRef()
    {
        return ++refCount_;
    }

    ULONG WINAPI DirectDrawSurfaceImpl::Release()
    {
        const ULONG value = --refCount_;
        if (value == 0) {
            delete this;
        }
        return value;
    }

    HRESULT DirectDrawSurfaceImpl::FillColor(const RECT* destRect, const DWORD fillColor)
    {
        if (type_ != SurfaceType::Offscreen) {
            return DDERR_UNSUPPORTED;
        }

        const RECT fillRect = ClampRect(destRect ? *destRect : GetFullRect(width_, height_), width_, height_);

        if (bpp_ == 8) {
            const auto index = static_cast<uint8_t>(fillColor & 0xFFu);
            for (int y = fillRect.top; y < fillRect.bottom; ++y) {
                for (int x = fillRect.left; x < fillRect.right; ++x) {
                    const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
                    pixels_[offset] = index;
                }
            }
            return DD_OK;
        }

        const auto r = static_cast<uint8_t>((fillColor >> 16) & 0xFFu);
        const auto g = static_cast<uint8_t>((fillColor >> 8) & 0xFFu);
        const auto b = static_cast<uint8_t>(fillColor & 0xFFu);

        for (int y = fillRect.top; y < fillRect.bottom; ++y) {
            for (int x = fillRect.left; x < fillRect.right; ++x) {
                const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4u;
                pixels_[offset + 0u] = r;
                pixels_[offset + 1u] = g;
                pixels_[offset + 2u] = b;
                pixels_[offset + 3u] = 255;
            }
        }

        return DD_OK;
    }

    HRESULT DirectDrawSurfaceImpl::BlitFrom(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect, bool useSrcColorKey)
    {
        if (type_ != SurfaceType::Offscreen || source.GetType() != SurfaceType::Offscreen) {
            return DDERR_UNSUPPORTED;
        }

        const RECT sourceClamped = ClampRect(srcRect ? *srcRect : GetFullRect(source.GetWidth(), source.GetHeight()), source.GetWidth(), source.GetHeight());
        const RECT destClamped = ClampRect(destRect ? *destRect : GetFullRect(width_, height_), width_, height_);

        const int srcWidth = RectWidth(sourceClamped);
        const int srcHeight = RectHeight(sourceClamped);
        const int dstWidth = RectWidth(destClamped);
        const int dstHeight = RectHeight(destClamped);
        if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
            return DD_OK;
        }

        uint32_t srcKey = 0;
        if (useSrcColorKey && source.hasSrcColorKey_) {
            srcKey = source.colorKey_.dwColorSpaceLowValue;
        }

        for (int y = 0; y < dstHeight; ++y) {
            const int srcY = sourceClamped.top + (y * srcHeight) / dstHeight;
            const int dstY = destClamped.top + y;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcX = sourceClamped.left + (x * srcWidth) / dstWidth;
                const int dstX = destClamped.left + x;

                if (bpp_ == 8 && source.GetBPP() == 8) {
                    const size_t srcOffset = static_cast<size_t>(srcY) * static_cast<size_t>(source.GetWidth()) + static_cast<size_t>(srcX);
                    const size_t dstOffset = static_cast<size_t>(dstY) * static_cast<size_t>(width_) + static_cast<size_t>(dstX);
                    const uint8_t index = source.GetPixels()[srcOffset];
                    if (useSrcColorKey && source.hasSrcColorKey_ && index == static_cast<uint8_t>(srcKey)) continue;
                    pixels_[dstOffset] = index;
                    continue;
                }

                if (bpp_ == 32 && source.GetBPP() == 32) {
                    const size_t srcOffset = (static_cast<size_t>(srcY) * static_cast<size_t>(source.GetWidth()) + static_cast<size_t>(srcX)) * 4u;
                    const size_t dstOffset = (static_cast<size_t>(dstY) * static_cast<size_t>(width_) + static_cast<size_t>(dstX)) * 4u;

                    const uint8_t r = source.GetPixels()[srcOffset + 0u];
                    const uint8_t g = source.GetPixels()[srcOffset + 1u];
                    const uint8_t b = source.GetPixels()[srcOffset + 2u];
                    const uint8_t a = source.GetPixels()[srcOffset + 3u];

                    if (useSrcColorKey && source.hasSrcColorKey_) {
                        // Assuming color key is in the same format as pixels (simplified)
                        const uint32_t pixelColor = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                        if (pixelColor == srcKey) continue;
                    }

                    pixels_[dstOffset + 0u] = r;
                    pixels_[dstOffset + 1u] = g;
                    pixels_[dstOffset + 2u] = b;
                    pixels_[dstOffset + 3u] = a;
                }
            }
        }

        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Blt(LPRECT lpDestRect,
                                              LPDIRECTDRAWSURFACE lpDDSrcSurface,
                                              LPRECT lpSrcRect,
                                              DWORD dwFlags,
                                              LPDDBLTFX lpDDBltFx)
    {
        if (type_ == SurfaceType::Primary) {
            if (!owner_ || !owner_->renderer_) return DDERR_INVALIDPARAMS;

            if ((dwFlags & DDBLT_COLORFILL) != 0) {
                if (!lpDDBltFx) return DDERR_INVALIDPARAMS;
                uint8_t r = (uint8_t)((lpDDBltFx->dwFillColor >> 16) & 0xFF);
                uint8_t g = (uint8_t)((lpDDBltFx->dwFillColor >> 8) & 0xFF);
                uint8_t b = (uint8_t)(lpDDBltFx->dwFillColor & 0xFF);
                SDL_SetRenderDrawColor(owner_->renderer_, r, g, b, 255);
                SDL_RenderClear(owner_->renderer_);
                return DD_OK;
            }

            auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
            if (!sourceSurface) {
                return DDERR_INVALIDPARAMS;
            }
            float angle = 0.0f;
            if (lpDDBltFx && (dwFlags & DDBLT_ROTATIONANGLE)) {
                angle = (float)lpDDBltFx->dwRotationAngle;
            }
            return owner_->PresentSurface(*sourceSurface, lpDestRect, lpSrcRect, angle);
        }

        if ((dwFlags & DDBLT_COLORFILL) != 0) {
            if (!lpDDBltFx) {
                return DDERR_INVALIDPARAMS;
            }
            return FillColor(lpDestRect, lpDDBltFx->dwFillColor);
        }

        if (lpDDSrcSurface) {
            auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
            if (!sourceSurface) {
                return DDERR_INVALIDPARAMS;
            }
            return BlitFrom(*sourceSurface, lpDestRect, lpSrcRect, false);
        }

        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
    {
        if (!lpDDSrcSurface) return DDERR_INVALIDPARAMS;
        auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        if (!sourceSurface) return DDERR_INVALIDPARAMS;

        RECT destRect{};
        destRect.left = static_cast<LONG>(dwX);
        destRect.top = static_cast<LONG>(dwY);
        if (lpSrcRect) {
            destRect.right = destRect.left + RectWidth(*lpSrcRect);
            destRect.bottom = destRect.top + RectHeight(*lpSrcRect);
        } else {
            destRect.right = destRect.left + sourceSurface->GetWidth();
            destRect.bottom = destRect.top + sourceSurface->GetHeight();
        }

        bool useKey = (dwTrans & DDBLTFAST_SRCCOLORKEY) != 0;
        return BlitFrom(*sourceSurface, &destRect, lpSrcRect, useKey);
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
    {
        if (clipper_) clipper_->Release();
        clipper_ = lpDDClipper;
        if (clipper_) clipper_->AddRef();
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
    {
        if (palette_) palette_->Release();
        palette_ = lpDDPalette;
        if (palette_) palette_->AddRef();
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::IsLost()
    {
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Restore()
    {
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::GetDC(HDC* lphDC)
    {
        if (lphDC) *lphDC = nullptr;
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::ReleaseDC(HDC hDC)
    {
        (void)hDC;
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc)
    {
        if (!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
        if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC)) return DDERR_INVALIDPARAMS;

        lpDDSurfaceDesc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
        lpDDSurfaceDesc->dwWidth = static_cast<DWORD>(width_);
        lpDDSurfaceDesc->dwHeight = static_cast<DWORD>(height_);
        lpDDSurfaceDesc->ddsCaps.dwCaps = (type_ == SurfaceType::Primary) ? DDSCAPS_PRIMARYSURFACE : DDSCAPS_OFFSCREENPLAIN;
        
        lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = (bpp_ == 8) ? 0x00000020L : 0x00000040L; // DDPF_PALETTEINDEXED8 : DDPF_RGB
        lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = static_cast<DWORD>(bpp_);
        if (bpp_ == 32) {
            lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
            lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
            lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
        }

        if (type_ == SurfaceType::Offscreen) {
            lpDDSurfaceDesc->dwFlags |= DDSD_PITCH | DDSD_LPSURFACE;
            lpDDSurfaceDesc->lPitch = static_cast<LONG>(width_ * (bpp_ / 8));
            lpDDSurfaceDesc->lpSurface = const_cast<uint8_t*>(pixels_.data());
        }

        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
    {
        (void)lpDestRect; (void)dwFlags; (void)hEvent;
        return GetSurfaceDesc(lpDDSurfaceDesc);
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Unlock(LPVOID lpSurfaceData)
    {
        (void)lpSurfaceData;
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
    {
        if (dwFlags & DDCKEY_SRCBLT) {
            if (lpDDColorKey) {
                colorKey_ = *lpDDColorKey;
                hasSrcColorKey_ = true;
            } else {
                hasSrcColorKey_ = false;
            }
            return DD_OK;
        }
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
    {
        (void)dwFlags; (void)lpDDSurfaceTargetOverride;

        if (type_ != SurfaceType::Primary || !owner_ || !owner_->renderer_) {
            return DDERR_UNSUPPORTED;
        }

        SDL_RenderPresent(owner_->renderer_);
        return DD_OK;
    }

    DirectDrawImpl::DirectDrawImpl()
        : refCount_(1),
          hwnd_(NULL),
          sdlWindow_(nullptr),
          renderer_(nullptr)
    {
    }

    DirectDrawImpl::~DirectDrawImpl()
    {
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
        }
    }

    HRESULT WINAPI DirectDrawImpl::QueryInterface(const GUID& riid, void** ppvObject)
    {
        (void)riid;
        (void)ppvObject;
        return DDERR_UNSUPPORTED;
    }

    ULONG WINAPI DirectDrawImpl::AddRef()
    {
        return ++refCount_;
    }

    ULONG WINAPI DirectDrawImpl::Release()
    {
        const ULONG value = --refCount_;
        if (value == 0) {
            delete this;
        }
        return value;
    }

    HRESULT WINAPI DirectDrawImpl::SetCooperativeLevel(HWND hWnd, DWORD dwFlags)
    {
        (void)dwFlags;

        if (!hWnd) {
            return DDERR_INVALIDPARAMS;
        }

        hwnd_ = hWnd;
        sdlWindow_ = reinterpret_cast<SDL_Window*>(hwnd_);

        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
        }

        renderer_ = SDL_CreateRenderer(sdlWindow_, NULL);
        if (!renderer_) {
            return DDERR_GENERIC;
        }

        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                                 LPDIRECTDRAWSURFACE* lplpDDSurface,
                                                 IUnknown* pUnkOuter)
    {
        if (!lpDDSurfaceDesc || !lplpDDSurface || pUnkOuter) {
            return DDERR_INVALIDPARAMS;
        }

        if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC)) {
            return DDERR_INVALIDPARAMS;
        }

        const bool primary = (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
        const bool offscreen = (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_OFFSCREENPLAIN) != 0;
        if (primary == offscreen) {
            return DDERR_INVALIDPARAMS;
        }

        int width = 640;
        int height = 480;
        int bpp = 32;

        if (primary) {
            if (sdlWindow_) {
                SDL_GetWindowSize(sdlWindow_, &width, &height);
            }
        } else {
            if ((lpDDSurfaceDesc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) == 0) {
                return DDERR_INVALIDPARAMS;
            }
            width = static_cast<int>(lpDDSurfaceDesc->dwWidth);
            height = static_cast<int>(lpDDSurfaceDesc->dwHeight);
            if (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) {
                bpp = static_cast<int>(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount);
            }
        }

        if (bpp != 8 && bpp != 32) bpp = 32;

        auto* surface = new (std::nothrow) DirectDrawSurfaceImpl(this,
                                                                  primary ? DirectDrawSurfaceImpl::SurfaceType::Primary
                                                                          : DirectDrawSurfaceImpl::SurfaceType::Offscreen,
                                                                  width,
                                                                  height,
                                                                  bpp);
        if (!surface) {
            return DDERR_OUTOFMEMORY;
        }

        *lplpDDSurface = surface;
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
    {
        (void)dwWidth; (void)dwHeight; (void)dwBPP;
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter)
    {
        if (!lplpDDPalette || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDPalette = new (std::nothrow) DirectDrawPaletteImpl(dwFlags, lpColorTable);
        return (*lplpDDPalette) ? DD_OK : DDERR_OUTOFMEMORY;
    }

    HRESULT WINAPI DirectDrawImpl::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter)
    {
        (void)dwFlags;
        if (!lplpDDClipper || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDClipper = new (std::nothrow) DirectDrawClipperImpl();
        return (*lplpDDClipper) ? DD_OK : DDERR_OUTOFMEMORY;
    }

    HRESULT DirectDrawImpl::PresentSurface(DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect, float rotationAngle)
    {
        if (!renderer_ || source.GetType() != DirectDrawSurfaceImpl::SurfaceType::Offscreen) {
            return DDERR_UNSUPPORTED;
        }

        if (!source.texture_) {
            source.texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, source.GetWidth(), source.GetHeight());
        }

        if (!source.texture_) {
            return DDERR_GENERIC;
        }

        if (source.GetBPP() == 8) {
            // Convert 8-bit to 32-bit using palette
            std::vector<uint32_t> temp(static_cast<size_t>(source.GetWidth()) * static_cast<size_t>(source.GetHeight()));
            PALETTEENTRY entries[256];
            bool hasPalette = false;
            if (source.palette_) {
                source.palette_->GetEntries(0, 0, 256, entries);
                hasPalette = true;
            }

            for (size_t i = 0; i < temp.size(); ++i) {
                uint8_t index = source.GetPixels()[i];
                if (hasPalette) {
                    temp[i] = (static_cast<uint32_t>(entries[index].peRed) << 16) |
                              (static_cast<uint32_t>(entries[index].peGreen) << 8) |
                              (static_cast<uint32_t>(entries[index].peBlue)) |
                              0xFF000000;
                } else {
                    temp[i] = (static_cast<uint32_t>(index) << 16) |
                              (static_cast<uint32_t>(index) << 8) |
                              (static_cast<uint32_t>(index)) |
                              0xFF000000;
                }
            }
            SDL_UpdateTexture(source.texture_, NULL, temp.data(), source.GetWidth() * 4);
        } else {
            SDL_UpdateTexture(source.texture_, NULL, source.GetPixels().data(), source.GetWidth() * 4);
        }

        const RECT src = srcRect ? ClampRect(*srcRect, source.GetWidth(), source.GetHeight()) : GetFullRect(source.GetWidth(), source.GetHeight());

        int winW, winH;
        SDL_GetRenderOutputSize(renderer_, &winW, &winH);

        const RECT dst = destRect ? ClampRect(*destRect, winW, winH) : GetFullRect(winW, winH);

        SDL_FRect srect = { (float)src.left, (float)src.top, (float)RectWidth(src), (float)RectHeight(src) };
        SDL_FRect drect = { (float)dst.left, (float)dst.top, (float)RectWidth(dst), (float)RectHeight(dst) };

        if (rotationAngle != 0.0f) {
            SDL_RenderTextureRotated(renderer_, source.texture_, &srect, &drect, (double)rotationAngle, NULL, SDL_FLIP_NONE);
        } else {
            SDL_RenderTexture(renderer_, source.texture_, &srect, &drect);
        }

        return DD_OK;
    }
}

HRESULT WINAPI DirectDrawCreate(const GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter)
{
    (void)lpGUID;

    if (!lplpDD || pUnkOuter) {
        return DDERR_INVALIDPARAMS;
    }

    auto* directDraw = new (std::nothrow) DirectDrawImpl();
    if (!directDraw) {
        return DDERR_OUTOFMEMORY;
    }

    *lplpDD = directDraw;
    return DD_OK;
}