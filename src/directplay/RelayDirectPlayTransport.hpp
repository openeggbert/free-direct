/**
 * @file RelayDirectPlayTransport.hpp
 * @brief `IDirectPlayTransport` over the Free Direct server (Internet multiplayer).
 *
 * Private to the DirectPlay implementation, compiled only with FREE_DIRECT_ENABLE_RELAY. The
 * session layer keeps its roles: the hosting game assigns DPIDs and admits or refuses joiners,
 * exactly as over loopback or ENet. The server only relays: a joiner's "connection" is its
 * membership in the host's session on the server, and a peer is addressed by the server's
 * connection id, which this class maps to and from DPIDs.
 *
 * One difference from the other backends: the server delivers a broadcast to every admitted
 * member itself (`FansOutBroadcasts()`), so the host must not relay a joiner's broadcast again
 * (docs/directplay-design.md Decision 30).
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "DirectPlayTransport.hpp"
#include "RelayClient.hpp"

#include <deque>
#include <unordered_map>
#include <vector>

namespace free_direct_directplay {

class RelayDirectPlayTransport final : public IDirectPlayTransport, public IRelaySessionListener {
public:
    RelayDirectPlayTransport() = default;
    ~RelayDirectPlayTransport() override;
    RelayDirectPlayTransport(const RelayDirectPlayTransport&) = delete;
    RelayDirectPlayTransport& operator=(const RelayDirectPlayTransport&) = delete;

    /// Hosts `info` on the server, in the room the process is in.
    bool Host(const free_direct_relay::SessionInfo& info, std::string& error);
    /// Joins the session `instance` of the current room. Admission follows over the session
    /// layer (Join, JoinAccept), like for the other backends.
    bool Join(const free_direct_relay::RelayGuid& instance, std::string& error);

    // IDirectPlayTransport. Listen/Connect are not used: Host/Join need DirectPlay data.
    bool Listen(std::uint16_t port) override;
    bool Connect(const char* address, std::uint16_t port) override;
    bool Send(DPID targetId, const void* data, std::size_t size, bool reliable) override;
    bool SendToAll(const void* data, std::size_t size, bool reliable) override;
    bool FansOutBroadcasts() const override { return true; }
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Service() override;
    bool HasPendingConnection() const override { return !pending_.empty(); }
    bool AssignPendingConnection(DPID id) override;
    bool RejectPendingConnection() override;
    bool HasDisconnectedPeer() const override { return !disconnected_.empty(); }
    bool TakeDisconnectedPeer(DPID* outId) override;
    bool IsConnectedToHost() const override { return role_ == Role::Member && memberConnected_; }
    void Shutdown() override;

    // IRelaySessionListener
    void OnPeerJoined(free_direct_relay::ConnId conn) override;
    void OnPeerLeft(free_direct_relay::ConnId conn) override;
    void OnSessionEnded(free_direct_relay::SessionEndReason reason) override;
    void OnDeliver(free_direct_relay::ConnId from, std::vector<std::uint8_t> payload) override;
    void OnDisconnected() override;

private:
    enum class Role { None, Host, Member };

    Role role_ = Role::None;
    bool memberConnected_ = false;
    std::deque<std::vector<std::uint8_t>> received_;
    std::deque<free_direct_relay::ConnId> pending_;                  ///< host: waiting for a DPID
    std::unordered_map<DPID, free_direct_relay::ConnId> peers_;      ///< host: assigned members
    std::deque<DPID> disconnected_;                                  ///< host: assigned members gone
};

} // namespace free_direct_directplay
