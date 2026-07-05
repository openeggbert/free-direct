/**
 * @file LoopbackDirectPlayTransport.cpp
 * @brief In-process `IDirectPlayTransport` backend, with no real sockets.
 * @note Status: PARTIAL
 */
#include "LoopbackDirectPlayTransport.hpp"

#include <cstring>

namespace free_direct_directplay {

bool LoopbackDirectPlayTransport::Listen(std::uint16_t /*port*/) { return true; }

bool LoopbackDirectPlayTransport::Connect(const char* /*address*/, std::uint16_t /*port*/) {
    return true;
}

bool LoopbackDirectPlayTransport::Send(const void* data, std::size_t size, bool /*reliable*/) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    buffered_.emplace_back(bytes, bytes + size);
    return true;
}

bool LoopbackDirectPlayTransport::Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) {
    if (buffered_.empty()) return false;
    const auto& front = buffered_.front();
    if (front.size() > bufferSize) return false;
    if (!front.empty()) std::memcpy(buffer, front.data(), front.size());
    if (outSize) *outSize = front.size();
    buffered_.pop_front();
    return true;
}

void LoopbackDirectPlayTransport::Service() {
    // No-op: loopback has no real network events - Send() already delivers synchronously.
}

bool LoopbackDirectPlayTransport::HasPendingConnection() const {
    // Loopback has no incoming-connection concept at all - it only ever talks to itself.
    return false;
}

bool LoopbackDirectPlayTransport::AssignPendingConnection(DPID /*id*/) {
    return false;
}

bool LoopbackDirectPlayTransport::RejectPendingConnection() {
    // No incoming-connection concept, so nothing to reject either.
    return false;
}

bool LoopbackDirectPlayTransport::HasDisconnectedPeer() const {
    // No incoming-connection concept, so no disconnect-of-one concept either.
    return false;
}

bool LoopbackDirectPlayTransport::TakeDisconnectedPeer(DPID* /*outId*/) {
    return false;
}

void LoopbackDirectPlayTransport::Shutdown() { buffered_.clear(); }

} // namespace free_direct_directplay
