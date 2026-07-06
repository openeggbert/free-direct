/**
 * @file DirectPlayTransport.hpp
 * @brief Internal DirectPlay transport abstraction (scaffolding only).
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed,
 * and it must never include any backend header (SDL3, SDL3_net, ENet, ...) -
 * see `CLAUDE.md`'s Internal Backend Policy. Concrete backends
 * (`LoopbackDirectPlayTransport` in Phase 4, `EnetDirectPlayTransport` in
 * Phase 5, optionally `SdlNetDirectPlayTransport` in Phase 12) implement
 * this interface in their own private `.cpp` files.
 *
 * The method set and signatures below are a first pass, not final - they
 * are expected to be refined once Phase 4 implements the first concrete
 * backend (loopback) against this interface. `DirectPlaySession` (and
 * transitively `DirectPlay.cpp`) now holds an (always-null today) owning
 * pointer to this interface; no concrete implementation exists yet.
 * @note Status: STUB
 */
#pragma once

#include "dplay.h"

#include <cstddef>
#include <cstdint>

namespace free_direct_directplay {

/**
 * @brief Backend-agnostic transport used by `DirectPlaySession` to move
 * bytes between peers, independent of whether the concrete backend is
 * in-process loopback, ENet, or (optionally, later) SDL3_net.
 * @note Status: STUB
 */
class IDirectPlayTransport {
public:
    virtual ~IDirectPlayTransport() = default;

    /**
     * @brief Starts hosting on `port`, so future peers can Connect() to this transport.
     * `port` is meaningless to backends with no real network concept (e.g. loopback),
     * which accept and ignore it.
     */
    virtual bool Listen(std::uint16_t port) = 0;

    /**
     * @brief Connects to a host at `address`:`port` previously started with Listen().
     * `address`/`port` are meaningless to backends with no real network concept (e.g.
     * loopback), which accept and ignore them. A `true` return means the connection
     * attempt was initiated (backend-defined whether that implies completion yet).
     */
    virtual bool Connect(const char* address, std::uint16_t port) = 0;

    /**
     * @brief Sends a payload to a specific peer, addressed by `targetId`
     * (`docs/directplay-design.md` Decision 14). `reliable` selects guaranteed, ordered
     * delivery vs. best-effort delivery; backends with no such distinction (e.g.
     * loopback) accept and ignore it.
     *
     * `targetId` is only meaningful for a hosting-role instance with more than one
     * connected peer (`connectedPeers_`, populated via `AssignPendingConnection()`) - it
     * is looked up there to find the one specific peer to send to; `false` is returned if
     * `targetId` names no currently-connected peer. A joining-role instance
     * (`hostPeer_` set) has exactly one possible destination - the host it `Connect()`ed
     * to - so `targetId` is accepted but ignored; there is nothing else to address. DPID
     * allocation/validation stays entirely outside the transport, as with every other
     * DPID-shaped parameter on this interface (`AssignPendingConnection()`,
     * `TakeDisconnectedPeer()`) - the transport only ever uses `targetId` as an opaque map
     * key into `connectedPeers_`.
     */
    virtual bool Send(DPID targetId, const void* data, std::size_t size, bool reliable) = 0;

    /** @brief Retrieves the next received payload, if any, without blocking. */
    virtual bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) = 0;

    /**
     * @brief Processes any pending network events (connections, disconnections, incoming
     * data) without blocking. `DirectPlay2AImpl::Receive()` calls this once before
     * consulting its message queue (`docs/directplay-design.md` Decision 6) - backends
     * with no real network concept (e.g. loopback, which is already synchronous) make
     * this a no-op.
     */
    virtual void Service() = 0;

    /**
     * @brief True if a peer has connected but not yet been assigned a DPID via
     * AssignPendingConnection() (`docs/directplay-design.md` Decision 7). Backends with
     * no real incoming-connection concept (e.g. loopback, or a joining-role instance)
     * always return `false` - DPID allocation is deliberately kept out of the
     * transport; this only reports the raw connection-shaped fact.
     */
    virtual bool HasPendingConnection() const = 0;

    /**
     * @brief Assigns `id` to the oldest pending connection, moving it from "connected
     * but unidentified" to "a known peer this transport can be told about again by
     * DPID" in later Phase 10 addressing work. Returns `false` (no-op) if there was no
     * pending connection - callers should check HasPendingConnection() first, or treat
     * a `false` return as "nothing to assign right now."
     */
    virtual bool AssignPendingConnection(DPID id) = 0;

    /**
     * @brief Rejects the oldest pending connection (mirrors AssignPendingConnection(),
     * but admits nothing - there is no DPID to assign to a connection being turned
     * away) via a graceful disconnect (`docs/directplay-design.md` Decision 9). Returns
     * `false` (no-op) if there was no pending connection. The rejected peer is never
     * reported via TakeDisconnectedPeer() - it was never assigned a DPID, so nothing
     * outside the transport ever knew about it.
     */
    virtual bool RejectPendingConnection() = 0;

    /**
     * @brief True if a previously-assigned peer (via AssignPendingConnection()) has
     * since disconnected and not yet been reported via TakeDisconnectedPeer()
     * (`docs/directplay-design.md` Decision 8). A peer that disconnects before ever
     * being assigned a DPID is not reported - nothing outside the transport knows
     * about it yet.
     */
    virtual bool HasDisconnectedPeer() const = 0;

    /**
     * @brief Pops the oldest queued disconnect into `*outId` (if non-null) and returns
     * `true`; returns `false` (no-op, `*outId` untouched) if none was queued. Uses an
     * output parameter rather than returning the DPID directly with `0` meaning "none",
     * since DPID `0` is a valid real player ID (`docs/directplay-design.md` Decision 3)
     * and can never double as an empty sentinel.
     */
    virtual bool TakeDisconnectedPeer(DPID* outId) = 0;

    /**
     * @brief Joining-role counterpart to `HasDisconnectedPeer()`/`TakeDisconnectedPeer()`:
     * true if this instance's own `Connect()` call is still considered connected to the
     * host it reached, false once that connection is gone (the host rejected it via
     * `RejectPendingConnection()`, the host disconnected/shut down, or `Connect()` was
     * never called at all). A hosting-role instance always returns `false` here - "am I
     * connected to a host" does not apply to a hosting instance itself; use
     * `HasPendingConnection()`/`ConnectedPeerCount()`-style backend accessors for the
     * hosting role's own peers instead (`docs/directplay-design.md` Decision 13).
     */
    virtual bool IsConnectedToHost() const = 0;

    /** @brief Tears down all connections and releases backend resources. */
    virtual void Shutdown() = 0;
};

} // namespace free_direct_directplay
