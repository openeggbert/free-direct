/**
 * @file integration_tests.cpp
 * @brief DirectDraw + DirectSound combined-usage tests (`plan.md` TASK-24H-0180).
 *
 * Both target games (`../free-eggbert`, `../planetblupi`) call `DirectDrawCreate` and
 * `DirectSoundCreate` unconditionally at startup, so both subsystems are simultaneously live for
 * the entire duration of every real game session. `directdraw_tests.cpp` and
 * `directsound_tests.cpp` each test their own subsystem in a fully isolated binary, and
 * `src/Main.cpp` (the demo) only ever calls `DirectDrawCreate` - so, until this file, the one
 * scenario that is true 100% of the time in real usage had zero test coverage anywhere in this
 * repo. This file closes that gap. It is not a whitebox test of either subsystem's internals (both
 * `DirectDrawImpl`/`DirectDrawSurfaceImpl` and `DirectSoundImpl`/`DirectSoundBufferImpl` live in
 * anonymous namespaces with no separate header, same as `directdraw_tests.cpp`/
 * `directsound_tests.cpp`) - every test below goes through the real public `IDirectDraw*`/
 * `IDirectSound*` interfaces only, exactly like its two single-subsystem siblings.
 *
 * Needs the real `free-direct` library (SDL3 video+audio + free-api), so it is only built through
 * CMake, same as `directdraw_tests.cpp`/`directsound_tests.cpp`:
 *
 *   cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON
 *   cmake --build <build>
 *   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest --test-dir <build> -L integration
 *
 * Both `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` are required (set via this target's
 * CTest `ENVIRONMENT` property, see tests/CMakeLists.txt) - every test here runs headlessly.
 *
 * Including both `ddraw.h` and `dsound.h` in one translation unit is not a novel combination this
 * file invents: both target games' own `src/misc.cpp` already do exactly this in real shipped game
 * code, so this is a proven-safe combination, not an assumption.
 */
#include <ddraw.h>
#include <dsound.h>
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

// TestHelpers.hpp's own functions use CHECK, so it must be included after the macro above is
// defined (see that header's own "Contract" note). Every helper this file used to define locally
// (CreateDirectDrawNoWindow/CreateTestWindow/CreatePrimarySurface/CreateOffscreenSurface/
// ReadPresentedPixel/CreateDirectSoundNoWindow/CreatePcmBuffer) is now shared (TASK-24H-0185).
// This also fixes a real inconsistency this file's own prior local copy of CreateOffscreenSurface
// had: it hardcoded DDPF_RGB unconditionally instead of matching directdraw_tests.cpp's original
// bpp-conditional DDPF_PALETTEINDEXED8/DDPF_RGB logic - see TestHelpers.hpp's own top-of-file
// comment for the correction on what that inconsistency actually did (and didn't) affect.
#include "TestHelpers.hpp"
using namespace free_direct_test_helpers;

// Mirrors both target games' actual unconditional startup sequence
// (`free-eggbert/src/pixmap.cpp:178` DirectDrawCreate, `free-eggbert/src/sound.cpp:373`
// DirectSoundCreate; `planetblupi` calls the same two in its own startup path) - not a
// hypothetical ordering, the one every real game session actually uses.
void Test_DirectDrawAndDirectSound_BothCreateSuccessfully_InSameProcess() {
    LPDIRECTDRAW dd = nullptr;
    CHECK(DirectDrawCreate(nullptr, &dd, nullptr) == DD_OK);
    CHECK(dd != nullptr);

    LPDIRECTSOUND ds = nullptr;
    CHECK(DirectSoundCreate(nullptr, &ds, nullptr) == DS_OK);
    CHECK(ds != nullptr);

    ds->Release();
    dd->Release();
}

// Regression coverage for the now-shared CreateOffscreenSurface, exercised from this translation
// unit specifically (not just directdraw_tests.cpp, which already covers 8-bit surfaces
// extensively). NOT a test of the dwFlags divergence TASK-24H-0185 fixed in CreateOffscreenSurface
// itself - tried that first, and found (by reading DirectDraw.cpp directly) that CreateSurface
// never reads ddpfPixelFormat.dwFlags from the caller at all, and GetSurfaceDesc derives the
// dwFlags it reports purely from the surface's own internal bpp_ - so a GetSurfaceDesc-based check
// would report DDPF_PALETTEINDEXED8 correctly regardless of what CreateOffscreenSurface's own
// descriptor said, and could never have caught that divergence. See TestHelpers.hpp's top-of-file
// comment for the full correction. This test instead confirms genuine 8-bit behavior (correct bit
// count and pitch) survives the consolidation.
void Test_CreateOffscreenSurface_8Bit_HasCorrectBitDepthAndPitch() {
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    LPDIRECTDRAWSURFACE surface = CreateOffscreenSurface(dd, 16, 16, 8);

    DDSURFACEDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(surface->GetSurfaceDesc(&desc) == DD_OK);
    CHECK(desc.ddpfPixelFormat.dwFlags == DDPF_PALETTEINDEXED8); // GetSurfaceDesc's own derivation
    CHECK(desc.ddpfPixelFormat.dwRGBBitCount == 8);
    CHECK(desc.lPitch == 16); // 8bpp: 1 byte/pixel

    surface->Release();
    dd->Release();
}

// The core coexistence test: a real DirectDraw BltFast+present and a real DirectSound Play() are
// interleaved in the same process, and both are verified to actually work correctly (not just
// return DD_OK/DS_OK) - a genuine rendered-pixel readback for DirectDraw, a genuine
// DSBSTATUS_PLAYING check for DirectSound - while the other subsystem is simultaneously live.
void Test_DirectDrawBltFastAndPresent_WorksCorrectly_WhileDirectSoundBufferIsPlaying() {
    HWND hwnd = CreateTestWindow();
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL) == DD_OK);

    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 44100, 16, 1, 22050); // ~1s of 16-bit mono

    // Start audio playing first, then perform the DirectDraw draw+present while it's active.
    CHECK(buf->Play(0, 0, 0) == DS_OK);

    LPDIRECTDRAWSURFACE primary = CreatePrimarySurface(dd);
    // 32x32 (not a tiny 4x4) and read back at physical (10,10) - not (0,0) - matching every other
    // ReadPresentedPixel test in directdraw_tests.cpp (e.g. Test_Flip_PresentsPrimarySurface):
    // physical (0,0) sits exactly on the letterbox/scale-math corner and is not a reliable sample
    // point, established convention in this codebase avoids it even for a full-surface fill.
    LPDIRECTDRAWSURFACE src = CreateOffscreenSurface(dd, 32, 32, 32);
    DDSURFACEDESC srcDesc{};
    std::memset(&srcDesc, 0, sizeof(srcDesc));
    srcDesc.dwSize = sizeof(DDSURFACEDESC);
    CHECK(src->Lock(nullptr, &srcDesc, 0, nullptr) == DD_OK);
    auto* srcPixels = static_cast<uint32_t*>(srcDesc.lpSurface);
    for (int i = 0; i < 32 * 32; ++i) srcPixels[i] = 0x00336699u; // solid RGB(0x99,0x66,0x33)
    src->Unlock(srcDesc.lpSurface);

    CHECK(primary->BltFast(0, 0, src, nullptr, DDBLTFAST_NOCOLORKEY) == DD_OK);

    // DirectSound must still report playing after the DirectDraw operations above ran.
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0);

    // DirectDraw's present must have actually drawn the right pixel, not merely returned DD_OK,
    // proving the two subsystems' presence together didn't corrupt either one's real output.
    // Per-channel extraction (not a packed hex-literal comparison) matches
    // Test_Flip_PresentsPrimarySurface's own established, less error-prone convention.
    const uint32_t pixel = ReadPresentedPixel(hwnd, 10, 10);
    CHECK(static_cast<uint8_t>(pixel & 0xFFu) == 0x99);         // R
    CHECK(static_cast<uint8_t>((pixel >> 8) & 0xFFu) == 0x66);  // G
    CHECK(static_cast<uint8_t>((pixel >> 16) & 0xFFu) == 0x33); // B

    src->Release();
    primary->Release();
    buf->Release();
    ds->Release();
    dd->Release();
    DestroyWindow(hwnd);
}

// Teardown-order test #1: releasing DirectSound first must not disturb DirectDraw, which is still
// live and must keep working correctly afterward.
void Test_ReleaseDirectSoundFirst_DirectDrawStillPresentsCorrectly() {
    HWND hwnd = CreateTestWindow();
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL) == DD_OK);

    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);
    CHECK(buf->Play(0, 0, 0) == DS_OK);

    buf->Release();
    ds->Release(); // DirectSound fully torn down; DirectDraw must be unaffected below.

    LPDIRECTDRAWSURFACE primary = CreatePrimarySurface(dd);
    DDBLTFX fx{};
    std::memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(DDBLTFX);
    fx.dwFillColor = 0x00112233u;
    CHECK(primary->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL, &fx) == DD_OK);

    const uint32_t pixel = ReadPresentedPixel(hwnd, 10, 10); // see the (10,10)-not-(0,0) note above
    CHECK(static_cast<uint8_t>(pixel & 0xFFu) == 0x11);         // R
    CHECK(static_cast<uint8_t>((pixel >> 8) & 0xFFu) == 0x22);  // G
    CHECK(static_cast<uint8_t>((pixel >> 16) & 0xFFu) == 0x33); // B

    primary->Release();
    dd->Release();
    DestroyWindow(hwnd);
}

// Teardown-order test #2: the reverse ordering - releasing DirectDraw first must not disturb a
// still-live DirectSound buffer's ability to keep reporting/playing correctly afterward.
void Test_ReleaseDirectDrawFirst_DirectSoundStillPlaysCorrectly() {
    HWND hwnd = CreateTestWindow();
    LPDIRECTDRAW dd = CreateDirectDrawNoWindow();
    CHECK(dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL) == DD_OK);
    LPDIRECTDRAWSURFACE primary = CreatePrimarySurface(dd);

    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 44100, 16, 1, 22050);

    primary->Release();
    dd->Release();
    DestroyWindow(hwnd); // DirectDraw fully torn down; DirectSound must be unaffected below.

    CHECK(buf->Play(0, 0, 0) == DS_OK);
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0);

    buf->Release();
    ds->Release();
}

int main() {
    Test_DirectDrawAndDirectSound_BothCreateSuccessfully_InSameProcess();
    Test_CreateOffscreenSurface_8Bit_HasCorrectBitDepthAndPitch();
    Test_DirectDrawBltFastAndPresent_WorksCorrectly_WhileDirectSoundBufferIsPlaying();
    Test_ReleaseDirectSoundFirst_DirectDrawStillPresentsCorrectly();
    Test_ReleaseDirectDrawFirst_DirectSoundStillPlaysCorrectly();

    if (g_failures == 0) {
        std::printf("OK: all integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
