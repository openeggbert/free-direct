/**
 * @file DirectPlayMessageQueue.hpp
 * @brief Internal DirectPlay receive message queue.
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * Pure data structure with no transport/backend dependency. Not yet wired
 * into `DirectPlay.cpp`'s `Receive` - that is a later `plan.md` Phase 3 task.
 * Nothing enqueues packets into it yet either; that starts once a real
 * delivery path exists (loopback in Phase 4, routing in Phase 10).
 * @note Status: PARTIAL
 */
#pragma once

#include "dplay.h"

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
    DirectPlayMessageQueue() = default;
    ~DirectPlayMessageQueue() = default;

    bool IsEmpty() const { return packets_.empty(); }

    /// Appends a packet to the back of the queue.
    void Enqueue(DirectPlayMessagePacket packet) { packets_.push_back(std::move(packet)); }

    /// Returns the packet at the front of the queue without removing it, or nullptr if empty.
    const DirectPlayMessagePacket* Front() const {
        return packets_.empty() ? nullptr : &packets_.front();
    }

    /// Removes the packet at the front of the queue. Precondition: `!IsEmpty()`.
    void PopFront() { packets_.pop_front(); }

private:
    std::deque<DirectPlayMessagePacket> packets_;
};

} // namespace free_direct_directplay
