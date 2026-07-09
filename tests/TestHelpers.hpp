/**
 * @file TestHelpers.hpp
 * @brief Shared DirectDraw/DirectSound black-box test scaffolding (`plan.md` TASK-24H-0185).
 *
 * Consolidates helper functions that were previously duplicated, near-byte-for-byte, across
 * `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`, and `tests/integration_tests.cpp`.
 * That duplication had already caused one real divergence before this file existed:
 * `integration_tests.cpp`'s own copy of `CreateOffscreenSurface` hardcoded `DDPF_RGB`
 * unconditionally instead of `directdraw_tests.cpp`'s original `(bpp == 8) ?
 * DDPF_PALETTEINDEXED8 : DDPF_RGB`. **Correction, found while writing this consolidation's own
 * regression test**: this specific field turned out to have no observable behavioral effect -
 * confirmed by reading `DirectDraw.cpp` directly, `CreateSurface` only ever reads
 * `ddpfPixelFormat.dwRGBBitCount` from a caller-supplied descriptor, never `dwFlags`, and
 * `GetSurfaceDesc` derives the `dwFlags` it reports purely from the surface's own internal `bpp_`,
 * not from whatever was originally passed at creation time - so no 8-bit surface behavior was
 * ever actually broken by this divergence, in this codebase, today. It was a real
 * correctness-of-intent bug (the helper claimed to build an accurate `DDSURFACEDESC`, and didn't)
 * worth fixing anyway - both for defensiveness against a future `DirectDraw.cpp` change that
 * might start reading this field, and because a test helper silently constructing the wrong
 * descriptor is exactly the kind of thing that erodes trust in test scaffolding - but it is not
 * the "silently dropped real 8-bit support" bug it was initially assumed to be; see
 * `plan.md` `TASK-24H-0185`'s own `Verified:` note for the full correction. This header exists to
 * close the general *class* of duplication risk this divergence is one instance of, reversing
 * this project's prior no-shared-test-header convention for this one, now-justified case (asked
 * of the user via `AskUserQuestion`, not decided unilaterally).
 *
 * Not a whitebox helper: every function here goes through the real public `IDirectDraw*`/
 * `IDirectSound*` interfaces only, same as every test file that includes it.
 *
 * **Contract**: the including `.cpp` file must define a `CHECK(expr)` macro (and the `Check`/
 * `g_failures` machinery it expands to) *before* `#include`-ing this header - every function below
 * uses `CHECK` to report failures into the including file's own failure counter. This mirrors how
 * `directdraw_tests.cpp`/`directsound_tests.cpp`/`integration_tests.cpp` were already structured
 * (a `CHECK`-defining block, then a helper-defining block) before this consolidation - only the
 * helpers moved, not the `CHECK`/`Check`/`g_failures` test-runner machinery itself, which stays
 * genuinely per-file (each binary's own independent failure count), matching this task's own
 * scope. `tests/directplay_tests.cpp`/`tests/enet_directplay_tests.cpp` are deliberately not
 * included in this consolidation - they test a different interface (whitebox DirectPlay
 * internals) with no overlap with the DirectDraw/DirectSound helpers here.
 *
 * Not installed, not part of `include/` - a private test-only header, never reachable from
 * production code.
 */
#ifndef FREE_DIRECT_TESTS_TEST_HELPERS_HPP
#define FREE_DIRECT_TESTS_TEST_HELPERS_HPP

#include <ddraw.h>
#include <dsound.h>
#include <windows.h>
#include <free_api_bridge.h>
#include <SDL3/SDL.h>

#include <cstring>

namespace free_direct_test_helpers {

// ===== DirectDraw-side helpers =====

inline LPDIRECTDRAW CreateDirectDrawNoWindow() {
    LPDIRECTDRAW dd = nullptr;
    CHECK(DirectDrawCreate(nullptr, &dd, nullptr) == DD_OK);
    return dd;
}

// bpp-conditional pixel format (DDPF_PALETTEINDEXED8 for 8bpp, DDPF_RGB otherwise) - this is the
// one detail that was found to have diverged between the pre-consolidation copies (see this
// file's own top-of-file comment for the correction on what that divergence actually did/didn't
// affect - it's a correctness-of-intent fix, not a functional-behavior one).
inline LPDIRECTDRAWSURFACE CreateOffscreenSurface(LPDIRECTDRAW dd, int width, int height, int bpp) {
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = static_cast<DWORD>(width);
    desc.dwHeight = static_cast<DWORD>(height);
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = (bpp == 8) ? DDPF_PALETTEINDEXED8 : DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = static_cast<DWORD>(bpp);

    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DD_OK);
    return surface;
}

inline LPDIRECTDRAWSURFACE CreatePrimarySurface(LPDIRECTDRAW dd) {
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DD_OK);
    return surface;
}

// Only used by tests that genuinely need a real SDL window/renderer (SetCooperativeLevel and the
// primary-surface auto-present path). Registers the window class once (RegisterClassA is
// idempotent - free-api just overwrites the same map entry - so calling it again per test is
// harmless, but a single static-guarded registration keeps intent clear). One shared class name
// across every test binary that uses this header - purely an internal Win32 identifier with no
// external meaning, so consolidating it needs no per-file distinction.
inline LRESULT CALLBACK TestWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, message, wParam, lParam);
}

inline HWND CreateTestWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSA windowClass{};
        windowClass.lpfnWndProc = TestWindowProc;
        windowClass.lpszClassName = "FreeDirectTestWindow";
        CHECK(RegisterClassA(&windowClass) != 0);
        registered = true;
    }
    HWND hwnd = CreateWindowExA(0, "FreeDirectTestWindow", "free-direct test", WS_OVERLAPPEDWINDOW,
                                 0, 0, 320, 240, NULL, NULL, NULL, NULL);
    CHECK(hwnd != nullptr);
    return hwnd;
}

// Reads back a single presented pixel as packed 0xAABBGGRR (SDL_PIXELFORMAT_RGBA32 byte order:
// byte0=R,1=G,2=B,3=A) via the real SDL renderer DirectDrawImpl::SetCooperativeLevel created for
// this window. `SDL_GetRenderer(window)` is public SDL3 API - it works here (not a whitebox hack)
// because CreateWindowExA/SetCooperativeLevel already establish HWND == SDL_Window* as a real,
// confirmed convention in this codebase (see free-api's own CreateWindowExA implementation), and
// SDL3 itself tracks one renderer per window, retrievable by anyone holding the window pointer.
// Physical (0,0) sits exactly on the letterbox/scale-math corner and is not a reliable sample
// point - callers should read at (10,10) or another interior point, matching every existing use.
inline uint32_t ReadPresentedPixel(HWND hwnd, int x, int y) {
    auto* window = reinterpret_cast<SDL_Window*>(hwnd);
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (!renderer) return 0;
    SDL_Rect rect{x, y, 1, 1};
    SDL_Surface* raw = SDL_RenderReadPixels(renderer, &rect);
    if (!raw) return 0;
    SDL_Surface* converted = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!converted) return 0;
    auto* p = static_cast<uint8_t*>(converted->pixels);
    const uint32_t pixel = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    SDL_DestroySurface(converted);
    return pixel;
}

// Fills the *entire* primary surface (NULL dest rect - whatever its actual size is, normally
// 640x480 by default since no SetDisplayMode call is involved here) with a solid 0x00RRGGBB color
// via Blt/DDBLT_COLORFILL. Filling the whole surface, not an arbitrary sub-rect, means any
// physical pixel read back via ReadPresentedPixel must show this color regardless of the letterbox
// scale/offset math SDL_SetRenderLogicalPresentation applies between the primary's logical size
// and the test window's physical size.
inline void FillPrimaryWithColor(LPDIRECTDRAWSURFACE primary, DWORD rgb) {
    DDBLTFX fx{};
    std::memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(DDBLTFX);
    fx.dwFillColor = rgb;
    CHECK(primary->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL, &fx) == DD_OK);
}

// ===== DirectSound-side helpers =====

inline LPDIRECTSOUND CreateDirectSoundNoWindow() {
    LPDIRECTSOUND ds = nullptr;
    CHECK(DirectSoundCreate(nullptr, &ds, nullptr) == DS_OK);
    return ds;
}

// Builds a real PCMWAVEFORMAT (not WAVEFORMATEX - DirectSoundBufferImpl's constructor reads it as
// const PCMWAVEFORMAT* specifically; reading it as WAVEFORMATEX* would read struct padding as
// wBitsPerSample and silently produce 0-bit audio) and creates a buffer with it. Matches README's
// documented supported-format table: 8-bit unsigned / 16-bit signed LE, mono/stereo,
// 11025/22050/44100 Hz.
inline LPDIRECTSOUNDBUFFER CreatePcmBuffer(LPDIRECTSOUND ds, DWORD bufferBytes, WORD bits,
                                            WORD channels, DWORD freq) {
    PCMWAVEFORMAT fmt{};
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
    fmt.wf.nChannels = channels;
    fmt.wf.nSamplesPerSec = freq;
    fmt.wf.nBlockAlign = static_cast<WORD>(channels * (bits / 8));
    fmt.wf.nAvgBytesPerSec = freq * fmt.wf.nBlockAlign;
    fmt.wBitsPerSample = bits;

    DSBUFFERDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
    desc.dwBufferBytes = bufferBytes;
    desc.lpwfxFormat = &fmt;

    LPDIRECTSOUNDBUFFER buf = nullptr;
    CHECK(ds->CreateSoundBuffer(&desc, &buf, nullptr) == DS_OK);
    return buf;
}

} // namespace free_direct_test_helpers

#endif // FREE_DIRECT_TESTS_TEST_HELPERS_HPP
