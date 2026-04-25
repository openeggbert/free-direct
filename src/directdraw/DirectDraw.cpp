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

    class DirectDrawSurfaceImpl final : public IDirectDrawSurface {
    public:
        enum class SurfaceType {
            Primary,
            Offscreen
        };

        DirectDrawSurfaceImpl(DirectDrawImpl* owner, SurfaceType type, int width, int height);
        ~DirectDrawSurfaceImpl() override;

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override;
        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;
        HRESULT WINAPI Blt(LPRECT lpDestRect,
                           LPDIRECTDRAWSURFACE lpDDSrcSurface,
                           LPRECT lpSrcRect,
                           DWORD dwFlags,
                           LPDDBLTFX lpDDBltFx) override;
        HRESULT WINAPI Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) override;

        [[nodiscard]] SurfaceType GetType() const { return type_; }
        [[nodiscard]] int GetWidth() const { return width_; }
        [[nodiscard]] int GetHeight() const { return height_; }
        [[nodiscard]] const std::vector<uint8_t>& GetPixels() const { return pixels_; }

        HRESULT FillColor(const RECT* destRect, DWORD fillColor);
        HRESULT BlitFrom(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect);

    private:
        friend class DirectDrawImpl;
        std::atomic<ULONG> refCount_;
        DirectDrawImpl* owner_;
        SurfaceType type_;
        int width_;
        int height_;
        std::vector<uint8_t> pixels_;
        SDL_Texture* texture_;
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

        HRESULT PresentSurface(DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect);

    private:
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


    DirectDrawSurfaceImpl::DirectDrawSurfaceImpl(DirectDrawImpl* owner, const SurfaceType type, const int width, const int height)
        : refCount_(1),
          owner_(owner),
          type_(type),
          width_(width),
          height_(height),
          texture_(nullptr)
    {
        if (type_ == SurfaceType::Offscreen) {
            pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u, 0);
        }
    }

    DirectDrawSurfaceImpl::~DirectDrawSurfaceImpl()
    {
        if (texture_) {
            SDL_DestroyTexture(texture_);
        }
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

    HRESULT DirectDrawSurfaceImpl::BlitFrom(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect)
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

        for (int y = 0; y < dstHeight; ++y) {
            const int srcY = sourceClamped.top + (y * srcHeight) / dstHeight;
            const int dstY = destClamped.top + y;
            for (int x = 0; x < dstWidth; ++x) {
                const int srcX = sourceClamped.left + (x * srcWidth) / dstWidth;
                const int dstX = destClamped.left + x;
                const size_t srcOffset = (static_cast<size_t>(srcY) * static_cast<size_t>(source.GetWidth()) + static_cast<size_t>(srcX)) * 4u;
                const size_t dstOffset = (static_cast<size_t>(dstY) * static_cast<size_t>(width_) + static_cast<size_t>(dstX)) * 4u;
                pixels_[dstOffset + 0u] = source.GetPixels()[srcOffset + 0u];
                pixels_[dstOffset + 1u] = source.GetPixels()[srcOffset + 1u];
                pixels_[dstOffset + 2u] = source.GetPixels()[srcOffset + 2u];
                pixels_[dstOffset + 3u] = source.GetPixels()[srcOffset + 3u];
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
            if ((dwFlags & DDBLT_COLORFILL) != 0) {
                (void)lpDDBltFx;
                (void)lpDestRect;
                return DDERR_UNSUPPORTED;
            }

            auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
            if (!sourceSurface || !owner_) {
                return DDERR_INVALIDPARAMS;
            }
            return owner_->PresentSurface(*sourceSurface, lpDestRect, lpSrcRect);
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
            return BlitFrom(*sourceSurface, lpDestRect, lpSrcRect);
        }

        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
    {
        (void)dwFlags;

        if (type_ != SurfaceType::Primary || !owner_ || !lpDDSurfaceTargetOverride) {
            return DDERR_UNSUPPORTED;
        }

        auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSurfaceTargetOverride);
        if (!sourceSurface) {
            return DDERR_INVALIDPARAMS;
        }

        return owner_->PresentSurface(*sourceSurface, nullptr, nullptr);
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
        if (primary) {
            if (sdlWindow_) {
                SDL_GetWindowSize(sdlWindow_, &width, &height);
            }
        } else {
            if ((lpDDSurfaceDesc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT)) {
                return DDERR_INVALIDPARAMS;
            }
            width = static_cast<int>(lpDDSurfaceDesc->dwWidth);
            height = static_cast<int>(lpDDSurfaceDesc->dwHeight);
        }

        auto* surface = new (std::nothrow) DirectDrawSurfaceImpl(this,
                                                                  primary ? DirectDrawSurfaceImpl::SurfaceType::Primary
                                                                          : DirectDrawSurfaceImpl::SurfaceType::Offscreen,
                                                                  width,
                                                                  height);
        if (!surface) {
            return DDERR_OUTOFMEMORY;
        }

        *lplpDDSurface = surface;
        return DD_OK;
    }

    HRESULT DirectDrawImpl::PresentSurface(DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect)
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

        SDL_UpdateTexture(source.texture_, NULL, source.GetPixels().data(), source.GetWidth() * 4);

        const RECT src = srcRect ? ClampRect(*srcRect, source.GetWidth(), source.GetHeight()) : GetFullRect(source.GetWidth(), source.GetHeight());

        int winW, winH;
        SDL_GetRenderOutputSize(renderer_, &winW, &winH);

        const RECT dst = destRect ? ClampRect(*destRect, winW, winH) : GetFullRect(winW, winH);

        SDL_FRect srect = { (float)src.left, (float)src.top, (float)RectWidth(src), (float)RectHeight(src) };
        SDL_FRect drect = { (float)dst.left, (float)dst.top, (float)RectWidth(dst), (float)RectHeight(dst) };

        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, source.texture_, &srect, &drect);
        SDL_RenderPresent(renderer_);

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