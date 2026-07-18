/**
 * @file DirectDrawSurface.cpp
 * @brief SDL3-based internal implementation of `IDirectDrawSurface` (`DirectDrawSurfaceImpl`).
 *
 * Split out of the former single-file `DirectDraw.cpp` (2026-07-18) to keep one primary
 * class-family per file, mirroring `src/directplay/`'s existing split. See
 * `DirectDrawInternal.hpp` for the class declaration (shared with `DirectDrawImpl`, which lives
 * in `DirectDraw.cpp`) and shared helper functions.
 * @note Status: IMPLEMENTED (Minimal backend mapping)
 */
#include "DirectDrawInternal.hpp"

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

namespace free_direct_directdraw {

    static std::atomic<uint64_t> g_nextSurfaceId{1};

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

        DirectDrawLog("free-direct CreateSurface/new surface: id=%llu type=%s size=%dx%d bpp=%d pitch=%ld palette=%s",
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
        DirectDrawLog("free-direct surface destroy: id=%llu type=%s texture=%p palette=%s",
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
        if (texturePalette_) {
            SDL_DestroyPalette(texturePalette_);
            texturePalette_ = nullptr;
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

        // Both branches fall through to the shared MarkDirty() tail below (docs/audit_ddraw.md
        // §5.1, F7, TASK-24H-0157) - the 8-bit branch used to return early here, skipping it, so
        // a fill on an 8-bit primary surface would never be flagged for presentation.
        if (bpp_ == 8) {
            const auto index = static_cast<uint8_t>(fillColor & 0xFFu);
            for (int y = fillRect.top; y < fillRect.bottom; ++y) {
                for (int x = fillRect.left; x < fillRect.right; ++x) {
                    const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
                    pixels_[offset] = index;
                }
            }
        } else {
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
        // A plain static_cast is safe here (docs/audit_ddraw.md §3.4, TASK-24H-0163, replacing a
        // former RTTI-based downcast): DirectDrawSurfaceImpl is the only concrete
        // IDirectDrawSurface in this codebase and is declared final, so there is no other type a
        // non-null lpDDSrcSurface could actually be - that RTTI lookup was pure overhead on every
        // blit call. A null lpDDSrcSurface still casts
        // to nullptr either way, so the null check right after this is unaffected.
        auto* sourceSurface = static_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        const bool requestSrcColorKey = (dwFlags & DDBLT_KEYSRC) != 0;
        DirectDrawLog("free-direct Blt: dstId=%llu dstType=%s srcId=%llu src=%p flags=0x%08lx hasPalette=%s hasSrcColorKey=%s", 
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
            DirectDrawLog("free-direct Blt: dstRect=[%ld,%ld,%ld,%ld]", 
                    static_cast<long>(lpDestRect->left),
                    static_cast<long>(lpDestRect->top),
                    static_cast<long>(lpDestRect->right),
                    static_cast<long>(lpDestRect->bottom));
        }
        if (lpSrcRect) {
            DirectDrawLog("free-direct Blt: srcRect=[%ld,%ld,%ld,%ld]", 
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
            DirectDrawLog("free-direct Blt: adjusted dstRect from screen [%ld,%ld,%ld,%ld] to client [%ld,%ld,%ld,%ld] (winPos=%d,%d)",
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
                DirectDrawLog("free-direct Blt: COLORFILL requested without DDBLTFX");
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
                DirectDrawLog("free-direct Blt: source surface type mismatch");
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

        DirectDrawLog("free-direct Blt: unsupported flag combination 0x%08lx", static_cast<unsigned long>(dwFlags));
        return DDERR_UNSUPPORTED;
    }

    HRESULT WINAPI DirectDrawSurfaceImpl::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
    {
        FREE_DIRECT_DIAG_INC(bltCallsThisWindow);
        FREE_DIRECT_DIAG_INC_TOTAL(bltFastCallsTotal);
        if (!lpDDSrcSurface) {
            DirectDrawLog("free-direct BltFast: null source surface");
            return DDERR_INVALIDPARAMS;
        }
        // A plain static_cast is safe here (docs/audit_ddraw.md §3.4, TASK-24H-0163, replacing a
        // former RTTI-based downcast): DirectDrawSurfaceImpl is the only concrete
        // IDirectDrawSurface in this codebase and is declared final, so there is no other type a
        // non-null lpDDSrcSurface could actually be - that RTTI lookup was pure overhead on every
        // blit call. A null lpDDSrcSurface still casts
        // to nullptr either way, so the null check right after this is unaffected.
        auto* sourceSurface = static_cast<DirectDrawSurfaceImpl*>(lpDDSrcSurface);
        if (!sourceSurface) {
            DirectDrawLog("free-direct BltFast: source surface type mismatch");
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
        DirectDrawLog("free-direct BltFast: dstId=%llu srcId=%llu xy=(%lu,%lu) trans=0x%08lx useSrcColorKey=%s", 
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
        DirectDrawLog("free-direct BltFast result: dstId=%llu srcId=%llu hr=0x%08lx", 
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
        DirectDrawLog("free-direct SetPalette: surfaceId=%llu type=%s palette=%p hasPalette=%s", 
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
            DirectDrawLog("free-direct GetDC: no pixel buffer surfaceId=%llu", static_cast<unsigned long long>(debugId_));
            return DDERR_UNSUPPORTED;
        }

        // A second GetDC() before an intervening ReleaseDC() used to silently re-return the same
        // handle instead of matching real DirectDraw semantics (docs/audit_ddraw.md §4.3, F8,
        // TASK-24H-0158) - DDERR_DCALREADYCREATED is already defined in include/ddraw.h but was
        // never returned anywhere.
        if (attachedDc_) {
            DirectDrawLog("free-direct GetDC: DC already created surfaceId=%llu dc=%p",
                    static_cast<unsigned long long>(debugId_), reinterpret_cast<void*>(attachedDc_));
            return DDERR_DCALREADYCREATED;
        }

        {
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
                DirectDrawLog("free-direct GetDC: FreeApiCreateSurfaceDC failed for surfaceId=%llu", static_cast<unsigned long long>(debugId_));
                return DDERR_GENERIC;
            }
        }

        *lphDC = attachedDc_;
        DirectDrawLog("free-direct GetDC: surfaceId=%llu dc=%p size=%dx%d pitch=%ld bpp=%d", 
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
            DirectDrawLog("free-direct ReleaseDC: unexpected dc=%p for surfaceId=%llu expected=%p", 
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

        DirectDrawLog("free-direct ReleaseDC: surfaceId=%llu dc=%p bpp=%d", 
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
        lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = (bpp_ == 8) ? DDPF_PALETTEINDEXED8 : DDPF_RGB;
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
        DirectDrawLog("free-direct Lock: surfaceId=%llu type=%s flags=0x%08lx hr=0x%08lx pitch=%ld bpp=%d size=%dx%d hasPalette=%s", 
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
            DirectDrawLog("free-direct Lock: lockRect=[%ld,%ld,%ld,%ld]", 
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
        DirectDrawLog("free-direct Unlock: surfaceId=%llu type=%s data=%p", 
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
            DirectDrawLog("free-direct SetColorKey: surfaceId=%llu type=%s flags=0x%08lx enabled=%s low=0x%08lx high=0x%08lx", 
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
        DirectDrawLog("free-direct SetColorKey: unsupported flags=0x%08lx on surfaceId=%llu", 
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
            DirectDrawLog("free-direct Flip: unsupported state (type=%s owner=%p renderer=%p)",
                    (type_ == SurfaceType::Primary) ? "primary" : "offscreen",
                    static_cast<void*>(owner_),
                    owner_ ? static_cast<void*>(owner_->renderer_) : nullptr);
            return DDERR_UNSUPPORTED;
        }

        return owner_->PresentPrimary(*this);
    }

} // namespace free_direct_directdraw
