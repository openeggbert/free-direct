/**
 * @file RelayServer.hpp
 * @brief The WebSocket front end of `free-direct-server`: IXWebSocket around `RelayHub`.
 *
 * Used by the program (`main.cpp`) and by Free Direct's relay integration tests, which run a
 * server in the test process on a free port.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "RelayHub.hpp"

#include <memory>
#include <string>

namespace free_direct_relay {

struct ServerOptions {
    std::string host = "0.0.0.0";
    int port = kDefaultRelayPort;  ///< 0: any free port (see RelayServer::Port)
    HubConfig hub;
    std::string tlsCert;  ///< PEM certificate chain; with tlsKey, serve wss://
    std::string tlsKey;
    int pingSeconds = 20;  ///< WebSocket ping interval; 0 disables
    /// Datagrams beside the WebSocket (RelayDatagram.hpp): a UDP socket on udpPort (0: the
    /// WebSocket's port number). If it cannot be opened the server runs without, and says so.
    bool datagrams = true;
    int udpPort = 0;
    /// The same datagrams over WebRTC data channels, for browsers (built with libdatachannel only):
    /// every peer connection's ICE on one UDP port, rtcPort (0: the datagram port + 1).
    bool webrtc = true;
    int rtcPort = 0;
};

class RelayServer {
public:
    RelayServer(ServerOptions options, RelayHub::Logger log);
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;

    /// Listens and starts serving. False with `error` if the port cannot be opened.
    bool Start(std::string& error);
    /// The port actually listened on.
    int Port() const;
    /// Deletes idle rooms; call about once a second.
    void Reap();
    /// Ends every session, closes every connection and stops listening.
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Fills `count` bytes from the operating system's random source.
void FillRandom(std::uint8_t* out, std::size_t count);

} // namespace free_direct_relay
