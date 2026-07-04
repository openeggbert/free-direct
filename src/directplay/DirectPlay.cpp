/**
 * @file DirectPlay.cpp
 * @brief Narrow DirectPlay subset reimplementation (Stubs).
 * @note Status: STUB
 */
#include "dplay.h"
#include "DirectPlaySession.hpp"
#include <atomic>
#include <cstring>
#include <new>

namespace {
    bool IsEqualGuid(const GUID& a, const GUID& b) {
        return std::memcmp(&a, &b, sizeof(GUID)) == 0;
    }

    class DirectPlay2AImpl final : public IDirectPlay2A {
    public:
        DirectPlay2AImpl() : refCount_(1) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            if (!ppvObject) return DPERR_INVALIDPARAMS;

            if (IsEqualGuid(riid, IID_IDirectPlay2A)) {
                AddRef();
                *ppvObject = static_cast<IDirectPlay2A*>(this);
                return DP_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) {
                // session_.transport is always null today (no concrete IDirectPlayTransport
                // exists until Phase 4/5), so this is currently a no-op in practice, but it is
                // the correct, safe shutdown call once a real transport is ever assigned.
                if (session_.transport) session_.transport->Shutdown();
                delete this;
            }
            return val;
        }

        HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) override {
            (void)dwTimeout; (void)lpContext; (void)dwFlags;
            // lpEnumSessionsDesc is optional (null means "enumerate everything"); its dwSize is
            // only validated when a filter descriptor is actually provided.
            if (lpEnumSessionsDesc && lpEnumSessionsDesc->dwSize != sizeof(DPSESSIONDESC2)) return DPERR_INVALIDPARAMS;
            if (!lpEnumSessionsCallback) return DPERR_INVALIDPARAMS;
            // Real session discovery lands in Phase 8 (hosting/joining don't exist yet), so
            // there is genuinely nothing to discover today: reporting zero sessions (never
            // invoking the callback) is honestly correct right now, not a placeholder stub.
            return DP_OK;
        }

        HRESULT WINAPI Open(LPDPSESSIONDESC2 lpSessionDesc, DWORD dwFlags) override {
            if (!lpSessionDesc || lpSessionDesc->dwSize != sizeof(DPSESSIONDESC2)) return DPERR_INVALIDPARAMS;
            if (dwFlags & ~static_cast<DWORD>(DPOPEN_CREATE | DPOPEN_JOIN | DPOPEN_OPENSESSION)) {
                return DPERR_INVALIDFLAGS;
            }
            // Only "already open" is rejected here; re-Open() after a real Close() is allowed
            // to proceed, matching this task's specific "already-open object" wording.
            if (session_.IsOpen()) return DPERR_ALREADYINITIALIZED;

            session_.isHost = (dwFlags & DPOPEN_CREATE) != 0;
            session_.applicationGuid = lpSessionDesc->guidApplication;
            session_.maxPlayers = lpSessionDesc->dwMaxPlayers;
            session_.currentPlayers = lpSessionDesc->dwCurrentPlayers;
            session_.sessionName.clear();
            if (lpSessionDesc->lpszSessionNameA) session_.sessionName = lpSessionDesc->lpszSessionNameA;
            session_.password.clear();
            if (lpSessionDesc->lpszPasswordA) session_.password = lpSessionDesc->lpszPasswordA;
            session_.state = free_direct_directplay::DirectPlayObjectState::Open;
            return DP_OK;
        }

        HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) override {
            (void)hEvent; (void)lpData; (void)dwDataSize; (void)dwFlags;
            // lpPlayerName is optional (a player may be created without a display name); only
            // its size is validated when one is actually provided.
            if (lpPlayerName && lpPlayerName->dwSize != sizeof(DPNAME)) return DPERR_INVALIDPARAMS;

            // Placeholder allocation strategy: a simple incrementing counter starting at 1
            // (0 is left unassigned, matching real DirectPlay's DPID_SYSMSG/DPID_ALLPLAYERS
            // convention). Phase 9 revisits this against the Phase 0 DPID-vs-array-index finding.
            const DPID newId = session_.nextPlayerId++;
            session_.localPlayerIds.push_back(newId);
            session_.currentPlayers++;
            if (lpidPlayer) *lpidPlayer = newId;
            return DP_OK;
        }

        HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) override {
            (void)idFrom; (void)idTo; (void)dwFlags; (void)lpData; (void)dwDataSize;
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // Sender/recipient player ID validation, payload validation, and actual delivery
            // all land in Phase 10.
            return DP_OK;
        }

        HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) override {
            (void)lpidFrom; (void)lpidTo; (void)dwFlags; (void)lpData; (void)lpdwDataSize;
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // DirectPlaySession has no message queue yet (Phase 3): there is genuinely nothing
            // to receive, so DPERR_NOMESSAGES is the honestly correct answer today, matching
            // exactly what free-eggbert's CNetwork::Receive checks for.
            return DPERR_NOMESSAGES;
        }

        HRESULT WINAPI Close() override {
            // Message-queue state is not cleared here yet - DirectPlaySession has no message
            // queue member until Phase 3 gives it one.
            session_.localPlayerIds.clear();
            session_.remotePlayerIds.clear();
            session_.nextPlayerId = 1;
            session_.sessionName.clear();
            session_.password.clear();
            session_.applicationGuid = GUID{};
            session_.maxPlayers = 0;
            session_.currentPlayers = 0;
            session_.isHost = false;
            session_.state = free_direct_directplay::DirectPlayObjectState::Closed;
            return DP_OK;
        }

    private:
        std::atomic<ULONG> refCount_;
        free_direct_directplay::DirectPlaySession session_;
    };

    class DirectPlayImpl final : public IDirectPlay {
    public:
        DirectPlayImpl() : refCount_(1) {}

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
                *ppvObject = new (std::nothrow) DirectPlay2AImpl();
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
    };
}

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext) {
    (void)lpEnumCallback; (void)lpContext;
    return DP_OK;
}

HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext) {
    (void)lpEnumCallback; (void)lpContext;
    return DP_OK;
}

HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter) {
    (void)lpGUID;
    if (!lplpDP) return DPERR_INVALIDPARAMS;
    *lplpDP = nullptr;
    if (pUnkOuter) return DPERR_NOAGGREGATION;
    *lplpDP = new (std::nothrow) DirectPlayImpl();
    return (*lplpDP) ? DP_OK : DPERR_OUTOFMEMORY;
}
