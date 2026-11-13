/**
 * @file DirectPlay.cpp
 * @brief `IDirectPlay` COM-boundary object (`DirectPlayImpl`) and the public entry points
 *        (`DirectPlayCreate`, `DirectPlayEnumerateA`/`W`). See include/dplay.h for accurate
 *        per-method status tags.
 *
 * `DirectPlay2AImpl` (the real `IDirectPlay2A` implementation, the dominant class in this
 * subsystem) was split out into `DirectPlayInternal.hpp`/`DirectPlay2A.cpp` on 2026-07-19 to
 * keep one primary class-family per file, mirroring `src/directdraw/`'s split from earlier the
 * same day. See `DirectPlayInternal.hpp` for `DirectPlay2AImpl`'s full definition and the shared
 * debug-logging/GUID/session-registry helper functions.
 * @note Status: PARTIAL
 */
#include "dplay.h"
#include "DirectPlayInternal.hpp"

using namespace free_direct_directplay;

namespace {
    class DirectPlayImpl final : public IDirectPlay {
    public:
        explicit DirectPlayImpl(bool relay) : refCount_(1), relay_(relay) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            if (!ppvObject) return DPERR_INVALIDPARAMS;

            if (IsEqualGuid(riid, IID_IDirectPlay)) {
                // Same object already implements IDirectPlay: return it, not a new instance.
                AddRef();
                *ppvObject = static_cast<IDirectPlay*>(this);
                return DP_OK;
            }
            if (IsEqualGuid(riid, IID_IDirectPlay2A)) {
                // A distinct interface: hand back a freshly constructed implementation.
                // Its constructor already starts refCount_ at 1, so no extra AddRef() here.
                *ppvObject = new (std::nothrow) DirectPlay2AImpl(relay_);
                return (*ppvObject) ? DP_OK : DPERR_OUTOFMEMORY;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

    private:
        std::atomic<ULONG> refCount_;
        bool relay_;  ///< created for the "FreeDirect server" provider
    };

    // FreeDirect-internal placeholder service provider (docs/directplay-design.md Decision 1,
    // TASK-24H-0100). Not a real Microsoft service-provider GUID - this project is explicitly not
    // wire-compatible with real DirectPlay (CLAUDE.md), so there is no external registry this needs
    // to match. Distinct from IID_IDirectPlay/IID_IDirectPlay2A (dplay.h, Data1 1 and 0) so it can
    // never be mistaken for either COM interface ID if ever compared.
    const GUID kFreeDirectServiceProviderGuid = {2};

    // The local provider's name, "Local network (Free Direct)" (the "FreeDirect" provider of the design notes; renamed
    // 2026-11-13 so that players can tell it from the server provider), in both encodings the two
    // callback signatures need. free-eggbert's own EnumProvidersCallback (src/network.cpp)
    // strcpy()s this into a 100-byte buffer (NamedGUID::name, include/network.hpp) and later
    // exposes it verbatim via CNetwork::GetProviderName() to the game's UI text rendering
    // (Decision 1's own note), so it must be a short, human-readable, null-terminated string - not
    // a placeholder-looking token. It is ASCII: the games' fonts draw it as it is.
    const char kFreeDirectServiceProviderNameA[] = "Local network (Free Direct)";
    // WCHAR is uint16_t (free-api/include/winnt.h), not the native (4-byte) wchar_t on this
    // platform - a plain L"..." literal would be the wrong width, so this is spelled out
    // char-by-char instead of relying on a wide-string-literal prefix.
    const WCHAR kFreeDirectServiceProviderNameW[] = {
        'L', 'o', 'c', 'a', 'l', ' ', 'n', 'e', 't', 'w', 'o', 'r', 'k', ' ', '(', 'F', 'r', 'e', 'e', ' ', 'D', 'i', 'r', 'e', 'c', 't', ')', 0
    };

    // The "FreeDirect server" provider (docs/directplay-design.md Decision 30): Internet play
    // through a Free Direct server, in builds with FREE_DIRECT_ENABLE_RELAY. Listed after the
    // local provider in native builds; the only provider of a web build, where the local one
    // cannot connect anything. Shown as "Internet through a Free Direct server" (since 2026-11-13; the name the
    // SDL3 port of blupi_recon gives its own server provider).
    const GUID kFreeDirectRelayProviderGuid = {3};
    const char kFreeDirectRelayProviderNameA[] = "Internet through a Free Direct server";
    const WCHAR kFreeDirectRelayProviderNameW[] = {
        'I', 'n', 't', 'e', 'r', 'n', 'e', 't', ' ', 't', 'h', 'r', 'o', 'u', 'g', 'h', ' ', 'a', ' ', 'F', 'r', 'e', 'e', ' ', 'D', 'i', 'r', 'e', 'c', 't', ' ', 's', 'e', 'r', 'v', 'e', 'r', 0
    };

#if defined(FREE_DIRECT_ENABLE_RELAY) && defined(__EMSCRIPTEN__)
    constexpr bool kListLocalProvider = false;
#else
    constexpr bool kListLocalProvider = true;
#endif
#ifdef FREE_DIRECT_ENABLE_RELAY
    constexpr bool kListRelayProvider = true;
#else
    constexpr bool kListRelayProvider = false;
#endif
}

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext) {
    if (!lpEnumCallback) return DPERR_INVALIDPARAMS;
    // The FreeDirect-internal placeholder provider (Decision 1) - enough to satisfy
    // free-eggbert's CNetwork::CreateProvider(0), the only index its reconstructed source ever
    // constructs - and, in relay builds, the "FreeDirect server" provider after it (Decision 30;
    // a web relay build lists only that one). Returning FALSE from the callback stops the list.
    if (kListLocalProvider &&
        !lpEnumCallback(const_cast<LPGUID>(&kFreeDirectServiceProviderGuid),
                        const_cast<LPSTR>(kFreeDirectServiceProviderNameA), 1, 0, lpContext)) {
        return DP_OK;
    }
    if (kListRelayProvider) {
        lpEnumCallback(const_cast<LPGUID>(&kFreeDirectRelayProviderGuid),
                       const_cast<LPSTR>(kFreeDirectRelayProviderNameA), 1, 0, lpContext);
    }
    return DP_OK;
}

HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext) {
    if (!lpEnumCallback) return DPERR_INVALIDPARAMS;
    if (kListLocalProvider &&
        !lpEnumCallback(const_cast<LPGUID>(&kFreeDirectServiceProviderGuid),
                        const_cast<LPWSTR>(kFreeDirectServiceProviderNameW), 1, 0, lpContext)) {
        return DP_OK;
    }
    if (kListRelayProvider) {
        lpEnumCallback(const_cast<LPGUID>(&kFreeDirectRelayProviderGuid),
                       const_cast<LPWSTR>(kFreeDirectRelayProviderNameW), 1, 0, lpContext);
    }
    return DP_OK;
}

HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter) {
    if (!lplpDP) return DPERR_INVALIDPARAMS;
    *lplpDP = nullptr;
    if (pUnkOuter) return DPERR_NOAGGREGATION;
    // Any other GUID (including null, which the tests pass) is the local provider, as before.
    const bool relay = kListRelayProvider && lpGUID && IsEqualGuid(*lpGUID, kFreeDirectRelayProviderGuid);
    *lplpDP = new (std::nothrow) DirectPlayImpl(relay);
    return (*lplpDP) ? DP_OK : DPERR_OUTOFMEMORY;
}
