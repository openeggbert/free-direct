/**
 * @file directsound_tests.cpp
 * @brief Standalone DirectSound unit tests (`plan.md`'s 24-Hour Stabilization Backlog,
 * TASK-24H-0056 onward).
 *
 * Like `directdraw_tests.cpp`, this needs the real `free-direct` library (SDL3 audio + free-api),
 * so it is only built through CMake:
 *
 *   cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON
 *   cmake --build <build>
 *   SDL_AUDIODRIVER=dummy ctest --test-dir <build> -L directsound
 *
 * `SDL_AUDIODRIVER=dummy` is required (set via this target's CTest `ENVIRONMENT` property, see
 * tests/CMakeLists.txt) so every test runs headlessly with no real audio hardware.
 *
 * Design notes that shaped this file:
 * - `DirectSoundImpl`/`DirectSoundBufferImpl` live in an anonymous namespace inside
 *   `DirectSound.cpp` with no separate header - every test goes through the real public
 *   `IDirectSound`/`IDirectSoundBuffer` interfaces only, same as `directdraw_tests.cpp`.
 * - `DSBUFFERDESC::lpwfxFormat` MUST point to a `PCMWAVEFORMAT`, not a `WAVEFORMATEX` -
 *   `DirectSoundBufferImpl`'s constructor reads it as `const PCMWAVEFORMAT*` specifically
 *   (`DirectSound.cpp`'s own comment: reading it as `WAVEFORMATEX*` would read struct padding as
 *   `wBitsPerSample` and silently produce 0-bit audio). `CreatePcmBuffer()` below builds a real
 *   `PCMWAVEFORMAT` for every test that needs a working format.
 * - `SharedAudioDevice` is a process-wide singleton, ref-counted by live `IDirectSound` objects.
 *   Its chosen SDL audio driver is sticky for the lifetime of the process once first initialized
 *   (`SDL_InitSubSystem`/`SDL_OpenAudioDevice` are called from `open()`; nothing ever calls
 *   `SDL_QuitSubSystem`) - so `SDL_AUDIODRIVER` must be `dummy` *before* the first
 *   `DirectSoundCreate` call in this process, which the CTest `ENVIRONMENT` property guarantees.
 *   This is also why this file does not attempt to force `DirectSoundCreate`'s `DSERR_NODRIVER`
 *   path in-process: forcing a driver-open failure after a real driver is already selected isn't
 *   reliably possible without a subprocess harness this file doesn't have. The graceful-failure
 *   code path (`SharedAudioDevice::open()` returning false) is straightforward to read directly
 *   from `DirectSound.cpp` instead.
 */
#include <dsound.h>
#include <windows.h>
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

LPDIRECTSOUND CreateDirectSoundNoWindow() {
    LPDIRECTSOUND ds = nullptr;
    CHECK(DirectSoundCreate(nullptr, &ds, nullptr) == DS_OK);
    return ds;
}

// Builds a real PCMWAVEFORMAT (not WAVEFORMATEX - see file header comment) and creates a buffer
// with it. Matches README's documented supported-format table: 8-bit unsigned / 16-bit signed
// LE, mono/stereo, 11025/22050/44100 Hz.
LPDIRECTSOUNDBUFFER CreatePcmBuffer(LPDIRECTSOUND ds, DWORD bufferBytes, WORD bits, WORD channels,
                                     DWORD freq) {
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

} // namespace

// ===== DirectSoundCreate =====

void Test_DirectSoundCreate_ReturnsOk() {
    LPDIRECTSOUND ds = nullptr;
    CHECK(DirectSoundCreate(nullptr, &ds, nullptr) == DS_OK);
    CHECK(ds != nullptr);
    ds->Release();
}

void Test_DirectSoundCreate_NullOutParam_ReturnsInvalidParam() {
    CHECK(DirectSoundCreate(nullptr, nullptr, nullptr) == DSERR_INVALIDPARAM);
}

void Test_DirectSoundCreate_NonNullOuter_ReturnsInvalidParam() {
    LPDIRECTSOUND ds = nullptr;
    int dummyOuter = 0;
    CHECK(DirectSoundCreate(nullptr, &ds, reinterpret_cast<IUnknown*>(&dummyOuter)) == DSERR_INVALIDPARAM);
}

// ===== SetCooperativeLevel =====

void Test_SetCooperativeLevel_NormalReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    CHECK(ds->SetCooperativeLevel(nullptr, DSSCL_NORMAL) == DS_OK);
    ds->Release();
}

// ===== CreateSoundBuffer =====

void Test_CreateSoundBuffer_16BitMono22050_ReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050); // 0.1s of 16-bit mono
    CHECK(buf != nullptr);
    buf->Release();
    ds->Release();
}

void Test_CreateSoundBuffer_8BitStereo11025_ReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 2205, 8, 2, 11025);
    CHECK(buf != nullptr);
    buf->Release();
    ds->Release();
}

void Test_CreateSoundBuffer_NullOutParam_ReturnsInvalidParam() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    DSBUFFERDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DSBUFFERDESC);
    CHECK(ds->CreateSoundBuffer(&desc, nullptr, nullptr) == DSERR_INVALIDPARAM);
    ds->Release();
}

void Test_CreateSoundBuffer_NonNullOuter_ReturnsInvalidParam() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    DSBUFFERDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DSBUFFERDESC);
    LPDIRECTSOUNDBUFFER buf = nullptr;
    int dummyOuter = 0;
    CHECK(ds->CreateSoundBuffer(&desc, &buf, reinterpret_cast<IUnknown*>(&dummyOuter)) == DSERR_INVALIDPARAM);
    ds->Release();
}

// Real, documented behavior (DirectSoundBufferImpl's constructor: `if (!desc) return;`) - a null
// descriptor is accepted, not rejected, producing a valid-but-empty (zero bufferBytes) buffer
// object. This is the "invalid descriptor" ask's null-descriptor case; the real implementation's
// answer is "degenerate but not an error," which this test locks in rather than assumes.
void Test_CreateSoundBuffer_NullDescriptor_ReturnsOkWithEmptyBuffer() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = nullptr;
    CHECK(ds->CreateSoundBuffer(nullptr, &buf, nullptr) == DS_OK);
    CHECK(buf != nullptr);
    // Play() on this degenerate buffer must be a safe no-op (bufferBytes_ == 0 path).
    CHECK(buf->Play(0, 0, 0) == DS_OK);
    buf->Release();
    ds->Release();
}

void Test_CreateSoundBuffer_ZeroSizedBuffer_ReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 0, 16, 1, 22050);
    CHECK(buf != nullptr);
    CHECK(buf->Play(0, 0, 0) == DS_OK); // safe no-op, matches the null-descriptor test above
    buf->Release();
    ds->Release();
}

// README: "unsupported formats fall back to a safe 16-bit mono 22050 Hz default." A null
// lpwfxFormat is the sharpest case of this - DirectSoundBufferImpl's constructor skips format
// parsing entirely, leaving srcSpec_ all-zero, and ensureStream() (called from Play()) detects
// srcSpec_.freq == 0 and substitutes the documented default rather than crashing.
void Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    DSBUFFERDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwBufferBytes = 1000;
    desc.lpwfxFormat = nullptr; // no format supplied at all

    LPDIRECTSOUNDBUFFER buf = nullptr;
    CHECK(ds->CreateSoundBuffer(&desc, &buf, nullptr) == DS_OK);
    CHECK(buf->Play(0, 0, 0) == DS_OK); // must not crash; falls back to 16-bit mono 22050 Hz

    buf->Release();
    ds->Release();
}

// docs/audit_dsound.md §6.1 (S1), TASK-24H-0164: dwBufferBytes must be bounded before it reaches
// an unguarded std::vector::resize inside DirectSoundBufferImpl's constructor - a value near
// DWORD max must be rejected outright, not attempted (which would otherwise throw uncaught,
// crashing the test process).
void Test_CreateSoundBuffer_HugeBufferBytes_ReturnsInvalidParam() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    DSBUFFERDESC desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwBufferBytes = 0xFFFFFFFFu;
    desc.lpwfxFormat = nullptr;

    LPDIRECTSOUNDBUFFER buf = reinterpret_cast<LPDIRECTSOUNDBUFFER>(1); // poison, must be nulled
    CHECK(ds->CreateSoundBuffer(&desc, &buf, nullptr) == DSERR_INVALIDPARAM);
    CHECK(buf == nullptr);

    ds->Release();
}

// docs/audit_dsound.md §7.2 (S7), TASK-24H-0169: an nSamplesPerSec near DWORD max casts to a
// negative/nonsensical int with no bound before reaching SDL_CreateAudioStream. Must resolve to
// some defined, non-crashing outcome - falling back to the safe default, same as the
// missing-format test above.
void Test_CreateSoundBuffer_HugeSampleRate_FallsBackGracefully() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 1000, 16, 1, 0xFFFFFFFFu);
    CHECK(buf->Play(0, 0, 0) == DS_OK); // must not crash; falls back to 16-bit mono 22050 Hz

    buf->Release();
    ds->Release();
}

// ===== Lock / Unlock =====

void Test_Lock_FromWriteCursor_ReturnsFullBufferSingleRegion() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);

    LPVOID ptr1 = nullptr; DWORD bytes1 = 0;
    LPVOID ptr2 = nullptr; DWORD bytes2 = 0;
    CHECK(buf->Lock(0, 0, &ptr1, &bytes1, &ptr2, &bytes2, DSBLOCK_FROMWRITECURSOR) == DS_OK);
    CHECK(ptr1 != nullptr);
    CHECK(bytes1 == 100); // whole buffer, regardless of the (ignored) offset/bytes args
    CHECK(ptr2 == nullptr);
    CHECK(bytes2 == 0);

    buf->Unlock(ptr1, bytes1, ptr2, bytes2);
    buf->Release();
    ds->Release();
}

// bufferBytes_=10, offset=8, requested=5 -> straddles the end (8+5=13 > 10): region1 covers the
// 2 remaining bytes at the end, region2 wraps to the 3 remaining bytes at the start.
void Test_Lock_WraparoundRegion_ReturnsTwoValidRegions() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 10, 16, 1, 22050);

    LPVOID ptr1 = nullptr; DWORD bytes1 = 0;
    LPVOID ptr2 = nullptr; DWORD bytes2 = 0;
    CHECK(buf->Lock(8, 5, &ptr1, &bytes1, &ptr2, &bytes2, 0) == DS_OK);
    CHECK(ptr1 != nullptr);
    CHECK(bytes1 == 2);
    CHECK(ptr2 != nullptr);
    CHECK(bytes2 == 3);

    buf->Unlock(ptr1, bytes1, ptr2, bytes2);
    buf->Release();
    ds->Release();
}

void Test_LockUnlock_WrittenDataUsedByPlay() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 200, 16, 1, 22050);

    LPVOID ptr = nullptr; DWORD bytes = 0;
    CHECK(buf->Lock(0, 0, &ptr, &bytes, nullptr, nullptr, DSBLOCK_FROMWRITECURSOR) == DS_OK);
    std::memset(ptr, 0x55, bytes); // non-silent PCM pattern
    buf->Unlock(ptr, bytes, nullptr, 0);

    CHECK(buf->Play(0, 0, 0) == DS_OK);
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0); // the written data was fed to the stream and is playing

    buf->Release();
    ds->Release();
}

// ===== Play / Stop / GetStatus =====

void Test_Play_SetsPlayingStatus() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);

    CHECK(buf->Play(0, 0, 0) == DS_OK);
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0);

    buf->Release();
    ds->Release();
}

void Test_Stop_ClearsPlayingStatus() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);

    CHECK(buf->Play(0, 0, 0) == DS_OK);
    CHECK(buf->Stop() == DS_OK);
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) == 0);

    buf->Release();
    ds->Release();
}

void Test_GetStatus_FreshBuffer_NotPlaying() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);

    DWORD status = 0xFFFFFFFFu;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) == 0);

    buf->Release();
    ds->Release();
}

void Test_GetStatus_NullOutParam_ReturnsInvalidParam() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);
    CHECK(buf->GetStatus(nullptr) == DSERR_INVALIDPARAM);
    buf->Release();
    ds->Release();
}

// "Play() always restarts" (docs/directsound-limitations.md): calling Play() a second time on an
// already-playing buffer must not error, and the buffer must still report DSBSTATUS_PLAYING
// afterward - proving the restart path (SDL_ClearAudioStream + re-feed) works, not just the
// first-ever Play() call.
void Test_Play_CalledTwiceInARow_StillReportsPlaying() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);

    CHECK(buf->Play(0, 0, 0) == DS_OK);
    CHECK(buf->Play(0, 0, 0) == DS_OK); // restart while already playing
    DWORD status = 0;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0);

    buf->Release();
    ds->Release();
}

// Real code path: `Stop()` only calls `SDL_ClearAudioStream` `if (stream_)` - a buffer that was
// never Play()'d has a null stream_, so Stop() must be a safe no-op, not a crash or error.
void Test_Stop_OnNeverPlayedBuffer_IsSafeNoOp() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);

    CHECK(buf->Stop() == DS_OK);
    DWORD status = 0xFFFFFFFFu;
    CHECK(buf->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) == 0);

    buf->Release();
    ds->Release();
}

// Buffer independence: two distinct buffers created from the same IDirectSound device must be
// able to play simultaneously without interfering with each other's status - matching real
// gameplay (multiple sound effects overlapping), each IDirectSoundBuffer owns its own
// SDL_AudioStream (DirectSound.cpp), not a shared one.
void Test_TwoBuffers_PlaySimultaneously_BothReportPlayingIndependently() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER bufA = CreatePcmBuffer(ds, 4410, 16, 1, 22050);
    LPDIRECTSOUNDBUFFER bufB = CreatePcmBuffer(ds, 4410, 8, 2, 11025); // deliberately different format

    CHECK(bufA->Play(0, 0, 0) == DS_OK);
    CHECK(bufB->Play(0, 0, 0) == DS_OK);

    DWORD statusA = 0, statusB = 0;
    CHECK(bufA->GetStatus(&statusA) == DS_OK);
    CHECK(bufB->GetStatus(&statusB) == DS_OK);
    CHECK((statusA & DSBSTATUS_PLAYING) != 0);
    CHECK((statusB & DSBSTATUS_PLAYING) != 0);

    // Stopping one must not affect the other.
    CHECK(bufA->Stop() == DS_OK);
    CHECK(bufA->GetStatus(&statusA) == DS_OK);
    CHECK(bufB->GetStatus(&statusB) == DS_OK);
    CHECK((statusA & DSBSTATUS_PLAYING) == 0);
    CHECK((statusB & DSBSTATUS_PLAYING) != 0);

    bufA->Release();
    bufB->Release();
    ds->Release();
}

// Realistic lifetime pattern for both target games: IDirectSound stays alive for the whole
// session while individual sound buffers are created and released over time. Releasing one
// buffer must not affect the shared audio device's availability for a buffer created afterward.
void Test_ReleaseBuffer_ThenCreateAndPlayAnother_OnSameDevice_StillWorks() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();

    LPDIRECTSOUNDBUFFER first = CreatePcmBuffer(ds, 4410, 16, 1, 22050);
    CHECK(first->Play(0, 0, 0) == DS_OK);
    first->Release();

    LPDIRECTSOUNDBUFFER second = CreatePcmBuffer(ds, 4410, 16, 1, 22050);
    CHECK(second->Play(0, 0, 0) == DS_OK);
    DWORD status = 0;
    CHECK(second->GetStatus(&status) == DS_OK);
    CHECK((status & DSBSTATUS_PLAYING) != 0);

    second->Release();
    ds->Release();
}

// docs/directsound-limitations.md ("Real device close/reopen cost..."), TASK-24H-0165: a
// sole-owner create/release cycle (the real SDL device actually closes and reopens each time,
// measured at ~51ms/cycle in docs/audit_dsound.md §8.3) must still complete without error or
// hang - no timing bound asserted here, only correctness, since exact timing is too
// environment-dependent for CI.
void Test_DirectSoundCreate_SoleOwnerCreateReleaseCycle_CompletesWithoutError() {
    for (int i = 0; i < 3; ++i) {
        LPDIRECTSOUND ds = nullptr;
        CHECK(DirectSoundCreate(nullptr, &ds, nullptr) == DS_OK);
        CHECK(ds != nullptr);
        CHECK(ds->Release() == 0); // sole owner - this really closes the SDL device
    }
}

// ===== AddRef / Release lifetime =====

void Test_DirectSound_AddRefRelease_AdjustsRefCount() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    CHECK(ds->AddRef() == 2);
    CHECK(ds->Release() == 1);
    CHECK(ds->Release() == 0); // final release - ds must not be touched after this
}

void Test_DirectSoundBuffer_AddRefRelease_AdjustsRefCount() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);
    CHECK(buf->AddRef() == 2);
    CHECK(buf->Release() == 1);
    CHECK(buf->Release() == 0); // final release - buf must not be touched after this
    ds->Release();
}

// ===== SetCurrentPosition =====

// The only value either target game ever passes (docs/audit-24h-free-direct.md §2.2) - a rewind
// to the start before the next Play().
void Test_SetCurrentPosition_Zero_ReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);
    CHECK(buf->SetCurrentPosition(0) == DS_OK);
    buf->Release();
    ds->Release();
}

// Real code path: `if (dwNewPosition >= bufferBytes_) dwNewPosition = 0;` - an out-of-range
// position is clamped back to 0 rather than rejected. Not directly observable via a getter, but
// verifiable indirectly: Play() after this must still succeed (no out-of-bounds read from
// data_.data() + playCursor_).
void Test_SetCurrentPosition_OutOfRange_ClampsAndPlaySucceeds() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);
    CHECK(buf->SetCurrentPosition(100000) == DS_OK); // far past bufferBytes_ (100)
    CHECK(buf->Play(0, 0, 0) == DS_OK); // would read out-of-bounds if not clamped
    buf->Release();
    ds->Release();
}

// ===== SetVolume =====

// No public getter exists for the stored volume (same observability limit as several DirectPlay
// session fields) - this can only verify that out-of-range values are accepted gracefully
// (DS_OK, no crash), matching real DirectSound's silent-clamp behavior, not that a specific
// clamped value round-trips.
void Test_SetVolume_OutOfRangeValues_ReturnOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050);
    CHECK(buf->SetVolume(-999999) == DS_OK); // far below DSBVOLUME_MIN (-10000)
    CHECK(buf->SetVolume(999999) == DS_OK);  // far above DSBVOLUME_MAX (0)
    CHECK(buf->SetVolume(0) == DS_OK);
    CHECK(buf->SetVolume(-10000) == DS_OK);
    buf->Release();
    ds->Release();
}

// ===== SetPan =====

void Test_SetPan_MonoSource_NeverCrashesAcrossFullRange() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 1, 22050); // mono source
    CHECK(buf->SetPan(-10000) == DS_OK); // DSBPAN_LEFT
    CHECK(buf->SetPan(0) == DS_OK);      // center
    CHECK(buf->SetPan(10000) == DS_OK);  // DSBPAN_RIGHT
    CHECK(buf->SetPan(-999999) == DS_OK); // out of range, must clamp not crash
    CHECK(buf->SetPan(999999) == DS_OK);
    buf->Release();
    ds->Release();
}

// Stereo sources: SetPan is documented (README) as "stored but not applied" - this locks in that
// it's still always accepted (DS_OK), not that it audibly pans (unobservable via the public API).
void Test_SetPan_StereoSource_StillReturnsOk() {
    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 100, 16, 2, 22050); // stereo source
    CHECK(buf->SetPan(-5000) == DS_OK);
    buf->Release();
    ds->Release();
}

// ===== Logging gate regression guard =====

namespace {
int g_dsLogCallCount = 0;

void CountingLogOutputFunction(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata; (void)category; (void)priority; (void)message;
    ++g_dsLogCallCount;
}
} // namespace

// 24-Hour Stabilization Backlog TASK-24H-0073 (plan.md), mirroring directdraw_tests.cpp's
// equivalent guard. Re-verifies TASK-24H-0114's audit finding (DS_LOG/DS_FMTLOG are correctly
// gated behind FREE_DIRECT_DEBUG_DSOUND/_FORMAT, unlike DirectDraw's per-call-uncached but still
// correctly gated checks) as an automated regression test across Play/Lock/Unlock.
void Test_PlayLockUnlock_NoUnconditionalLogOutput_WhenDebugFlagsUnset() {
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_DSOUND");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_DEBUG_DSOUND_FORMAT");

    SDL_LogOutputFunction prevCallback = nullptr;
    void* prevUserdata = nullptr;
    SDL_GetLogOutputFunction(&prevCallback, &prevUserdata);
    g_dsLogCallCount = 0;
    SDL_SetLogOutputFunction(CountingLogOutputFunction, nullptr);

    LPDIRECTSOUND ds = CreateDirectSoundNoWindow();
    LPDIRECTSOUNDBUFFER buf = CreatePcmBuffer(ds, 4410, 16, 1, 22050);
    for (int i = 0; i < 20; ++i) {
        LPVOID ptr = nullptr; DWORD bytes = 0;
        buf->Lock(0, 0, &ptr, &bytes, nullptr, nullptr, DSBLOCK_FROMWRITECURSOR);
        buf->Unlock(ptr, bytes, nullptr, 0);
        buf->Play(0, 0, 0);
    }
    buf->Release();
    ds->Release();

    SDL_SetLogOutputFunction(prevCallback, prevUserdata);

    CHECK(g_dsLogCallCount == 0);
}

int main() {
    Test_DirectSoundCreate_ReturnsOk();
    Test_DirectSoundCreate_NullOutParam_ReturnsInvalidParam();
    Test_DirectSoundCreate_NonNullOuter_ReturnsInvalidParam();

    Test_SetCooperativeLevel_NormalReturnsOk();

    Test_CreateSoundBuffer_16BitMono22050_ReturnsOk();
    Test_CreateSoundBuffer_8BitStereo11025_ReturnsOk();
    Test_CreateSoundBuffer_NullOutParam_ReturnsInvalidParam();
    Test_CreateSoundBuffer_NonNullOuter_ReturnsInvalidParam();
    Test_CreateSoundBuffer_NullDescriptor_ReturnsOkWithEmptyBuffer();
    Test_CreateSoundBuffer_ZeroSizedBuffer_ReturnsOk();
    Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully();
    Test_CreateSoundBuffer_HugeBufferBytes_ReturnsInvalidParam();
    Test_CreateSoundBuffer_HugeSampleRate_FallsBackGracefully();

    Test_Lock_FromWriteCursor_ReturnsFullBufferSingleRegion();
    Test_Lock_WraparoundRegion_ReturnsTwoValidRegions();
    Test_LockUnlock_WrittenDataUsedByPlay();

    Test_Play_SetsPlayingStatus();
    Test_Stop_ClearsPlayingStatus();
    Test_GetStatus_FreshBuffer_NotPlaying();
    Test_GetStatus_NullOutParam_ReturnsInvalidParam();
    Test_Play_CalledTwiceInARow_StillReportsPlaying();
    Test_Stop_OnNeverPlayedBuffer_IsSafeNoOp();
    Test_TwoBuffers_PlaySimultaneously_BothReportPlayingIndependently();
    Test_ReleaseBuffer_ThenCreateAndPlayAnother_OnSameDevice_StillWorks();
    Test_DirectSoundCreate_SoleOwnerCreateReleaseCycle_CompletesWithoutError();

    Test_DirectSound_AddRefRelease_AdjustsRefCount();
    Test_DirectSoundBuffer_AddRefRelease_AdjustsRefCount();

    Test_SetCurrentPosition_Zero_ReturnsOk();
    Test_SetCurrentPosition_OutOfRange_ClampsAndPlaySucceeds();

    Test_SetVolume_OutOfRangeValues_ReturnOk();

    Test_SetPan_MonoSource_NeverCrashesAcrossFullRange();
    Test_SetPan_StereoSource_StillReturnsOk();

    Test_PlayLockUnlock_NoUnconditionalLogOutput_WhenDebugFlagsUnset();

    if (g_failures == 0) {
        std::printf("OK: all DirectSound tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
