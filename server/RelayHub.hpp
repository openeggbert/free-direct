/**
 * @file RelayHub.hpp
 * @brief The room and session logic of `free-direct-server`, independent of any network code.
 *
 * The WebSocket front end (`main.cpp`) reports connection events and received messages; the hub
 * answers with a list of actions (messages to send, connections to close) for the caller to
 * execute after it has released its lock. The hub itself is not thread-safe: call it under one
 * mutex. Time is passed in, so tests can drive idle timeouts without waiting.
 *
 * Model (docs/directplay-relay-protocol.md):
 * - A connection says Hello (protocol version, optional server password, compatibility identity
 *   of its game and edition), then creates a room or joins one with its secret ID.
 * - A room is a private virtual LAN for one game and edition. Inside it, a member can host a
 *   DirectPlay session or join one; the session's host admits or refuses each joiner.
 * - Data travels only within a session: member -> host, host -> member, or to all others.
 * - A room without members is deleted after `roomIdleTimeout`.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "RelayDatagram.hpp"
#include "RelayProtocol.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace free_direct_relay {

struct HubConfig {
    std::string password;  ///< empty: no server password
    std::size_t maxConnections = 512;
    std::size_t maxRooms = 256;
    std::size_t maxMembersPerRoom = 16;
    std::size_t maxSessionsPerRoom = 8;
    std::chrono::seconds roomIdleTimeout{600};
    unsigned maxFailuresPerConnection = 5;           ///< wrong room IDs before the connection is closed
    unsigned maxFailuresPerAddress = 20;             ///< wrong passwords/room IDs per address and window
    std::chrono::seconds failureWindow{600};
    std::vector<std::string> allowedOrigins;         ///< browser Origin values; empty: any
    /// The server's UDP port for datagrams (RelayDatagram.hpp); 0: none. Welcome offers it.
    std::uint16_t datagramPort = 0;
};

struct HubAction {
    enum class Kind { Send, Close };
    Kind kind = Kind::Send;
    ConnId conn = 0;
    std::vector<std::uint8_t> bytes;  ///< Send
    std::uint16_t closeCode = 1000;   ///< Close
    std::string reason;               ///< Close
    /// Send: a Deliver of a message that came as a datagram; the server sends it as a datagram to a
    /// connection with a bound address, over the WebSocket to any other.
    bool datagram = false;
};

class RelayHub {
public:
    using Clock = std::chrono::steady_clock;
    /// Fills `count` bytes with cryptographically random data (room IDs).
    using RandomSource = std::function<void(std::uint8_t* out, std::size_t count)>;
    using Logger = std::function<void(const std::string& line)>;

    RelayHub(HubConfig config, RandomSource random, Logger log = nullptr);

    std::vector<HubAction> OnOpen(ConnId conn, const std::string& address, const std::string& origin,
                                  Clock::time_point now);
    std::vector<HubAction> OnMessage(ConnId conn, const std::uint8_t* data, std::size_t size,
                                     Clock::time_point now);
    std::vector<HubAction> OnClose(ConnId conn, Clock::time_point now);
    /// Ends every session (SessionEnded ServerShutdown) and closes every connection.
    std::vector<HubAction> Shutdown();
    /// Deletes rooms that have had no members for roomIdleTimeout.
    void Reap(Clock::time_point now);

    /// Datagrams (RelayDatagram.hpp): offered in Welcome from now on.
    void SetDatagramPort(std::uint16_t port) { config_.datagramPort = port; }
    /// The welcomed connection a datagram token belongs to; 0 if none.
    ConnId ConnForToken(const DatagramToken& token) const;
    /// A non-guaranteed Data (route, target, payload) that came as a datagram from the connection's
    /// bound address: routed as the WebSocket's Data, its Deliver actions marked `datagram`.
    std::vector<HubAction> OnDatagramData(ConnId conn, const std::uint8_t* body, std::size_t size,
                                          Clock::time_point now);

    std::size_t ConnectionCount() const { return conns_.size(); }
    std::size_t RoomCount() const { return rooms_.size(); }
    std::size_t SessionCount() const;

private:
    enum class Role { None, Host, Member };

    struct Conn {
        std::string address;
        bool welcomed = false;
        std::string compat;
        std::string roomId;  ///< normalized; empty: in no room
        unsigned failures = 0;
        Role role = Role::None;
        RelayGuid session{};
        bool admitted = false;  ///< Role::Member only
        DatagramToken token{};  ///< with datagrams: given in Welcome
    };

    struct Session {
        SessionInfo info;
        ConnId host = 0;
        std::map<ConnId, bool> members;  ///< member conn -> admitted
    };

    struct Room {
        std::string compat;
        std::set<ConnId> members;
        std::map<RelayGuid, Session> sessions;
        Clock::time_point emptySince{};
    };

    void HandleHello(ConnId id, Conn& c, Reader& r, Clock::time_point now, std::vector<HubAction>& out);
    void HandleCreateRoom(ConnId id, Conn& c, Clock::time_point now, std::vector<HubAction>& out);
    void HandleJoinRoom(ConnId id, Conn& c, Reader& r, Clock::time_point now, std::vector<HubAction>& out);
    void HandleListSessions(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out);
    void HandleHostSession(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out);
    void HandleJoinSession(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out);
    void HandleAdmitOrRefuse(ConnId id, Conn& c, Reader& r, bool admit, std::vector<HubAction>& out);
    void HandleData(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out, bool datagram = false);

    /// Leaves the connection's session (ending it if the connection hosts it).
    void LeaveSession(ConnId id, Conn& c, SessionEndReason reason, std::vector<HubAction>& out);
    void LeaveRoom(ConnId id, Conn& c, Clock::time_point now, std::vector<HubAction>& out);
    Session* FindSession(const Conn& c);

    /// Counts a failed password/room attempt. Returns true when the connection must be closed.
    bool CountFailure(ConnId id, Conn& c, Clock::time_point now);
    bool AddressRateLimited(const std::string& address, Clock::time_point now);

    static void Send(std::vector<HubAction>& out, ConnId to, std::vector<std::uint8_t> bytes);
    static void SendError(std::vector<HubAction>& out, ConnId to, MsgType request, ErrorCode code);
    static void Close(std::vector<HubAction>& out, ConnId conn, std::uint16_t code, const std::string& reason);
    void Log(const std::string& line) const;
    static std::string MaskRoomId(const std::string& normalized);

    HubConfig config_;
    RandomSource random_;
    Logger log_;
    std::unordered_map<ConnId, Conn> conns_;
    std::unordered_map<std::string, Room> rooms_;
    std::unordered_map<std::string, std::deque<Clock::time_point>> addressFailures_;
    std::map<DatagramToken, ConnId> tokens_;
};

/// Constant-time comparison for the server password.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

} // namespace free_direct_relay
