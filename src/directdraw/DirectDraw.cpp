#include "ddraw.h"

#include <SDL3/SDL.h>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace {
    using CNA::Internal::Backends::CreateGraphicsBackend;
    using CNA::Internal::Backends::GraphicsBackendCreateArgs;
    using CNA::Internal::Backends::IGraphicsBackend;
    using CNA::Internal::Backends::ISpriteBatchBackend;
    using CNA::Internal::Backends::ITextureBackend;
    using CNA::Internal::Graphics::ImageData;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;

    class DirectDrawImpl;

    class DirectDrawSurfaceImpl final : public IDirectDrawSurface {
    public:
        enum class SurfaceType {
            Primary,
            Offscreen
        };

        DirectDrawSurfaceImpl(DirectDrawImpl* owner, SurfaceType type, int width, int height);

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
        std::atomic<ULONG> refCount_;
        DirectDrawImpl* owner_;
        SurfaceType type_;
        int width_;
        int height_;
        std::vector<uint8_t> pixels_;
    };

    class DirectDrawImpl final : public IDirectDraw {
    public:
        DirectDrawImpl();

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override;
        ULONG WINAPI AddRef() override;
        ULONG WINAPI Release() override;
        HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwFlags) override;
        HRESULT WINAPI CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                     LPDIRECTDRAWSURFACE* lplpDDSurface,
                                     IUnknown* pUnkOuter) override;

        HRESULT PresentSurface(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect);

    private:
        std::atomic<ULONG> refCount_;
        HWND hwnd_;
        SDL_Window* sdlWindow_;
        std::unique_ptr<IGraphicsBackend> backend_;
        std::unique_ptr<ISpriteBatchBackend> spriteBatch_;
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
          height_(height)
    {
        if (type_ == SurfaceType::Offscreen) {
            pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u, 0);
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
          sdlWindow_(nullptr)
    {
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

        GraphicsBackendCreateArgs createArgs{};
        createArgs.window = sdlWindow_;
        backend_ = CreateGraphicsBackend(createArgs);
        if (!backend_) {
            return DDERR_GENERIC;
        }

        spriteBatch_ = backend_->CreateSpriteBatch();
        if (!spriteBatch_) {
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

    HRESULT DirectDrawImpl::PresentSurface(const DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect)
    {
        if (!backend_ || !spriteBatch_ || source.GetType() != DirectDrawSurfaceImpl::SurfaceType::Offscreen) {
            return DDERR_UNSUPPORTED;
        }

        ImageData imageData{};
        imageData.width = source.GetWidth();
        imageData.height = source.GetHeight();
        imageData.pixels = source.GetPixels();

        std::unique_ptr<ITextureBackend> texture = backend_->CreateTexture(imageData);
        if (!texture) {
            return DDERR_GENERIC;
        }

        const RECT src = srcRect ? ClampRect(*srcRect, source.GetWidth(), source.GetHeight()) : GetFullRect(source.GetWidth(), source.GetHeight());
        RECT dst = {};
        if (destRect) {
            int viewportWidth = 0;
            int viewportHeight = 0;
            backend_->GetViewportSize(viewportWidth, viewportHeight);
            dst = ClampRect(*destRect, viewportWidth, viewportHeight);
        } else {
            int viewportWidth = 0;
            int viewportHeight = 0;
            backend_->GetViewportSize(viewportWidth, viewportHeight);
            dst = GetFullRect(viewportWidth, viewportHeight);
        }

        backend_->Clear(0.0f, 0.0f, 0.0f, 1.0f);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture,
                           Rectangle(src.left, src.top, RectWidth(src), RectHeight(src)),
                           Rectangle(dst.left, dst.top, RectWidth(dst), RectHeight(dst)),
                           Color(255, 255, 255, 255));
        spriteBatch_->End();
        backend_->Present();

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