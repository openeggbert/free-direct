/**
 * @file DirectDraw.cpp
 * @brief SDL3-based internal implementation of `IDirectDraw` (`DirectDrawImpl`) and the public
 *        `DirectDrawCreate` entry point.
 *
 * `DirectDrawSurfaceImpl` (the other half of this subsystem) was split out into
 * `DirectDrawSurface.cpp` on 2026-07-18 to keep one primary class-family per file, mirroring
 * `src/directplay/`'s existing split; `DirectDrawPaletteImpl`/`DirectDrawClipperImpl` moved to
 * `DirectDrawPalette.cpp`/`DirectDrawClipper.cpp` at the same time. See
 * `DirectDrawInternal.hpp` for the shared class declarations and helper functions.
 * @note Status: IMPLEMENTED (Minimal backend mapping)
 */
#include <ddraw.h>

#include <free_api_bridge.h>
#include <SDL3/SDL.h>
#include "../diagnostics/Diagnostics.hpp"
#include "DirectDrawInternal.hpp"
#include "DirectDrawPalette.hpp"
#include "DirectDrawClipper.hpp"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace free_direct_directdraw {

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
        DirectDrawLog("free-direct DirectDrawImpl ctor: debugPrimaryClearEnabled=%s", BoolToText(debugPrimaryClearEnabled_));
    }

    DirectDrawImpl::~DirectDrawImpl()
    {
        DirectDrawLog("free-direct DirectDrawImpl dtor: primaryPresented=%s presentCalls=%llu renderer=%p window=%p", 
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
        DirectDrawLog("free-direct SetCooperativeLevel: hWnd=%p flags=0x%08lx", hWnd, static_cast<unsigned long>(dwFlags));

        if (!hWnd) {
            DirectDrawLog("free-direct SetCooperativeLevel: invalid null HWND");
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
            DirectDrawLog("free-direct SetCooperativeLevel: destroyed pre-existing SDL renderer=%p", static_cast<void*>(windowRenderer));
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
                if (surface->texturePalette_) {
                    SDL_DestroyPalette(surface->texturePalette_);
                    surface->texturePalette_ = nullptr;
                }
            }
            DirectDrawLog("free-direct SetCooperativeLevel: destroyed previous renderer=%p", static_cast<void*>(renderer_));
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
            DirectDrawLog("free-direct SDL_CreateRenderer: window=%p renderer=%p backend=default vsync=%s", static_cast<void*>(sdlWindow_), static_cast<void*>(renderer_), BoolToText(enableVsync));
        }
        if (!renderer_) {
            // PARTIAL: Legacy compatibility fallback for environments where
            // the default renderer cannot be created for the existing window.
            renderer_ = SDL_CreateRenderer(sdlWindow_, "software");
            if (renderer_) {
                DirectDrawLog("free-direct SDL_CreateRenderer: fallback backend=software renderer=%p", static_cast<void*>(renderer_));
            }
        }
        if (!renderer_) {
            DirectDrawLog("free-direct SetCooperativeLevel: SDL_CreateRenderer failed: %s", SDL_GetError());
            return DDERR_GENERIC;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(sdlWindow_, &windowWidth, &windowHeight);
        DirectDrawLog("free-direct SetCooperativeLevel: SDL window=%p size=%dx%d id=%u", 
                static_cast<void*>(sdlWindow_),
                windowWidth,
                windowHeight,
                static_cast<unsigned>(SDL_GetWindowID(sdlWindow_)));

        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
        DirectDrawLog("free-direct SetCooperativeLevel: renderer output size=%dx%d", outputWidth, outputHeight);

        if (debugPrimaryClearEnabled_ && !debugPrimaryClearDone_) {
            SDL_SetRenderDrawColor(renderer_, 0, 128, 255, 255);
            SDL_RenderClear(renderer_);
            SDL_RenderPresent(renderer_);
            presentCallCount_++;
            primaryPresented_ = true;
            debugPrimaryClearDone_ = true;
            DirectDrawLog("free-direct debug primary clear/present: executed in SetCooperativeLevel to validate visible output");
        }

        return DD_OK;
    }

    HRESULT WINAPI DirectDrawImpl::CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                                 LPDIRECTDRAWSURFACE* lplpDDSurface,
                                                 IUnknown* pUnkOuter)
    {
        DirectDrawLog("free-direct CreateSurface: desc=%p out=%p outer=%p", 
                static_cast<const void*>(lpDDSurfaceDesc),
                static_cast<void*>(lplpDDSurface),
                static_cast<void*>(pUnkOuter));

        if (!lpDDSurfaceDesc || !lplpDDSurface || pUnkOuter) {
            DirectDrawLog("free-direct CreateSurface: invalid params");
            return DDERR_INVALIDPARAMS;
        }

        if (lpDDSurfaceDesc->dwSize != sizeof(DDSURFACEDESC)) {
            DirectDrawLog("free-direct CreateSurface: invalid desc size=%lu expected=%zu", 
                    static_cast<unsigned long>(lpDDSurfaceDesc->dwSize),
                    sizeof(DDSURFACEDESC));
            return DDERR_INVALIDPARAMS;
        }

        const DWORD caps = lpDDSurfaceDesc->ddsCaps.dwCaps;
        const bool primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
        const bool offscreenPlain = (caps & DDSCAPS_OFFSCREENPLAIN) != 0;
        const bool systemMemory = (caps & DDSCAPS_SYSTEMMEMORY) != 0;

        DirectDrawLog("free-direct CreateSurface request: flags=0x%08lx caps=0x%08lx primary=%s offscreenPlain=%s systemMemory=%s width=%lu height=%lu pfFlags=0x%08lx pfBpp=%lu", 
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
            DirectDrawLog("free-direct CreateSurface: invalid caps combination (primary=%s offscreen=%s)", BoolToText(primary), BoolToText(offscreen));
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
                DirectDrawLog("free-direct CreateSurface: primary using display mode %dx%d", width, height);
            } else {
                DirectDrawLog("free-direct CreateSurface: primary using default %dx%d (no display mode set)", width, height);
            }
            // Minimal test-support consistency fix (docs/audit_ddraw.md §5.1, F7,
            // TASK-24H-0157's own out-of-scope clause pre-authorized this): honors
            // DDSD_PIXELFORMAT the same way the offscreen branch below already does, purely so
            // an 8-bit primary surface can be constructed at all for
            // Test_FillColor_8BitPrimary_MarksDirty. Neither target game ever sets
            // DDSD_PIXELFORMAT for any surface (primary or offscreen, per
            // docs/directdraw-limitations.md), so this changes no real behavior.
            if (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) {
                bpp = static_cast<int>(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount);
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
            DirectDrawLog("free-direct CreateSurface: invalid size %dx%d (max %dx%d)", width, height,
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
            DirectDrawLog("free-direct CreateSurface: out of memory for %dx%d bpp=%d", width, height, bpp);
            return DDERR_OUTOFMEMORY;
        }

        *lplpDDSurface = surface;
        DirectDrawLog("free-direct CreateSurface result: surfaceId=%llu ptr=%p type=%s size=%dx%d bpp=%d pitch=%ld hasPalette=%s", 
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
        DirectDrawLog("free-direct SetDisplayMode: width=%lu height=%lu bpp=%lu", 
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
        DirectDrawLog("free-direct CreatePalette: flags=0x%08lx colorTable=%p out=%p outer=%p", 
                static_cast<unsigned long>(dwFlags),
                static_cast<void*>(lpColorTable),
                static_cast<void*>(lplpDDPalette),
                static_cast<void*>(pUnkOuter));
        if (!lplpDDPalette || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDPalette = new (std::nothrow) DirectDrawPaletteImpl(dwFlags, lpColorTable);
        DirectDrawLog("free-direct CreatePalette result: palette=%p", static_cast<void*>(*lplpDDPalette));
        return (*lplpDDPalette) ? DD_OK : DDERR_OUTOFMEMORY;
    }

    HRESULT WINAPI DirectDrawImpl::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter)
    {
        (void)dwFlags;
        DirectDrawLog("free-direct CreateClipper: out=%p outer=%p", static_cast<void*>(lplpDDClipper), static_cast<void*>(pUnkOuter));
        if (!lplpDDClipper || pUnkOuter) return DDERR_INVALIDPARAMS;
        *lplpDDClipper = new (std::nothrow) DirectDrawClipperImpl();
        DirectDrawLog("free-direct CreateClipper result: clipper=%p", static_cast<void*>(*lplpDDClipper));
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
            DirectDrawLog("free-direct PresentPrimary: no renderer");
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
#ifdef FREE_DIRECT_ENABLE_INDEXED_TEXTURES
            // GPU path: an 8-bit surface gets a native indexed texture, so the renderer
            // backend's own texture sampling performs the index->color lookup instead of a
            // CPU-side per-pixel conversion (see step 2 below).
            //
            // The palette MUST be attached via SDL_CreateTextureWithProperties'
            // SDL_PROP_TEXTURE_CREATE_PALETTE_POINTER property at creation time -- confirmed
            // by testing that SDL_SetTexturePalette() (attaching a palette to an
            // already-created texture) is silently ignored by at least the software renderer
            // backend, even though the call itself reports success. Once attached this way,
            // later color changes to the same SDL_Palette object (SDL_SetPaletteColors, done
            // every dirty present in step 2) DO take effect without recreating the texture --
            // confirmed by testing too.
            if (primary.GetBPP() == 8) {
                primary.texturePalette_ = SDL_CreatePalette(256);
                if (primary.texturePalette_) {
                    SDL_PropertiesID props = SDL_CreateProperties();
                    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_INDEX8);
                    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
                    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, primary.GetWidth());
                    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, primary.GetHeight());
                    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_PALETTE_POINTER, primary.texturePalette_);
                    primary.texture_ = SDL_CreateTextureWithProperties(renderer_, props);
                    SDL_DestroyProperties(props);
                    if (!primary.texture_) {
                        SDL_DestroyPalette(primary.texturePalette_);
                        primary.texturePalette_ = nullptr;
                    }
                } else {
                    DirectDrawLog("free-direct PresentPrimary: SDL_CreatePalette failed: %s", SDL_GetError());
                }
            } else {
                primary.texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                      SDL_TEXTUREACCESS_STREAMING,
                                                      primary.GetWidth(), primary.GetHeight());
            }
#else
            primary.texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  primary.GetWidth(), primary.GetHeight());
#endif
            if (primary.texture_) {
                FREE_DIRECT_DIAG_INC(sdlTextures);
                FREE_DIRECT_DIAG_INC_EVER(sdlTexturesEver, "tex");
                SDL_SetTextureBlendMode(primary.texture_, SDL_BLENDMODE_NONE);
            } else {
                DirectDrawLog("free-direct PresentPrimary: SDL_CreateTexture failed: %s", SDL_GetError());
                return DDERR_GENERIC;
            }
        }

        // 2. Upload CPU pixel buffer to texture.
        if (primary.GetBPP() == 8) {
#ifdef FREE_DIRECT_ENABLE_INDEXED_TEXTURES
            if (primary.texturePalette_) {
                // GPU path: upload the raw 8-bit indices as-is (no per-pixel CPU conversion);
                // refresh the attached SDL_Palette's colors every dirty present, mirroring the
                // CPU path's own fresh GetEntries() read below.
                PALETTEENTRY entries[256];
                if (primary.palette_) {
                    primary.palette_->GetEntries(0, 0, 256, entries);
                } else {
                    GetDefault332Palette(entries);
                }
                SDL_Color colors[256];
                for (int i = 0; i < 256; ++i) {
                    colors[i].r = entries[i].peRed;
                    colors[i].g = entries[i].peGreen;
                    colors[i].b = entries[i].peBlue;
                    colors[i].a = 255;
                }
                if (!SDL_SetPaletteColors(primary.texturePalette_, colors, 0, 256)) {
                    DirectDrawLog("free-direct PresentPrimary: SDL_SetPaletteColors failed: %s", SDL_GetError());
                }

                PresentLog("free-direct PresentPrimary: uploading 8-bit indices (GPU palette lookup)");
                if (!SDL_UpdateTexture(primary.texture_, NULL, primary.GetPixels().data(), primary.GetWidth())) {
                    DirectDrawLog("free-direct PresentPrimary: SDL_UpdateTexture (INDEX8) failed: %s", SDL_GetError());
                }
                FREE_DIRECT_DIAG_INC_TOTAL(sdlTextureUpdateCallsTotal);
            } else
#endif
            {
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
            }
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

} // namespace free_direct_directdraw

using namespace free_direct_directdraw;

HRESULT WINAPI DirectDrawCreate(const GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter)
{
    DirectDrawLog("free-direct DirectDrawCreate: guid=%p out=%p outer=%p", 
            static_cast<const void*>(lpGUID),
            static_cast<void*>(lplpDD),
            static_cast<void*>(pUnkOuter));
    (void)lpGUID;

    if (!lplpDD || pUnkOuter) {
        DirectDrawLog("free-direct DirectDrawCreate: invalid params");
        return DDERR_INVALIDPARAMS;
    }

    auto* directDraw = new (std::nothrow) DirectDrawImpl();
    if (!directDraw) {
        DirectDrawLog("free-direct DirectDrawCreate: out of memory");
        return DDERR_OUTOFMEMORY;
    }

    *lplpDD = directDraw;
    DirectDrawLog("free-direct DirectDrawCreate result: dd=%p", static_cast<void*>(directDraw));
    return DD_OK;
}