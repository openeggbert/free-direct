/**
 * @file directsound_nodriver_test.cpp
 * @brief Forces and verifies `DirectSoundCreate`'s graceful `DSERR_NODRIVER` failure path
 * (`plan.md` TASK-24H-0181, closing out `TASK-24H-0057`).
 *
 * `SharedAudioDevice`'s chosen SDL audio driver is sticky for the lifetime of the process once
 * `SDL_InitSubSystem(SDL_INIT_AUDIO)` first runs (nothing ever calls `SDL_QuitSubSystem`) - a
 * standalone probe confirmed this stickiness holds even across an explicit
 * `SDL_CloseAudioDevice`+`SDL_QuitSubSystem`+re-`SDL_InitSubSystem` cycle with a *different*
 * `SDL_AUDIODRIVER` value in the same process, so no in-process trick can force this path once any
 * other test in a shared binary has already initialized audio. This is therefore its own,
 * separate, minimal CTest binary - not a new test appended to `directsound_tests.cpp` - whose
 * entire purpose is to be launched as a genuinely fresh process with `SDL_AUDIODRIVER` set (via
 * this target's CTest `ENVIRONMENT` property, see tests/CMakeLists.txt) to a name SDL3 cannot
 * resolve, so its first-ever `DirectSoundCreate` call is guaranteed to hit the no-driver path.
 *
 *   cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON
 *   cmake --build <build>
 *   ctest --test-dir <build> -R directsound_nodriver_test
 */
#include <dsound.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>

int main() {
    LPDIRECTSOUND ds = nullptr;
    HRESULT hr = DirectSoundCreate(nullptr, &ds, nullptr);
    if (hr != DSERR_NODRIVER) {
        std::fprintf(stderr,
                      "FAILED: DirectSoundCreate returned 0x%08lx, expected DSERR_NODRIVER "
                      "(0x%08lx) - is SDL_AUDIODRIVER set to an unresolvable name for this test?\n",
                      static_cast<unsigned long>(static_cast<uint32_t>(hr)),
                      static_cast<unsigned long>(static_cast<uint32_t>(DSERR_NODRIVER)));
        return 1;
    }
    if (ds != nullptr) {
        std::fprintf(stderr, "FAILED: DirectSoundCreate returned DSERR_NODRIVER but ppDS was not "
                              "left null\n");
        return 1;
    }
    std::printf("OK: DirectSoundCreate returned DSERR_NODRIVER as expected.\n");
    return 0;
}
