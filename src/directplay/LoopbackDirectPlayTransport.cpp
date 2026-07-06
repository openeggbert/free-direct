/**
 * @file LoopbackDirectPlayTransport.cpp
 * @brief In-process `IDirectPlayTransport` backend, with no real sockets.
 * @note Status: PARTIAL
 */
#include "LoopbackDirectPlayTransport.hpp"

#include <cstring>

namespace free_direct_directplay {

namespace {
// Process-wide static registry (docs/directplay-design.md Decision 10), keyed by the `port`
// argument Listen()/Connect() are called with - lets a separate "client" instance find and
// connect to a separate "host" instance in the same process. Function-local static avoids
// static-init-order issues.
std::unordered_map<std::uint16_t, LoopbackDirectPlayTransport*>& LoopbackRegistry() {
    static std::unordered_map<std::uint16_t, LoopbackDirectPlayTransport*> registry;
    return registry;
}
} // namespace

LoopbackDirectPlayTransport::~LoopbackDirectPlayTransport() { Shutdown(); }

bool LoopbackDirectPlayTransport::Listen(std::uint16_t port) {
    if (listening_ || hostPeer_) return false;

    auto& registry = LoopbackRegistry();
    // Mirrors EnetDirectPlayTransport::Listen()'s real bind()-conflict behavior: a port already
    // in use fails rather than "coexisting", so Connect() always resolves a port to exactly one
    // host deterministically (docs/directplay-design.md Decision 10).
    if (registry.find(port) != registry.end()) return false;

    registry[port] = this;
    listenPort_ = port;
    listening_ = true;
    return true;
}

bool LoopbackDirectPlayTransport::Connect(const char* /*address*/, std::uint16_t port) {
    if (listening_ || hostPeer_) return false;

    auto& registry = LoopbackRegistry();
    auto it = registry.find(port);
    // Deliberate deviation from EnetDirectPlayTransport::Connect(), which succeeds even with
    // nobody listening yet (discovered later via a Service() timeout): loopback has synchronous,
    // perfect knowledge of whether a host is registered, so Connect() fails immediately here
    // instead of queuing a doomed attempt (docs/directplay-design.md Decision 10).
    if (it == registry.end()) return false;

    hostPeer_ = it->second;
    hostPeer_->pendingPeers_.push_back(this);
    return true;
}

bool LoopbackDirectPlayTransport::Send(DPID targetId, const void* data, std::size_t size,
                                        bool /*reliable*/) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);

    // Hosting role: targetId addresses one specific connected peer out of potentially many
    // (docs/directplay-design.md Decision 14) - reaching into that peer's own buffered_
    // directly, the same mechanism Connect()/RejectPendingConnection()/Shutdown() already use.
    if (listening_) {
        auto it = connectedPeers_.find(targetId);
        if (it == connectedPeers_.end()) return false;
        it->second->buffered_.emplace_back(bytes, bytes + size);
        return true;
    }

    // Joining role: exactly one possible destination - the host - so targetId is accepted but
    // ignored.
    if (hostPeer_) {
        hostPeer_->buffered_.emplace_back(bytes, bytes + size);
        return true;
    }

    // Self-send-only mode (never Listen()/Connect()ed): push into this instance's own inbox.
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
    // No-op: loopback has no real network events - Connect()/AssignPendingConnection() etc.
    // already update connection state synchronously.
}

bool LoopbackDirectPlayTransport::HasPendingConnection() const { return !pendingPeers_.empty(); }

bool LoopbackDirectPlayTransport::AssignPendingConnection(DPID id) {
    if (pendingPeers_.empty()) return false;
    connectedPeers_[id] = pendingPeers_.front();
    pendingPeers_.pop_front();
    return true;
}

bool LoopbackDirectPlayTransport::RejectPendingConnection() {
    if (pendingPeers_.empty()) return false;

    // "Graceful disconnect": loopback is synchronous and in-process, so there is no wait to
    // perform - clearing the rejected peer's hostPeer_ immediately is the whole of it.
    LoopbackDirectPlayTransport* peer = pendingPeers_.front();
    pendingPeers_.pop_front();
    peer->hostPeer_ = nullptr;
    return true;
}

bool LoopbackDirectPlayTransport::HasDisconnectedPeer() const { return !disconnectedPeerIds_.empty(); }

bool LoopbackDirectPlayTransport::TakeDisconnectedPeer(DPID* outId) {
    if (disconnectedPeerIds_.empty()) return false;
    if (outId) *outId = disconnectedPeerIds_.front();
    disconnectedPeerIds_.pop_front();
    return true;
}

bool LoopbackDirectPlayTransport::IsConnectedToHost() const { return hostPeer_ != nullptr; }

void LoopbackDirectPlayTransport::Shutdown() {
    if (listening_) {
        // Scrub every known peer's hostPeer_ back to null - required for memory safety (a peer
        // outliving this host must not hold a dangling pointer), not just ENet-parity ceremony.
        for (auto& [id, peer] : connectedPeers_) peer->hostPeer_ = nullptr;
        for (LoopbackDirectPlayTransport* peer : pendingPeers_) peer->hostPeer_ = nullptr;
        connectedPeers_.clear();
        pendingPeers_.clear();
        disconnectedPeerIds_.clear();

        auto& registry = LoopbackRegistry();
        auto it = registry.find(listenPort_);
        // Defensive identity check: guards a double Shutdown() call, and correctness after a
        // later instance has already reused this same port number.
        if (it != registry.end() && it->second == this) registry.erase(it);
        listening_ = false;
    }

    if (hostPeer_) {
        bool wasAssigned = false;
        DPID assignedId = 0;
        for (auto it = hostPeer_->connectedPeers_.begin(); it != hostPeer_->connectedPeers_.end(); ++it) {
            if (it->second == this) {
                assignedId = it->first;
                wasAssigned = true;
                hostPeer_->connectedPeers_.erase(it);
                break;
            }
        }
        if (wasAssigned) {
            // Only an already-assigned peer's DPID is reported (docs/directplay-design.md
            // Decision 8's rule, mirrored here) - a peer still only pending has no DPID for
            // anyone outside the transport to reconcile against.
            hostPeer_->disconnectedPeerIds_.push_back(assignedId);
        } else {
            for (auto it = hostPeer_->pendingPeers_.begin(); it != hostPeer_->pendingPeers_.end(); ++it) {
                if (*it == this) {
                    hostPeer_->pendingPeers_.erase(it);
                    break;
                }
            }
        }
        hostPeer_ = nullptr;
    }

    buffered_.clear();
}

} // namespace free_direct_directplay
