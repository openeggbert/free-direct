/**
 * @file RelayDirectPlayTransport.cpp
 * @brief `IDirectPlayTransport` over the Free Direct server. See RelayDirectPlayTransport.hpp.
 * @note Status: IMPLEMENTED
 */
#include "RelayDirectPlayTransport.hpp"

#include <algorithm>
#include <cstring>

namespace free_direct_directplay {

using namespace free_direct_relay;

namespace {
/// A host keeps at most this many received messages; beyond it the oldest are dropped, as
/// DirectPlayMessageQueue does for its own bounded queue.
constexpr std::size_t kMaxReceivedMessages = 4096;
} // namespace

RelayDirectPlayTransport::~RelayDirectPlayTransport() { Shutdown(); }

bool RelayDirectPlayTransport::Host(const SessionInfo& info, std::string& error) {
    if (role_ != Role::None) return false;
    RelayClient& client = RelayClient::Instance();
    // The role is set before the request: while waiting for the server's answer the client
    // keeps processing messages, and a member's PeerJoined can follow SessionHosted at once.
    role_ = Role::Host;
    client.SetListener(this);
    if (!client.HostSession(info, error)) {
        client.ClearListener(this);
        role_ = Role::None;
        return false;
    }
    return true;
}

bool RelayDirectPlayTransport::Join(const RelayGuid& instance, std::string& error) {
    if (role_ != Role::None) return false;
    RelayClient& client = RelayClient::Instance();
    // Set before the request, for the same reason as in Host(): the host can admit this member
    // and send its JoinAccept before the server's SessionJoined has been processed here.
    role_ = Role::Member;
    memberConnected_ = true;
    client.SetListener(this);
    if (!client.JoinSession(instance, error)) {
        client.ClearListener(this);
        role_ = Role::None;
        memberConnected_ = false;
        received_.clear();
        return false;
    }
    return true;
}

bool RelayDirectPlayTransport::Listen(std::uint16_t) { return false; }
bool RelayDirectPlayTransport::Connect(const char*, std::uint16_t) { return false; }

bool RelayDirectPlayTransport::Send(DPID targetId, const void* data, std::size_t size, bool) {
    RelayClient& client = RelayClient::Instance();
    if (role_ == Role::Member) {
        if (!memberConnected_) return false;
        return client.SendData(Route::ToHost, 0, data, size);
    }
    if (role_ == Role::Host) {
        auto it = peers_.find(targetId);
        if (it == peers_.end()) return false;
        return client.SendData(Route::ToMember, it->second, data, size);
    }
    return false;
}

bool RelayDirectPlayTransport::SendToAll(const void* data, std::size_t size, bool) {
    if (role_ == Role::None || (role_ == Role::Member && !memberConnected_)) return false;
    return RelayClient::Instance().SendData(Route::ToAllOthers, 0, data, size);
}

bool RelayDirectPlayTransport::Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) {
    if (received_.empty()) return false;
    const auto& front = received_.front();
    if (front.size() > bufferSize) {
        // Larger than any DirectPlay message: drop it rather than wedge the queue behind it.
        received_.pop_front();
        return false;
    }
    if (!front.empty()) std::memcpy(buffer, front.data(), front.size());
    if (outSize) *outSize = front.size();
    received_.pop_front();
    return true;
}

void RelayDirectPlayTransport::Service() { RelayClient::Instance().Pump(); }

bool RelayDirectPlayTransport::AssignPendingConnection(DPID id) {
    if (pending_.empty()) return false;
    const ConnId conn = pending_.front();
    pending_.pop_front();
    peers_[id] = conn;
    RelayClient::Instance().Admit(conn);
    return true;
}

bool RelayDirectPlayTransport::RejectPendingConnection() {
    if (pending_.empty()) return false;
    const ConnId conn = pending_.front();
    pending_.pop_front();
    RelayClient::Instance().Refuse(conn);
    return true;
}

bool RelayDirectPlayTransport::TakeDisconnectedPeer(DPID* outId) {
    if (disconnected_.empty()) return false;
    if (outId) *outId = disconnected_.front();
    disconnected_.pop_front();
    return true;
}

void RelayDirectPlayTransport::Shutdown() {
    RelayClient& client = RelayClient::Instance();
    if (role_ == Role::Host) {
        client.CloseSession();
    } else if (role_ == Role::Member && memberConnected_) {
        client.LeaveSession();
    }
    client.ClearListener(this);
    role_ = Role::None;
    memberConnected_ = false;
    received_.clear();
    pending_.clear();
    peers_.clear();
    disconnected_.clear();
}

void RelayDirectPlayTransport::OnPeerJoined(ConnId conn) {
    if (role_ == Role::Host) pending_.push_back(conn);
}

void RelayDirectPlayTransport::OnPeerLeft(ConnId conn) {
    if (role_ != Role::Host) return;
    auto pendingIt = std::find(pending_.begin(), pending_.end(), conn);
    if (pendingIt != pending_.end()) {
        pending_.erase(pendingIt);
        return;
    }
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->second == conn) {
            disconnected_.push_back(it->first);
            peers_.erase(it);
            return;
        }
    }
}

void RelayDirectPlayTransport::OnSessionEnded(SessionEndReason) {
    if (role_ == Role::Member) memberConnected_ = false;
}

void RelayDirectPlayTransport::OnDeliver(ConnId, std::vector<std::uint8_t> payload) {
    if (role_ == Role::None) return;
    if (received_.size() >= kMaxReceivedMessages) received_.pop_front();
    received_.push_back(std::move(payload));
}

void RelayDirectPlayTransport::OnDisconnected() {
    if (role_ == Role::Member) memberConnected_ = false;
    if (role_ == Role::Host) {
        for (const auto& [id, conn] : peers_) disconnected_.push_back(id);
        peers_.clear();
        pending_.clear();
    }
}

} // namespace free_direct_directplay
