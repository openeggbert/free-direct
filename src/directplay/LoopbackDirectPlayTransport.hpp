/**
 * @file LoopbackDirectPlayTransport.hpp
 * @brief In-process `IDirectPlayTransport` backend, with no real sockets.
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * Per `CLAUDE.md`'s Internal Backend Policy, it must never include any real
 * backend header (SDL3, SDL3_net, ENet, ...) - it doesn't need to, since
 * "loopback" means everything stays in-process, backed by a plain in-memory
 * byte-buffer queue.
 *
 * `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`) assigns one of these to
 * every session unconditionally, since no other transport backend exists
 * yet (`EnetDirectPlayTransport` lands in Phase 5).
 * @note Status: PARTIAL
 */
#pragma once

#include "DirectPlayTransport.hpp"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace free_direct_directplay {

/// Default loopback registry port `Open()` uses for both `Listen()` (hosting,
/// `DPOPEN_CREATE`) and `Connect()` (joining, `DPOPEN_JOIN`/`DPOPEN_OPENSESSION`)
/// (`docs/directplay-design.md` Decisions 11/12) - mirrors `EnetDirectPlayTransport`'s
/// `kDefaultDirectPlayEnetPort` (Decision 5): `DPSESSIONDESC2` has no port-like field a
/// real value could be derived from, so a single fixed, FreeDirect-internal constant is
/// used instead. Consequence: only one loopback-hosted session can exist per process at
/// a time - the same "one session per machine" simplification Decision 5 already made
/// for the real ENet port.
inline constexpr std::uint16_t kDefaultDirectPlayLoopbackPort = 51322;

/**
 * @brief `IDirectPlayTransport` implemented as a same-process byte-buffer queue,
 * with real multi-instance connection lifecycle (`docs/directplay-design.md`
 * Decision 10) and real addressed delivery (Decision 14).
 *
 * Every instance owns one inbox, `buffered_` - a plain FIFO of received byte blobs.
 * `Receive()` always just pops from this instance's own `buffered_`, regardless of role;
 * what varies by role is *who can push into it* via `Send()`:
 * - **Self-send** (no `Listen()`/`Connect()` ever called): `Send()` pushes into this same
 *   instance's own `buffered_` - the only mode the default (non-ENet) build's `Open()`
 *   ever puts a transport into, though `DirectPlay2AImpl::Send()`'s self-send path
 *   (`idTo == idFrom`) no longer even goes through the transport for this (Decision 12) -
 *   this mode now exists mainly for whitebox test symmetry with the connected modes below.
 * - **Joining role** (`Connect()` succeeded, `hostPeer_` set): `Send()` pushes into
 *   `hostPeer_->buffered_` - the one host this instance connected to. `targetId` is
 *   accepted but ignored (there is only one possible destination).
 * - **Hosting role** (`Listen()` succeeded): `Send(targetId, ...)` looks up `targetId` in
 *   `connectedPeers_` and pushes into *that specific peer's* `buffered_` - reaching into
 *   another instance's private state directly, the same mechanism already used by
 *   `Connect()`/`RejectPendingConnection()`/`Shutdown()` (Decision 10). Returns `false` if
 *   `targetId` names no currently-connected peer.
 * @note Status: PARTIAL
 */
class LoopbackDirectPlayTransport final : public IDirectPlayTransport {
public:
    LoopbackDirectPlayTransport() = default;
    ~LoopbackDirectPlayTransport() override;

    LoopbackDirectPlayTransport(const LoopbackDirectPlayTransport&) = delete;
    LoopbackDirectPlayTransport& operator=(const LoopbackDirectPlayTransport&) = delete;
    LoopbackDirectPlayTransport(LoopbackDirectPlayTransport&&) = delete;
    LoopbackDirectPlayTransport& operator=(LoopbackDirectPlayTransport&&) = delete;

    bool Listen(std::uint16_t port) override;
    bool Connect(const char* address, std::uint16_t port) override;
    bool Send(DPID targetId, const void* data, std::size_t size, bool reliable) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Service() override;
    bool HasPendingConnection() const override;
    bool AssignPendingConnection(DPID id) override;
    bool RejectPendingConnection() override;
    bool HasDisconnectedPeer() const override;
    bool TakeDisconnectedPeer(DPID* outId) override;
    bool IsConnectedToHost() const override;
    void Shutdown() override;

    /** @brief Test-only accessor: number of assigned (DPID-mapped) peers this hosting-role instance tracks. */
    std::size_t ConnectedPeerCount() const { return connectedPeers_.size(); }
    /** @brief Test-only accessor: number of not-yet-assigned pending connections this hosting-role instance tracks. */
    std::size_t PendingConnectionCount() const { return pendingPeers_.size(); }

private:
    std::deque<std::vector<std::uint8_t>> buffered_;

    bool listening_ = false;
    std::uint16_t listenPort_ = 0;

    LoopbackDirectPlayTransport* hostPeer_ = nullptr;

    std::deque<LoopbackDirectPlayTransport*> pendingPeers_;
    std::unordered_map<DPID, LoopbackDirectPlayTransport*> connectedPeers_;
    std::deque<DPID> disconnectedPeerIds_;
};

} // namespace free_direct_directplay
