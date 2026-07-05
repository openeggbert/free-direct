/**
 * @file EnetDirectPlayTransport.hpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 *
 * Compiled only when `FREE_DIRECT_ENABLE_ENET` is `ON` - `CMakeLists.txt` only adds
 * `EnetDirectPlayTransport.cpp` to `target_sources()` under that option, and only defines
 * the `FREE_DIRECT_ENABLE_ENET` macro `DirectPlay.cpp` `#ifdef`s on under the same option
 * (`docs/directplay-design.md` Decision 4). `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`)
 * now includes this header and constructs this class under that macro; it still always
 * uses `LoopbackDirectPlayTransport` in the default (`FREE_DIRECT_ENABLE_ENET=OFF`) build.
 *
 * This header is intentionally private to the DirectPlay implementation: it must
 * never be included from `include/dplay.h` and must never be installed. Per
 * `CLAUDE.md`'s Internal Backend Policy, a private header under `src/**` that is only
 * ever included by `.cpp` files (as this one is, today) is allowed to name ENet types
 * directly - the policy bans ENet from reaching `include/`, not from internal headers.
 *
 * Real behavior implemented so far (`plan.md` Phase 5/6, in order): the
 * constructor/destructor join/leave a process-wide `enet_initialize()`/
 * `enet_deinitialize()` reference count (see the `.cpp` file); `Listen(port)` creates a
 * real listening `ENetHost`; `Connect(address, port)` creates a client-role `ENetHost`
 * (no listen address) and calls `enet_host_connect` (implemented together, since a
 * created-but-never-connected client host is not independently testable); `Shutdown()`
 * (and the destructor) perform a real graceful disconnect (`enet_peer_disconnect` +
 * a bounded wait for `ENET_EVENT_TYPE_DISCONNECT`, generalized to every peer this
 * instance still knows about - see below) before destroying the host; `Send()` sends a
 * real ENet packet (`ENET_PACKET_FLAG_RELIABLE` or `ENET_PACKET_FLAG_UNSEQUENCED`,
 * selected by its `reliable` parameter) to `hostPeer_` (client role only - see below);
 * `Service()` (`docs/directplay-design.md` Decision 6) drains pending ENet events.
 *
 * Hosting (`Listen()`) and joining (`Connect()`) are not symmetric
 * (`docs/directplay-design.md` Decision 7), and are tracked separately:
 * - **Joining role**: `hostPeer_`, a single `ENetPeer*` - the one host this instance
 *   connected to. `Send()`/`Shutdown()`/`HasPeer()` all operate on this field only.
 * - **Hosting role**: `connectedPeers_` (a `DPID → ENetPeer*` map, populated only via
 *   `AssignPendingConnection()` - the transport never allocates a DPID itself) and
 *   `pendingPeers_` (a queue of connected-but-unidentified peers, populated by
 *   `Service()` on `ENET_EVENT_TYPE_CONNECT`). A hosting instance's `hostPeer_` stays
 *   null forever, so `Send()`/`Receive()` always return `false` for it today - real
 *   per-DPID-addressed send/receive to a specific connected peer is `plan.md` Phase
 *   10's job, not this one's (this is an intentional, documented limitation, not a
 *   silent regression - see Decision 7's sub-question 3). When an *already-assigned*
 *   peer disconnects, its `DPID` is queued in `disconnectedPeerIds_`
 *   (`docs/directplay-design.md` Decision 8), retrievable via
 *   `HasDisconnectedPeer()`/`TakeDisconnectedPeer()` - a peer that disconnects while
 *   still only in `pendingPeers_` (never assigned) is not reported, since nothing
 *   outside the transport knows about it yet.
 * `Open(..., DPOPEN_CREATE)` constructs this class under `FREE_DIRECT_ENABLE_ENET`, calls
 * `Listen(kDefaultDirectPlayEnetPort)` when hosting, and calls `Service()` once per
 * `IDirectPlay2A::Receive()` call - see `kDefaultDirectPlayEnetPort` below.
 * @note Status: PARTIAL
 */
#pragma once

#include "DirectPlayTransport.hpp"

#include <cstdint>
#include <deque>
#include <enet/enet.h>
#include <unordered_map>

namespace free_direct_directplay {

/// Default ENet listen port `Open(..., DPOPEN_CREATE)` uses when hosting under
/// `FREE_DIRECT_ENABLE_ENET` (`docs/directplay-design.md` Decision 5). A fixed,
/// FreeDirect-internal constant, not derived from any real DirectPlay API value -
/// `DPSESSIONDESC2` has no port-like field (real DirectPlay abstracts network
/// addressing behind service providers FreeDirect does not implement).
inline constexpr std::uint16_t kDefaultDirectPlayEnetPort = 51321;

/**
 * @brief `IDirectPlayTransport` implemented over real ENet reliable/unreliable UDP.
 * @note Status: PARTIAL
 */
class EnetDirectPlayTransport final : public IDirectPlayTransport {
public:
    EnetDirectPlayTransport();
    ~EnetDirectPlayTransport() override;

    bool Listen(std::uint16_t port) override;
    bool Connect(const char* address, std::uint16_t port) override;
    bool Send(const void* data, std::size_t size, bool reliable) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Service() override;
    bool HasPendingConnection() const override;
    bool AssignPendingConnection(DPID id) override;
    bool RejectPendingConnection() override;
    bool HasDisconnectedPeer() const override;
    bool TakeDisconnectedPeer(DPID* outId) override;
    void Shutdown() override;

    /// True once this instance's constructor successfully joined the process-wide
    /// enet_initialize() reference count; false if enet_initialize() itself failed.
    /// Exists so tests can observe real init behavior without needing a way to drive
    /// Listen()/Connect() yet.
    bool IsEnetReady() const { return enetReady_; }

    /// True once Listen() or Connect() has created a real ENetHost that has not yet
    /// been torn down by Shutdown(). Exists purely so a test can observe real
    /// host-creation behavior directly - there is no other public way to tell a
    /// genuinely-created ENetHost apart from Listen()/Connect() having merely returned
    /// true.
    bool HasHost() const { return host_ != nullptr; }

    /// True once Connect() has queued a connection attempt (does *not* by itself mean
    /// that attempt completed - checking ENetPeer::state directly is out of scope) or
    /// Service() adopted the resulting connection (this *does* mean it completed).
    /// Client-role (Connect()) only - a hosting instance's hostPeer_ stays null
    /// forever; use HasPendingConnection()/ConnectedPeerCount() for the hosting role
    /// (docs/directplay-design.md Decision 7).
    bool HasPeer() const { return hostPeer_ != nullptr; }

    /// Number of peers this instance has assigned a DPID to via AssignPendingConnection()
    /// (hosting role only). Test-only purpose, mirroring HasHost()/HasPeer().
    std::size_t ConnectedPeerCount() const { return connectedPeers_.size(); }

private:
    bool enetReady_ = false;
    /// True once Listen() has succeeded - distinguishes the hosting role from the
    /// joining role for Service()'s CONNECT/DISCONNECT handling, since both roles
    /// share the same host_/Service() code path but track peers differently.
    bool listening_ = false;
    ENetHost* host_ = nullptr;
    /// Joining-role only (Connect()) - the one host this instance connected to.
    ENetPeer* hostPeer_ = nullptr;
    /// Hosting-role only (Listen()) - peers with an assigned DPID.
    std::unordered_map<DPID, ENetPeer*> connectedPeers_;
    /// Hosting-role only (Listen()) - connected peers not yet assigned a DPID.
    std::deque<ENetPeer*> pendingPeers_;
    /// Hosting-role only (Listen()) - DPIDs of assigned peers that have since
    /// disconnected, queued for TakeDisconnectedPeer() (Decision 8).
    std::deque<DPID> disconnectedPeerIds_;
};

} // namespace free_direct_directplay
