/**
 * @file RelayClient.hpp
 * @brief The process-wide connection to a Free Direct server: hello, room, session requests.
 *
 * Private to the DirectPlay implementation, compiled only with FREE_DIRECT_ENABLE_RELAY. One
 * connection serves the whole process: the room the player entered stays entered while the game
 * creates, joins and closes DirectPlay sessions in it, and while it re-creates its DirectPlay
 * object. Requests are synchronous with a bounded wait (FREE_DIRECT_RELAY_TIMEOUT_MS, default
 * 8000 ms) that keeps processing incoming messages; the legacy DirectPlay API is synchronous and
 * the games call it from their one thread. Session traffic goes to the attached
 * `IRelaySessionListener` (the session's `RelayDirectPlayTransport`).
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "RelayDatagrams.hpp"
#include "RelayProtocol.hpp"
#include "RelaySocket.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace free_direct_directplay {

/// Receives the session events of the one session this process is in.
class IRelaySessionListener {
public:
    virtual ~IRelaySessionListener() = default;
    virtual void OnPeerJoined(free_direct_relay::ConnId conn) = 0;
    virtual void OnPeerLeft(free_direct_relay::ConnId conn) = 0;
    virtual void OnSessionEnded(free_direct_relay::SessionEndReason reason) = 0;
    virtual void OnDeliver(free_direct_relay::ConnId from, std::vector<std::uint8_t> payload) = 0;
    /// The connection to the server is gone; so is the session.
    virtual void OnDisconnected() = 0;
};

class RelayClient {
public:
    static RelayClient& Instance();

    /// Connects to `url` and says hello with `password` and `compat` (the game and edition
    /// identity). Reuses the current connection if it is to the same url with the same
    /// password and compat. On failure, `error` says why in English.
    bool Connect(const std::string& url, const std::string& password, const std::string& compat,
                 std::string& error);
    /// Creates a room (leaving the current one); `outRoomId` is its formatted secret ID.
    bool CreateRoom(std::string& outRoomId, std::string& error);
    /// Enters the room with this secret ID (typed or pasted; separators and case do not matter).
    bool JoinRoom(const std::string& roomId, std::string& error);
    /// Closes the connection.
    void Disconnect();

    bool IsConnected() const { return connected_; }
    bool InRoom() const { return connected_ && !roomId_.empty(); }
    const std::string& Url() const { return url_; }
    const std::string& RoomId() const { return roomId_; }

    bool ListSessions(std::vector<free_direct_relay::SessionInfo>& out, std::string& error);
    bool HostSession(const free_direct_relay::SessionInfo& info, std::string& error);
    bool JoinSession(const free_direct_relay::RelayGuid& instance, std::string& error);
    void CloseSession();
    void LeaveSession();
    void Admit(free_direct_relay::ConnId conn);
    void Refuse(free_direct_relay::ConnId conn);
    /// A non-guaranteed message (`reliable` false) goes as a datagram once the server has bound this
    /// program's address (RelayDatagrams.hpp); anything else over the WebSocket.
    bool SendData(free_direct_relay::Route route, free_direct_relay::ConnId target, const void* data,
                  std::size_t size, bool reliable = true);
    /// Datagrams are on: the server acknowledged this program's address.
    bool DatagramsOn() const { return datagrams_ && datagrams_->Ready(); }

    /// Processes every queued incoming event; called from DirectPlay's Receive() servicing and
    /// from every bounded wait.
    void Pump();

    void SetListener(IRelaySessionListener* listener) { listener_ = listener; }
    void ClearListener(IRelaySessionListener* listener) {
        if (listener_ == listener) listener_ = nullptr;
    }

private:
    RelayClient() = default;

    /// What the current request waits for: success when a message of `successType` arrives,
    /// failure when an Error for `requestType` arrives or the connection closes.
    struct Pending {
        bool active = false;
        bool done = false;
        bool ok = false;
        free_direct_relay::MsgType requestType{};
        free_direct_relay::MsgType successType{};
        std::uint32_t requestId = 0;
        std::string error;
        std::vector<std::uint8_t> reply;  ///< the success message, without its type byte
    };

    bool Request(free_direct_relay::Writer message, free_direct_relay::MsgType successType,
                 std::uint32_t requestId, std::vector<std::uint8_t>& reply, std::string& error);
    bool WaitFor(bool (*done)(const RelayClient&), long timeoutMs);
    void Handle(const std::vector<std::uint8_t>& message);
    void OnClosed(const std::string& reason);
    static long TimeoutMs();

    std::unique_ptr<IRelaySocket> socket_;
    std::unique_ptr<IRelayDatagrams> datagrams_;  ///< when Welcome offers datagrams
    free_direct_relay::DatagramToken datagramToken_{};
    bool opened_ = false;
    bool connected_ = false;   ///< opened and welcomed
    std::string closeReason_;
    std::string url_;
    std::string password_;
    std::string compat_;
    std::string roomId_;
    std::uint32_t nextRequestId_ = 1;
    Pending pending_;
    IRelaySessionListener* listener_ = nullptr;
};

} // namespace free_direct_directplay
