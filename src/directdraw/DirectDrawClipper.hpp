/**
 * @file DirectDrawClipper.hpp
 * @brief Internal DirectDraw clipper object (`IDirectDrawClipper` implementation).
 *
 * This header is intentionally private to the DirectDraw implementation: it must never be
 * included from `include/ddraw.h` and must never be installed. `DirectDrawImpl::CreateClipper`
 * (`DirectDraw.cpp`) constructs instances of this class and returns them as
 * `LPDIRECTDRAWCLIPPER`.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <ddraw.h>

#include "../diagnostics/Diagnostics.hpp"

#include <atomic>

namespace free_direct_directdraw {

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

} // namespace free_direct_directdraw
