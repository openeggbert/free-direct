/**
 * @file DirectDraw.cpp
 * @brief SDL3-based internal implementation of DirectDraw subset.
 * @note Status: IMPLEMENTED (Minimal backend mapping)
 */
#include <ddraw.h>

#include <free_api_bridge.h>
#include <SDL3/SDL.h>
#include "../diagnostics/Diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {
    bool IsEnvFlagEnabled(const char* envName)
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
    bool IsDirectDrawDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_DDRAW
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_DDRAW");
#endif
    }

    bool IsPresentationDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_PRESENTATION
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_PRESENTATION");
#endif
    }

    bool IsColorKeyDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_COLORKEY
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_COLORKEY");
#endif
    }

    void DirectDrawLog(const char* format, ...)
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
    void PresentLog(const char* format, ...)
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
    void ColorKeyLog(const char* format, ...)
    {
        if (!IsColorKeyDebugEnabled()) {
            return;
        }

        va_list args;
        va_start(args, format);
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
        va_end(args);
    }

    void GetDefault332Palette(PALETTEENTRY entries[256])
    {
        for (int i = 0; i < 256; ++i) {
            entries[i].peRed   = static_cast<BYTE>(((i >> 5) & 0x07) * 255 / 7);
            entries[i].peGreen = static_cast<BYTE>(((i >> 2) & 0x07) * 255 / 7);
            entries[i].peBlue  = static_cast<BYTE>(((i >> 0) & 0x03) * 255 / 3);
            entries[i].peFlags = 0;
        }
    }

#define SDL_Log DirectDrawLog

    const char* BoolToText(const bool value)
    {
        return value ? "yes" : "no";
    }

    bool IsPerfDebugEnabled()
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

        void PerfLog(const char* format, ...)
        {
            if (!IsPerfDebugEnabled()) return;
            va_list args;
            va_start(args, format);
            SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
            va_end(args);
        }

        bool IsDebugPrimaryClearEnabled()
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

    std::atomic<uint64_t> g_nextSurfaceId{1};

    class DirectDrawImpl;
    class DirectDrawSurfaceImpl;

    class DirectDrawPaletteImpl final : public IDirectDrawPalette {
    public:
        DirectDrawPaletteImpl(DWORD dwFlags, LPPALETTEENTRY lpColorTable) : refCount_(1), flags_(dwFlags) {
            FREE_DIRECT_DIAG_INC(ddPalettes);
            FREE_DIRECT_DIAG_INC_EVER(ddPalettesEver, "pal");
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

        ~DirectDrawPaletteImpl() override
        {
            FREE_DIRECT_DIAG_DEC(ddPalettes);
            FREE_DIRECT_DIAG_INC_TOTAL(ddPalettesDestroyed);
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
            // dwBase + dwNumEntries > 256 (the original check) can wrap in DWORD arithmetic -
            // e.g. dwBase=0xFFFFFFFF, dwNumEntries=2 wraps to 1, which passes that check and
            // then indexes entries_[0xFFFFFFFF], an out-of-bounds read (docs/audit_ddraw.md
            // §5.2, F5, TASK-24H-0155). Rewritten to never add two DWORDs that could overflow:
            // dwBase > 256 short-circuits before 256 - dwBase could underflow.
            if (!lpEntries || dwBase > 256 || dwNumEntries > 256 - dwBase) return DDERR_INVALIDPARAMS;
            for (DWORD i = 0; i < dwNumEntries; ++i) {
                lpEntries[i] = entries_[dwBase + i];
            }
            return DD_OK;
        }

        HRESULT WINAPI SetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) override {
            (void)dwFlags;
            // See GetEntries' comment above - same overflow-safe rewrite. SetEntries' version of
            // this bug is the more serious one: the bypassed check would have led to an
            // out-of-bounds *write* into entries_.
            if (!lpEntries || dwBase > 256 || dwNumEntries > 256 - dwBase) return DDERR_INVALIDPARAMS;
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
        DirectDrawClipperImpl() : refCount_(1), hwnd_(NULL)
        {
            FREE_DIRECT_DIAG_INC(ddClippers);
            FREE_DIRECT_DIAG_INC_EVER(ddClippersEver, "clip");
        }
        ~DirectDrawClipperImpl() override
        {
            FREE_DIRECT_DIAG_DEC(ddClippers);
            FREE_DIRECT_DIAG_INC_TOTAL(ddClippersDestroyed);
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
        FREE_DIRECT_DIAG_INC(ddSurfaces);
        FREE_DIRECT_DIAG_INC_EVER(ddSurfacesEver, "surf");
        // Both primary and offscreen surfaces own CPU pixel buffers.
        // Primary surface pixels are uploaded to SDL_Texture during presentation (Flip).
        pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * (bpp_ / 8u), 0);
        diagPixelCapacityBytes_ = pixels_.capacity();
        FREE_DIRECT_DIAG_ADD_BYTES(ddSurfacePixelCapacityBytes,
                                   ddSurfacePixelCapacityHighWaterBytes,
                                   static_cast<int64_t>(diagPixelCapacityBytes_));
        // Registers with owner_ so SetCooperativeLevel can find and invalidate this surface's
        // texture_ if it ever replaces renderer_ (docs/audit_ddraw.md §4.6, F6,
        // TASK-24H-0156) - owner_ is otherwise never read, only stored (§4.1, F11). Deliberately
        // placed after pixels_.resize() above, not before: nothing between here and the end of
        // the constructor can throw, so a partially-constructed surface can never end up
        // registered with no matching destructor call to unregister it.
        if (owner_) owner_->liveSurfaces_.push_back(this);

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

        // Unregisters from owner_'s liveSurfaces_ (TASK-24H-0156/0161) - must happen before this
        // object's memory is freed, so SetCooperativeLevel never iterates a dangling pointer.
        if (owner_) {
            auto& live = owner_->liveSurfaces_;
            live.erase(std::remove(live.begin(), live.end(), this), live.end());
        }

        if (attachedDc_) {
            FreeApiDestroySurfaceDC(attachedDc_);
            attachedDc_ = nullptr;
        }

        if (texture_) {
            SDL_DestroyTexture(texture_);
            FREE_DIRECT_DIAG_DEC(sdlTextures);
            FREE_DIRECT_DIAG_INC_TOTAL(sdlTexturesDestroyed);
        }
        if (palette_) palette_->Release();
        if (clipper_) clipper_->Release();
        FREE_DIRECT_DIAG_ADD_BYTES(ddSurfacePixelCapacityBytes,
                                   ddSurfacePixelCapacityHighWaterBytes,
                                   -static_cast<int64_t>(diagPixelCapacityBytes_));
        FREE_DIRECT_DIAG_ADD_BYTES(ddSurfaceDcTempCapacityBytes,
                                   ddSurfaceDcTempCapacityHighWaterBytes,
                                   -static_cast<int64_t>(diagDcTempCapacityBytes_));
        FREE_DIRECT_DIAG_DEC(ddSurfaces);
        FREE_DIRECT_DIAG_INC_TOTAL(ddSurfaceFinalReleases);
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

    /**
     * @brief Fill a rectangle on this surface with a solid color.
     *
     * Works on both primary and offscreen surfaces. Writes directly to the CPU pixel buffer.
     * Marks the surface dirty if it is the primary surface.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT DirectDrawSurfaceImpl::FillColor(const RECT* destRect, const DWORD fillColor)
    {

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

        if (type_ == SurfaceType::Primary) {
            MarkDirty();
        }
        return DD_OK;
    }

    /**
     * @brief Copy pixels from a source surface to this surface (CPU-to-CPU blit).
     *
     * Works on both primary and offscreen destination surfaces.
     * Handles 8-bit and 32-bit pixel formats, optional source color key, and scaling.
     * Marks the destination surface dirty if it is the primary surface.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT DirectDrawSurfaceImpl::BlitFrom(const DirectDrawSurfaceImpl& source,
                                            const RECT* destRect,
                                            const RECT* srcRect,
                                            const bool useSrcColorKey,
                                            uint64_t* copiedPixelCount,
                                            uint64_t* skippedPixelCount)
    {

        const RECT sourceClamped = ClampRect(srcRect ? *srcRect : GetFullRect(source.GetWidth(), source.GetHeight()), source.GetWidth(), source.GetHeight());
        const RECT destClamped = ClampRect(destRect ? *destRect : GetFullRect(width_, height_), width_, height_);

        const int srcWidth = RectWidth(sourceClamped);
        const int srcHeight = RectHeight(sourceClamped);
        const int dstWidth = RectWidth(destClamped);
        const int dstHeight = RectHeight(destClamped);
        if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
            return DD_OK;
        }

        uint64_t copied = 0;
        uint64_t skipped = 0;

        // Fast path: an unscaled (1:1) copy with no active source color key is done as a
        // straight per-row memcpy instead of the general per-pixel loop below - measured ~29x
        // faster than the per-pixel path for a 640x480 copy at -O3, ~206x at this project's
        // unoptimized default (docs/audit_ddraw.md §3.2/§7, TASK-24H-0152). Must stay a runtime
        // size check, not an assumption based on which method (Blt/BltFast) called this -
        // CPixmap::Display()'s primary-present Blt can genuinely scale when the window size
        // doesn't match the game's logical resolution (docs/audit_ddraw.md §8.3), and must keep
        // taking the general scaling path below in that case.
        const bool isUnscaled = (srcWidth == dstWidth) && (srcHeight == dstHeight);
        const bool colorKeyActive = useSrcColorKey && source.hasSrcColorKey_;
        if (isUnscaled && !colorKeyActive && bpp_ == source.GetBPP() && (bpp_ == 8 || bpp_ == 32)) {
            const size_t bytesPerPixel = static_cast<size_t>(bpp_ / 8);
            const size_t rowBytes = static_cast<size_t>(dstWidth) * bytesPerPixel;
            for (int y = 0; y < dstHeight; ++y) {
                const size_t srcRowBase = (static_cast<size_t>(sourceClamped.top + y) * static_cast<size_t>(source.GetWidth()) + static_cast<size_t>(sourceClamped.left)) * bytesPerPixel;
                const size_t dstRowBase = (static_cast<size_t>(destClamped.top + y) * static_cast<size_t>(width_) + static_cast<size_t>(destClamped.left)) * bytesPerPixel;
                std::memcpy(pixels_.data() + dstRowBase, source.GetPixels().data() + srcRowBase, rowBytes);
                if (bpp_ == 32) {
                    // Force alpha opaque, matching the general path's per-pixel behavior below
                    // (DirectDraw blits are opaque by default) - the memcpy above copied the
                    // source's real, possibly-undefined alpha bytes verbatim, so fix them up.
                    for (int x = 0; x < dstWidth; ++x) {
                        pixels_[dstRowBase + static_cast<size_t>(x) * 4u + 3u] = 255;
                    }
                }
            }
            copied = static_cast<uint64_t>(dstWidth) * static_cast<uint64_t>(dstHeight);

            if (copiedPixelCount) {
                *copiedPixelCount = copied;
            }
            if (skippedPixelCount) {
                *skippedPixelCount = skipped;
            }
            if (type_ == SurfaceType::Primary) {
                MarkDirty();
            }
            return DD_OK;
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
                    if (useSrcColorKey && source.hasSrcColorKey_) {
                        const auto srcKeyLow = static_cast<uint8_t>(source.colorKey_.dwColorSpaceLowValue & 0xFFu);
                        const auto srcKeyHigh = static_cast<uint8_t>(source.colorKey_.dwColorSpaceHighValue & 0xFFu);
                        if (index >= srcKeyLow && index <= srcKeyHigh) {
                            ++skipped;
                            continue;
                        }
                    }
                    pixels_[dstOffset] = index;
                    ++copied;
                    continue;
                }

                if (bpp_ == 32 && source.GetBPP() == 32) {
                    const size_t srcOffset = (static_cast<size_t>(srcY) * static_cast<size_t>(source.GetWidth()) + static_cast<size_t>(srcX)) * 4u;
                    const size_t dstOffset = (static_cast<size_t>(dstY) * static_cast<size_t>(width_) + static_cast<size_t>(dstX)) * 4u;

                    const uint8_t r = source.GetPixels()[srcOffset + 0u];
                    const uint8_t g = source.GetPixels()[srcOffset + 1u];
                    const uint8_t b = source.GetPixels()[srcOffset + 2u];
                    if (useSrcColorKey && source.hasSrcColorKey_) {
                        const uint32_t pixelRaw = static_cast<uint32_t>(source.GetPixels()[srcOffset + 0u])
                            | (static_cast<uint32_t>(source.GetPixels()[srcOffset + 1u]) << 8u)
                            | (static_cast<uint32_t>(source.GetPixels()[srcOffset + 2u]) << 16u)
                            | (static_cast<uint32_t>(source.GetPixels()[srcOffset + 3u]) << 24u);
                        const uint32_t srcKeyLowRaw = source.colorKey_.dwColorSpaceLowValue;
                        const uint32_t srcKeyHighRaw = source.colorKey_.dwColorSpaceHighValue;
                        const bool rawRangeMatch = pixelRaw >= srcKeyLowRaw && pixelRaw <= srcKeyHighRaw;

                        const uint32_t pixelRgb = pixelRaw & 0x00FFFFFFu;
                        const uint32_t srcKeyLowRgb = srcKeyLowRaw & 0x00FFFFFFu;
                        const uint32_t srcKeyHighRgb = srcKeyHighRaw & 0x00FFFFFFu;
                        const bool rgbRangeMatch = pixelRgb >= srcKeyLowRgb && pixelRgb <= srcKeyHighRgb;
                        if (rawRangeMatch || rgbRangeMatch) {
                            ++skipped;
                            continue;
                        }
                    }

                    pixels_[dstOffset + 0u] = r;
                    pixels_[dstOffset + 1u] = g;
                    pixels_[dstOffset + 2u] = b;
                    // PARTIAL: DirectDraw blits are opaque by default; avoid transparent
                    // desktop-window output when legacy assets have undefined alpha bytes.
                    pixels_[dstOffset + 3u] = 255;
                    ++copied;
                }
            }
        }

        if (copiedPixelCount) {
            *copiedPixelCount = copied;
        }
        if (skippedPixelCount) {
            *skippedPixelCount = skipped;
        }

        if (type_ == SurfaceType::Primary) {
            MarkDirty();
        }
        return DD_OK;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::Blt(LPRECT lpDestRect,
                                              LPDIRECTDRAWSURFACE lpDDSrcSurface,
                                              LPRECT lpSrcRect,
                                              DWORD dwFlags,
                                              LPDDBLTFX lpDDBltFx)
    {
        FREE_DIRECT_DIAG_INC(bltCallsThisWindow);
        FREE_DIRECT_DIAG_INC_TOTAL(bltCallsTotal);
        auto* sourceSurface = dynamic_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        const bool requestSrcColorKey = (dwFlags & DDBLT_KEYSRC) != 0;
        SDL_Log("free-direct Blt: dstId=%llu dstType=%s srcId=%llu src=%p flags=0x%08lx hasPalette=%s hasSrcColorKey=%s", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                sourceSurface ? static_cast<unsigned long long>(sourceSurface->GetDebugId()) : 0ULL,
                static_cast<void*>(lpDDSrcSurface),
                static_cast<unsigned long>(dwFlags),
                BoolToText(HasPalette()),
                BoolToText(hasSrcColorKey_));
        ColorKeyLog("free-direct Blt colorkey: dstId=%llu srcId=%llu flags=0x%08lx useKeysrc=%s srcHasColorKey=%s",
                    static_cast<unsigned long long>(debugId_),
                    sourceSurface ? static_cast<unsigned long long>(sourceSurface->GetDebugId()) : 0ULL,
                    static_cast<unsigned long>(dwFlags),
                    BoolToText(requestSrcColorKey),
                    BoolToText(sourceSurface && sourceSurface->hasSrcColorKey_));

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

        // In windowed mode the game converts client coords to screen coords
        // before blitting to the primary surface.  On real DirectDraw the
        // primary surface represents the whole desktop, but here it is only
        // window-sized.  Translate the dest rect back to client-local coords
        // by subtracting the window position so the pixels land inside the
        // surface buffer.
        RECT adjustedDestRect{};
        LPRECT effectiveDestRect = lpDestRect;
        if (lpDestRect && type_ == SurfaceType::Primary && clipper_ && owner_ && owner_->sdlWindow_) {
            int winX = 0, winY = 0;
            SDL_GetWindowPosition(owner_->sdlWindow_, &winX, &winY);
            adjustedDestRect.left   = lpDestRect->left   - static_cast<LONG>(winX);
            adjustedDestRect.top    = lpDestRect->top    - static_cast<LONG>(winY);
            adjustedDestRect.right  = lpDestRect->right  - static_cast<LONG>(winX);
            adjustedDestRect.bottom = lpDestRect->bottom - static_cast<LONG>(winY);
            effectiveDestRect = &adjustedDestRect;
            SDL_Log("free-direct Blt: adjusted dstRect from screen [%ld,%ld,%ld,%ld] to client [%ld,%ld,%ld,%ld] (winPos=%d,%d)",
                    static_cast<long>(lpDestRect->left), static_cast<long>(lpDestRect->top),
                    static_cast<long>(lpDestRect->right), static_cast<long>(lpDestRect->bottom),
                    static_cast<long>(adjustedDestRect.left), static_cast<long>(adjustedDestRect.top),
                    static_cast<long>(adjustedDestRect.right), static_cast<long>(adjustedDestRect.bottom),
                    winX, winY);
        }

        // All blits go through CPU pixel buffer. When the destination is the
        // primary surface, present immediately so games that never call Flip
        // (e.g. Speedy Blupi) still get visible output.
        if ((dwFlags & DDBLT_COLORFILL) != 0) {
            if (!lpDDBltFx) {
                SDL_Log("free-direct Blt: COLORFILL requested without DDBLTFX");
                return DDERR_INVALIDPARAMS;
            }
            PresentLog("free-direct Blt COLORFILL: dstId=%llu type=%s color=0x%08lx",
                    static_cast<unsigned long long>(debugId_),
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<unsigned long>(lpDDBltFx->dwFillColor));
            const HRESULT hr = FillColor(effectiveDestRect, lpDDBltFx->dwFillColor);
            if (SUCCEEDED(hr) && type_ == SurfaceType::Primary && owner_ && owner_->renderer_ && !owner_->usesFlip_) {
                owner_->PresentPrimary(*this);
            }
            return hr;
        }

        if (lpDDSrcSurface) {
            if (!sourceSurface) {
                SDL_Log("free-direct Blt: source surface type mismatch");
                return DDERR_INVALIDPARAMS;
            }
            uint64_t copiedPixels = 0;
            uint64_t skippedPixels = 0;
            const HRESULT hr = BlitFrom(*sourceSurface,
                                        effectiveDestRect,
                                        lpSrcRect,
                                        requestSrcColorKey,
                                        &copiedPixels,
                                        &skippedPixels);
            ColorKeyLog("free-direct Blt colorkey result: dstId=%llu srcId=%llu copied=%llu skipped=%llu",
                        static_cast<unsigned long long>(debugId_),
                        static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                        static_cast<unsigned long long>(copiedPixels),
                        static_cast<unsigned long long>(skippedPixels));
            PresentLog("free-direct Blt: dstId=%llu srcId=%llu type=%s hr=0x%08lx",
                    static_cast<unsigned long long>(debugId_),
                    static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<unsigned long>(hr));
            if (SUCCEEDED(hr) && type_ == SurfaceType::Primary && owner_ && owner_->renderer_ && !owner_->usesFlip_) {
                if (owner_) owner_->perfBltCalls_++;
                owner_->PresentPrimary(*this);
            }
            return hr;
        }

        SDL_Log("free-direct Blt: unsupported flag combination 0x%08lx", static_cast<unsigned long>(dwFlags));
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
    {
        FREE_DIRECT_DIAG_INC(bltCallsThisWindow);
        FREE_DIRECT_DIAG_INC_TOTAL(bltFastCallsTotal);
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

        const bool useKey = (dwTrans & DDBLTFAST_SRCCOLORKEY) != 0;
        SDL_Log("free-direct BltFast: dstId=%llu srcId=%llu xy=(%lu,%lu) trans=0x%08lx useSrcColorKey=%s", 
                static_cast<unsigned long long>(debugId_),
                static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                static_cast<unsigned long>(dwX),
                static_cast<unsigned long>(dwY),
                static_cast<unsigned long>(dwTrans),
                BoolToText(useKey));
        ColorKeyLog("free-direct BltFast colorkey: dstId=%llu srcId=%llu trans=0x%08lx useSrcColorKey=%s srcHasColorKey=%s",
                    static_cast<unsigned long long>(debugId_),
                    static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                    static_cast<unsigned long>(dwTrans),
                    BoolToText(useKey),
                    BoolToText(sourceSurface->hasSrcColorKey_));

        uint64_t copiedPixels = 0;
        uint64_t skippedPixels = 0;
        const HRESULT hr = BlitFrom(*sourceSurface, &destRect, lpSrcRect, useKey, &copiedPixels, &skippedPixels);
        ColorKeyLog("free-direct BltFast colorkey result: dstId=%llu srcId=%llu copied=%llu skipped=%llu",
                    static_cast<unsigned long long>(debugId_),
                    static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                    static_cast<unsigned long long>(copiedPixels),
                    static_cast<unsigned long long>(skippedPixels));
        SDL_Log("free-direct BltFast result: dstId=%llu srcId=%llu hr=0x%08lx", 
                static_cast<unsigned long long>(debugId_),
                static_cast<unsigned long long>(sourceSurface->GetDebugId()),
                static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr) && type_ == SurfaceType::Primary && owner_ && owner_->renderer_ && !owner_->usesFlip_) {
            owner_->perfBltCalls_++;
            owner_->PresentPrimary(*this);
        }
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

    /** @note Status: STUB - Surfaces are currently kept resident in this backend. */
    HRESULT WINAPI DirectDrawSurfaceImpl::IsLost()
    {
        return DD_OK;
    }

    /** @note Status: STUB - Returns success because no real lost-surface recovery is required yet. */
    HRESULT WINAPI DirectDrawSurfaceImpl::Restore()
    {
        return DD_OK;
    }

    /**
     * @brief Provides a compatibility DC for surface pixel access via GDI functions.
     *
     * For 32-bit surfaces the DC points directly to the pixel buffer.
     * For 8-bit paletted surfaces a temporary 32-bit RGBA buffer is allocated,
     * palette-expanded from the native 8-bit pixels, and the DC is created over
     * that temp buffer.  ReleaseDC converts the temp buffer back to palette indices.
     * This is needed so that DDColorMatch (SetPixel/GetPixel -> Lock -> read back)
     * works correctly for palette-index color key matching.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI DirectDrawSurfaceImpl::GetDC(HDC* lphDC)
    {
        if (!lphDC) {
            return DDERR_INVALIDPARAMS;
        }

        if (pixels_.empty()) {
            *lphDC = nullptr;
            SDL_Log("free-direct GetDC: no pixel buffer surfaceId=%llu", static_cast<unsigned long long>(debugId_));
            return DDERR_UNSUPPORTED;
        }

        if (!attachedDc_) {
            if (bpp_ == 8) {
                // Expand 8-bit palette indices to a temporary 32-bit RGBA buffer.
                const size_t pixelCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
                const size_t oldCapacity = dcTempBuffer_.capacity();
                dcTempBuffer_.resize(pixelCount * 4u);
                if (dcTempBuffer_.capacity() != oldCapacity) {
                    const auto delta = static_cast<int64_t>(dcTempBuffer_.capacity() - oldCapacity);
                    diagDcTempCapacityBytes_ = dcTempBuffer_.capacity();
                    FREE_DIRECT_DIAG_ADD_BYTES(ddSurfaceDcTempCapacityBytes,
                                               ddSurfaceDcTempCapacityHighWaterBytes,
                                               delta);
                }
                PALETTEENTRY entries[256] = {};
                if (palette_) {
                    palette_->GetEntries(0, 0, 256, entries);
                } else {
                    GetDefault332Palette(entries);
                }
                for (size_t i = 0; i < pixelCount; ++i) {
                    const uint8_t idx = pixels_[i];
                    dcTempBuffer_[i * 4u + 0u] = entries[idx].peRed;
                    dcTempBuffer_[i * 4u + 1u] = entries[idx].peGreen;
                    dcTempBuffer_[i * 4u + 2u] = entries[idx].peBlue;
                    dcTempBuffer_[i * 4u + 3u] = 255;
                }
                attachedDc_ = FreeApiCreateSurfaceDC(dcTempBuffer_.data(), width_, height_, width_ * 4, 32);
            } else {
                attachedDc_ = FreeApiCreateSurfaceDC(pixels_.data(), width_, height_, static_cast<int>(GetPitch()), bpp_);
            }
            if (!attachedDc_) {
                *lphDC = nullptr;
                dcTempBuffer_.clear();
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

    /**
     * @brief Release the DC obtained from GetDC.
     *
     * For 8-bit paletted surfaces the temporary 32-bit RGBA buffer is converted
     * back to palette indices using nearest-match, then the temp buffer is freed.
     * The DC is destroyed so a fresh one is created on the next GetDC call.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI DirectDrawSurfaceImpl::ReleaseDC(HDC hDC)
    {
        if (hDC != attachedDc_) {
            SDL_Log("free-direct ReleaseDC: unexpected dc=%p for surfaceId=%llu expected=%p", 
                    reinterpret_cast<void*>(hDC),
                    static_cast<unsigned long long>(debugId_),
                    reinterpret_cast<void*>(attachedDc_));
            return DDERR_INVALIDPARAMS;
        }

        // For 8-bit surfaces, convert the temp 32-bit buffer back to palette indices.
        if (bpp_ == 8 && !dcTempBuffer_.empty()) {
            PALETTEENTRY entries[256] = {};
            if (palette_) {
                palette_->GetEntries(0, 0, 256, entries);
            } else {
                GetDefault332Palette(entries);
            }
            const size_t pixelCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t r = dcTempBuffer_[i * 4u + 0u];
                const uint8_t g = dcTempBuffer_[i * 4u + 1u];
                const uint8_t b = dcTempBuffer_[i * 4u + 2u];
                // Find the nearest palette entry (minimise squared RGB distance). Visits
                // entries 0..255 in order, same as before, so tie-breaking (first index with
                // minimum distance wins) is unchanged - but skips the G/B terms once the
                // running partial sum already can't beat bestDist, since every squared term is
                // non-negative (docs/audit_ddraw.md §3.3, TASK-24H-0153). This is a proven-
                // equivalent transformation, not a different algorithm: dist >= bestDist after
                // adding only dr*dr implies the full dr*dr+dg*dg+db*db would be >= bestDist too.
                int bestIdx = 0;
                int bestDist = INT_MAX;
                for (int j = 0; j < 256; ++j) {
                    const int dr = static_cast<int>(r) - static_cast<int>(entries[j].peRed);
                    int dist = dr * dr;
                    if (dist >= bestDist) continue;
                    const int dg = static_cast<int>(g) - static_cast<int>(entries[j].peGreen);
                    dist += dg * dg;
                    if (dist >= bestDist) continue;
                    const int db = static_cast<int>(b) - static_cast<int>(entries[j].peBlue);
                    dist += db * db;
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestIdx = j;
                        if (dist == 0) break;
                    }
                }
                pixels_[i] = static_cast<uint8_t>(bestIdx);
            }
            dcTempBuffer_.clear();
        }

        // Destroy the DC so the next GetDC creates a fresh one.
        FreeApiDestroySurfaceDC(attachedDc_);
        attachedDc_ = nullptr;

        SDL_Log("free-direct ReleaseDC: surfaceId=%llu dc=%p bpp=%d", 
                static_cast<unsigned long long>(debugId_),
                reinterpret_cast<void*>(hDC),
                bpp_);
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
        FREE_DIRECT_DIAG_INC_TOTAL(lockCallsTotal);

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
        FREE_DIRECT_DIAG_INC_TOTAL(unlockCallsTotal);
        SDL_Log("free-direct Unlock: surfaceId=%llu type=%s data=%p", 
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                lpSurfaceData);
        return DD_OK;
    }

    /**
     * @brief Store source blit color key on this surface.
     *
     * The stored values are kept exactly as provided by DirectDraw callers.
     * During blits with source-key flags, 8-bit surfaces compare palette index values
     * while 32-bit surfaces compare both raw packed pixel value and RGB-masked value
     * for compatibility with DDColorMatch-based keys.
     *
     * @note Status: IMPLEMENTED (range compare supported; legacy key-generation quirks handled)
     */
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
            ColorKeyLog("free-direct SetColorKey detail: surfaceId=%llu bpp=%d pitch=%ld low=0x%08lx high=0x%08lx",
                        static_cast<unsigned long long>(debugId_),
                        bpp_,
                        static_cast<long>(GetPitch()),
                        static_cast<unsigned long>(hasSrcColorKey_ ? colorKey_.dwColorSpaceLowValue : 0),
                        static_cast<unsigned long>(hasSrcColorKey_ ? colorKey_.dwColorSpaceHighValue : 0));
            return DD_OK;
        }
        SDL_Log("free-direct SetColorKey: unsupported flags=0x%08lx on surfaceId=%llu", 
                static_cast<unsigned long>(dwFlags),
                static_cast<unsigned long long>(debugId_));
        return DDERR_UNSUPPORTED;
    }

    /**
     * @brief Present the primary surface to the screen.
     *
     * Delegates to DirectDrawImpl::PresentPrimary which uploads the CPU pixel buffer
     * to an SDL_Texture and calls SDL_RenderPresent exactly once.
     * This is the single presentation path for the DirectDraw subset.
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT WINAPI DirectDrawSurfaceImpl::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
    {
        (void)lpDDSurfaceTargetOverride;
        (void)dwFlags;
        FREE_DIRECT_DIAG_INC_TOTAL(flipCallsTotal);

        PresentLog("free-direct Flip: surfaceId=%llu type=%s dirty=%s flags=0x%08lx",
                static_cast<unsigned long long>(debugId_),
                (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                BoolToText(dirty_),
                static_cast<unsigned long>(dwFlags));

        // Mark that this game uses Flip for presentation — disables auto-present from Blt/BltFast.
        if (owner_) {
            owner_->usesFlip_ = true;
        }

        if (type_ != SurfaceType::Primary || !owner_ || !owner_->renderer_) {
            SDL_Log("free-direct Flip: unsupported state (type=%s owner=%p renderer=%p)",
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<void*>(owner_),
                    owner_ ? static_cast<void*>(owner_->renderer_) : nullptr);
            return DDERR_UNSUPPORTED;
        }

        return owner_->PresentPrimary(*this);
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
        FREE_DIRECT_DIAG_INC(ddInstances);
        // Allow overriding target FPS via env var FREE_DIRECT_TARGET_FPS.
        const char* fpsCStr = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_TARGET_FPS");
        if (fpsCStr) {
            const int fps = static_cast<int>(SDL_strtol(fpsCStr, nullptr, 10));
            if (fps > 0 && fps <= 1000) {
                presentIntervalNs_ = 1000000000ULL / static_cast<uint64_t>(fps);
            }
        }
        perfWindowStart_ = SDL_GetTicksNS();
        SDL_Log("free-direct DirectDrawImpl ctor: debugPrimaryClearEnabled=%s", BoolToText(debugPrimaryClearEnabled_));
    }

    DirectDrawImpl::~DirectDrawImpl()
    {
        SDL_Log("free-direct DirectDrawImpl dtor: primaryPresented=%s presentCalls=%llu renderer=%p window=%p", 
                BoolToText(primaryPresented_),
                static_cast<unsigned long long>(presentCallCount_),
                static_cast<void*>(renderer_),
                static_cast<void*>(sdlWindow_));
        if (hwnd_) {
            FreeApiSetWindowFullscreen(hwnd_, false);
        }
        if (renderer_) SDL_DestroyRenderer(renderer_);
        FREE_DIRECT_DIAG_DEC(ddInstances);
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

        if (dwFlags & DDSCL_FULLSCREEN) {
            FreeApiSetWindowFullscreen(hwnd_, true);
        } else if (dwFlags & DDSCL_NORMAL) {
            FreeApiSetWindowFullscreen(hwnd_, false);
        }

        SDL_Renderer* windowRenderer = SDL_GetRenderer(sdlWindow_);
        if (windowRenderer && windowRenderer != renderer_) {
            // PARTIAL: The legacy game may recreate DirectDraw over the same HWND
            // during cache/bootstrap. Ensure renderer recreation does not fail due
            // to an already attached renderer on the SDL window.
            SDL_DestroyRenderer(windowRenderer);
            SDL_Log("free-direct SetCooperativeLevel: destroyed pre-existing SDL renderer=%p", static_cast<void*>(windowRenderer));
        }

        if (renderer_) {
            // Every live surface's cached texture_ (if any) was created against this exact
            // renderer_ (PresentPrimary only ever creates one, lazily, per surface) and would
            // otherwise dangle once it's destroyed below - SDL_RenderTexture-ing a texture that
            // belonged to an already-destroyed renderer is undefined behavior, not merely a
            // resource leak (docs/audit_ddraw.md §4.6, F6, TASK-24H-0156). Destroying it here and
            // marking the surface dirty makes PresentPrimary's own `if (!primary.texture_)` cache
            // check recreate it fresh against the new renderer_ below, rather than skipping
            // recreation entirely because ConsumeAndClearDirty()/the dirty check thinks nothing
            // changed.
            for (DirectDrawSurfaceImpl* surface : liveSurfaces_) {
                if (surface->texture_) {
                    SDL_DestroyTexture(surface->texture_);
                    surface->texture_ = nullptr;
                    surface->MarkDirty();
                    FREE_DIRECT_DIAG_DEC(sdlTextures);
                    FREE_DIRECT_DIAG_INC_TOTAL(sdlTexturesDestroyed);
                }
            }
            SDL_Log("free-direct SetCooperativeLevel: destroyed previous renderer=%p", static_cast<void*>(renderer_));
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }

        // Enable vsync by default to limit frame rate at the driver level.
        // Can be disabled via FREE_DIRECT_ENABLE_VSYNC=0.
        const char* vsyncEnv = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_ENABLE_VSYNC");
        const bool enableVsync = (vsyncEnv == nullptr) || SDL_strcasecmp(vsyncEnv, "0") != 0;
        renderer_ = SDL_CreateRenderer(sdlWindow_, NULL);
        if (renderer_) {
            if (enableVsync) {
                SDL_SetRenderVSync(renderer_, 1);
            }
            SDL_Log("free-direct SDL_CreateRenderer: window=%p renderer=%p backend=default vsync=%s", static_cast<void*>(sdlWindow_), static_cast<void*>(renderer_), BoolToText(enableVsync));
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
            // Use the game's logical resolution from SetDisplayMode if available,
            // NOT the physical window size. On Android the window is fullscreen
            // (e.g. 2400x1080) but the game expects 640x480. Using window size
            // makes SDL_SetRenderLogicalPresentation a no-op and stretches output.
            //
            // When SetDisplayMode was not called (non-fullscreen mode), keep the
            // default 640x480 — do NOT fall back to SDL_GetWindowSize, because
            // the game still renders at its fixed logical resolution.
            if (displayModeWidth_ > 0 && displayModeHeight_ > 0) {
                width = displayModeWidth_;
                height = displayModeHeight_;
                SDL_Log("free-direct CreateSurface: primary using display mode %dx%d", width, height);
            } else {
                SDL_Log("free-direct CreateSurface: primary using default %dx%d (no display mode set)", width, height);
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

        // Bound width/height before ever constructing DirectDrawSurfaceImpl, which otherwise
        // resizes its pixel buffer unconditionally (docs/audit_ddraw.md §4.4, F4,
        // TASK-24H-0154) - an unsatisfiable resize throws uncaught, crossing the COM-style
        // interface boundary CLAUDE.md's Coding Style says must never be crossed by an
        // exception. Also catches a huge DWORD (near 0xFFFFFFFF) that went negative once cast
        // to int above (width/height <= 0 fails this check too). 4096 per dimension is a
        // generous ceiling - both target games only ever request 640x480 or smaller
        // icon-sized sub-images - bounding worst-case allocation to 4096*4096*4 = 64MiB.
        constexpr int kMaxSurfaceDimension = 4096;
        if (width <= 0 || width > kMaxSurfaceDimension || height <= 0 || height > kMaxSurfaceDimension) {
            SDL_Log("free-direct CreateSurface: invalid size %dx%d (max %dx%d)", width, height,
                    kMaxSurfaceDimension, kMaxSurfaceDimension);
            return DDERR_INVALIDPARAMS;
        }

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

        displayModeWidth_ = static_cast<int>(dwWidth);
        displayModeHeight_ = static_cast<int>(dwHeight);

        if (hwnd_) {
            FreeApiSetWindowFullscreen(hwnd_, true);
        }

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

    /**
     * @brief Single presentation path: upload primary CPU buffer to SDL texture, render, present.
     *
     * Steps:
     * 1. Create streaming SDL_Texture if not yet created (cached on primary surface).
     * 2. Upload CPU pixel buffer to the SDL_Texture (handles 8-bit palette conversion).
     * 3. SDL_RenderClear (immediately before render to avoid stale back-buffer).
     * 4. SDL_RenderTexture (full primary → full window).
     * 5. SDL_RenderPresent (exactly once per call).
     *
     * @note Status: IMPLEMENTED
     */
    HRESULT DirectDrawImpl::PresentPrimary(DirectDrawSurfaceImpl& primary)
    {
        if (!renderer_) {
            SDL_Log("free-direct PresentPrimary: no renderer");
            return DDERR_UNSUPPORTED;
        }

        perfPresentAttempts_++;

        // Throttle: skip present if called too soon after the last frame.
        const uint64_t nowNs = SDL_GetTicksNS();
        if (lastPresentNs_ != 0 && (nowNs - lastPresentNs_) < presentIntervalNs_) {
            perfPresentThrottled_++;
            return DD_OK;
        }

        // Also skip upload+present when surface has not changed since last present.
        if (!primary.dirty_ && lastPresentNs_ != 0) {
            perfPresentThrottled_++;
            return DD_OK;
        }

        PresentLog("free-direct PresentPrimary: id=%llu size=%dx%d bpp=%d dirty=%s presentCalls=%llu",
                static_cast<unsigned long long>(primary.GetDebugId()),
                primary.GetWidth(),
                primary.GetHeight(),
                primary.GetBPP(),
                BoolToText(primary.dirty_),
                static_cast<unsigned long long>(presentCallCount_));

        // 1. Create streaming texture if needed (cached on primary, not recreated per frame).
        if (!primary.texture_) {
            PresentLog("free-direct PresentPrimary: creating streaming texture %dx%d",
                    primary.GetWidth(), primary.GetHeight());
            primary.texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  primary.GetWidth(), primary.GetHeight());
            if (primary.texture_) {
                FREE_DIRECT_DIAG_INC(sdlTextures);
                FREE_DIRECT_DIAG_INC_EVER(sdlTexturesEver, "tex");
                SDL_SetTextureBlendMode(primary.texture_, SDL_BLENDMODE_NONE);
            } else {
                SDL_Log("free-direct PresentPrimary: SDL_CreateTexture failed: %s", SDL_GetError());
                return DDERR_GENERIC;
            }
        }

        // 2. Upload CPU pixel buffer to texture.
        if (primary.GetBPP() == 8) {
            const size_t pixelCount = static_cast<size_t>(primary.GetWidth()) * static_cast<size_t>(primary.GetHeight());
            // Reuse cached buffer to avoid per-frame heap allocation (was: std::vector<uint32_t> temp(pixelCount)).
            primary.paletteConvertBuffer_.resize(pixelCount);
            PALETTEENTRY entries[256];
            bool hasPalette = false;
            if (primary.palette_) {
                primary.palette_->GetEntries(0, 0, 256, entries);
                hasPalette = true;
            } else {
                GetDefault332Palette(entries);
                hasPalette = true;
            }
            for (size_t i = 0; i < pixelCount; ++i) {
                const uint8_t index = primary.GetPixels()[i];
                if (hasPalette) {
                    primary.paletteConvertBuffer_[i] = (static_cast<uint32_t>(entries[index].peRed))
                            | (static_cast<uint32_t>(entries[index].peGreen) << 8)
                            | (static_cast<uint32_t>(entries[index].peBlue) << 16)
                            | 0xFF000000u;
                } else {
                    primary.paletteConvertBuffer_[i] = static_cast<uint32_t>(index)
                            | (static_cast<uint32_t>(index) << 8)
                            | (static_cast<uint32_t>(index) << 16)
                            | 0xFF000000u;
                }
            }
            PresentLog("free-direct PresentPrimary: uploading 8-bit→RGBA32 hasPalette=%s", BoolToText(hasPalette));
            SDL_UpdateTexture(primary.texture_, NULL, primary.paletteConvertBuffer_.data(), primary.GetWidth() * 4);
            FREE_DIRECT_DIAG_INC_TOTAL(sdlTextureUpdateCallsTotal);
        } else {
            PresentLog("free-direct PresentPrimary: uploading 32-bit RGBA");
            SDL_UpdateTexture(primary.texture_, NULL, primary.GetPixels().data(), primary.GetWidth() * 4);
            FREE_DIRECT_DIAG_INC_TOTAL(sdlTextureUpdateCallsTotal);
        }
        perfTextureUploads_++;

        // 3. Clear renderer (immediately before drawing current content — no gap).
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        // 4. Set SDL3 logical presentation (once) and render texture.
        if (!logicalPresentationSet_) {
            const int srcW = primary.GetWidth();
            const int srcH = primary.GetHeight();
            if (srcW > 0 && srcH > 0) {
                SDL_SetRenderLogicalPresentation(renderer_, srcW, srcH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
                logicalPresentationSet_ = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "free-direct PresentPrimary: set logical presentation %dx%d LETTERBOX", srcW, srcH);
            }
        }
        SDL_RenderTexture(renderer_, primary.texture_, NULL, NULL);

        // 5. Present exactly once.
        SDL_RenderPresent(renderer_);
        lastPresentNs_ = SDL_GetTicksNS();
        presentCallCount_++;
        FREE_DIRECT_DIAG_INC_TOTAL(presentCallsTotal);
        primaryPresented_ = true;
        primary.ConsumeAndClearDirty();
        FREE_DIRECT_DIAG_INC(presentsThisWindow);
        FREE_DIRECT_DIAG_HEARTBEAT();

        PresentLog("free-direct PresentPrimary: presented frame #%llu",
                static_cast<unsigned long long>(presentCallCount_));

        // Perf summary: print once per second when FREE_DIRECT_DEBUG_PERF=1.
        if (IsPerfDebugEnabled()) {
            const uint64_t elapsed = lastPresentNs_ - perfWindowStart_;
            if (elapsed >= 1000000000ULL) {
                PerfLog("[PERF] presents=%llu throttled=%llu uploads=%llu blts=%llu (window=%.2fs)",
                        static_cast<unsigned long long>(presentCallCount_),
                        static_cast<unsigned long long>(perfPresentThrottled_),
                        static_cast<unsigned long long>(perfTextureUploads_),
                        static_cast<unsigned long long>(perfBltCalls_),
                        static_cast<double>(elapsed) / 1e9);
                perfWindowStart_ = lastPresentNs_;
                perfBltCalls_ = 0;
                perfPresentAttempts_ = 0;
                perfPresentThrottled_ = 0;
                perfTextureUploads_ = 0;
            }
        }

        // Diagnostic: FREE_DIRECT_PRESENT log once per second (always enabled).
        {
            static uint64_t diagPerfStart = 0;
            static uint64_t diagFrames = 0;
            static bool diagRendererLogged = false;
            diagFrames++;
            if (diagPerfStart == 0) diagPerfStart = lastPresentNs_;
            const uint64_t diagElapsed = lastPresentNs_ - diagPerfStart;
            if (diagElapsed >= 1000000000ULL) {
                int winW = 0, winH = 0;
                if (sdlWindow_) SDL_GetWindowSize(sdlWindow_, &winW, &winH);
                const double fps = static_cast<double>(diagFrames) * 1e9 / static_cast<double>(diagElapsed);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "FREE_DIRECT_PERF: source/backbuffer=%dx%d window=%dx%d mode=letterbox"
                        " present_count=%llu FPS=%.1f bpp=%d tex_recreated=no"
                        " uploads_per_sec=%llu build=%s",
                        primary.GetWidth(), primary.GetHeight(),
                        winW, winH,
                        static_cast<unsigned long long>(presentCallCount_),
                        fps,
                        primary.GetBPP(),
                        static_cast<unsigned long long>(diagFrames),
#ifdef NDEBUG
                        "Release"
#else
                        "Debug"
#endif
                        );
                diagPerfStart = lastPresentNs_;
                diagFrames = 0;
            }
            if (!diagRendererLogged && renderer_) {
                const char* name = SDL_GetRendererName(renderer_);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "FREE_DIRECT_PRESENT: renderer=%s", name ? name : "unknown");
                diagRendererLogged = true;
            }
        }

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