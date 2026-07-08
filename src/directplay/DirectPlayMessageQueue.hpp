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
#include <cstring>
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

    /**
     * @brief Implements `IDirectPlay2A::Receive`'s buffer-size-query / `DPERR_NOMESSAGES` /
     * too-small-buffer / successful-copy logic against this queue.
     *
     * Does not check whether the owning session is open - `DirectPlay2AImpl::Receive`
     * (`DirectPlay.cpp`) does that first and returns `DPERR_NOCONNECTION` itself before ever
     * calling this. Extracted as its own method (rather than living inline in `Receive()`) so
     * it can be exercised directly by tests without needing a way to inject a message into a
     * live `IDirectPlay2A` object, which does not exist yet (`Send()` doesn't enqueue - that is
     * Phase 10; no transport delivers one either - that is Phase 4).
     * @note Status: PARTIAL
     */
    HRESULT TryReceive(DPID* lpidFrom, DPID* lpidTo, void* lpData, DWORD* lpdwDataSize) {
        if (!lpdwDataSize) return DPERR_INVALIDPARAMS;

        const DirectPlayMessagePacket* front = Front();
        if (!front) return DPERR_NOMESSAGES;

        const DWORD payloadSize = static_cast<DWORD>(front->payload.size());

        // Buffer-size query: caller wants to know how big a buffer it needs, without
        // dequeuing anything yet.
        if (!lpData && *lpdwDataSize == 0) {
            *lpdwDataSize = payloadSize;
            return DP_OK;
        }

        if (*lpdwDataSize < payloadSize) {
            // Report the required size but leave the packet queued - no dedicated "buffer too
            // small" code exists in include/dplay.h, and free-eggbert's CNetwork::Receive
            // doesn't distinguish this case either, so DPERR_INVALIDPARAMS is reused here.
            *lpdwDataSize = payloadSize;
            return DPERR_INVALIDPARAMS;
        }
        if (!lpData) return DPERR_INVALIDPARAMS;

        // payloadSize == 0 is a legitimate zero-byte message (Send() allows a null/zero-length
        // payload). An empty std::vector<std::uint8_t>::data() is permitted to return nullptr,
        // which UBSan correctly flags as passing a null src to memcpy's nonnull-declared
        // parameter even at count 0 - skip the call entirely rather than relying on memcpy
        // tolerating a null pointer at zero length (found via ASan/UBSan build, TASK-24H-0010).
        if (payloadSize > 0) {
            std::memcpy(lpData, front->payload.data(), payloadSize);
        }
        *lpdwDataSize = payloadSize;
        if (lpidFrom) *lpidFrom = front->idFrom;
        if (lpidTo) *lpidTo = front->idTo;
        PopFront();
        return DP_OK;
    }

private:
    std::deque<DirectPlayMessagePacket> packets_;
};

} // namespace free_direct_directplay
