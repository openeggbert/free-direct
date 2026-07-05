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
#include <vector>

namespace free_direct_directplay {

/**
 * @brief `IDirectPlayTransport` implemented as a same-process byte-buffer queue.
 *
 * `Send()` appends a byte-copy of its input to an internal FIFO; `Receive()`
 * pops the front entry into the caller's buffer. There is no concept of
 * "peers" here - a `LoopbackDirectPlayTransport` only ever talks to itself,
 * which is exactly what `DirectPlay2AImpl::Send()`'s self-send path
 * (`idTo == idFrom`) uses it for.
 * @note Status: PARTIAL
 */
class LoopbackDirectPlayTransport final : public IDirectPlayTransport {
public:
    bool Listen(std::uint16_t port) override;
    bool Connect(const char* address, std::uint16_t port) override;
    bool Send(const void* data, std::size_t size, bool reliable) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Shutdown() override;

private:
    std::deque<std::vector<std::uint8_t>> buffered_;
};

} // namespace free_direct_directplay
