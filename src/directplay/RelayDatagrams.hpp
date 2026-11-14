/**
 * @file RelayDatagrams.hpp
 * @brief The client's datagram channel to the Free Direct server, beside the WebSocket
 * (src/relay/RelayDatagram.hpp, docs/directplay-relay-protocol.md "Datagrams").
 *
 * RelayClient sends non-guaranteed Data through it once the server has bound this program's
 * address (BindAck), and hands its Deliver datagrams to the session like the WebSocket's. Native
 * programs use UDP (RelayDatagramsUdp.cpp); where there is none, CreateRelayDatagrams() returns
 * nothing and everything stays on the WebSocket.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "RelayDatagram.hpp"
#include "RelayProtocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace free_direct_directplay {

class IRelayDatagrams {
public:
    using DeliverFn = std::function<void(free_direct_relay::ConnId from, std::vector<std::uint8_t> payload)>;

    virtual ~IRelayDatagrams() = default;
    /// Starts binding to the server at `host` (the WebSocket's host) and `port` (Welcome's).
    /// False if no socket could be made; the caller then keeps to the WebSocket.
    virtual bool Start(const std::string& host, std::uint16_t port, const free_direct_relay::DatagramToken& token) = 0;
    /// The server has acknowledged this program's address.
    virtual bool Ready() const = 0;
    /// Sends one datagram (already encoded); false if it could not be sent.
    virtual bool Send(const std::vector<std::uint8_t>& datagram) = 0;
    /// Reads what has arrived (Deliver goes to `deliver`), repeats Bind until acknowledged, gives up
    /// after kDatagramBindTimeoutMs, and keeps the address bound afterwards.
    virtual void Pump(const DeliverFn& deliver) = 0;
};

/// A datagram channel, or nothing on a platform without one.
std::unique_ptr<IRelayDatagrams> CreateRelayDatagrams();

/// The host of a ws:// or wss:// URL, without brackets.
std::string RelayUrlHost(const std::string& url);

} // namespace free_direct_directplay
