/**
 * @file DirectDrawInternal.hpp
 * @brief Internal DirectDraw shared state: debug logging helpers, rect helpers, and the
 *        `DirectDrawSurfaceImpl`/`DirectDrawImpl` class declarations.
 *
 * This header is intentionally private to the DirectDraw implementation: it must never be
 * included from `include/ddraw.h` and must never be installed. `DirectDrawSurfaceImpl` and
 * `DirectDrawImpl` are mutually `friend`ed (Surface holds a raw, non-owning `DirectDrawImpl*
 * owner_` back-pointer; DirectDrawImpl tracks every live surface it created in
 * `liveSurfaces_` so `SetCooperativeLevel` can invalidate their cached textures), so both must
 * be declared together in one header rather than split further. Method bodies live in
 * `DirectDrawSurface.cpp` (DirectDrawSurfaceImpl) and `DirectDraw.cpp` (DirectDrawImpl +
 * the public `DirectDrawCreate` entry point) — mirrors the split already used by
 * `src/directplay/` (see e.g. `DirectPlaySession.hpp`).
 *
 * Every test exercises these classes only through the real public `IDirectDraw*` interfaces
 * (no whitebox testing) — this header must never be included by a test file.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <ddraw.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <vector>

namespace free_direct_directdraw {

    inline bool IsEnvFlagEnabled(const char* envName)
    {
        const char* env = SDL_getenv(envName);
        if (!env) {
            return false;
        }

        return SDL_strcasecmp(env, "1") == 0
            || SDL_strcasecmp(env, "true") == 0
            || SDL_strcasecmp(env, "yes") == 0
            || SDL_strcasecmp(env, "on") == 0;
    }

    // Each flag's runtime env-var check remains the primary/default mechanism; the #ifdef is an
    // additive CMake-level force-on path (FREE_DIRECT_FORCE_DEBUG_* options, TASK-24H-0119) for
    // cases like a CI diagnostic build where forcing a flag at compile time beats setting an env
    // var at every invocation. Mirrors the pattern already used by DirectSound.cpp's dsDebugEnabled().
    inline bool IsDirectDrawDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_DDRAW
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_DDRAW");
#endif
    }

    inline bool IsPresentationDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_PRESENTATION
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_PRESENTATION");
#endif
    }

    inline bool IsColorKeyDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_COLORKEY
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_COLORKEY");
#endif
    }

    inline void DirectDrawLog(const char* format, ...)
    {
        if (!IsDirectDrawDebugEnabled()) {
            return;
        }

        va_list args;
        va_start(args, format);
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
        va_end(args);
    }

    /** @brief Presentation-specific debug log, controlled by FREE_DIRECT_DEBUG_PRESENTATION env var. */
    inline void PresentLog(const char* format, ...)
    {
        if (!IsPresentationDebugEnabled()) {
            return;
        }

        va_list args;
        va_start(args, format);
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
        va_end(args);
    }

    /** @brief Color-key specific debug log, controlled by FREE_DIRECT_DEBUG_COLORKEY env var. */
    inline void ColorKeyLog(const char* format, ...)
    {
        if (!IsColorKeyDebugEnabled()) {
            return;
        }

        va_list args;
        va_start(args, format);
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
        va_end(args);
    }

    inline void GetDefault332Palette(PALETTEENTRY entries[256])
    {
        for (int i = 0; i < 256; ++i) {
            entries[i].peRed   = static_cast<BYTE>(((i >> 5) & 0x07) * 255 / 7);
            entries[i].peGreen = static_cast<BYTE>(((i >> 2) & 0x07) * 255 / 7);
            entries[i].peBlue  = static_cast<BYTE>(((i >> 0) & 0x03) * 255 / 3);
            entries[i].peFlags = 0;
        }
    }

    inline const char* BoolToText(const bool value)
    {
        return value ? "yes" : "no";
    }

    inline bool IsPerfDebugEnabled()
        {
            static int v = -1;
            if (v < 0) {
#ifdef FREE_DIRECT_DEBUG_PERF
                v = 1;
#else
                v = IsEnvFlagEnabled("FREE_DIRECT_DEBUG_PERF") ? 1 : 0;
#endif
            }
            return v != 0;
        }

        inline void PerfLog(const char* format, ...)
        {
            if (!IsPerfDebugEnabled()) return;
            va_list args;
            va_start(args, format);
            SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
            va_end(args);
        }

        inline bool IsDebugPrimaryClearEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_PRIMARY_CLEAR
        return true;
#else
        const char* env = SDL_getenv("FREE_DIRECT_DEBUG_PRIMARY_CLEAR");
        if (!env) {
            return false;
        }

        return SDL_strcasecmp(env, "1") == 0
            || SDL_strcasecmp(env, "true") == 0
            || SDL_strcasecmp(env, "yes") == 0
            || SDL_strcasecmp(env, "on") == 0;
#endif
    }

class DirectDrawImpl;
class DirectDrawSurfaceImpl;

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
        HRESULT BlitFrom(const DirectDrawSurfaceImpl& source,
                        const RECT* destRect,
                        const RECT* srcRect,
                        bool useSrcColorKey,
                        uint64_t* copiedPixelCount = nullptr,
                        uint64_t* skippedPixelCount = nullptr);

        /** @brief Mark this surface as dirty (content changed, needs re-upload for presentation). */
        void MarkDirty() { dirty_ = true; }
        /** @brief Check and clear dirty flag. Returns true if surface was dirty. */
        bool ConsumeAndClearDirty() { bool was = dirty_; dirty_ = false; return was; }

    private:
        friend class DirectDrawImpl;
        std::atomic<ULONG> refCount_;
        /// The DirectDrawImpl that created this surface via CreateSurface(), or null (test-only
        /// construction paths). Raw, non-owning back-pointer: this surface does not extend
        /// owner_'s lifetime, and must not be dereferenced once owner_ has been destroyed.
        /// Lifetime contract (docs/audit_ddraw.md §4.1, F11, TASK-24H-0161): a real DirectDraw
        /// application (and both target games) always releases every surface it created before
        /// releasing the IDirectDraw object itself, so this ordering is never actually violated
        /// in practice - but it is not independently enforced by this code either. Used by the
        /// constructor/destructor to register/unregister this surface in owner_->liveSurfaces_
        /// (TASK-24H-0156), so SetCooperativeLevel can invalidate this surface's texture_ if it
        /// ever replaces owner_'s renderer_.
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
        bool dirty_ = false;
        uint64_t debugId_ = 0;
        HDC attachedDc_ = nullptr;
        /// Temporary 32-bit RGBA buffer used by GetDC/ReleaseDC for 8-bit surfaces.
        std::vector<uint8_t> dcTempBuffer_;
        /// Cached RGBA32 conversion buffer for 8-bit palette surfaces — reused every frame to avoid per-frame heap allocation.
        std::vector<uint32_t> paletteConvertBuffer_;
        /// SDL_Palette attached to texture_ when FREE_DIRECT_ENABLE_INDEXED_TEXTURES is on and
        /// texture_ was created as SDL_PIXELFORMAT_INDEX8 (null otherwise). Owned 1:1 with
        /// texture_: created/destroyed alongside it (see PresentPrimary and SetCooperativeLevel).
        SDL_Palette* texturePalette_ = nullptr;
        size_t diagPixelCapacityBytes_ = 0;
        size_t diagDcTempCapacityBytes_ = 0;
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

        /**
         * @brief Upload the primary surface CPU pixel buffer to an SDL_Texture and present it.
         *
         * This is the single presentation path. Called from Flip (or end-of-frame fallback).
         * The primary surface's CPU pixel buffer is uploaded to a streaming SDL_Texture,
         * rendered via SDL_RenderTexture, and then SDL_RenderPresent is called exactly once.
         *
         * @note Status: IMPLEMENTED
         */
        HRESULT PresentPrimary(DirectDrawSurfaceImpl& primary);

    private:
        friend class DirectDrawSurfaceImpl;
        std::atomic<ULONG> refCount_;
        HWND hwnd_;
        SDL_Window* sdlWindow_;
        SDL_Renderer* renderer_;
        /// Every surface this instance has created, registered/unregistered by
        /// DirectDrawSurfaceImpl's own constructor/destructor via the existing mutual friend
        /// relationship (docs/audit_ddraw.md §4.1/§4.6, F11/F6, TASK-24H-0156/0161) - lets
        /// SetCooperativeLevel invalidate every live surface's cached texture_ when it replaces
        /// renderer_, instead of leaving them dangling against a destroyed renderer. Raw,
        /// non-owning pointers: DirectDrawImpl does not own surface lifetime (callers do, via
        /// AddRef/Release), it only needs to reach already-live surfaces while they exist.
        std::vector<DirectDrawSurfaceImpl*> liveSurfaces_;
        bool primaryPresented_;
        uint64_t presentCallCount_;
        bool debugPrimaryClearDone_;
        bool debugPrimaryClearEnabled_;
        /// True once Flip has been called at least once; disables auto-present from Blt/BltFast.
        bool usesFlip_ = false;
        /// True once SDL_SetRenderLogicalPresentation has been configured for the primary surface size.
        bool logicalPresentationSet_ = false;
        /// Timestamp of the last successful SDL_RenderPresent (nanoseconds from SDL_GetTicksNS).
        uint64_t lastPresentNs_ = 0;
        /// Minimum nanoseconds between presents (default = 1s/60 ≈ 16.67 ms).
        uint64_t presentIntervalNs_ = 1000000000ULL / 60ULL;
        /// Display mode requested by SetDisplayMode (game logical resolution).
        int displayModeWidth_ = 0;
        int displayModeHeight_ = 0;
        /// Perf counters (per-second summary when FREE_DIRECT_DEBUG_PERF=1).
        uint64_t perfWindowStart_ = 0;
        uint64_t perfBltCalls_ = 0;
        uint64_t perfPresentAttempts_ = 0;
        uint64_t perfPresentThrottled_ = 0;
        uint64_t perfTextureUploads_ = 0;
    };

    inline RECT GetFullRect(const int width, const int height)
    {
        RECT rect{};
        rect.left = 0;
        rect.top = 0;
        rect.right = width;
        rect.bottom = height;
        return rect;
    }

    inline RECT ClampRect(const RECT& input, const int width, const int height)
    {
        RECT rect{};
        rect.left = std::clamp(input.left, static_cast<LONG>(0), static_cast<LONG>(width));
        rect.top = std::clamp(input.top, static_cast<LONG>(0), static_cast<LONG>(height));
        rect.right = std::clamp(input.right, rect.left, static_cast<LONG>(width));
        rect.bottom = std::clamp(input.bottom, rect.top, static_cast<LONG>(height));
        return rect;
    }

    inline int RectWidth(const RECT& rect)
    {
        return static_cast<int>(rect.right - rect.left);
    }

    inline int RectHeight(const RECT& rect)
    {
        return static_cast<int>(rect.bottom - rect.top);
    }

} // namespace free_direct_directdraw
