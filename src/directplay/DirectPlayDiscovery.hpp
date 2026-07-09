/**
 * @file DirectPlayDiscovery.hpp
 * @brief Raw-UDP LAN session discovery for the ENet backend (`docs/directplay-design.md`
 * Decision 23, `plan.md` TASK-24H-0150).
 *
 * Private to the DirectPlay implementation: must never be included from `include/dplay.h` and
 * must never be installed. Compiled only under `FREE_DIRECT_ENABLE_ENET` (matching
 * `EnetDirectPlayTransport.hpp`'s own gate) - the default build has zero dependency on this file.
 *
 * Deliberately built on ENet's own portable `ENetSocket`/`enet_socket_*` primitives, not raw
 * platform sockets (`<sys/socket.h>`/`<winsock2.h>`) and not `ENetHost`/`ENetPeer`. `ENetSocket`
 * already gives cross-platform (Windows/Unix, see `third_party/enet/include/enet/{win32,unix}.h`)
 * non-blocking/broadcast UDP for free, without FreeDirect hand-rolling per-platform socket code;
 * `ENetHost`/`ENetPeer` are deliberately avoided because they are connection-oriented, a poor
 * semantic match for a stateless "is anyone here" announce/reply exchange (Decision 23's own
 * rationale - see there for the full "why raw UDP, not ENetHost" reasoning).
 *
 * **Precondition**: `enet_initialize()` must already be in effect for the lifetime of any
 * `DirectPlayDiscoveryService` instance and for the duration of `BroadcastAndCollect()`. This
 * class does not manage that lifecycle itself - `EnetDirectPlayTransport`'s constructor/destructor
 * already do process-wide `enet_initialize()`/`enet_deinitialize()` reference counting
 * (`EnetDirectPlayTransport.cpp`), and every real call site in `DirectPlay.cpp` already has (or
 * constructs, for `EnumSessions()`'s standalone case) a live `EnetDirectPlayTransport` for exactly
 * this reason - duplicating a second, independent reference count here would risk the "exactly
 * once per process" invariant that comment already documents.
 * @note Status: PARTIAL
 */
#pragma once
#ifdef FREE_DIRECT_ENABLE_ENET

#include "dplay.h"

#include <enet/enet.h>

#include <cstdint>
#include <string>
#include <vector>

namespace free_direct_directplay {

/// Fixed discovery port, distinct from the ENet hosting port (`kDefaultDirectPlayEnetPort`,
/// `51321`, Decision 5) and the loopback backend's port (`kDefaultDirectPlayLoopbackPort`,
/// `51322`, Decisions 11/12) - all three are separate, independently-bound sockets/mechanisms.
inline constexpr std::uint16_t kDefaultDirectPlayDiscoveryPort = 51323;

/**
 * @brief One discovered session's real fields, as reported by a `DiscoveryResponse` reply.
 *
 * Mirrors exactly what `EnumSessions()`'s existing loopback path already reports from the live
 * `DirectPlaySession` registry (Decision 18) - the two sources are unioned by the caller
 * (`DirectPlay.cpp`'s `EnumSessions()`), not merged here.
 */
struct DiscoveredSessionInfo {
    GUID applicationGuid{};
    GUID sessionInstanceGuid{};
    DWORD maxPlayers = 0;
    DWORD currentPlayers = 0;
    std::string sessionName;
};

/**
 * @brief Owns (for the hosting role) or transiently uses (for `EnumSessions()`) one raw UDP
 * socket dedicated to LAN session discovery.
 * @note Status: PARTIAL
 */
class DirectPlayDiscoveryService {
public:
    DirectPlayDiscoveryService() = default;
    ~DirectPlayDiscoveryService() { StopListening(); }
    DirectPlayDiscoveryService(const DirectPlayDiscoveryService&) = delete;
    DirectPlayDiscoveryService& operator=(const DirectPlayDiscoveryService&) = delete;

    /// Hosting role: binds a `SO_BROADCAST`-enabled, non-blocking UDP socket to
    /// `kDefaultDirectPlayDiscoveryPort`. Returns `false` on failure (e.g. already bound by
    /// another FreeDirect process on this machine - mirrors `EnetDirectPlayTransport::Listen()`'s
    /// own bind-conflict behavior for the hosting port).
    bool StartListening();
    void StopListening();
    bool IsListening() const { return socket_ != ENET_SOCKET_NULL; }

    /// Non-blocking: drains every currently-pending `Discovery` request and replies with a
    /// `DiscoveryResponse` (unicast, back to the requester's real source address) whenever the
    /// request's `applicationGuid` matches `info.applicationGuid`, or the request's
    /// `applicationGuid` is the all-zero wildcard. Called from `DirectPlay2AImpl::Receive()`'s
    /// existing `Service()` piggyback (Decision 6's polling model - no new API, no background
    /// thread). A no-op if `!IsListening()`.
    void RespondToPendingRequests(const DiscoveredSessionInfo& info);

    /// Enumerating role: broadcasts one `Discovery` packet (`filterApplicationGuid` all-zero
    /// means "any application") to the LAN broadcast address on `kDefaultDirectPlayDiscoveryPort`,
    /// then collects `DiscoveryResponse` replies for up to `timeoutMs` milliseconds (`0` means
    /// "don't wait, return whatever already arrived" - never blocks longer than `timeoutMs`
    /// regardless of the value given). Uses its own transient socket, independent of any
    /// `StartListening()`-bound instance. Returns one entry per reply received (a caller wanting
    /// "distinct responding hosts" dedupes by `sessionInstanceGuid`, matching how a single real
    /// session should only ever send one reply per request anyway).
    static std::vector<DiscoveredSessionInfo> BroadcastAndCollect(const GUID& filterApplicationGuid,
                                                                    std::uint32_t timeoutMs);

private:
    ENetSocket socket_ = ENET_SOCKET_NULL;
};

} // namespace free_direct_directplay

#endif // FREE_DIRECT_ENABLE_ENET
