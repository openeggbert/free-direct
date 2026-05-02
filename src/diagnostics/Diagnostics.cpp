// Free Direct internal diagnostics counters (implementation).
//
// See Diagnostics.hpp. Built unconditionally so that turning the macro on/off
// only changes which symbols actually generate calls; when the macro is off
// the .cpp is essentially empty.
#include "Diagnostics.hpp"

#ifdef FREE_DIRECT_DIAGNOSTICS

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace free_direct_diag {

Counters& Get()
{
    static Counters instance;
    return instance;
}

bool RuntimeEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = SDL_getenv("FREE_DIRECT_DIAGNOSTICS");
        cached = (v && *v && std::strcmp(v, "0") != 0) ? 1 : 0;
        if (cached) {
            // Best-effort final dump on process exit so leaked-object counts are
            // always visible even when the game terminates abnormally.
            std::atexit([]() { Snapshot("atexit"); });
            Snapshot("startup");
        }
    }
    return cached != 0;
}

void OnObjectCreated(const char* kind, int64_t everCount)
{
    if (!RuntimeEnabled()) return;
    // Auto-snapshot every 16 ever-created objects of a given kind so we can
    // observe per-world-transition growth without modifying game source.
    if ((everCount % 16) == 0) {
        char tag[64];
        std::snprintf(tag, sizeof(tag), "auto:%s=%lld", kind ? kind : "?",
                      static_cast<long long>(everCount));
        Snapshot(tag);
    }
}

namespace {

// Best-effort RSS read on Linux (returns 0 elsewhere). Heartbeat-only path.
long ReadRssKB()
{
#if defined(__linux__)
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    long rss = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    std::fclose(f);
    return rss;
#else
    return 0;
#endif
}

void EmitLine(const char* tag)
{
    Counters& c = Get();
    const long rssKb = ReadRssKB();
    SDL_Log("[FREE_DIRECT_DIAG][%s] rss=%ldKB rssMB=%.1f "
            "live: dd=%lld ddSurf=%lld/%lld/%lld sdlSurf=%lld/%lld/%lld pal=%lld/%lld/%lld clip=%lld/%lld/%lld "
            "sdlTex=%lld/%lld/%lld ds=%lld dsBuf=%lld/%lld/%lld audioStream=%lld/%lld/%lld mixChunk=%lld/%lld/%lld "
            "totals: createSurface=%lld surfaceFinalRelease=%lld lock=%lld unlock=%lld blt=%lld bltFast=%lld flip=%lld present=%lld textureUpdate=%lld "
            "win[present=%lld blt=%lld] cache: freeDirect=0 freeApi=see_FREE_API_DIAG",
        tag,
        rssKb,
        static_cast<double>(rssKb) / 1024.0,
        (long long)c.ddInstances.load(),
        (long long)c.ddSurfaces.load(),
        (long long)c.ddSurfacesEver.load(),
        (long long)c.ddSurfaceFinalReleases.load(),
        (long long)c.sdlSurfaces.load(),
        (long long)c.sdlSurfacesEver.load(),
        (long long)c.sdlSurfacesDestroyed.load(),
        (long long)c.ddPalettes.load(),
        (long long)c.ddPalettesEver.load(),
        (long long)c.ddPalettesDestroyed.load(),
        (long long)c.ddClippers.load(),
        (long long)c.ddClippersEver.load(),
        (long long)c.ddClippersDestroyed.load(),
        (long long)c.sdlTextures.load(),
        (long long)c.sdlTexturesEver.load(),
        (long long)c.sdlTexturesDestroyed.load(),
        (long long)c.dsInstances.load(),
        (long long)c.dsBuffers.load(),
        (long long)c.dsBuffersEver.load(),
        (long long)c.dsBuffersDestroyed.load(),
        (long long)c.sdlAudioStreams.load(),
        (long long)c.sdlAudioStreamsEver.load(),
        (long long)c.sdlAudioStreamsDestroyed.load(),
        (long long)c.mixChunks.load(),
        (long long)c.mixChunksEver.load(),
        (long long)c.mixChunksDestroyed.load(),
        (long long)c.ddSurfacesEver.load(),
        (long long)c.ddSurfaceFinalReleases.load(),
        (long long)c.lockCallsTotal.load(),
        (long long)c.unlockCallsTotal.load(),
        (long long)c.bltCallsTotal.load(),
        (long long)c.bltFastCallsTotal.load(),
        (long long)c.flipCallsTotal.load(),
        (long long)c.presentCallsTotal.load(),
        (long long)c.sdlTextureUpdateCallsTotal.load(),
        (long long)c.presentsThisWindow.load(),
        (long long)c.bltCallsThisWindow.load());
}

} // namespace

void HeartbeatTick()
{
    if (!RuntimeEnabled()) return;
    static std::atomic<uint64_t> lastNs{0};
    const uint64_t now = SDL_GetTicksNS();
    uint64_t prev = lastNs.load(std::memory_order_relaxed);
    if (prev != 0 && now - prev < 5000000000ULL) return;
    if (!lastNs.compare_exchange_strong(prev, now)) return;
    EmitLine("periodic5s");
    Counters& c = Get();
    c.presentsThisWindow.store(0, std::memory_order_relaxed);
    c.bltCallsThisWindow.store(0, std::memory_order_relaxed);
}

void Snapshot(const char* tag)
{
    if (!RuntimeEnabled()) return;
    EmitLine(tag ? tag : "snapshot");
}

} // namespace free_direct_diag

#endif // FREE_DIRECT_DIAGNOSTICS
