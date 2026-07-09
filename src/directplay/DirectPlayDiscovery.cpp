/**
 * @file DirectPlayDiscovery.cpp
 * @brief Raw-UDP LAN session discovery implementation. See DirectPlayDiscovery.hpp.
 * @note Status: PARTIAL
 */
#include "DirectPlayDiscovery.hpp"
#include "DirectPlayWireProtocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace free_direct_directplay {

namespace {

// Flat, padding-free serialization for a DiscoveryResponse's payload (the fields
// DirectPlayWirePacketHeader itself has no room for): dwMaxPlayers, dwCurrentPlayers, then a
// length-prefixed session name. Mirrors DirectPlayWireProtocol.hpp's own per-field memcpy style
// rather than bulk-copying a struct's in-memory layout.
void SerializeDiscoveryResponsePayload(const DiscoveredSessionInfo& info,
                                        std::vector<std::uint8_t>& out) {
    const std::uint32_t maxPlayers = info.maxPlayers;
    const std::uint32_t currentPlayers = info.currentPlayers;
    const std::uint32_t nameLength = static_cast<std::uint32_t>(info.sessionName.size());

    const std::size_t base = out.size();
    out.resize(base + sizeof(maxPlayers) + sizeof(currentPlayers) + sizeof(nameLength) + nameLength);
    std::uint8_t* p = out.data() + base;
    std::memcpy(p, &maxPlayers, sizeof(maxPlayers));
    p += sizeof(maxPlayers);
    std::memcpy(p, &currentPlayers, sizeof(currentPlayers));
    p += sizeof(currentPlayers);
    std::memcpy(p, &nameLength, sizeof(nameLength));
    p += sizeof(nameLength);
    if (nameLength > 0) std::memcpy(p, info.sessionName.data(), nameLength);
}

// Inverse of SerializeDiscoveryResponsePayload. Defensive against a truncated/malformed buffer
// (an untrusted network payload), matching TryDeserializeDirectPlayWireHeader's own posture -
// returns false without reading past dataSize on any size mismatch.
bool TryDeserializeDiscoveryResponsePayload(const std::uint8_t* data, std::size_t dataSize,
                                             DWORD& outMaxPlayers, DWORD& outCurrentPlayers,
                                             std::string& outSessionName) {
    constexpr std::size_t kFixedPartSize = sizeof(std::uint32_t) * 3;
    if (dataSize < kFixedPartSize) return false;

    std::uint32_t maxPlayers = 0, currentPlayers = 0, nameLength = 0;
    const std::uint8_t* p = data;
    std::memcpy(&maxPlayers, p, sizeof(maxPlayers));
    p += sizeof(maxPlayers);
    std::memcpy(&currentPlayers, p, sizeof(currentPlayers));
    p += sizeof(currentPlayers);
    std::memcpy(&nameLength, p, sizeof(nameLength));
    p += sizeof(nameLength);

    if (dataSize - kFixedPartSize != nameLength) return false;

    outMaxPlayers = maxPlayers;
    outCurrentPlayers = currentPlayers;
    outSessionName.assign(reinterpret_cast<const char*>(p), nameLength);
    return true;
}

bool IsWildcardOrMatchingGuid(const GUID& filter, const GUID& candidate) {
    static const GUID zeroGuid{};
    if (std::memcmp(&filter, &zeroGuid, sizeof(GUID)) == 0) return true; // wildcard
    return std::memcmp(&filter, &candidate, sizeof(GUID)) == 0;
}

} // namespace

bool DirectPlayDiscoveryService::StartListening() {
    if (IsListening()) return false;

    socket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket_ == ENET_SOCKET_NULL) return false;

    enet_socket_set_option(socket_, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(socket_, ENET_SOCKOPT_BROADCAST, 1);
    // Lets a second listener bind the same port after the first one's socket is destroyed but
    // the OS hasn't fully released it yet (TIME_WAIT-adjacent) - matches the intent of
    // EnetDirectPlayTransport's own port-reuse-after-Shutdown() behavior, just via a socket
    // option here since ENetHost doesn't expose this specific knob.
    enet_socket_set_option(socket_, ENET_SOCKOPT_REUSEADDR, 1);

    ENetAddress bindAddress;
    bindAddress.host = ENET_HOST_ANY;
    bindAddress.port = kDefaultDirectPlayDiscoveryPort;
    if (enet_socket_bind(socket_, &bindAddress) != 0) {
        enet_socket_destroy(socket_);
        socket_ = ENET_SOCKET_NULL;
        return false;
    }
    return true;
}

void DirectPlayDiscoveryService::StopListening() {
    if (!IsListening()) return;
    enet_socket_destroy(socket_);
    socket_ = ENET_SOCKET_NULL;
}

void DirectPlayDiscoveryService::RespondToPendingRequests(const DiscoveredSessionInfo& info) {
    if (!IsListening()) return;

    std::uint8_t buf[512];
    for (;;) {
        ENetBuffer recvBuffer;
        recvBuffer.data = buf;
        recvBuffer.dataLength = sizeof(buf);
        ENetAddress fromAddress{};
        const int received = enet_socket_receive(socket_, &fromAddress, &recvBuffer, 1);
        if (received <= 0) break; // nothing pending (0) or a real error (<0) - either way, done

        const auto header = TryDeserializeDirectPlayWireHeader(buf, static_cast<std::size_t>(received));
        if (!header || header->type != DirectPlayWirePacketType::Discovery) continue;
        if (!IsWildcardOrMatchingGuid(header->applicationGuid, info.applicationGuid)) continue;

        DirectPlayWirePacketHeader responseHeader;
        responseHeader.type = DirectPlayWirePacketType::DiscoveryResponse;
        responseHeader.applicationGuid = info.applicationGuid;
        responseHeader.sessionGuid = info.sessionInstanceGuid;

        std::vector<std::uint8_t> payload;
        SerializeDiscoveryResponsePayload(info, payload);
        responseHeader.payloadLength = static_cast<std::uint32_t>(payload.size());

        std::vector<std::uint8_t> responseBytes;
        SerializeDirectPlayWireHeader(responseHeader, responseBytes);
        responseBytes.insert(responseBytes.end(), payload.begin(), payload.end());

        ENetBuffer sendBuffer;
        sendBuffer.data = responseBytes.data();
        sendBuffer.dataLength = responseBytes.size();
        // Unicast reply, straight back to the requester's real source address (populated by
        // enet_socket_receive() above) - never re-broadcast.
        enet_socket_send(socket_, &fromAddress, &sendBuffer, 1);
    }
}

std::vector<DiscoveredSessionInfo> DirectPlayDiscoveryService::BroadcastAndCollect(
    const GUID& filterApplicationGuid, std::uint32_t timeoutMs) {
    std::vector<DiscoveredSessionInfo> results;

    ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (sock == ENET_SOCKET_NULL) return results;
    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);

    // Bound to an OS-assigned ephemeral port (ENET_PORT_ANY), not kDefaultDirectPlayDiscoveryPort
    // - this is the enumerating side, not a second listener competing for the well-known port
    // (which only a real hosting session's DirectPlayDiscoveryService binds, via
    // StartListening()). Replies are addressed back to whatever source port this socket actually
    // sent the request from, so binding here is only needed to have *a* local port to receive on
    // at all.
    ENetAddress bindAddress;
    bindAddress.host = ENET_HOST_ANY;
    bindAddress.port = ENET_PORT_ANY;
    if (enet_socket_bind(sock, &bindAddress) != 0) {
        enet_socket_destroy(sock);
        return results;
    }

    DirectPlayWirePacketHeader requestHeader;
    requestHeader.type = DirectPlayWirePacketType::Discovery;
    requestHeader.applicationGuid = filterApplicationGuid;
    std::vector<std::uint8_t> requestBytes;
    SerializeDirectPlayWireHeader(requestHeader, requestBytes);

    ENetAddress broadcastAddress;
    broadcastAddress.host = ENET_HOST_BROADCAST;
    broadcastAddress.port = kDefaultDirectPlayDiscoveryPort;
    ENetBuffer sendBuffer;
    sendBuffer.data = requestBytes.data();
    sendBuffer.dataLength = requestBytes.size();
    enet_socket_send(sock, &broadcastAddress, &sendBuffer, 1);

    // Collect replies until timeoutMs elapses (0 means "don't wait at all" - the loop below
    // exits on its very first deadline check without ever calling enet_socket_wait, since a
    // reply cannot plausibly have arrived in the microseconds since the request was just sent
    // above). enet_socket_wait blocks efficiently (poll()/select() under the hood, not a busy
    // loop) until either data is ready or the remaining budget expires, so multiple replies
    // arriving close together are all collected rather than the timeout being a fixed
    // per-reply slice.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::uint8_t buf[512];
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(sock, &condition, static_cast<enet_uint32>(remainingMs)) != 0) break;
        if (!(condition & ENET_SOCKET_WAIT_RECEIVE)) break; // timed out, nothing arrived

        ENetBuffer recvBuffer;
        recvBuffer.data = buf;
        recvBuffer.dataLength = sizeof(buf);
        ENetAddress fromAddress{};
        const int received = enet_socket_receive(sock, &fromAddress, &recvBuffer, 1);
        if (received <= 0) continue; // spurious wakeup or transient error - keep waiting

        const auto header = TryDeserializeDirectPlayWireHeader(buf, static_cast<std::size_t>(received));
        if (!header || header->type != DirectPlayWirePacketType::DiscoveryResponse) continue;

        DiscoveredSessionInfo info;
        info.applicationGuid = header->applicationGuid;
        info.sessionInstanceGuid = header->sessionGuid;
        if (!TryDeserializeDiscoveryResponsePayload(
                buf + kDirectPlayWireHeaderSize, static_cast<std::size_t>(received) - kDirectPlayWireHeaderSize,
                info.maxPlayers, info.currentPlayers, info.sessionName)) {
            continue;
        }
        results.push_back(std::move(info));
    }

    enet_socket_destroy(sock);
    return results;
}

} // namespace free_direct_directplay
