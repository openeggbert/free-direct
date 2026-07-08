/**
 * @file directdraw_tests.cpp
 * @brief Standalone DirectDraw unit tests (`plan.md`'s 24-Hour Stabilization Backlog,
 * TASK-24H-0026 onward).
 *
 * Unlike `directplay_tests.cpp`, this file needs the real `free-direct` library (SDL3 +
 * SDL3_image + SDL3_mixer + free-api), so it is only built through CMake:
 *
 *   cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON   # from ../free-eggbert, ../planetblupi, or
 *                                                     # standalone if system SDL3/_image/_mixer
 *                                                     # are available (-DFREE_API_USE_SYSTEM_SDL3=ON)
 *   cmake --build <build>
 *   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest --test-dir <build> -L directdraw
 *
 * `SDL_VIDEODRIVER=dummy` is required (set via this target's CTest `ENVIRONMENT` property, see
 * tests/CMakeLists.txt) — every test here must run headlessly with no real display.
 *
 * Design notes that shaped this file (read before adding more tests):
 * - `DirectDrawSurfaceImpl`/`DirectDrawImpl`/etc. live in an anonymous namespace inside
 *   `DirectDraw.cpp` with no separate header — there is no whitebox path here, unlike
 *   `directplay_tests.cpp`'s access to `DirectPlayMessageQueue`/`LoopbackDirectPlayTransport`.
 *   Every test below goes through the real public `IDirectDraw`/`IDirectDrawSurface`/
 *   `IDirectDrawPalette`/`IDirectDrawClipper` interfaces only.
 * - `CreateSurface`/`Lock`/`Unlock`/`Blt`/`BltFast`/`SetColorKey`/`SetPalette`/`GetDC`/`ReleaseDC`
 *   never touch the SDL renderer at all (confirmed by reading `DirectDraw.cpp`) — only
 *   `SetCooperativeLevel` and the primary-surface auto-present path inside `Blt`/`BltFast`/`Flip`
 *   need a real SDL window/renderer. Most tests below therefore never create a window, for speed
 *   and simplicity; only `Test_SetCooperativeLevel_*` does.
 * - `Lock()`/`GetSurfaceDesc()` only populate `lpSurface`/`lPitch` for **offscreen** surfaces, not
 *   the primary surface (confirmed by reading `DirectDraw.cpp`) — matching real target-game usage,
 *   where `Lock` is only ever called on offscreen/temporary surfaces
 *   (`docs/audit-24h-free-direct.md` §4). All Lock/pixel-memory tests below use offscreen surfaces.
 */
#include <ddraw.h>
#include <windows.h>
#include <free_api_bridge.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s (%s:%d)\n", expr, file, line);
        ++g_failures;
    }
}

} // namespace

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

namespace {

// Shared helper: a fresh IDirectDraw with no window/renderer attached (SetCooperativeLevel never
// called). Sufficient for every surface/blit/lock/color-key/palette/DC test — see the file header
// comment for why no renderer is needed for those.
LPDIRECTDRAW CreateDirectDrawNoWindow() {
    LPDIRECTDRAW dd = nullptr;
    CHECK(DirectDrawCreate(nullptr, &dd, nullptr) == DD_OK);
    return dd;
}

LPDIRECTDRAWSURFACE CreateOffscreenSurface(LPDIRECTDRAW dd, int width, int height, int bpp) {
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

LPDIRECTDRAWSURFACE CreatePrimarySurface(LPDIRECTDRAW dd) {
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DD_OK);
    return surface;
}

// Only used by the two SetCooperativeLevel tests, which are the only tests that genuinely need a
// real SDL window/renderer (see the file header comment). Registers the window class once
// (RegisterClassA is idempotent - free-api just overwrites the same map entry - so calling it
// again per test is harmless, but a single static-guarded registration keeps intent clear).
LRESULT CALLBACK TestWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, message, wParam, lParam);
}

HWND CreateTestWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSA windowClass{};
        windowClass.lpfnWndProc = TestWindowProc;
        windowClass.lpszClassName = "FreeDirectDdrawTestWindow";
        CHECK(RegisterClassA(&windowClass) != 0);
        registered = true;
    }
    HWND hwnd = CreateWindowExA(0, "FreeDirectDdrawTestWindow", "ddraw test", WS_OVERLAPPEDWINDOW,
                                 0, 0, 320, 240, NULL, NULL, NULL, NULL);
    CHECK(hwnd != nullptr);
    return hwnd;
}

} // namespace

// ===== Group 2: creation/lifecycle =====

void Test_DirectDrawCreate_ReturnsOk() {
    LPDIRECTDRAW dd = nullptr;
    CHECK(DirectDrawCreate(nullptr, &dd, nullptr) == DD_OK);
    CHECK(dd != nullptr);
    dd->Release();
}

void Test_DirectDrawCreate_NullOutParam_ReturnsInvalidParams() {
    CHECK(DirectDrawCreate(nullptr, nullptr, nullptr) == DDERR_INVALIDPARAMS);
}

void Test_DirectDrawCreate_NonNullOuter_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = nullptr;
    // pUnkOuter is a COM aggregation parameter this project does not support (matches
    // DirectPlayCreate's DPERR_NOAGGREGATION-shaped rejection, though ddraw.h has no distinct
    // "no aggregation" error code, so DDERR_INVALIDPARAMS is what the real implementation uses).
    int dummyOuter = 0;
    CHECK(DirectDrawCreate(nullptr, &dd, reinterpret_cast<IUnknown*>(&dummyOuter)) == DDERR_INVALIDPARAMS);
}

void Test_DirectDraw_AddRefRelease_AdjustsRefCount() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->AddRef() == 2);
    CHECK(dd->Release() == 1);
    CHECK(dd->Release() == 0); // final release - dd must not be touched after this
}

void Test_SetCooperativeLevel_NormalReturnsOk() {
    HWND hwnd = CreateTestWindow();
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();

    CHECK(dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL) == DD_OK);

    dd->Release();
    DestroyWindow(hwnd);
}

void Test_SetCooperativeLevel_NullHwnd_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->SetCooperativeLevel(nullptr, DDSCL_NORMAL) == DDERR_INVALIDPARAMS);
    dd->Release();
}

// free-eggbert/planetblupi both call SetCooperativeLevel(window, DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)
// (docs/audit-24h-free-direct.md §2.1). SetCooperativeLevel doesn't check
// FreeApiSetWindowFullscreen's return value before proceeding to renderer creation, so this must
// return DD_OK even under the dummy driver, where real OS fullscreen is not meaningful.
void Test_SetCooperativeLevel_FullscreenReturnsOk() {
    HWND hwnd = CreateTestWindow();
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();

    CHECK(dd->SetCooperativeLevel(hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN) == DD_OK);

    dd->Release();
    DestroyWindow(hwnd);
}

void Test_SetDisplayMode_ReturnsOk() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->SetDisplayMode(640, 480, 8) == DD_OK);
    dd->Release();
}

void Test_CreateSurface_Primary_ReturnsOk() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreatePrimarySurface(dd);
    CHECK(surface != nullptr);
    surface->Release();
    dd->Release();
}

void Test_CreateSurface_SystemMemoryOffscreen_ReturnsOk() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 64, 64, 32);
    CHECK(surface != nullptr);
    surface->Release();
    dd->Release();
}

void Test_CreateSurface_NullDescriptor_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(nullptr, &surface, nullptr) == DDERR_INVALIDPARAMS);
    dd->Release();
}

void Test_CreateSurface_MalformedDwSize_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC) - 1; // malformed
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DDERR_INVALIDPARAMS);
    dd->Release();
}

// Real code path (DirectDraw.cpp): `if (primary == offscreen) return DDERR_INVALIDPARAMS;` -
// neither flag set is just as invalid as both set.
void Test_CreateSurface_InvalidCapsCombination_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    desc.ddsCaps.dwCaps = 0; // neither PRIMARYSURFACE nor OFFSCREENPLAIN/SYSTEMMEMORY
    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DDERR_INVALIDPARAMS);

    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY; // both set
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DDERR_INVALIDPARAMS);

    dd->Release();
}

void Test_CreateSurface_OffscreenMissingWidthHeightFlags_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    desc.dwFlags = DDSD_CAPS; // DDSD_WIDTH/DDSD_HEIGHT deliberately omitted
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    LPDIRECTDRAWSURFACE surface = nullptr;
    CHECK(dd->CreateSurface(&desc, &surface, nullptr) == DDERR_INVALIDPARAMS);
    dd->Release();
}

void Test_GetSurfaceDesc_MatchesCreatedDimensions() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 100, 50, 32);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->GetSurfaceDesc(&desc) == DD_OK);
    CHECK(desc.dwWidth == 100);
    CHECK(desc.dwHeight == 50);
    CHECK(desc.ddsCaps.dwCaps == DDSCAPS_OFFSCREENPLAIN);
    CHECK(desc.lPitch == 100 * 4); // 32bpp: 4 bytes/pixel

    surface->Release();
    dd->Release();
}

void Test_GetSurfaceDesc_NullDescriptor_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 16, 16, 32);
    CHECK(surface->GetSurfaceDesc(nullptr) == DDERR_INVALIDPARAMS);
    surface->Release();
    dd->Release();
}

void Test_GetSurfaceDesc_MalformedDwSize_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 16, 16, 32);
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC) - 1;
    CHECK(surface->GetSurfaceDesc(&desc) == DDERR_INVALIDPARAMS);
    surface->Release();
    dd->Release();
}

// ===== Group 3: surface memory =====

void Test_CreateSurface_8Bit_ReturnsCorrectPixelFormat() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 32, 32, 8);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->GetSurfaceDesc(&desc) == DD_OK);
    CHECK(desc.ddpfPixelFormat.dwRGBBitCount == 8);
    CHECK(desc.lPitch == 32); // 8bpp: 1 byte/pixel

    surface->Release();
    dd->Release();
}

void Test_CreateSurface_32Bit_ReturnsCorrectPixelFormat() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 32, 32, 32);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->GetSurfaceDesc(&desc) == DD_OK);
    CHECK(desc.ddpfPixelFormat.dwRGBBitCount == 32);
    CHECK(desc.ddpfPixelFormat.dwRBitMask == 0x00FF0000);
    CHECK(desc.ddpfPixelFormat.dwGBitMask == 0x0000FF00);
    CHECK(desc.ddpfPixelFormat.dwBBitMask == 0x000000FF);

    surface->Release();
    dd->Release();
}

void Test_LockUnlock_OffscreenSurface_PitchMatchesRowStride() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 17, 5, 32); // odd width, not a round number

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    CHECK(desc.lpSurface != nullptr);
    CHECK(desc.lPitch == 17 * 4);

    // Write a distinct pixel on row 1 (not row 0) using the reported pitch, then read it back via
    // a fresh Lock() to confirm the pitch is truly the row stride, not an approximation.
    auto* row1 = static_cast<uint8_t*>(desc.lpSurface) + desc.lPitch;
    row1[0] = 0x11; row1[1] = 0x22; row1[2] = 0x33; row1[3] = 0xFF;
    surface->Unlock(desc.lpSurface);

    DDSURFACEDESC desc2{};
    std::memset(&desc2, 0, sizeof(desc2));
    desc2.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc2, 0, nullptr) == DD_OK);
    auto* row1Again = static_cast<uint8_t*>(desc2.lpSurface) + desc2.lPitch;
    CHECK(row1Again[0] == 0x11 && row1Again[1] == 0x22 && row1Again[2] == 0x33);
    surface->Unlock(desc2.lpSurface);

    surface->Release();
    dd->Release();
}

void Test_LockWriteReadPixelMemory_RoundTrips_8Bit() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 8, 8, 8);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    auto* pixels = static_cast<uint8_t*>(desc.lpSurface);
    for (int i = 0; i < 8 * 8; ++i) pixels[i] = static_cast<uint8_t>(i);
    surface->Unlock(desc.lpSurface);

    DDSURFACEDESC desc2{};
    std::memset(&desc2, 0, sizeof(desc2));
    desc2.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc2, 0, nullptr) == DD_OK);
    auto* readBack = static_cast<uint8_t*>(desc2.lpSurface);
    bool allMatch = true;
    for (int i = 0; i < 8 * 8; ++i) {
        if (readBack[i] != static_cast<uint8_t>(i)) { allMatch = false; break; }
    }
    CHECK(allMatch);
    surface->Unlock(desc2.lpSurface);

    surface->Release();
    dd->Release();
}

void Test_LockWriteReadPixelMemory_RoundTrips_32Bit() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    auto* pixels = static_cast<uint32_t*>(desc.lpSurface);
    for (int i = 0; i < 4 * 4; ++i) pixels[i] = 0xAABBCCDDu + static_cast<uint32_t>(i);
    surface->Unlock(desc.lpSurface);

    DDSURFACEDESC desc2{};
    std::memset(&desc2, 0, sizeof(desc2));
    desc2.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc2, 0, nullptr) == DD_OK);
    auto* readBack = static_cast<uint32_t*>(desc2.lpSurface);
    bool allMatch = true;
    for (int i = 0; i < 4 * 4; ++i) {
        if (readBack[i] != 0xAABBCCDDu + static_cast<uint32_t>(i)) { allMatch = false; break; }
    }
    CHECK(allMatch);
    surface->Unlock(desc2.lpSurface);

    surface->Release();
    dd->Release();
}

// ===== Group 4: blits =====

void Test_BltFast_OpaqueCopy_32Bit_PixelsMatchSource() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 4, 4, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 8, 8, 32);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint32_t*>(srcDesc.lpSurface);
    for (int i = 0; i < 4 * 4; ++i) srcPixels[i] = 0x11223344u + static_cast<uint32_t>(i);
    src->Unlock(srcDesc.lpSurface);

    CHECK(dst->BltFast(2, 3, src, nullptr, DDBLTFAST_NOCOLORKEY) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint32_t*>(dstDesc.lpSurface);
    // Compare RGB bytes only, forcing alpha to 0xFF on both sides: BlitFrom always writes 255 to
    // the destination alpha byte regardless of the source's, by design (DirectDraw.cpp: "DirectDraw
    // blits are opaque by default; avoid transparent desktop-window output when legacy assets have
    // undefined alpha bytes") - comparing raw 32-bit values including alpha would fail here for a
    // reason unrelated to what this test actually checks (RGB copy correctness).
    bool allMatch = true;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint32_t expected = (0x11223344u + static_cast<uint32_t>(y * 4 + x)) & 0x00FFFFFFu;
            const uint32_t actual = dstPixels[(3 + y) * 8 + (2 + x)] & 0x00FFFFFFu;
            if (actual != expected) { allMatch = false; break; }
        }
    }
    CHECK(allMatch);
    CHECK((dstPixels[3 * 8 + 2] >> 24) == 0xFFu); // alpha forced opaque
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

void Test_BltFast_OpaqueCopy_8Bit_PixelsMatchSource() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 4, 4, 8);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 8, 8, 8);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint8_t*>(srcDesc.lpSurface);
    for (int i = 0; i < 4 * 4; ++i) srcPixels[i] = static_cast<uint8_t>(100 + i);
    src->Unlock(srcDesc.lpSurface);

    CHECK(dst->BltFast(1, 1, src, nullptr, DDBLTFAST_NOCOLORKEY) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    bool allMatch = true;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t expected = static_cast<uint8_t>(100 + y * 4 + x);
            const uint8_t actual = dstPixels[(1 + y) * 8 + (1 + x)];
            if (actual != expected) { allMatch = false; break; }
        }
    }
    CHECK(allMatch);
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

// Blits at a destination position that would overrun the buffer if BltFast didn't clip - the
// real risk this audit flagged for BltFast, the heaviest-called DirectDraw method in both target
// games (docs/audit-24h-free-direct.md §2.1). Passing under ASan (TASK-24H-0010) would give this
// extra weight; passing at all already proves no crash/hang for an off-edge destination.
void Test_BltFast_PartiallyOffscreenDest_ClipsWithoutOverrun() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 8, 8, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 8, 8, 32);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    std::memset(srcDesc.lpSurface, 0x7F, static_cast<size_t>(srcDesc.lPitch) * 8);
    src->Unlock(srcDesc.lpSurface);

    // Destination position puts most of the source rect outside the 8x8 destination bounds.
    CHECK(dst->BltFast(5, 5, src, nullptr, DDBLTFAST_NOCOLORKEY) == DD_OK);

    // In-bounds pixel (5,5) must have been written; nothing crashed writing the clipped region.
    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    const size_t offset = static_cast<size_t>(5) * static_cast<size_t>(dstDesc.lPitch) + static_cast<size_t>(5) * 4u;
    CHECK(dstPixels[offset] == 0x7F);
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

// Matches both target games' real BltFast call pattern (docs/audit-24h-free-direct.md §2.1:
// DDBLTFAST_SRCCOLORKEY used on every live BltFast call site in both games).
void Test_BltFast_SrcColorKey_8Bit_SkipsKeyedPixels() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 2, 1, 8);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 2, 1, 8);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    static_cast<uint8_t*>(srcDesc.lpSurface)[0] = 5;  // will be the color key
    static_cast<uint8_t*>(srcDesc.lpSurface)[1] = 200; // opaque
    src->Unlock(srcDesc.lpSurface);

    DDCOLORKEY key{5, 5};
    CHECK(src->SetColorKey(DDCKEY_SRCBLT, &key) == DD_OK);

    // Pre-fill destination with a sentinel so we can detect "not written" vs "written".
    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    static_cast<uint8_t*>(dstDesc.lpSurface)[0] = 77;
    static_cast<uint8_t*>(dstDesc.lpSurface)[1] = 77;
    dst->Unlock(dstDesc.lpSurface);

    CHECK(dst->BltFast(0, 0, src, nullptr, DDBLTFAST_SRCCOLORKEY) == DD_OK);

    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    CHECK(dstPixels[0] == 77);  // keyed pixel skipped, sentinel survives
    CHECK(dstPixels[1] == 200); // opaque pixel copied
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

void Test_BltFast_SrcColorKey_32Bit_SkipsKeyedPixels() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 2, 1, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 2, 1, 32);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint8_t*>(srcDesc.lpSurface);
    // Pixel 0: R=255,G=0,B=255 (magenta - a common legacy sprite color key, matches Main.cpp's
    // own demo usage). Pixel 1: opaque distinct color.
    srcPixels[0] = 255; srcPixels[1] = 0; srcPixels[2] = 255; srcPixels[3] = 255;
    srcPixels[4] = 10; srcPixels[5] = 20; srcPixels[6] = 30; srcPixels[7] = 255;
    src->Unlock(srcDesc.lpSurface);

    DDCOLORKEY key{0x00FF00FFu, 0x00FF00FFu}; // exact magenta match, packed 0x00RRGGBB-shaped
    CHECK(src->SetColorKey(DDCKEY_SRCBLT, &key) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    std::memset(dstDesc.lpSurface, 9, static_cast<size_t>(dstDesc.lPitch));
    dst->Unlock(dstDesc.lpSurface);

    CHECK(dst->BltFast(0, 0, src, nullptr, DDBLTFAST_SRCCOLORKEY) == DD_OK);

    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    CHECK(dstPixels[0] == 9); // keyed magenta pixel skipped, sentinel survives
    CHECK(dstPixels[4] == 10 && dstPixels[5] == 20 && dstPixels[6] == 30); // opaque pixel copied
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

void Test_Blt_ColorFill_FillsDestRectWithColor() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);

    RECT fillRect{1, 1, 3, 3};
    DDBLTFX fx{};
    std::memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(DDBLTFX);
    fx.dwFillColor = 0x00AABBCC;
    CHECK(surface->Blt(&fillRect, nullptr, nullptr, DDBLT_COLORFILL, &fx) == DD_OK);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    auto* pixels = static_cast<uint8_t*>(desc.lpSurface);
    // (1,1) is inside the fill rect: expect R=0xAA G=0xBB B=0xCC per FillColor's 0x00RRGGBB read.
    const size_t insideOffset = static_cast<size_t>(1) * static_cast<size_t>(desc.lPitch) + static_cast<size_t>(1) * 4u;
    CHECK(pixels[insideOffset + 0] == 0xAA);
    CHECK(pixels[insideOffset + 1] == 0xBB);
    CHECK(pixels[insideOffset + 2] == 0xCC);
    // (0,0) is outside the fill rect: must remain untouched (zero-initialized).
    CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
    surface->Unlock(desc.lpSurface);

    surface->Release();
    dd->Release();
}

void Test_Blt_ColorFill_WithoutBltFx_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);
    CHECK(surface->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL, nullptr) == DDERR_INVALIDPARAMS);
    surface->Release();
    dd->Release();
}

// Replicates CPixmap::Display()'s exact call pattern in both target games: DDBLT_WAIT, full-
// surface src/dest rects (NULL), zero-initialized-effects-unused, copying an offscreen back
// buffer onto the primary surface once per frame (docs/audit-24h-free-direct.md §1/§2.1 - the
// single highest-risk untested DirectDraw path found in the prior session's audit).
void Test_Blt_FullSurfaceCopy_MatchesSource() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE primary = CreatePrimarySurface(dd);
    LPDIRECTDRAWSURFACE back = CreateOffscreenSurface(dd, 32, 32, 32); // primary defaults to 640x480; use a small back buffer sized to the compared region

    // Primary defaults to 640x480 (no SetDisplayMode called) - Blt's per-pixel loop scales
    // src->dest by ratio, so to assert an exact 1:1 region match we compare only the region the
    // small back buffer actually covers via an explicit equal-size dest rect instead of NULL.
    DDSURFACEDESC backDesc{};
    std::memset(&backDesc, 0, sizeof(backDesc));
    backDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(back->Lock(nullptr, &backDesc, 0, nullptr) == DD_OK);
    auto* backPixels = static_cast<uint32_t*>(backDesc.lpSurface);
    for (int i = 0; i < 32 * 32; ++i) backPixels[i] = 0x01020304u + static_cast<uint32_t>(i);
    back->Unlock(backDesc.lpSurface);

    RECT dstRect{0, 0, 32, 32};
    CHECK(primary->Blt(&dstRect, back, nullptr, DDBLT_WAIT, nullptr) == DD_OK);

    DDSURFACEDESC primaryDesc{};
    std::memset(&primaryDesc, 0, sizeof(primaryDesc));
    primaryDesc.dwSize = sizeof(DDSURFACEDESC);
    // Lock() on a primary surface does not populate lpSurface (see file header comment) - use
    // GetDC instead to read back what Blt actually wrote, exercising the same DC bridge path
    // group 7 covers, but here as a correctness oracle for this specific test.
    HDC hdc = nullptr;
    CHECK(primary->GetDC(&hdc) == DD_OK);
    CHECK(hdc != nullptr);
    const COLORREF c = GetPixel(hdc, 5, 5);
    // Expected pixel at (5,5): index 5*32+5=165, value 0x01020304+165 = 0x010203A9.
    // GetPixel returns RGB(r,g,b) = r | (g<<8) | (b<<16); Blt/BlitFrom stores r,g,b at offsets
    // 0,1,2 respectively from the low 3 bytes of the source uint32_t (little-endian layout).
    const uint32_t expectedPixel = 0x01020304u + 165u;
    const auto expectedR = static_cast<uint8_t>(expectedPixel & 0xFFu);
    const auto expectedG = static_cast<uint8_t>((expectedPixel >> 8) & 0xFFu);
    const auto expectedB = static_cast<uint8_t>((expectedPixel >> 16) & 0xFFu);
    CHECK(static_cast<uint8_t>(c & 0xFFu) == expectedR);
    CHECK(static_cast<uint8_t>((c >> 8) & 0xFFu) == expectedG);
    CHECK(static_cast<uint8_t>((c >> 16) & 0xFFu) == expectedB);
    primary->ReleaseDC(hdc);

    back->Release();
    primary->Release();
    dd->Release();
}

void Test_Blt_PartialRectCopy_ClipsToDestBounds() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 4, 4, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 4, 4, 32);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    std::memset(srcDesc.lpSurface, 0x55, static_cast<size_t>(srcDesc.lPitch) * 4);
    src->Unlock(srcDesc.lpSurface);

    // Dest rect intentionally extends past the 4x4 destination surface bounds.
    RECT dstRect{2, 2, 10, 10};
    CHECK(dst->Blt(&dstRect, src, nullptr, DDBLT_WAIT, nullptr) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    // In-bounds corner (2,2) must be written; (0,0) (outside the dest rect) must remain zero.
    const size_t insideOffset = static_cast<size_t>(2) * static_cast<size_t>(dstDesc.lPitch) + static_cast<size_t>(2) * 4u;
    CHECK(dstPixels[insideOffset] == 0x55);
    CHECK(dstPixels[0] == 0);
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

// BlitFrom's nearest-neighbor scaling is already implemented (src/dest rects of different sizes
// scale via `srcX = sourceClamped.left + (x * srcWidth) / dstWidth`) and is exercised by
// free-direct's own in-repo demo (src/Main.cpp's "scaled sprite" section) - not by either target
// game, but it costs nothing to verify already-implemented behavior stays correct. Does not add
// or change any scaling behavior.
void Test_Blt_ScalingUpsamplesSourceToLargerDest() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 2, 2, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 4, 4, 32);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint32_t*>(srcDesc.lpSurface);
    srcPixels[0] = 0x11111111; srcPixels[1] = 0x22222222;
    srcPixels[2] = 0x33333333; srcPixels[3] = 0x44444444;
    src->Unlock(srcDesc.lpSurface);

    RECT dstRect{0, 0, 4, 4}; // 2x upsample
    CHECK(dst->Blt(&dstRect, src, nullptr, DDBLT_WAIT, nullptr) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint32_t*>(dstDesc.lpSurface);
    // Each source pixel should now cover a 2x2 block in the destination. RGB-only comparison:
    // alpha is always forced to 0xFF on copy (see the 32-bit BltFast test above for why).
    CHECK((dstPixels[0 * 4 + 0] & 0x00FFFFFFu) == 0x00111111u); // top-left quadrant
    CHECK((dstPixels[0 * 4 + 3] & 0x00FFFFFFu) == 0x00222222u); // top-right quadrant
    CHECK((dstPixels[3 * 4 + 0] & 0x00FFFFFFu) == 0x00333333u); // bottom-left quadrant
    CHECK((dstPixels[3 * 4 + 3] & 0x00FFFFFFu) == 0x00444444u); // bottom-right quadrant
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

void Test_Blt_NullSourceWithoutColorFill_ReturnsUnsupported() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);
    CHECK(surface->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT, nullptr) == DDERR_UNSUPPORTED);
    surface->Release();
    dd->Release();
}

// ===== Group 5: color key =====

// Range (not single-value) color key, matching README's documented "range compare" semantics
// and planetblupi's 2 real SetColorKey call sites (docs/audit-24h-free-direct.md §2.1/§5).
void Test_SetColorKey_RangeAppliedOnSubsequentBltFast() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 3, 1, 8);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 3, 1, 8);

    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint8_t*>(srcDesc.lpSurface);
    srcPixels[0] = 5; srcPixels[1] = 7; srcPixels[2] = 10; // 5 and 7 are inside [5,8]; 10 is not
    src->Unlock(srcDesc.lpSurface);

    DDCOLORKEY key{5, 8}; // range, not a single value
    CHECK(src->SetColorKey(DDCKEY_SRCBLT, &key) == DD_OK);

    DDSURFACEDESC dstDesc{};
    std::memset(&dstDesc, 0, sizeof(dstDesc));
    dstDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    std::memset(dstDesc.lpSurface, 99, 3);
    dst->Unlock(dstDesc.lpSurface);

    CHECK(dst->BltFast(0, 0, src, nullptr, DDBLTFAST_SRCCOLORKEY) == DD_OK);

    CHECK(dst->Lock(nullptr, &dstDesc, 0, nullptr) == DD_OK);
    auto* dstPixels = static_cast<uint8_t*>(dstDesc.lpSurface);
    CHECK(dstPixels[0] == 99);  // 5: inside range, skipped
    CHECK(dstPixels[1] == 99);  // 7: inside range, skipped
    CHECK(dstPixels[2] == 10);  // 10: outside range, copied
    dst->Unlock(dstDesc.lpSurface);

    src->Release();
    dst->Release();
    dd->Release();
}

void Test_SetColorKey_UnsupportedFlags_ReturnsUnsupported() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 8);
    DDCOLORKEY key{0, 0};
    CHECK(surface->SetColorKey(0, &key) == DDERR_UNSUPPORTED); // no DDCKEY_SRCBLT flag
    surface->Release();
    dd->Release();
}

// ===== Group 6: palette =====

void Test_CreatePalette_SetEntriesGetEntries_RoundTrips() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();

    PALETTEENTRY initial[256] = {};
    LPDIRECTDRAWPALETTE palette = nullptr;
    CHECK(dd->CreatePalette(DDPCAPS_8BIT, initial, &palette, nullptr) == DD_OK);
    CHECK(palette != nullptr);

    PALETTEENTRY entries[256] = {};
    for (int i = 0; i < 256; ++i) {
        entries[i] = {static_cast<BYTE>(i), static_cast<BYTE>(255 - i), static_cast<BYTE>(i / 2), 0};
    }
    CHECK(palette->SetEntries(0, 0, 256, entries) == DD_OK);

    PALETTEENTRY readBack[256] = {};
    CHECK(palette->GetEntries(0, 0, 256, readBack) == DD_OK);
    bool allMatch = true;
    for (int i = 0; i < 256; ++i) {
        if (readBack[i].peRed != entries[i].peRed || readBack[i].peGreen != entries[i].peGreen ||
            readBack[i].peBlue != entries[i].peBlue) {
            allMatch = false;
            break;
        }
    }
    CHECK(allMatch);

    palette->Release();
    dd->Release();
}

void Test_Palette_GetEntries_OutOfRangeReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWPALETTE palette = nullptr;
    CHECK(dd->CreatePalette(DDPCAPS_8BIT, nullptr, &palette, nullptr) == DD_OK);

    PALETTEENTRY entries[256] = {};
    CHECK(palette->GetEntries(0, 250, 10, entries) == DDERR_INVALIDPARAMS); // 250+10 > 256

    palette->Release();
    dd->Release();
}

void Test_Palette_SetEntries_OutOfRangeReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWPALETTE palette = nullptr;
    CHECK(dd->CreatePalette(DDPCAPS_8BIT, nullptr, &palette, nullptr) == DD_OK);

    PALETTEENTRY entries[256] = {};
    CHECK(palette->SetEntries(0, 250, 10, entries) == DDERR_INVALIDPARAMS);

    palette->Release();
    dd->Release();
}

void Test_SetPalette_OnSurface_ReturnsOk() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 8, 8, 8);
    LPDIRECTDRAWPALETTE palette = nullptr;
    CHECK(dd->CreatePalette(DDPCAPS_8BIT, nullptr, &palette, nullptr) == DD_OK);

    CHECK(surface->SetPalette(palette) == DD_OK);

    palette->Release();
    surface->Release();
    dd->Release();
}

// Ties palette application to an observable effect: GetDC() on an 8-bit surface expands palette
// indices to RGBA using the surface's attached palette (DirectDraw.cpp's GetDC implementation) -
// this is the effect a real present would also apply, verified here via the GDI bridge instead of
// a full render+readback pipeline (simpler, and doubles as group 7's DC-bridge coverage).
void Test_SetPalette_AffectsGetDCColorExpansion() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 8);

    PALETTEENTRY entries[256] = {};
    entries[42] = {10, 20, 30, 0};
    LPDIRECTDRAWPALETTE palette = nullptr;
    CHECK(dd->CreatePalette(DDPCAPS_8BIT, entries, &palette, nullptr) == DD_OK);
    CHECK(surface->SetPalette(palette) == DD_OK);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    static_cast<uint8_t*>(desc.lpSurface)[0] = 42; // pixel (0,0) = palette index 42
    surface->Unlock(desc.lpSurface);

    HDC hdc = nullptr;
    CHECK(surface->GetDC(&hdc) == DD_OK);
    const COLORREF c = GetPixel(hdc, 0, 0);
    CHECK(static_cast<uint8_t>(c & 0xFFu) == 10);         // R
    CHECK(static_cast<uint8_t>((c >> 8) & 0xFFu) == 20);  // G
    CHECK(static_cast<uint8_t>((c >> 16) & 0xFFu) == 30); // B
    surface->ReleaseDC(hdc);

    palette->Release();
    surface->Release();
    dd->Release();
}

// ===== Group 7: DC bridge =====

// The core "free-api GDI access sees the same backing pixels as DirectDraw surface lock" claim:
// for a 32-bit surface, GetDC's FreeApiCreateSurfaceDC wraps the surface's own pixel buffer
// directly, with no copy (confirmed by reading DirectDraw.cpp's GetDC implementation) - unlike
// the 8-bit case, which goes through a temporary palette-expansion buffer (see the palette-group
// test above for that path instead).
void Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);

    // Write via Lock, read via GDI GetPixel.
    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc, 0, nullptr) == DD_OK);
    auto* pixels = static_cast<uint8_t*>(desc.lpSurface);
    pixels[0] = 111; pixels[1] = 222; pixels[2] = 33; pixels[3] = 255; // pixel (0,0)
    surface->Unlock(desc.lpSurface);

    HDC hdc = nullptr;
    CHECK(surface->GetDC(&hdc) == DD_OK);
    CHECK(hdc != nullptr);
    const COLORREF viaGdi = GetPixel(hdc, 0, 0);
    CHECK(static_cast<uint8_t>(viaGdi & 0xFFu) == 111);
    CHECK(static_cast<uint8_t>((viaGdi >> 8) & 0xFFu) == 222);
    CHECK(static_cast<uint8_t>((viaGdi >> 16) & 0xFFu) == 33);

    // Write via GDI SetPixel, read back via a fresh Lock - proves the sharing is bidirectional.
    CHECK(SetPixel(hdc, 1, 0, RGB(9, 8, 7)) == RGB(9, 8, 7));
    surface->ReleaseDC(hdc);

    DDSURFACEDESC desc2{};
    std::memset(&desc2, 0, sizeof(desc2));
    desc2.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->Lock(nullptr, &desc2, 0, nullptr) == DD_OK);
    auto* pixels2 = static_cast<uint8_t*>(desc2.lpSurface);
    CHECK(pixels2[4] == 9 && pixels2[5] == 8 && pixels2[6] == 7); // pixel (1,0)
    surface->Unlock(desc2.lpSurface);

    surface->Release();
    dd->Release();
}

void Test_GetDC_NullOutParam_ReturnsInvalidParams() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);
    CHECK(surface->GetDC(nullptr) == DDERR_INVALIDPARAMS);
    surface->Release();
    dd->Release();
}

// ===== Group 8: lost/restore =====

// Characterization tests, not correctness tests: IsLost/Restore are honestly documented STUBs
// (DirectDraw.cpp: "@note Status: STUB - Returns success because no real lost-surface recovery is
// required yet"). These lock in that exact, current, intentional behavior.
void Test_IsLost_AlwaysReturnsNotLost() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);
    CHECK(surface->IsLost() == DD_OK);
    surface->Release();
    dd->Release();
}

void Test_Restore_ReturnsOkUnconditionally() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 4, 4, 32);
    CHECK(surface->Restore() == DD_OK);
    surface->Release();
    dd->Release();
}

// ===== Group 9: logging gate regression guard =====

namespace {
int g_logCallCount = 0;

void CountingLogOutputFunction(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata; (void)category; (void)priority; (void)message;
    ++g_logCallCount;
}
} // namespace

// 24-Hour Stabilization Backlog TASK-24H-0048 (plan.md), re-verifying the finding recorded in
// TASK-24H-0111/0117 (no ungated hot-path log exists in Blt/BltFast) as an automated regression
// guard rather than a one-time manual audit. Installs a custom SDL log callback (more robust than
// capturing stdout/stderr - independent of platform console routing) and asserts it is never
// invoked across many BltFast/Blt calls when all FREE_DIRECT_DEBUG_* env vars are unset.
void Test_BltFast_NoUnconditionalLogOutput_WhenDebugFlagsUnset() {
    // Best-effort: ensure the relevant debug flags are unset for this test regardless of the
    // invoking environment (a developer's shell might have one exported from a prior session).
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_DDRAW");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_PRESENTATION");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_COLORKEY");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_PERF");

    SDL_LogOutputFunction prevCallback = nullptr;
    void* prevUserdata = nullptr;
    SDL_GetLogOutputFunction(&prevCallback, &prevUserdata);
    g_logCallCount = 0;
    SDL_SetLogOutputFunction(CountingLogOutputFunction, nullptr);

    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 8, 8, 32);
    LPDIRECTDRAWSURFACE dst = CreateOffscreenSurface(dd, 8, 8, 32);
    for (int i = 0; i < 50; ++i) {
        dst->BltFast(0, 0, src, nullptr, DDBLTFAST_NOCOLORKEY);
    }
    RECT full{0, 0, 8, 8};
    for (int i = 0; i < 50; ++i) {
        dst->Blt(&full, src, nullptr, DDBLT_WAIT, nullptr);
    }
    src->Release();
    dst->Release();
    dd->Release();

    SDL_SetLogOutputFunction(prevCallback, prevUserdata);

    CHECK(g_logCallCount == 0);
}

int main() {
    // Group 2: creation/lifecycle
    Test_DirectDrawCreate_ReturnsOk();
    Test_DirectDrawCreate_NullOutParam_ReturnsInvalidParams();
    Test_DirectDrawCreate_NonNullOuter_ReturnsInvalidParams();
    Test_DirectDraw_AddRefRelease_AdjustsRefCount();
    Test_SetCooperativeLevel_NormalReturnsOk();
    Test_SetCooperativeLevel_NullHwnd_ReturnsInvalidParams();
    Test_SetCooperativeLevel_FullscreenReturnsOk();
    Test_SetDisplayMode_ReturnsOk();
    Test_CreateSurface_Primary_ReturnsOk();
    Test_CreateSurface_SystemMemoryOffscreen_ReturnsOk();
    Test_CreateSurface_NullDescriptor_ReturnsInvalidParams();
    Test_CreateSurface_MalformedDwSize_ReturnsInvalidParams();
    Test_CreateSurface_InvalidCapsCombination_ReturnsInvalidParams();
    Test_CreateSurface_OffscreenMissingWidthHeightFlags_ReturnsInvalidParams();
    Test_GetSurfaceDesc_MatchesCreatedDimensions();
    Test_GetSurfaceDesc_NullDescriptor_ReturnsInvalidParams();
    Test_GetSurfaceDesc_MalformedDwSize_ReturnsInvalidParams();

    // Group 3: surface memory
    Test_CreateSurface_8Bit_ReturnsCorrectPixelFormat();
    Test_CreateSurface_32Bit_ReturnsCorrectPixelFormat();
    Test_LockUnlock_OffscreenSurface_PitchMatchesRowStride();
    Test_LockWriteReadPixelMemory_RoundTrips_8Bit();
    Test_LockWriteReadPixelMemory_RoundTrips_32Bit();

    // Group 4: blits
    Test_BltFast_OpaqueCopy_32Bit_PixelsMatchSource();
    Test_BltFast_OpaqueCopy_8Bit_PixelsMatchSource();
    Test_BltFast_PartiallyOffscreenDest_ClipsWithoutOverrun();
    Test_BltFast_SrcColorKey_8Bit_SkipsKeyedPixels();
    Test_BltFast_SrcColorKey_32Bit_SkipsKeyedPixels();
    Test_Blt_ColorFill_FillsDestRectWithColor();
    Test_Blt_ColorFill_WithoutBltFx_ReturnsInvalidParams();
    Test_Blt_FullSurfaceCopy_MatchesSource();
    Test_Blt_PartialRectCopy_ClipsToDestBounds();
    Test_Blt_ScalingUpsamplesSourceToLargerDest();
    Test_Blt_NullSourceWithoutColorFill_ReturnsUnsupported();

    // Group 5: color key
    Test_SetColorKey_RangeAppliedOnSubsequentBltFast();
    Test_SetColorKey_UnsupportedFlags_ReturnsUnsupported();

    // Group 6: palette
    Test_CreatePalette_SetEntriesGetEntries_RoundTrips();
    Test_Palette_GetEntries_OutOfRangeReturnsInvalidParams();
    Test_Palette_SetEntries_OutOfRangeReturnsInvalidParams();
    Test_SetPalette_OnSurface_ReturnsOk();
    Test_SetPalette_AffectsGetDCColorExpansion();

    // Group 7: DC bridge
    Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock();
    Test_GetDC_NullOutParam_ReturnsInvalidParams();

    // Group 8: lost/restore
    Test_IsLost_AlwaysReturnsNotLost();
    Test_Restore_ReturnsOkUnconditionally();

    // Group 9: logging gate regression guard
    Test_BltFast_NoUnconditionalLogOutput_WhenDebugFlagsUnset();

    if (g_failures == 0) {
        std::printf("OK: all DirectDraw tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
