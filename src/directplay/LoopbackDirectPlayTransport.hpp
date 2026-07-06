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
 * Decision 10).
 *
 * Two distinct usage modes:
 * - **Self-send** (no `Listen()`/`Connect()` ever called): `Send()` appends to
 *   this instance's own FIFO, `Receive()` pops from the same FIFO. This is the
 *   only mode the default (non-ENet) build uses today, via
 *   `DirectPlay2AImpl::Send()`'s self-send path (`idTo == idFrom`).
 * - **Connected** (`Listen()` or `Connect()` succeeded): a process-wide static
 *   registry (keyed by the `port` passed to `Listen()`/`Connect()`) lets a
 *   separate "client" instance find and connect to a separate "host" instance
 *   in the same process, mirroring `EnetDirectPlayTransport`'s
 *   pending/connected/disconnected-peer model. `Send()`/`Receive()`
 *   deliberately return `false` for both roles once connected - real payload
 *   delivery is a separate, later design decision (Decision 10).
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
    bool Send(const void* data, std::size_t size, bool reliable) override;
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
