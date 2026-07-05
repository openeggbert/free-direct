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
     * @brief Sends a payload to a peer. `reliable` selects guaranteed, ordered
     * delivery vs. best-effort delivery; backends with no such distinction (e.g.
     * loopback) accept and ignore it.
     */
    virtual bool Send(const void* data, std::size_t size, bool reliable) = 0;

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

    /** @brief Tears down all connections and releases backend resources. */
    virtual void Shutdown() = 0;
};

} // namespace free_direct_directplay
