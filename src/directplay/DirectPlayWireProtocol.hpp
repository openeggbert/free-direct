/**
 * @file DirectPlayWireProtocol.hpp
 * @brief Internal wire packet header for the (planned) ENet-backed DirectPlay transport.
 *
 * Pure data structure with zero ENet dependency, so it can be implemented and tested
 * (`plan.md` Phase 5) before any real ENet code exists. Private to the DirectPlay
 * implementation: must never be included from `include/dplay.h` and must never be
 * installed.
 *
 * This header defines the packet *header* fields, their flat (de)serialization, and
 * defensive size validation for an untrusted receive buffer
 * (`TryDeserializeDirectPlayWireHeader`). That validation deliberately does not check
 * `magic`/`version` - a wrong-protocol/wrong-version packet is a different rejection
 * reason than a malformed/truncated buffer, with its own `DPERR_*` mapping to be decided
 * once a real transport (`EnetDirectPlayTransport`, still to be added in this same
 * phase) actually receives packets.
 *
 * The `idFrom`/`idTo` fields are serialized using the local `sizeof(DPID)` as-is
 * (whatever `include/dplay.h` currently typedefs `DPID` to). This is tied to the open
 * DPID-size question tracked in NEXT.md Section 4 / plan.md Phase 9; if that question
 * changes DPID's width, this wire format's DPID fields change with it.
 * @note Status: PARTIAL
 */
#pragma once

#include "dplay.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace free_direct_directplay {

/// Distinguishes the kinds of messages peers exchange once a real transport exists to
/// send them (`plan.md` Phases 5-8). Only `Data` is exercised today (the self-send
/// loopback path); the rest are placeholders for session hosting/joining/discovery.
enum class DirectPlayWirePacketType : std::uint32_t {
    Join = 0,
    JoinAccept = 1,
    JoinReject = 2,
    Data = 3,
    Discovery = 4,
    DiscoveryResponse = 5,
};

/// Rejects non-FreeDirect traffic early, before any other field is trusted. Arbitrary
/// but fixed ("FRDP"); must never change once any real transport ships packets built
/// with it.
inline constexpr std::uint32_t kDirectPlayWireMagic = 0x46524450;

/// Bumped whenever the wire layout below changes incompatibly.
inline constexpr std::uint32_t kDirectPlayWireProtocolVersion = 1;

/**
 * @brief Fixed-layout header prefixing every FreeDirect-to-FreeDirect DirectPlay wire
 * packet. The payload bytes (if any) follow immediately after the header on the wire;
 * this struct does not own or store them - only their length.
 * @note Status: PARTIAL
 */
struct DirectPlayWirePacketHeader {
    std::uint32_t magic = kDirectPlayWireMagic;
    std::uint32_t version = kDirectPlayWireProtocolVersion;
    DirectPlayWirePacketType type = DirectPlayWirePacketType::Data;
    GUID applicationGuid{};
    GUID sessionGuid{};
    DPID idFrom = 0;
    DPID idTo = 0;
    std::uint32_t payloadLength = 0;
};

/// Byte size of a serialized `DirectPlayWirePacketHeader`. Deliberately not
/// `sizeof(DirectPlayWirePacketHeader)`: `Serialize`/`Deserialize` below use a flat,
/// padding-free layout (per-field `memcpy`), not the struct's in-memory layout.
inline constexpr std::size_t kDirectPlayWireHeaderSize =
    sizeof(std::uint32_t) +  // magic
    sizeof(std::uint32_t) +  // version
    sizeof(std::uint32_t) +  // type
    sizeof(GUID) +           // applicationGuid
    sizeof(GUID) +           // sessionGuid
    sizeof(DPID) +           // idFrom
    sizeof(DPID) +           // idTo
    sizeof(std::uint32_t);   // payloadLength

/// Appends `header`'s flat wire representation to `out`, in field-declaration order.
/// Payload bytes (if any) are the caller's responsibility to append separately.
inline void SerializeDirectPlayWireHeader(const DirectPlayWirePacketHeader& header,
                                           std::vector<std::uint8_t>& out) {
    const std::size_t base = out.size();
    out.resize(base + kDirectPlayWireHeaderSize);
    std::uint8_t* p = out.data() + base;

    const std::uint32_t type = static_cast<std::uint32_t>(header.type);
    std::memcpy(p, &header.magic, sizeof(header.magic));
    p += sizeof(header.magic);
    std::memcpy(p, &header.version, sizeof(header.version));
    p += sizeof(header.version);
    std::memcpy(p, &type, sizeof(type));
    p += sizeof(type);
    std::memcpy(p, &header.applicationGuid, sizeof(GUID));
    p += sizeof(GUID);
    std::memcpy(p, &header.sessionGuid, sizeof(GUID));
    p += sizeof(GUID);
    std::memcpy(p, &header.idFrom, sizeof(DPID));
    p += sizeof(DPID);
    std::memcpy(p, &header.idTo, sizeof(DPID));
    p += sizeof(DPID);
    std::memcpy(p, &header.payloadLength, sizeof(header.payloadLength));
}

/// Parses a flat wire header out of `data`. Precondition: `data` points at at least
/// `kDirectPlayWireHeaderSize` bytes. Callers that cannot guarantee this (e.g. an
/// untrusted, possibly-truncated receive buffer) must use
/// `TryDeserializeDirectPlayWireHeader` below instead.
inline DirectPlayWirePacketHeader DeserializeDirectPlayWireHeader(const std::uint8_t* data) {
    DirectPlayWirePacketHeader header;
    std::uint32_t type = 0;
    const std::uint8_t* p = data;

    std::memcpy(&header.magic, p, sizeof(header.magic));
    p += sizeof(header.magic);
    std::memcpy(&header.version, p, sizeof(header.version));
    p += sizeof(header.version);
    std::memcpy(&type, p, sizeof(type));
    p += sizeof(type);
    header.type = static_cast<DirectPlayWirePacketType>(type);
    std::memcpy(&header.applicationGuid, p, sizeof(GUID));
    p += sizeof(GUID);
    std::memcpy(&header.sessionGuid, p, sizeof(GUID));
    p += sizeof(GUID);
    std::memcpy(&header.idFrom, p, sizeof(DPID));
    p += sizeof(DPID);
    std::memcpy(&header.idTo, p, sizeof(DPID));
    p += sizeof(DPID);
    std::memcpy(&header.payloadLength, p, sizeof(header.payloadLength));
    return header;
}

/// Validates an untrusted received buffer before trusting it as a wire header, then
/// parses it. Rejects (returns `std::nullopt`, without reading past `dataSize` bytes):
///  - a buffer smaller than `kDirectPlayWireHeaderSize` (too small to even hold a
///    header);
///  - a buffer whose parsed `payloadLength` disagrees with the actual trailing byte
///    count (`dataSize - kDirectPlayWireHeaderSize`) - i.e. the header claims a payload
///    size that does not match what was actually received.
/// Does not check `magic`/`version` - see the file-level comment above.
inline std::optional<DirectPlayWirePacketHeader> TryDeserializeDirectPlayWireHeader(
    const std::uint8_t* data, std::size_t dataSize) {
    if (dataSize < kDirectPlayWireHeaderSize) return std::nullopt;

    const DirectPlayWirePacketHeader header = DeserializeDirectPlayWireHeader(data);
    const std::size_t actualPayloadSize = dataSize - kDirectPlayWireHeaderSize;
    if (static_cast<std::size_t>(header.payloadLength) != actualPayloadSize) return std::nullopt;

    return header;
}

} // namespace free_direct_directplay
