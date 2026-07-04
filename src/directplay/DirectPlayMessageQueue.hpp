/**
 * @file DirectPlayMessageQueue.hpp
 * @brief Internal DirectPlay receive message queue.
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * Pure data structure with no transport/backend dependency. `DirectPlaySession`
 * owns one instance; `DirectPlay.cpp`'s `Receive`/`Close` read/clear it.
 * Nothing enqueues packets into it yet - that starts once a real delivery
 * path exists (loopback in Phase 4, routing in Phase 10).
 * @note Status: PARTIAL
 */
#pragma once

#include "dplay.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace free_direct_directplay {

/**
 * @brief One queued message: who it is from/to, its flags, and its payload bytes.
 * @note Status: PARTIAL
 */
struct DirectPlayMessagePacket {
    DPID idFrom = 0;
    DPID idTo = 0;
    DWORD flags = 0;
    std::vector<std::uint8_t> payload;
};

/**
 * @brief FIFO queue of received messages, backing `IDirectPlay2A::Receive`.
 * @note Status: PARTIAL
 */
class DirectPlayMessageQueue {
public:
    /// Bounds memory growth if a peer stops calling Receive(). `plan.md` Phase 10 decides the
    /// exact `DPERR_*` code `Send()` maps a full queue to once it actually calls `Enqueue()`;
    /// this class just refuses to grow past this count.
    static constexpr std::size_t kMaxQueuedMessages = 256;

    /// Generously exceeds every payload size observed in the Phase 0 `free-eggbert` audit (a
    /// fixed 500-byte receive buffer, with actual payloads in the low hundreds of bytes).
    /// `plan.md` Phase 10 maps a rejection here to `DPERR_SENDTOOBIG` once `Send()` calls
    /// `Enqueue()`.
    static constexpr std::size_t kMaxPayloadBytes = 4096;

    DirectPlayMessageQueue() = default;
    ~DirectPlayMessageQueue() = default;

    bool IsEmpty() const { return packets_.empty(); }

    /// Appends a packet to the back of the queue. Returns false, without enqueuing, if the
    /// payload exceeds `kMaxPayloadBytes` or the queue is already at `kMaxQueuedMessages`.
    bool Enqueue(DirectPlayMessagePacket packet) {
        if (packet.payload.size() > kMaxPayloadBytes) return false;
        if (packets_.size() >= kMaxQueuedMessages) return false;
        packets_.push_back(std::move(packet));
        return true;
    }

    /// Returns the packet at the front of the queue without removing it, or nullptr if empty.
    const DirectPlayMessagePacket* Front() const {
        return packets_.empty() ? nullptr : &packets_.front();
    }

    /// Removes the packet at the front of the queue. Precondition: `!IsEmpty()`.
    void PopFront() { packets_.pop_front(); }

    /// Discards all queued packets, e.g. when `Close()` resets a `DirectPlaySession`.
    void Clear() { packets_.clear(); }

private:
    std::deque<DirectPlayMessagePacket> packets_;
};

} // namespace free_direct_directplay
