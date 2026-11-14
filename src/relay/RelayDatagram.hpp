/**
 * @file RelayDatagram.hpp
 * @brief The Free Direct server's datagrams: non-guaranteed Data and its Deliver over UDP, beside
 * the WebSocket (docs/directplay-relay-protocol.md, "Datagrams").
 *
 * Over the WebSocket every message is delivered reliably and in order, so one lost TCP segment
 * holds back every message behind it until TCP resends it, and they then arrive together: a game
 * that predicts the other players for a few frames shows a stop and a jump (docs/relay-latency.md).
 * A datagram is lost on its own, as DirectPlay's own non-guaranteed messages were. Everything
 * guaranteed, and every control message, stays on the WebSocket.
 *
 * Shared by the server and the clients; header-only, no sockets.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace free_direct_relay {

constexpr std::uint32_t kDatagramMagic = 0x55524446;  ///< "FDRU", little-endian
constexpr std::size_t kDatagramTokenBytes = 16;
/// Larger datagrams risk fragmentation; a client sends bigger non-guaranteed messages over the
/// WebSocket instead.
constexpr std::size_t kMaxDatagramBytes = 1200;
/// Welcome's flags (after the connection ID).
constexpr std::uint8_t kWelcomeFlagPassword = 0x01;
/// The server takes datagrams: Welcome's flags are followed by the UDP port `u16` and the
/// connection's datagram token (16 bytes).
constexpr std::uint8_t kWelcomeFlagDatagrams = 0x02;
/// A client that has had no BindAck within this time sends everything over the WebSocket.
constexpr unsigned kDatagramBindTimeoutMs = 3000;
constexpr unsigned kDatagramBindRetryMs = 250;
/// A bound client repeats Bind this often, which keeps its NAT mapping open.
constexpr unsigned kDatagramKeepAliveMs = 15000;

using DatagramToken = std::array<std::uint8_t, kDatagramTokenBytes>;

enum class DatagramType : std::uint8_t {
    Bind = 0x01,      ///< client: token only; binds (or rebinds) the sender's address to the connection
    Data = 0x02,      ///< client: route u8, target connection u32, payload (as the WebSocket's Data)
    BindAck = 0x81,   ///< server: the address is bound
    Deliver = 0x82,   ///< server: sender connection u32, payload (as the WebSocket's Deliver)
};

namespace datagram_detail {
inline void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
inline std::uint32_t GetU32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
} // namespace datagram_detail

/// Client to server: magic, token, type, then the type's fields.
inline std::vector<std::uint8_t> ClientDatagram(const DatagramToken& token, DatagramType type) {
    std::vector<std::uint8_t> b;
    datagram_detail::PutU32(b, kDatagramMagic);
    b.insert(b.end(), token.begin(), token.end());
    b.push_back(static_cast<std::uint8_t>(type));
    return b;
}

inline std::vector<std::uint8_t> DataDatagram(const DatagramToken& token, std::uint8_t route, std::uint32_t target,
                                              const void* payload, std::size_t size) {
    std::vector<std::uint8_t> b = ClientDatagram(token, DatagramType::Data);
    b.push_back(route);
    datagram_detail::PutU32(b, target);
    const auto* p = static_cast<const std::uint8_t*>(payload);
    b.insert(b.end(), p, p + size);
    return b;
}

/// A client datagram's parts; false if it is not one (wrong magic, too short, too long).
struct ClientDatagramView {
    DatagramToken token{};
    DatagramType type = DatagramType::Bind;
    const std::uint8_t* body = nullptr;  ///< the type's fields
    std::size_t bodySize = 0;
};
inline bool ParseClientDatagram(const std::uint8_t* data, std::size_t size, ClientDatagramView& out) {
    constexpr std::size_t header = 4 + kDatagramTokenBytes + 1;
    if (size < header || size > kMaxDatagramBytes || datagram_detail::GetU32(data) != kDatagramMagic) return false;
    std::memcpy(out.token.data(), data + 4, kDatagramTokenBytes);
    out.type = static_cast<DatagramType>(data[4 + kDatagramTokenBytes]);
    out.body = data + header;
    out.bodySize = size - header;
    return out.type == DatagramType::Bind || out.type == DatagramType::Data;
}

/// Server to client: magic, type, then the type's fields.
inline std::vector<std::uint8_t> ServerDatagram(DatagramType type) {
    std::vector<std::uint8_t> b;
    datagram_detail::PutU32(b, kDatagramMagic);
    b.push_back(static_cast<std::uint8_t>(type));
    return b;
}

inline std::vector<std::uint8_t> DeliverDatagram(std::uint32_t sender, const std::uint8_t* payload, std::size_t size) {
    std::vector<std::uint8_t> b = ServerDatagram(DatagramType::Deliver);
    datagram_detail::PutU32(b, sender);
    b.insert(b.end(), payload, payload + size);
    return b;
}

/// A server datagram: BindAck, or Deliver with its sender and payload. False if it is not one.
struct ServerDatagramView {
    DatagramType type = DatagramType::BindAck;
    std::uint32_t sender = 0;                ///< Deliver
    const std::uint8_t* payload = nullptr;   ///< Deliver
    std::size_t payloadSize = 0;
};
inline bool ParseServerDatagram(const std::uint8_t* data, std::size_t size, ServerDatagramView& out) {
    if (size < 5 || size > kMaxDatagramBytes || datagram_detail::GetU32(data) != kDatagramMagic) return false;
    out.type = static_cast<DatagramType>(data[4]);
    if (out.type == DatagramType::BindAck) return true;
    if (out.type != DatagramType::Deliver || size < 9) return false;
    out.sender = datagram_detail::GetU32(data + 5);
    out.payload = data + 9;
    out.payloadSize = size - 9;
    return true;
}

} // namespace free_direct_relay
