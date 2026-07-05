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

void LoopbackDirectPlayTransport::Shutdown() { buffered_.clear(); }

} // namespace free_direct_directplay
