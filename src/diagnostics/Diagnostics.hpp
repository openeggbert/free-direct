// Free Direct internal diagnostics counters.
//
// Compile-time-gated by FREE_DIRECT_DIAGNOSTICS. When the macro is not defined,
// every helper compiles to a no-op so there is zero runtime cost in release
// builds. When defined, counters are kept as atomic<int64_t> and a one-line
// heartbeat snapshot can be emitted via DiagHeartbeatTick() once per second
// when the env var FREE_DIRECT_DIAGNOSTICS=1 is set.
//
// This header is intentionally PRIVATE: it is not exposed in any public API
// (ddraw.h / dsound.h / dplay.h) and must never appear in installed headers.
//
// Usage:
//   #include "Diagnostics.hpp"
//   FREE_DIRECT_DIAG_INC(ddSurfaces);
//   FREE_DIRECT_DIAG_DEC(ddSurfaces);
//   FREE_DIRECT_DIAG_INC_TOTAL(ddSurfacesEverCreated);
//   FREE_DIRECT_DIAG_HEARTBEAT();   // typically called from PresentPrimary
#pragma once

#ifdef FREE_DIRECT_DIAGNOSTICS

#include <atomic>
#include <cstdint>

namespace free_direct_diag {

struct Counters {
    // live (currently-alive) object counts
    std::atomic<int64_t> ddInstances{0};
    std::atomic<int64_t> ddSurfaces{0};
    std::atomic<int64_t> sdlSurfaces{0};
    std::atomic<int64_t> ddPalettes{0};
    std::atomic<int64_t> ddClippers{0};
    std::atomic<int64_t> sdlTextures{0};
    std::atomic<int64_t> sdlAudioStreams{0};
    std::atomic<int64_t> dsBuffers{0};
    std::atomic<int64_t> mixChunks{0};
    std::atomic<int64_t> dsInstances{0};

    // cumulative-ever-created counters (monotonic)
    std::atomic<int64_t> ddSurfacesEver{0};
    std::atomic<int64_t> sdlSurfacesEver{0};
    std::atomic<int64_t> ddPalettesEver{0};
    std::atomic<int64_t> ddClippersEver{0};
    std::atomic<int64_t> sdlTexturesEver{0};
    std::atomic<int64_t> sdlAudioStreamsEver{0};
    std::atomic<int64_t> dsBuffersEver{0};
    std::atomic<int64_t> mixChunksEver{0};

    // cumulative destroy/final-release counters (monotonic)
    std::atomic<int64_t> ddSurfaceFinalReleases{0};
    std::atomic<int64_t> sdlSurfacesDestroyed{0};
    std::atomic<int64_t> ddPalettesDestroyed{0};
    std::atomic<int64_t> ddClippersDestroyed{0};
    std::atomic<int64_t> sdlTexturesDestroyed{0};
    std::atomic<int64_t> sdlAudioStreamsDestroyed{0};
    std::atomic<int64_t> dsBuffersDestroyed{0};
    std::atomic<int64_t> mixChunksDestroyed{0};

    // cumulative API activity counters (monotonic)
    std::atomic<int64_t> lockCallsTotal{0};
    std::atomic<int64_t> unlockCallsTotal{0};
    std::atomic<int64_t> bltCallsTotal{0};
    std::atomic<int64_t> bltFastCallsTotal{0};
    std::atomic<int64_t> flipCallsTotal{0};
    std::atomic<int64_t> presentCallsTotal{0};
    std::atomic<int64_t> sdlTextureUpdateCallsTotal{0};

    // per-frame counters (reset by heartbeat)
    std::atomic<int64_t> presentsThisWindow{0};
    std::atomic<int64_t> bltCallsThisWindow{0};
};

Counters& Get();

// True if FREE_DIRECT_DIAGNOSTICS=1 in env (cached after first call).
bool RuntimeEnabled();

// Should be called once per Present (or per main-loop tick). Emits a single
// SDL_Log line approximately once per second when RuntimeEnabled().
void HeartbeatTick();

// Force snapshot to log immediately (used at shutdown via atexit).
void Snapshot(const char* tag);

// Called from CreateSurface / CreateSoundBuffer / CreatePalette / CreateTexture
// hot paths; emits an automatic snapshot every 16 objects of the given kind so
// world-transition growth is observable without modifying game source.
void OnObjectCreated(const char* kind, int64_t everCount);

} // namespace free_direct_diag

#define FREE_DIRECT_DIAG_INC(field) \
    do { ::free_direct_diag::Get().field.fetch_add(1, std::memory_order_relaxed); } while (0)
#define FREE_DIRECT_DIAG_DEC(field) \
    do { ::free_direct_diag::Get().field.fetch_sub(1, std::memory_order_relaxed); } while (0)
#define FREE_DIRECT_DIAG_INC_TOTAL(field) \
    do { ::free_direct_diag::Get().field.fetch_add(1, std::memory_order_relaxed); } while (0)
#define FREE_DIRECT_DIAG_HEARTBEAT() ::free_direct_diag::HeartbeatTick()
#define FREE_DIRECT_DIAG_SNAPSHOT(tag) ::free_direct_diag::Snapshot(tag)
// Increment total ever-created counter AND optionally auto-snapshot.
#define FREE_DIRECT_DIAG_INC_EVER(field, kind) \
    do { \
        const int64_t v_ = ::free_direct_diag::Get().field.fetch_add(1, std::memory_order_relaxed) + 1; \
        ::free_direct_diag::OnObjectCreated((kind), v_); \
    } while (0)

#else // !FREE_DIRECT_DIAGNOSTICS

#define FREE_DIRECT_DIAG_INC(field) do {} while (0)
#define FREE_DIRECT_DIAG_DEC(field) do {} while (0)
#define FREE_DIRECT_DIAG_INC_TOTAL(field) do {} while (0)
#define FREE_DIRECT_DIAG_HEARTBEAT() do {} while (0)
#define FREE_DIRECT_DIAG_SNAPSHOT(tag) do {} while (0)
#define FREE_DIRECT_DIAG_INC_EVER(field, kind) do {} while (0)

#endif
