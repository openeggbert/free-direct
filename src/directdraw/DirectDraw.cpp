/**
 * @file DirectDraw.cpp
 * @brief SDL3-based internal implementation of DirectDraw subset.
 * @note Status: IMPLEMENTED (Minimal backend mapping)
 */
#include <ddraw.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

extern "C" HDC FreeApiCreateSurfaceDC(void* pixels, int width, int height, int pitch, int bitsPerPixel);
extern "C" BOOL FreeApiDestroySurfaceDC(HDC hdc);

namespace {
    const char* BoolToText(const bool value)
    {
        return value ? "yes" : "no";
    }

    bool IsDebugPrimaryClearEnabled()
    {
        const char* env = SDL_getenv("FREE_DIRECT_DEBUG_PRIMARY_CLEAR");
        if (!env) {
            return false;
        }

        return SDL_strcasecmp(env, "1") == 0
            || SDL_strcasecmp(env, "true") == 0
            || SDL_strcasecmp(env, "yes") == 0
            || SDL_strcasecmp(env, "on") == 0;
    }

    std::atomic<uint64_t> g_nextSurfaceId{1};

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
        [[nodiscard]] LONG GetPitch() const { return static_cast<LONG>(width_ * (bpp_ / 8)); }
        [[nodiscard]] const std::vector<uint8_t>& GetPixels() const { return pixels_; }
        [[nodiscard]] bool HasPalette() const { return palette_ != nullptr; }
        [[nodiscard]] uint64_t GetDebugId() const { return debugId_; }

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
        uint64_t debugId_ = 0;
        HDC attachedDc_ = nullptr;
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
        bool primaryPresented_;
        uint64_t presentCallCount_;
        bool debugPrimaryClearDone_;
        bool debugPrimaryClearEnabled_;
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
          texture_(nullptr),
          debugId_(g_nextSurfaceId.fetch_add(1))
    {
        if (type_ == SurfaceType::Offscreen) {
            pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * (bpp_ / 8u), 0);
        }

        SDL_Log("free-direct CreateSurface/new surface: id=%llu type=%s size=%dx%d bpp=%d pitch=%ld palette=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                width_,
                height_,
                bpp_,
                static_cast<long>(GetPitch()),
                BoolToText(HasPalette()));
    }

    DirectDrawSurfaceImpl::~DirectDrawSurfaceImpl()
    {
        SDL_Log("free-direct surface destroy: id=%llu type=%s texture=%p palette=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                static_cast<void*>(texture_),
                BoolToText(HasPalette()));

        if (attachedDc_) {
            FreeApiDestroySurfaceDC(attachedDc_);
            attachedDc_ = nullptr;
        }

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
                    if (useSrcColorKey && source.hasSrcColorKey_) {
                        // Assuming color key is in the same format as pixels (simplified)
                        const uint32_t pixelColor = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                        if (pixelColor == srcKey) continue;
                    }

                    pixels_[dstOffset + 0u] = r;
                    pixels_[dstOffset + 1u] = g;
                    pixels_[dstOffset + 2u] = b;
                    // PARTIAL: DirectDraw blits are opaque by default; avoid transparent
                    // desktop-window output when legacy assets have undefined alpha bytes.
                    pixels_[dstOffset + 3u] = 255;
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
        auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        SDL_Log("free-direct Blt: dstId=%llu dstType=%s srcId=%llu src=%p flags=0x%08lx hasPalette=%s hasSrcColorKey=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                sourceSurface ? static_cast<unsigned long long>(sourceSurface->GetDebugId()) : 0ULL,
                static_cast<void*>(lpDDSrcSurface),
                static_cast<unsigned long>(dwFlags),
                BoolToText(HasPalette()),
                BoolToText(hasSrcColorKey_));

        if (lpDestRect) {
            SDL_Log("free-direct Blt: dstRect=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(lpDestRect->left),
                    static_cast<long>(lpDestRect->top),
                    static_cast<long>(lpDestRect->right),
                    static_cast<long>(lpDestRect->bottom));
        }
        if (lpSrcRect) {
            SDL_Log("free-direct Blt: srcRect=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(lpSrcRect->left),
                    static_cast<long>(lpSrcRect->top),
                    static_cast<long>(lpSrcRect->right),
                    static_cast<long>(lpSrcRect->bottom));
        }

        if (type_ == SurfaceType::Primary) {
            if (!owner_ || !owner_->renderer_) {
                SDL_Log("free-direct Blt: primary surface has no active renderer");
                return DDERR_INVALIDPARAMS;
            }

            if ((dwFlags & DDBLT_COLORFILL) != 0) {
                if (!lpDDBltFx) {
                    SDL_Log("free-direct Blt: COLORFILL requested without DDBLTFX");
                    return DDERR_INVALIDPARAMS;
                }
                uint8_t r = (uint8_t)((lpDDBltFx->dwFillColor >> 16) & 0xFF);
                uint8_t g = (uint8_t)((lpDDBltFx->dwFillColor >> 8) & 0xFF);
                uint8_t b = (uint8_t)(lpDDBltFx->dwFillColor & 0xFF);
                SDL_SetRenderDrawColor(owner_->renderer_, r, g, b, 255);
                SDL_RenderClear(owner_->renderer_);
                SDL_Log("free-direct Blt primary COLORFILL: color=0x%08lx", static_cast<unsigned long>(lpDDBltFx->dwFillColor));
                return DD_OK;
            }

            if (!sourceSurface) {
                SDL_Log("free-direct Blt: source surface type mismatch or null");
                return DDERR_INVALIDPARAMS;
            }
            float angle = 0.0f;
            if (lpDDBltFx && (dwFlags & DDBLT_ROTATIONANGLE)) {
                angle = (float)lpDDBltFx->dwRotationAngle;
            }
            const HRESULT hr = owner_->PresentSurface(*sourceSurface, lpDestRect, lpSrcRect, angle);
            SDL_Log("free-direct Blt primary->PresentSurface: dstId=%llu srcId=%llu hr=0x%08lx primaryPresented=%s presentCalls=%llu", 
                    static_cast<unsigned long long>(debugId_),
                    static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                    static_cast<unsigned long>(hr),
                    BoolToText(owner_->primaryPresented_),
                    static_cast<unsigned long long>(owner_->presentCallCount_));
            return hr;
        }

        if ((dwFlags & DDBLT_COLORFILL) != 0) {
            if (!lpDDBltFx) {
                SDL_Log("free-direct Blt: offscreen COLORFILL requested without DDBLTFX");
                return DDERR_INVALIDPARAMS;
            }
            return FillColor(lpDestRect, lpDDBltFx->dwFillColor);
        }

        if (lpDDSrcSurface) {
            if (!sourceSurface) {
                SDL_Log("free-direct Blt: offscreen source surface type mismatch");
                return DDERR_INVALIDPARAMS;
            }
            const HRESULT hr = BlitFrom(*sourceSurface, lpDestRect, lpSrcRect, false);
            SDL_Log("free-direct Blt offscreen: dstId=%llu srcId=%llu hr=0x%08lx", 
                    static_cast<unsigned long long>(debugId_),
                    static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                    static_cast<unsigned long>(hr));
            return hr;
        }

        SDL_Log("free-direct Blt: unsupported flag combination 0x%08lx", static_cast<unsigned long>(dwFlags));
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
    {
        if (!lpDDSrcSurface) {
            SDL_Log("free-direct BltFast: null source surface");
            return DDERR_INVALIDPARAMS;
        }
        auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        if (!sourceSurface) {
            SDL_Log("free-direct BltFast: source surface type mismatch");
            return DDERR_INVALIDPARAMS;
        }

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
        SDL_Log("free-direct BltFast: dstId=%llu srcId=%llu xy=(%lu,%lu) trans=0x%08lx useSrcColorKey=%s", 
                static_cast<unsigned long long>(debugId_),
                static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                static_cast<unsigned long>(dwX),
                static_cast<unsigned long>(dwY),
                static_cast<unsigned long>(dwTrans),
                BoolToText(useKey));

        const HRESULT hr = BlitFrom(*sourceSurface, &destRect, lpSrcRect, useKey);
        SDL_Log("free-direct BltFast result: dstId=%llu srcId=%llu hr=0x%08lx", 
                static_cast<unsigned long long>(debugId_),
                static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                static_cast<unsigned long>(hr));
        return hr;
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
        SDL_Log("free-direct SetPalette: surfaceId=%llu type=%s palette=%p hasPalette=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                static_cast<void*>(lpDDPalette),
                BoolToText(HasPalette()));
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
        if (!lphDC) {
            return DDERR_INVALIDPARAMS;
        }

        if (type_ != SurfaceType::Offscreen || bpp_ != 32 || pixels_.empty()) {
            *lphDC = nullptr;
            SDL_Log("free-direct GetDC: unsupported surfaceId=%llu type=%s bpp=%d", 
                    static_cast<unsigned long long>(debugId_),
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    bpp_);
            return DDERR_UNSUPPORTED;
        }

        if (!attachedDc_) {
            attachedDc_ = FreeApiCreateSurfaceDC(pixels_.data(), width_, height_, static_cast<int>(GetPitch()), bpp_);
            if (!attachedDc_) {
                *lphDC = nullptr;
                SDL_Log("free-direct GetDC: FreeApiCreateSurfaceDC failed for surfaceId=%llu", static_cast<unsigned long long>(debugId_));
                return DDERR_GENERIC;
            }
        }

        *lphDC = attachedDc_;
        SDL_Log("free-direct GetDC: surfaceId=%llu dc=%p size=%dx%d pitch=%ld bpp=%d", 
                static_cast<unsigned long long>(debugId_),
                reinterpret_cast<void*>(attachedDc_),
                width_,
                height_,
                static_cast<long>(GetPitch()),
                bpp_);
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::ReleaseDC(HDC hDC)
    {
        if (hDC != attachedDc_) {
            SDL_Log("free-direct ReleaseDC: unexpected dc=%p for surfaceId=%llu expected=%p", 
                    reinterpret_cast<void*>(hDC),
                    static_cast<unsigned long long>(debugId_),
                    reinterpret_cast<void*>(attachedDc_));
            return DDERR_INVALIDPARAMS;
        }

        SDL_Log("free-direct ReleaseDC: surfaceId=%llu dc=%p", 
                static_cast<unsigned long long>(debugId_),
                reinterpret_cast<void*>(hDC));
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
        (void)hEvent;

        const HRESULT hr = GetSurfaceDesc(lpDDSurfaceDesc);
        SDL_Log("free-direct Lock: surfaceId=%llu type=%s flags=0x%08lx hr=0x%08lx pitch=%ld bpp=%d size=%dx%d hasPalette=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                static_cast<unsigned long>(dwFlags),
                static_cast<unsigned long>(hr),
                static_cast<long>(GetPitch()),
                bpp_,
                width_,
                height_,
                BoolToText(HasPalette()));

        if (lpDestRect) {
            SDL_Log("free-direct Lock: lockRect=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(lpDestRect->left),
                    static_cast<long>(lpDestRect->top),
                    static_cast<long>(lpDestRect->right),
                    static_cast<long>(lpDestRect->bottom));
        }

        return hr;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Unlock(LPVOID lpSurfaceData)
    {
        SDL_Log("free-direct Unlock: surfaceId=%llu type=%s data=%p", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                lpSurfaceData);
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
            SDL_Log("free-direct SetColorKey: surfaceId=%llu type=%s flags=0x%08lx enabled=%s low=0x%08lx high=0x%08lx", 
                    static_cast<unsigned long long>(debugId_),
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<unsigned long>(dwFlags),
                    BoolToText(hasSrcColorKey_),
                    static_cast<unsigned long>(hasSrcColorKey_ ? colorKey_.dwColorSpaceLowValue : 0),
                    static_cast<unsigned long>(hasSrcColorKey_ ? colorKey_.dwColorSpaceHighValue : 0));
            return DD_OK;
        }
        SDL_Log("free-direct SetColorKey: unsupported flags=0x%08lx on surfaceId=%llu", 
                static_cast<unsigned long>(dwFlags),
                static_cast<unsigned long long>(debugId_));
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
    {
        (void)lpDDSurfaceTargetOverride;

        SDL_Log("free-direct Flip: surfaceId=%llu type=%s flags=0x%08lx", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                static_cast<unsigned long>(dwFlags));

        if (type_ != SurfaceType::Primary || !owner_ || !owner_->renderer_) {
            SDL_Log("free-direct Flip: unsupported state (type=%s owner=%p renderer=%p)",
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<void*>(owner_),
                    owner_ ? static_cast<void*>(owner_->renderer_) : nullptr);
            return DDERR_UNSUPPORTED;
        }

        if (owner_->debugPrimaryClearEnabled_ && !owner_->debugPrimaryClearDone_) {
            SDL_SetRenderDrawColor(owner_->renderer_, 255, 0, 255, 255);
            SDL_RenderClear(owner_->renderer_);
            SDL_RenderPresent(owner_->renderer_);
            owner_->presentCallCount_++;
            owner_->primaryPresented_ = true;
            owner_->debugPrimaryClearDone_ = true;
            SDL_Log("free-direct debug primary clear/present: executed once before normal Flip present (set FREE_DIRECT_DEBUG_PRIMARY_CLEAR=1)");
        }

        const bool firstPrimaryPresent = !owner_->primaryPresented_;

        SDL_RenderPresent(owner_->renderer_);
        owner_->presentCallCount_++;
        owner_->primaryPresented_ = true;
        SDL_Log("free-direct Flip present: first=%s primaryPresented=%s presentCalls=%llu", 
                BoolToText(firstPrimaryPresent),
                BoolToText(owner_->primaryPresented_),
                static_cast<unsigned long long>(owner_->presentCallCount_));
        return DD_OK;
    }

    DirectDrawImpl::DirectDrawImpl()
        : refCount_(1),
          hwnd_(NULL),
          sdlWindow_(nullptr),
          renderer_(nullptr),
          primaryPresented_(false),
          presentCallCount_(0),
          debugPrimaryClearDone_(false),
          debugPrimaryClearEnabled_(IsDebugPrimaryClearEnabled())
    {
        SDL_Log("free-direct DirectDrawImpl ctor: debugPrimaryClearEnabled=%s", BoolToText(debugPrimaryClearEnabled_));
    }

    DirectDrawImpl::~DirectDrawImpl()
    {
        SDL_Log("free-direct DirectDrawImpl dtor: primaryPresented=%s presentCalls=%llu renderer=%p window=%p", 
                BoolToText(primaryPresented_),
                static_cast<unsigned long long>(presentCallCount_),
                static_cast<void*>(renderer_),
                static_cast<void*>(sdlWindow_));
        if (renderer_) SDL_DestroyRenderer(renderer_);
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
        SDL_Log("free-direct SetCooperativeLevel: hWnd=%p flags=0x%08lx", hWnd, static_cast<unsigned long>(dwFlags));

        if (!hWnd) {
            SDL_Log("free-direct SetCooperativeLevel: invalid null HWND");
            return DDERR_INVALIDPARAMS;
        }

        hwnd_ = hWnd;
        sdlWindow_ = reinterpret_cast<SDL_Window*>(hwnd_);

        SDL_Renderer* windowRenderer = SDL_GetRenderer(sdlWindow_);
        if (windowRenderer && windowRenderer != renderer_) {
            // PARTIAL: The legacy game may recreate DirectDraw over the same HWND
            // during cache/bootstrap. Ensure renderer recreation does not fail due
            // to an already attached renderer on the SDL window.
            SDL_DestroyRenderer(windowRenderer);
            SDL_Log("free-direct SetCooperativeLevel: destroyed pre-existing SDL renderer=%p", static_cast<void*>(windowRenderer));
        }

        if (renderer_) {
            SDL_Log("free-direct SetCooperativeLevel: destroyed previous renderer=%p", static_cast<void*>(renderer_));
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }

        renderer_ = SDL_CreateRenderer(sdlWindow_, NULL);
        if (renderer_) {
            SDL_Log("free-direct SDL_CreateRenderer: window=%p renderer=%p backend=default", static_cast<void*>(sdlWindow_), static_cast<void*>(renderer_));
        }
        if (!renderer_) {
            // PARTIAL: Legacy compatibility fallback for environments where
            // the default renderer cannot be created for the existing window.
            renderer_ = SDL_CreateRenderer(sdlWindow_, "software");
            if (renderer_) {
                SDL_Log("free-direct SDL_CreateRenderer: fallback backend=software renderer=%p", static_cast<void*>(renderer_));
            }
        }
        if (!renderer_) {
            SDL_Log("free-direct SetCooperativeLevel: SDL_CreateRenderer failed: %s", SDL_GetError());
            return DDERR_GENERIC;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(sdlWindow_, &windowWidth, &windowHeight);
        SDL_Log("free-direct SetCooperativeLevel: SDL window=%p size=%dx%d id=%u", 
                static_cast<void*>(sdlWindow_),
                windowWidth,
                windowHeight,
                static_cast<unsigned>(SDL_GetWindowID(sdlWindow_)));

        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
        SDL_Log("free-direct SetCooperativeLevel: renderer output size=%dx%d", outputWidth, outputHeight);

        if (debugPrimaryClearEnabled_ && !debugPrimaryClearDone_) {
            SDL_SetRenderDrawColor(renderer_, 0, 128, 255, 255);
            SDL_RenderClear(renderer_);
            SDL_RenderPresent(renderer_);
            presentCallCount_++;
            primaryPresented_ = true;
            debugPrimaryClearDone_ = true;
            SDL_Log("free-direct debug primary clear/present: executed in SetCooperativeLevel to validate visible output");
        }

        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                                 LPDIRECTDRAWSURFACE* lplpDDSurface,
                                                 IUnknown* pUnkOuter)
    {
        SDL_Log("free-direct CreateSurface: desc=%p out=%p outer=%p", 
                static_cast<const void*>(lpDDSurfaceDesc),
                static_cast<void*>(lplpDDSurface),
                static_cast<void*>(pUnkOuter));

        if (!lpDDSurfaceDesc || !lplpDDSurface || pUnkOuter) {
            SDL_Log("free-direct CreateSurface: invalid params");
            return DDERR_INVALIDPARAMS;
        }

        if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC)) {
            SDL_Log("free-direct CreateSurface: invalid desc size=%lu expected=%zu", 
                    static_cast<unsigned long>(lpDDSurfaceDesc->dwSize),
                    sizeof(DDSURFACEDESC));
            return DDERR_INVALIDPARAMS;
        }

        const DWORD caps = lpDDSurfaceDesc->ddsCaps.dwCaps;
        const bool primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
        const bool offscreenPlain = (caps & DDSCAPS_OFFSCREENPLAIN) != 0;
        const bool systemMemory = (caps & DDSCAPS_SYSTEMMEMORY) != 0;

        SDL_Log("free-direct CreateSurface request: flags=0x%08lx caps=0x%08lx primary=%s offscreenPlain=%s systemMemory=%s width=%lu height=%lu pfFlags=0x%08lx pfBpp=%lu", 
                static_cast<unsigned long>(lpDDSurfaceDesc->dwFlags),
                static_cast<unsigned long>(caps),
                BoolToText(primary),
                BoolToText(offscreenPlain),
                BoolToText(systemMemory),
                static_cast<unsigned long>(lpDDSurfaceDesc->dwWidth),
                static_cast<unsigned long>(lpDDSurfaceDesc->dwHeight),
                static_cast<unsigned long>(lpDDSurfaceDesc->ddpfPixelFormat.dwFlags),
                static_cast<unsigned long>(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount));

        // PARTIAL: DX3 compatibility accepts SYSTEMMEMORY-only offscreen surfaces
        // used by legacy game code during back/mouse surface creation.
        const bool offscreen = offscreenPlain || systemMemory;
        if (primary == offscreen) {
            SDL_Log("free-direct CreateSurface: invalid caps combination (primary=%s offscreen=%s)", BoolToText(primary), BoolToText(offscreen));
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
            SDL_Log("free-direct CreateSurface: out of memory for %dx%d bpp=%d", width, height, bpp);
            return DDERR_OUTOFMEMORY;
        }

        *lplpDDSurface = surface;
        SDL_Log("free-direct CreateSurface result: surfaceId=%llu ptr=%p type=%s size=%dx%d bpp=%d pitch=%ld hasPalette=%s", 
                static_cast<unsigned long long>(surface->GetDebugId()),
                static_cast<void*>(surface),
                primary ? "primary" : "offscreen",
                width,
                height,
                bpp,
                static_cast<long>(surface->GetPitch()),
                BoolToText(surface->HasPalette()));
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
    {
        SDL_Log("free-direct SetDisplayMode: width=%lu height=%lu bpp=%lu", 
                static_cast<unsigned long>(dwWidth),
                static_cast<unsigned long>(dwHeight),
                static_cast<unsigned long>(dwBPP));
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter)
    {
        SDL_Log("free-direct CreatePalette: flags=0x%08lx colorTable=%p out=%p outer=%p", 
                static_cast<unsigned long>(dwFlags),
                static_cast<void*>(lpColorTable),
                static_cast<void*>(lplpDDPalette),
                static_cast<void*>(pUnkOuter));
        if (!lplpDDPalette || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDPalette = new (std::nothrow) DirectDrawPaletteImpl(dwFlags, lpColorTable);
        SDL_Log("free-direct CreatePalette result: palette=%p", static_cast<void*>(*lplpDDPalette));
        return (*lplpDDPalette) ? DD_OK : DDERR_OUTOFMEMORY;
    }

    HRESULT WINAPI DirectDrawImpl::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter)
    {
        (void)dwFlags;
        SDL_Log("free-direct CreateClipper: out=%p outer=%p", static_cast<void*>(lplpDDClipper), static_cast<void*>(pUnkOuter));
        if (!lplpDDClipper || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDClipper = new (std::nothrow) DirectDrawClipperImpl();
        SDL_Log("free-direct CreateClipper result: clipper=%p", static_cast<void*>(*lplpDDClipper));
        return (*lplpDDClipper) ? DD_OK : DDERR_OUTOFMEMORY;
    }

    HRESULT DirectDrawImpl::PresentSurface(DirectDrawSurfaceImpl& source, const RECT* destRect, const RECT* srcRect, float rotationAngle)
    {
        SDL_Log("free-direct PresentSurface begin: srcId=%llu srcType=%s srcSize=%dx%d srcBpp=%d srcPitch=%ld srcHasPalette=%s primaryPresented=%s", 
                static_cast<unsigned long long>(source.GetDebugId()),
                (source.GetType() == DirectDrawSurfaceImpl::SurfaceType::Primary) ? "primary" : "offscreen",
                source.GetWidth(),
                source.GetHeight(),
                source.GetBPP(),
                static_cast<long>(source.GetPitch()),
                BoolToText(source.HasPalette()),
                BoolToText(primaryPresented_));

        if (source.GetBPP() == 32 && !source.GetPixels().empty() && (presentCallCount_ < 8 || !primaryPresented_)) {
            const auto& pixels = source.GetPixels();
            const int pitch = static_cast<int>(source.GetPitch());
            const int centerX = source.GetWidth() / 2;
            const int centerY = source.GetHeight() / 2;
            const size_t topLeftOffset = 0;
            const size_t centerOffset = static_cast<size_t>(centerY) * static_cast<size_t>(pitch) + static_cast<size_t>(centerX) * 4u;
            if (pixels.size() >= centerOffset + 4u) {
                SDL_Log("free-direct PresentSurface pixels: srcId=%llu tl=(%u,%u,%u,%u) center=(%u,%u,%u,%u)",
                        static_cast<unsigned long long>(source.GetDebugId()),
                        static_cast<unsigned>(pixels[topLeftOffset + 0u]),
                        static_cast<unsigned>(pixels[topLeftOffset + 1u]),
                        static_cast<unsigned>(pixels[topLeftOffset + 2u]),
                        static_cast<unsigned>(pixels[topLeftOffset + 3u]),
                        static_cast<unsigned>(pixels[centerOffset + 0u]),
                        static_cast<unsigned>(pixels[centerOffset + 1u]),
                        static_cast<unsigned>(pixels[centerOffset + 2u]),
                        static_cast<unsigned>(pixels[centerOffset + 3u]));
            }
        }

        if (!renderer_ || source.GetType() != DirectDrawSurfaceImpl::SurfaceType::Offscreen) {
            SDL_Log("free-direct PresentSurface: unsupported renderer=%p sourceType=%s", 
                    static_cast<void*>(renderer_),
                    (source.GetType() == DirectDrawSurfaceImpl::SurfaceType::Primary) ? "primary" : "offscreen");
            return DDERR_UNSUPPORTED;
        }

        if (!source.texture_) {
            SDL_Log("free-direct SDL_CreateTexture: srcId=%llu size=%dx%d format=%s access=streaming", 
                    static_cast<unsigned long long>(source.GetDebugId()),
                    source.GetWidth(),
                    source.GetHeight(),
                    SDL_GetPixelFormatName(SDL_PIXELFORMAT_RGBA32));
            source.texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, source.GetWidth(), source.GetHeight());
            if (source.texture_) {
                SDL_SetTextureBlendMode(source.texture_, SDL_BLENDMODE_NONE);
                SDL_Log("free-direct SDL_CreateTexture result: texture=%p blend=none", static_cast<void*>(source.texture_));
            } else {
                SDL_Log("free-direct SDL_CreateTexture failed: %s", SDL_GetError());
            }
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
            SDL_Log("free-direct SDL_UpdateTexture: srcId=%llu mode=8to32 pitch=%d hasPalette=%s", 
                    static_cast<unsigned long long>(source.GetDebugId()),
                    source.GetWidth() * 4,
                    BoolToText(hasPalette));
            SDL_UpdateTexture(source.texture_, NULL, temp.data(), source.GetWidth() * 4);
        } else {
            SDL_Log("free-direct SDL_UpdateTexture: srcId=%llu mode=32bit pitch=%d", 
                    static_cast<unsigned long long>(source.GetDebugId()),
                    source.GetWidth() * 4);
            SDL_UpdateTexture(source.texture_, NULL, source.GetPixels().data(), source.GetWidth() * 4);
        }

        RECT src = srcRect ? ClampRect(*srcRect, source.GetWidth(), source.GetHeight()) : GetFullRect(source.GetWidth(), source.GetHeight());
        if (RectWidth(src) <= 0 || RectHeight(src) <= 0) {
            src = GetFullRect(source.GetWidth(), source.GetHeight());
            SDL_Log("free-direct PresentSurface srcRect fallback: clamped rect collapsed, using full source=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(src.left),
                    static_cast<long>(src.top),
                    static_cast<long>(src.right),
                    static_cast<long>(src.bottom));
        }

        int winW = 0;
        int winH = 0;
        SDL_GetRenderOutputSize(renderer_, &winW, &winH);

        RECT dstInput = destRect ? *destRect : GetFullRect(winW, winH);
        RECT dstForRender = dstInput;

        if (destRect && sdlWindow_) {
            int windowX = 0;
            int windowY = 0;
            SDL_GetWindowPosition(sdlWindow_, &windowX, &windowY);

            const bool outsideOutputBounds = dstInput.left >= winW
                || dstInput.top >= winH
                || dstInput.right > winW
                || dstInput.bottom > winH;

            if (outsideOutputBounds) {
                dstForRender.left -= windowX;
                dstForRender.top -= windowY;
                dstForRender.right -= windowX;
                dstForRender.bottom -= windowY;
                SDL_Log("free-direct PresentSurface dstRect normalize: windowPos=(%d,%d) input=[%ld,%ld,%ld,%ld] normalized=[%ld,%ld,%ld,%ld]", 
                        windowX,
                        windowY,
                        static_cast<long>(dstInput.left),
                        static_cast<long>(dstInput.top),
                        static_cast<long>(dstInput.right),
                        static_cast<long>(dstInput.bottom),
                        static_cast<long>(dstForRender.left),
                        static_cast<long>(dstForRender.top),
                        static_cast<long>(dstForRender.right),
                        static_cast<long>(dstForRender.bottom));
            }
        }

        RECT dst = ClampRect(dstForRender, winW, winH);
        if (RectWidth(dst) <= 0 || RectHeight(dst) <= 0) {
            dst = GetFullRect(winW, winH);
            SDL_Log("free-direct PresentSurface dstRect fallback: clamped rect collapsed, using full renderer output=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(dst.left),
                    static_cast<long>(dst.top),
                    static_cast<long>(dst.right),
                    static_cast<long>(dst.bottom));
        }

        SDL_FRect srect = { (float)src.left, (float)src.top, (float)RectWidth(src), (float)RectHeight(src) };
        SDL_FRect drect = { (float)dst.left, (float)dst.top, (float)RectWidth(dst), (float)RectHeight(dst) };

        SDL_Log("free-direct PresentSurface render: srcRect=[%ld,%ld,%ld,%ld] dstRect=[%ld,%ld,%ld,%ld] angle=%.2f", 
                static_cast<long>(src.left),
                static_cast<long>(src.top),
                static_cast<long>(src.right),
                static_cast<long>(src.bottom),
                static_cast<long>(dst.left),
                static_cast<long>(dst.top),
                static_cast<long>(dst.right),
                static_cast<long>(dst.bottom),
                static_cast<double>(rotationAngle));

        if (rotationAngle != 0.0f) {
            SDL_RenderTextureRotated(renderer_, source.texture_, &srect, &drect, (double)rotationAngle, NULL, SDL_FLIP_NONE);
        } else {
            SDL_RenderTexture(renderer_, source.texture_, &srect, &drect);
        }

        const bool firstPrimaryPresent = !primaryPresented_;
        SDL_RenderPresent(renderer_);
        presentCallCount_++;
        primaryPresented_ = true;
        SDL_Log("free-direct PresentSurface implicit present: srcId=%llu first=%s primaryPresented=%s presentCalls=%llu", 
                static_cast<unsigned long long>(source.GetDebugId()),
                BoolToText(firstPrimaryPresent),
                BoolToText(primaryPresented_),
                static_cast<unsigned long long>(presentCallCount_));

        return DD_OK;
    }
}

HRESULT WINAPI DirectDrawCreate(const GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter)
{
    SDL_Log("free-direct DirectDrawCreate: guid=%p out=%p outer=%p", 
            static_cast<const void*>(lpGUID),
            static_cast<void*>(lplpDD),
            static_cast<void*>(pUnkOuter));
    (void)lpGUID;

    if (!lplpDD || pUnkOuter) {
        SDL_Log("free-direct DirectDrawCreate: invalid params");
        return DDERR_INVALIDPARAMS;
    }

    auto* directDraw = new (std::nothrow) DirectDrawImpl();
    if (!directDraw) {
        SDL_Log("free-direct DirectDrawCreate: out of memory");
        return DDERR_OUTOFMEMORY;
    }

    *lplpDD = directDraw;
    SDL_Log("free-direct DirectDrawCreate result: dd=%p", static_cast<void*>(directDraw));
    return DD_OK;
}