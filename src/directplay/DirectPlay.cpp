/**
 * @file DirectPlay.cpp
 * @brief Narrow DirectPlay subset reimplementation (Stubs).
 * @note Status: STUB
 */
#include "dplay.h"
#include "DirectPlaySession.hpp"
#include "LoopbackDirectPlayTransport.hpp"
#ifdef FREE_DIRECT_ENABLE_ENET
#include "EnetDirectPlayTransport.hpp"
#endif
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {
    bool IsEqualGuid(const GUID& a, const GUID& b) {
        return std::memcmp(&a, &b, sizeof(GUID)) == 0;
    }

    // Not a real UUID generator (no RFC 4122 version/variant bits) - FreeDirect's
    // DirectPlay is explicitly not wire-compatible with real DirectPlay (CLAUDE.md), so
    // there is no external protocol this needs to satisfy. Random bits in each named
    // field are enough to make session instance GUIDs "very likely unique" for
    // FreeDirect-to-FreeDirect sessions, which is all Open()'s guidInstance generation
    // actually needs. Fills fields individually rather than bulk-memcpy'ing a fixed byte
    // count, since GUID's Data1 is `unsigned long` - 8 bytes on this platform, not the 4
    // bytes real DirectPlay's Data1 documents, so sizeof(GUID) isn't portably 16.
    GUID GenerateSessionInstanceGuid() {
        static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::uint32_t> dist32;
        std::uniform_int_distribution<std::uint16_t> dist16;
        std::uniform_int_distribution<int> distByte(0, 255);

        GUID guid{};
        guid.Data1 = dist32(rng);
        guid.Data2 = dist16(rng);
        guid.Data3 = dist16(rng);
        for (auto& byte : guid.Data4) byte = static_cast<unsigned char>(distByte(rng));
        return guid;
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
                // Covers the case where Release() is called without a prior Close() - Open()
                // (Phase 4) now assigns a real LoopbackDirectPlayTransport, and Close() already
                // shuts it down and clears session_.transport itself, so this is a no-op then.
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
            // Only generate when hosting and the caller didn't already supply one -
            // DPOPEN_JOIN/DPOPEN_OPENSESSION callers already know the target session's
            // real instance GUID (from EnumSessions, plan.md Phase 8) and must not have
            // it silently replaced. Written back into the caller's struct, matching real
            // DirectPlay's Open() behavior for a caller-omitted instance GUID.
            if (session_.isHost && IsEqualGuid(lpSessionDesc->guidInstance, GUID{})) {
                lpSessionDesc->guidInstance = GenerateSessionInstanceGuid();
            }
            session_.sessionInstanceGuid = lpSessionDesc->guidInstance;
            session_.maxPlayers = lpSessionDesc->dwMaxPlayers;
            session_.currentPlayers = lpSessionDesc->dwCurrentPlayers;
            session_.sessionName.clear();
            if (lpSessionDesc->lpszSessionNameA) session_.sessionName = lpSessionDesc->lpszSessionNameA;
            session_.password.clear();
            if (lpSessionDesc->lpszPasswordA) session_.password = lpSessionDesc->lpszPasswordA;
            // Backend selection is build-time-only (docs/directplay-design.md Decision 4), driven
            // directly by FREE_DIRECT_ENABLE_ENET - no run-time switch, no new API parameter.
#ifdef FREE_DIRECT_ENABLE_ENET
            session_.transport = std::make_unique<free_direct_directplay::EnetDirectPlayTransport>();
            if (session_.isHost) {
                // Fixed default port (docs/directplay-design.md Decision 5) - DPSESSIONDESC2
                // has no port-like field to derive one from. Only the hosting role listens
                // here; a joining role calling Connect() is Phase 7's job, not this one's.
                if (!session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayEnetPort)) {
                    session_.transport.reset();
                    return DPERR_CANTCREATESESSION;
                }
            }
#else
            session_.transport = std::make_unique<free_direct_directplay::LoopbackDirectPlayTransport>();
#endif
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
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;

            // Only the self-send loopback path (Phase 4) is implemented so far. Sender/recipient
            // player ID validation, payload validation, host routing, and broadcast all land in
            // Phase 10; a non-self idTo is currently a silent no-op, matching the pre-Phase-4
            // stub behavior for anything this phase doesn't cover.
            if (idTo == idFrom && session_.transport) {
                // Round-trip through the transport rather than enqueuing directly, so
                // LoopbackDirectPlayTransport's own Send()/Receive() are genuinely exercised
                // (matching the eventual shape of a real backend), even though for loopback the
                // round-trip is synchronous and same-process.
                const bool reliable = (dwFlags & DPSEND_GUARANTEED) != 0;
                if (!session_.transport->Send(lpData, dwDataSize, reliable)) {
                    return DPERR_GENERIC;
                }

                std::vector<std::uint8_t> received(dwDataSize);
                std::size_t receivedSize = 0;
                if (!session_.transport->Receive(received.empty() ? nullptr : received.data(),
                                                  received.size(), &receivedSize)) {
                    return DPERR_GENERIC;
                }
                received.resize(receivedSize);

                free_direct_directplay::DirectPlayMessagePacket packet;
                packet.idFrom = idFrom;
                packet.idTo = idTo;
                packet.flags = dwFlags;
                packet.payload = std::move(received);
                if (!session_.messageQueue.Enqueue(std::move(packet))) return DPERR_SENDTOOBIG;
            }
            return DP_OK;
        }

        HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) override {
            (void)dwFlags;
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // Piggyback event servicing on the caller's own polling pattern rather than
            // adding a new API or a background thread (docs/directplay-design.md
            // Decision 6) - free-eggbert's own CNetwork::Receive() is already called
            // repeatedly from the game's loop, so this is the one call site every real
            // Receive() path already goes through. LoopbackDirectPlayTransport's
            // Service() is a no-op; EnetDirectPlayTransport's drains pending ENet events
            // (peer connect/disconnect bookkeeping) non-blockingly.
            if (session_.transport) session_.transport->Service();
            // The buffer-size-query/DPERR_NOMESSAGES/too-small/successful-copy logic lives on
            // DirectPlayMessageQueue itself (DirectPlayMessageQueue.hpp's TryReceive), so it can
            // be exercised directly by tests/directplay_tests.cpp without needing a way to
            // inject a message into a live IDirectPlay2A object.
            return session_.messageQueue.TryReceive(lpidFrom, lpidTo, lpData, lpdwDataSize);
        }

        HRESULT WINAPI Close() override {
            if (session_.transport) {
                session_.transport->Shutdown();
                session_.transport.reset();
            }
            session_.messageQueue.Clear();
            session_.localPlayerIds.clear();
            session_.remotePlayerIds.clear();
            session_.nextPlayerId = 1;
            session_.sessionName.clear();
            session_.password.clear();
            session_.applicationGuid = GUID{};
            session_.sessionInstanceGuid = GUID{};
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
