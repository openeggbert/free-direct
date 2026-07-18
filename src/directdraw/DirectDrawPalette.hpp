/**
 * @file DirectDrawPalette.hpp
 * @brief Internal DirectDraw palette object (`IDirectDrawPalette` implementation).
 *
 * This header is intentionally private to the DirectDraw implementation: it must never be
 * included from `include/ddraw.h` and must never be installed. `DirectDrawImpl::CreatePalette`
 * (`DirectDraw.cpp`) constructs instances of this class and returns them as
 * `LPDIRECTDRAWPALETTE`.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <ddraw.h>

#include "../diagnostics/Diagnostics.hpp"

#include <atomic>

namespace free_direct_directdraw {

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

} // namespace free_direct_directdraw
