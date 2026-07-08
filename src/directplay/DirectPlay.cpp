/**
 * @file DirectPlay.cpp
 * @brief Narrow DirectPlay subset reimplementation over a real loopback (and, partially, ENet)
 *        transport. See include/dplay.h for accurate per-method status tags.
 * @note Status: PARTIAL
 */
#include "dplay.h"
#include "DirectPlaySession.hpp"
#include "DirectPlayWireProtocol.hpp"
#include "LoopbackDirectPlayTransport.hpp"
#ifdef FREE_DIRECT_ENABLE_ENET
#include "EnetDirectPlayTransport.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <unordered_map>
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

    // Process-wide static registry (docs/directplay-design.md Decision 18), keyed by the fixed
    // loopback port (docs/directplay-design.md Decisions 11/12) - lets EnumSessions() find a
    // currently-hosted loopback session's *live* DirectPlaySession directly and synchronously,
    // with no wire-protocol round-trip needed. Deliberately separate from
    // LoopbackDirectPlayTransport's own port registry (Decision 10): that one maps to a
    // transport instance for Connect() to reach; this one maps to DirectPlay-level session
    // state (applicationGuid, sessionName, player counts, ...) that the transport layer must
    // never know about. Loopback-only - a session hosted over ENet is not discoverable yet (real
    // Discovery/DiscoveryResponse wire packets, already defined in DirectPlayWireProtocol.hpp,
    // are a separate, later task for that backend).
    std::unordered_map<std::uint16_t, free_direct_directplay::DirectPlaySession*>&
    LoopbackHostedSessionRegistry() {
        static std::unordered_map<std::uint16_t, free_direct_directplay::DirectPlaySession*> registry;
        return registry;
    }

    void UnregisterHostedSession(free_direct_directplay::DirectPlaySession* session) {
        auto& registry = LoopbackHostedSessionRegistry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (it->second == session) {
                registry.erase(it);
                return;
            }
        }
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
                // Same defensive reasoning for the EnumSessions() registry (Decision 18).
                UnregisterHostedSession(&session_);
                if (session_.transport) session_.transport->Shutdown();
                delete this;
            }
            return val;
        }

        HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) override {
            // lpEnumSessionsDesc is optional (null means "enumerate everything"); its dwSize is
            // only validated when a filter descriptor is actually provided.
            if (lpEnumSessionsDesc && lpEnumSessionsDesc->dwSize != sizeof(DPSESSIONDESC2)) return DPERR_INVALIDPARAMS;
            if (!lpEnumSessionsCallback) return DPERR_INVALIDPARAMS;

            // Explicit-host-only discovery (docs/directplay-design.md Decision 18), asked of and
            // confirmed by the user - a synchronous, DirectPlay-level registry lookup, not a
            // wire-protocol round-trip. guidApplication is a real filter free-eggbert's own
            // CNetwork::EnumSessions() actually supplies (docs/directplay-callsite-audit.md);
            // DPENUMSESSIONS_AVAILABLE is the other real flag that call site passes.
            const GUID zeroGuid{};
            const bool hasGuidFilter =
                lpEnumSessionsDesc && !IsEqualGuid(lpEnumSessionsDesc->guidApplication, zeroGuid);
            const bool availableOnly = (dwFlags & DPENUMSESSIONS_AVAILABLE) != 0;

            for (const auto& [port, hostedSession] : LoopbackHostedSessionRegistry()) {
                (void)port;
                if (hasGuidFilter &&
                    !IsEqualGuid(hostedSession->applicationGuid, lpEnumSessionsDesc->guidApplication)) {
                    continue;
                }
                if (availableOnly && hostedSession->maxPlayers != 0 &&
                    hostedSession->currentPlayers >= hostedSession->maxPlayers) {
                    continue;
                }

                DPSESSIONDESC2 desc{};
                desc.dwSize = sizeof(DPSESSIONDESC2);
                desc.guidApplication = hostedSession->applicationGuid;
                desc.guidInstance = hostedSession->sessionInstanceGuid;
                desc.dwMaxPlayers = hostedSession->maxPlayers;
                desc.dwCurrentPlayers = hostedSession->currentPlayers;
                // Valid only for the duration of this callback invocation, matching real
                // DirectPlay's documented descriptor lifetime - never retained past it.
                desc.lpszSessionNameA = hostedSession->sessionName.empty()
                                            ? nullptr
                                            : const_cast<char*>(hostedSession->sessionName.c_str());
                // Never revealed via enumeration, matching real DirectPlay convention - a
                // password is only required at Open(DPOPEN_JOIN)/OPENSESSION) time.
                desc.lpszPasswordA = nullptr;

                if (!lpEnumSessionsCallback(&desc, &dwTimeout, dwFlags, lpContext)) break;
            }
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
                // here; a joining role calling Connect() over ENet still isn't wired - how it
                // would resolve a host address is a separate, still-open design question
                // (plan.md Phase 7), not decided by this task.
                if (!session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayEnetPort)) {
                    session_.transport.reset();
                    return DPERR_CANTCREATESESSION;
                }
            }
#else
            session_.transport = std::make_unique<free_direct_directplay::LoopbackDirectPlayTransport>();
            // Fixed default port (docs/directplay-design.md Decisions 11/12) - DPSESSIONDESC2
            // has no port-like field to derive one from. Self-send no longer routes through
            // the transport (Decision 12), so it is now safe for the hosting role to call
            // Listen() here without breaking it, unlike when this was first attempted (see
            // Decision 11's regression note).
            if (session_.isHost) {
                if (!session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayLoopbackPort)) {
                    session_.transport.reset();
                    return DPERR_CANTCREATESESSION;
                }
                // Makes this session discoverable via EnumSessions() (docs/directplay-design.md
                // Decision 18) - registered by live pointer, not a snapshot, so the reported
                // dwCurrentPlayers/etc. stay accurate as the session changes after this point.
                LoopbackHostedSessionRegistry()[free_direct_directplay::kDefaultDirectPlayLoopbackPort] =
                    &session_;
            } else {
                // DPOPEN_JOIN/DPOPEN_OPENSESSION: Connect() resolves the host synchronously via
                // the fixed port's registry entry and fails immediately - no timeout needed -
                // when nothing is listening there, which maps directly to DPERR_NOSESSIONS.
                if (!session_.transport->Connect(nullptr, free_direct_directplay::kDefaultDirectPlayLoopbackPort)) {
                    session_.transport.reset();
                    return DPERR_NOSESSIONS;
                }
                // Send a join-request packet to the host (docs/directplay-design.md Decision 16),
                // fire-and-forget - no synchronous wait for a reply here (confirmed with the
                // user: matches the polling model every other event in this codebase already
                // uses, Decision 6, rather than real DirectPlay's blocking Open()). This session
                // has no DPID yet - that is exactly what the host's eventual join-accepted
                // response provides, processed by Receive()'s drain loop below - so idFrom/idTo
                // are left at their defaults (unused/meaningless for this packet type) rather than
                // routed through the validated public Send() path.
                free_direct_directplay::DirectPlayWirePacketHeader joinHeader;
                joinHeader.type = free_direct_directplay::DirectPlayWirePacketType::Join;
                joinHeader.applicationGuid = session_.applicationGuid;
                joinHeader.sessionGuid = session_.sessionInstanceGuid;
                std::vector<std::uint8_t> joinBytes;
                free_direct_directplay::SerializeDirectPlayWireHeader(joinHeader, joinBytes);
                session_.transport->Send(0, joinBytes.data(), joinBytes.size(), true);
            }
#endif
            session_.state = free_direct_directplay::DirectPlayObjectState::Open;
            return DP_OK;
        }

        HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) override {
            (void)hEvent; (void)lpData; (void)dwDataSize; (void)dwFlags;
            // lpPlayerName is optional (a player may be created without a display name); only
            // its size is validated when one is actually provided.
            if (lpPlayerName && lpPlayerName->dwSize != sizeof(DPNAME)) return DPERR_INVALIDPARAMS;

            // dwMaxPlayers == 0 means "no limit" (docs/directplay-design.md Decision 9's
            // existing convention, reused here) - a local CreatePlayer() call counts against
            // the same cap as remote assignment (Receive()'s assignment loop), since
            // dwMaxPlayers bounds the session's total player count, not just remote ones.
            if (session_.maxPlayers != 0 && session_.currentPlayers >= session_.maxPlayers) {
                return DPERR_CANTCREATEPLAYER;
            }

            // Sequential allocator starting at 0 (docs/directplay-design.md Decision 3) -
            // the host's own first local player gets DPID 0, matching free-eggbert's own
            // comparison pattern, not real DirectPlay's DPID_SYSMSG/DPID_ALLPLAYERS
            // reservation convention.
            const DPID newId = session_.nextPlayerId++;
            session_.localPlayerIds.push_back(newId);
            session_.currentPlayers++;
            if (lpidPlayer) *lpidPlayer = newId;
            return DP_OK;
        }

        HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) override {
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // A null payload with zero length is a valid no-payload send (plan.md Phase 10); a
            // null payload with nonzero length has no bytes to actually read and was previously
            // unchecked here (TASK-24H-0106's null-pointer sweep) - both self-send's and the
            // unicast path's `bytes + dwDataSize` pointer arithmetic below are undefined behavior
            // on a null `lpData` when `dwDataSize > 0`, a real crash risk, not a hypothetical one.
            if (!lpData && dwDataSize > 0) return DPERR_INVALIDPARAMS;

            if (idTo == idFrom) {
                // Validated against localPlayerIds (docs/directplay-design.md Decision 16) - this
                // was previously unchecked, an inconsistency with the unicast path below now that
                // one exists. Matters concretely for a joining session: its localPlayerIds only
                // contains a real entry once a join-accepted packet has been processed (Decision
                // 16), so this doubles as an observable proof that adoption actually happened,
                // not just an abstract correctness nicety.
                if (std::find(session_.localPlayerIds.begin(), session_.localPlayerIds.end(), idFrom) ==
                    session_.localPlayerIds.end()) {
                    return DPERR_INVALIDPLAYER;
                }
                // Enqueued directly into session_.messageQueue rather than round-tripping
                // through session_.transport (Phase 4's original approach, changed here per
                // docs/directplay-design.md Decision 12): sending a message to yourself is
                // always a purely local operation, regardless of whether this session's
                // transport is idle, hosting (Listen()ing), or joined - it must not depend on,
                // or be affected by, the transport's connection state. This also sidesteps
                // Decision 10's deliberate `false` return from a connected transport's
                // Send()/Receive() (there is no ambiguity to avoid here: idFrom/idTo are known
                // at this layer, never passed down to the transport, which is exactly why the
                // transport itself could never distinguish "self-send" from any other traffic).
                const auto* bytes = static_cast<const std::uint8_t*>(lpData);
                free_direct_directplay::DirectPlayMessagePacket packet;
                packet.idFrom = idFrom;
                packet.idTo = idTo;
                packet.flags = dwFlags;
                packet.payload.assign(bytes, bytes + dwDataSize);
                if (!session_.messageQueue.Enqueue(std::move(packet))) return DPERR_SENDTOOBIG;
                return DP_OK;
            }

            // Real unicast-to-a-specific-remote-player delivery (docs/directplay-design.md
            // Decision 15), host role only for now: idFrom must be a locally-registered
            // player, and idTo must be a remote player this host has actually assigned a DPID
            // to (Decision 7/9's assignment loop) - anything else is DPERR_INVALIDPLAYER,
            // matching plan.md Phase 10's own validation tasks rather than the old silent
            // no-op. The joining role cannot yet address a specific remote player at all - it
            // has no way to learn any remote DPID (including the host's own) before the
            // join-accepted handshake exists (blocked Phase 7 tasks) - so it always gets
            // DPERR_INVALIDPLAYER here too, an honest "not supported yet."
            if (std::find(session_.localPlayerIds.begin(), session_.localPlayerIds.end(), idFrom) ==
                session_.localPlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            if (!session_.isHost || !session_.transport) return DPERR_INVALIDPLAYER;
            if (std::find(session_.remotePlayerIds.begin(), session_.remotePlayerIds.end(), idTo) ==
                session_.remotePlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            // Enforced here, before ever reaching the transport, not just as a Receive()-side
            // nicety: an oversized packet that made it onto the wire would arrive larger than
            // Receive()'s fixed-size read buffer (see Receive(), below) and get stuck at the
            // front of the receiver's queue forever, wedging every message behind it too.
            if (dwDataSize > free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes) {
                return DPERR_SENDTOOBIG;
            }

            free_direct_directplay::DirectPlayWirePacketHeader header;
            header.applicationGuid = session_.applicationGuid;
            header.sessionGuid = session_.sessionInstanceGuid;
            header.idFrom = idFrom;
            header.idTo = idTo;
            header.payloadLength = dwDataSize;

            std::vector<std::uint8_t> wireBytes;
            free_direct_directplay::SerializeDirectPlayWireHeader(header, wireBytes);
            const auto* payloadBytes = static_cast<const std::uint8_t*>(lpData);
            wireBytes.insert(wireBytes.end(), payloadBytes, payloadBytes + dwDataSize);

            const bool reliable = (dwFlags & DPSEND_GUARANTEED) != 0;
            if (!session_.transport->Send(idTo, wireBytes.data(), wireBytes.size(), reliable)) {
                return DPERR_GENERIC;
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
            // Process departures before admitting new arrivals (docs/directplay-design.md
            // Decision 8): remove each disconnected peer's DPID from remotePlayerIds and
            // decrement currentPlayers. Only an already-assigned peer's disconnect is ever
            // reported here - one that disconnects before being assigned was never added
            // to remotePlayerIds/currentPlayers in the first place, so there is nothing to
            // reconcile for it.
            if (session_.transport && session_.isHost) {
                DPID disconnectedId = 0;
                while (session_.transport->HasDisconnectedPeer() &&
                       session_.transport->TakeDisconnectedPeer(&disconnectedId)) {
                    auto it = std::find(session_.remotePlayerIds.begin(),
                                        session_.remotePlayerIds.end(), disconnectedId);
                    if (it != session_.remotePlayerIds.end()) {
                        session_.remotePlayerIds.erase(it);
                        if (session_.currentPlayers > 0) session_.currentPlayers--;
                    }
                }
            }
            // Assign a real DPID to each pending incoming connection, up to dwMaxPlayers
            // (docs/directplay-design.md Decision 7) - the transport only reports "a peer
            // connected but has no DPID yet"; allocation policy (which counter, the cap,
            // player-list bookkeeping) stays here, not in the transport.
            if (session_.transport && session_.isHost) {
                // dwMaxPlayers == 0 means "no limit" (real DirectPlay's documented
                // convention) - free-eggbert never actually relies on this (it always
                // sets a concrete MAXNETPLAYER), but nothing else validates dwMaxPlayers
                // anywhere in this codebase yet, so this is the one place that gives the
                // field its first real meaning; getting the well-known zero case wrong
                // here would be a landmine for later, not a deliberate scope decision.
                while ((session_.maxPlayers == 0 || session_.currentPlayers < session_.maxPlayers) &&
                       session_.transport->HasPendingConnection()) {
                    const DPID newId = session_.nextPlayerId++;
                    if (!session_.transport->AssignPendingConnection(newId)) break;
                    session_.remotePlayerIds.push_back(newId);
                    session_.currentPlayers++;

                    // Send a join-accepted packet to the newly-assigned peer
                    // (docs/directplay-design.md Decision 16), addressed by the DPID it was
                    // just assigned - connectedPeers_[newId] now exists on the transport, so
                    // Send() can reach it directly (Decision 14). Fire-and-forget, same as the
                    // join-request this answers; no observable error path if it fails.
                    free_direct_directplay::DirectPlayWirePacketHeader acceptHeader;
                    acceptHeader.type = free_direct_directplay::DirectPlayWirePacketType::JoinAccept;
                    acceptHeader.applicationGuid = session_.applicationGuid;
                    acceptHeader.sessionGuid = session_.sessionInstanceGuid;
                    acceptHeader.idTo = newId;
                    std::vector<std::uint8_t> acceptBytes;
                    free_direct_directplay::SerializeDirectPlayWireHeader(acceptHeader, acceptBytes);
                    session_.transport->Send(newId, acceptBytes.data(), acceptBytes.size(), true);
                }
                // Session is full for real (dwMaxPlayers != 0 - "0" never rejects
                // anything, matching the "no limit" convention above): any connection
                // still pending at this point cannot be assigned, so reject it outright
                // rather than leaving it connected-but-unassigned forever
                // (docs/directplay-design.md Decision 9).
                if (session_.maxPlayers != 0) {
                    while (session_.currentPlayers >= session_.maxPlayers &&
                           session_.transport->RejectPendingConnection()) {
                    }
                }
            }
            // Drain every real transport-delivered wire packet (docs/directplay-design.md
            // Decision 15/16), for both roles - a host receiving from one of its
            // remotePlayerIds (or a join-request), or a joining session receiving from the host
            // it Connect()ed to (a join-accepted response, or ordinary data). Each blob handed
            // back by transport->Receive() is exactly one Send()-call's worth
            // (LoopbackDirectPlayTransport's buffered_ never coalesces or splits), so one wire
            // header + payload is parsed per iteration. A blob that fails to deserialize (too
            // small, or a payloadLength that disagrees with what actually arrived) is dropped
            // silently rather than crashing or corrupting the queue - the same defensive posture
            // TryDeserializeDirectPlayWireHeader was built for in Phase 5.
            if (session_.transport) {
                constexpr std::size_t kMaxWireBufferSize =
                    free_direct_directplay::kDirectPlayWireHeaderSize +
                    free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes;
                std::vector<std::uint8_t> wireBuf(kMaxWireBufferSize);
                std::size_t receivedSize = 0;
                while (session_.transport->Receive(wireBuf.data(), wireBuf.size(), &receivedSize)) {
                    const auto header = free_direct_directplay::TryDeserializeDirectPlayWireHeader(
                        wireBuf.data(), receivedSize);
                    if (!header) continue;

                    switch (header->type) {
                        case free_direct_directplay::DirectPlayWirePacketType::Data: {
                            // A full messageQueue silently drops the packet (Enqueue()'s
                            // existing bounded-growth contract, unchanged) - there is no
                            // send-side acknowledgement/backpressure to report the drop to yet.
                            free_direct_directplay::DirectPlayMessagePacket packet;
                            packet.idFrom = header->idFrom;
                            packet.idTo = header->idTo;
                            packet.payload.assign(
                                wireBuf.begin() + free_direct_directplay::kDirectPlayWireHeaderSize,
                                wireBuf.begin() + receivedSize);
                            session_.messageQueue.Enqueue(std::move(packet));
                            break;
                        }
                        case free_direct_directplay::DirectPlayWirePacketType::JoinAccept: {
                            // Adopts the host-assigned DPID as this session's own local player
                            // identity (docs/directplay-design.md Decision 16) - only meaningful
                            // for a joining role that hasn't already processed this. Not
                            // enqueued into messageQueue - this is session control state, not a
                            // user-visible Data message.
                            if (!session_.isHost) {
                                session_.applicationGuid = header->applicationGuid;
                                session_.sessionInstanceGuid = header->sessionGuid;
                                const DPID assignedId = header->idTo;
                                if (std::find(session_.localPlayerIds.begin(),
                                              session_.localPlayerIds.end(),
                                              assignedId) == session_.localPlayerIds.end()) {
                                    session_.localPlayerIds.push_back(assignedId);
                                }
                                if (session_.nextPlayerId <= assignedId) {
                                    session_.nextPlayerId = assignedId + 1;
                                }
                            }
                            break;
                        }
                        default:
                            // Join (host-side only meaningful, and assignment already happens
                            // independently of it - see the assignment loop above),
                            // JoinReject/Discovery/DiscoveryResponse (not implemented yet) -
                            // consumed from the queue and otherwise ignored.
                            break;
                    }
                }
            }
            // The buffer-size-query/DPERR_NOMESSAGES/too-small/successful-copy logic lives on
            // DirectPlayMessageQueue itself (DirectPlayMessageQueue.hpp's TryReceive), so it can
            // be exercised directly by tests/directplay_tests.cpp without needing a way to
            // inject a message into a live IDirectPlay2A object.
            const HRESULT hr = session_.messageQueue.TryReceive(lpidFrom, lpidTo, lpData, lpdwDataSize);
            // Client-side rejection/disconnection observability (docs/directplay-design.md
            // Decision 13): a joining session whose host connection is gone (rejected over
            // dwMaxPlayers, or the host shut down) reports DPERR_NOCONNECTION - but only once
            // there is truly nothing left to deliver. A locally-queued message (e.g. an earlier
            // self-send, Decision 12) is still handed back first: losing the host connection
            // must not erase messages that never depended on it.
            if (session_.transport && !session_.isHost && hr == DPERR_NOMESSAGES &&
                !session_.transport->IsConnectedToHost()) {
                return DPERR_NOCONNECTION;
            }
            return hr;
        }

        HRESULT WINAPI Close() override {
            // Makes this session stop being discoverable via EnumSessions() (docs/
            // directplay-design.md Decision 18) - a no-op if it was never registered (joining
            // role, or hosting under FREE_DIRECT_ENABLE_ENET).
            UnregisterHostedSession(&session_);
            if (session_.transport) {
                session_.transport->Shutdown();
                session_.transport.reset();
            }
            session_.messageQueue.Clear();
            session_.localPlayerIds.clear();
            session_.remotePlayerIds.clear();
            session_.nextPlayerId = 0;
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
