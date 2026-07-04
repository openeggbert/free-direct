/**
 * @file DirectPlayMessageQueue.hpp
 * @brief Internal DirectPlay receive message queue (scaffolding only).
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * `DirectPlayMessageQueue` does not yet hold any real state - the FIFO
 * packet queue backing `IDirectPlay2A::Receive` is added starting in
 * `plan.md` Phase 3. It is not yet used by `DirectPlay.cpp`.
 * @note Status: STUB
 */
#pragma once

namespace free_direct_directplay {

class DirectPlayMessageQueue {
public:
    DirectPlayMessageQueue() = default;
    ~DirectPlayMessageQueue() = default;
};

} // namespace free_direct_directplay
