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
 * defensive validation for an untrusted receive buffer (`TryDeserializeDirectPlayWireHeader`),
 * which checks size, `magic`/`version`, and `payloadLength` consistency, rejecting anything that
 * fails any of those the same way (`std::nullopt`; the caller already silently drops a malformed
 * packet, so a wrong-protocol/wrong-version packet needs no separate `DPERR_*` mapping of its
 * own). `magic`/`version` validation was deliberately deferred until a real transport
 * (`EnetDirectPlayTransport`) existed to receive real UDP packets - see
 * `docs/audit_dplay.md` §6.4/§9 (D6, TASK-24H-0176) for the fresh 2026-07-09 decision to add it
 * now that this precondition has been met.
 *
 * The layout is fixed-width and little-endian on every platform (version 2, 2026-11-11):
 * a `GUID` is always 16 bytes (`Data1` as 32 bits, although `GUID::Data1` is an
 * `unsigned long` - 8 bytes on 64-bit Linux, 4 on Windows and WebAssembly) and a `DPID`
 * always 32 bits. Version 1 copied `sizeof(GUID)` and `sizeof(DPID)` bytes as they were in
 * memory, so a 64-bit Linux build and a WebAssembly build could not read each other's
 * packets (`docs/directplay-design.md` Decision 28).
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
inline constexpr std::uint32_t kDirectPlayWireProtocolVersion = 2;

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

/// Bytes of a `GUID` on the wire (`Data1` 32 bits, `Data2`, `Data3` 16 bits, `Data4` 8 bytes),
/// independent of `sizeof(GUID)`.
inline constexpr std::size_t kDirectPlayWireGuidSize = 16;

/// Byte size of a serialized `DirectPlayWirePacketHeader`. Deliberately not
/// `sizeof(DirectPlayWirePacketHeader)`: `Serialize`/`Deserialize` below use a flat,
/// padding-free, fixed-width little-endian layout, not the struct's in-memory layout.
inline constexpr std::size_t kDirectPlayWireHeaderSize =
    4 +                        // magic
    4 +                        // version
    4 +                        // type
    kDirectPlayWireGuidSize +  // applicationGuid
    kDirectPlayWireGuidSize +  // sessionGuid
    4 +                        // idFrom
    4 +                        // idTo
    4;                         // payloadLength

inline void PutWireU16(std::uint8_t*& p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p += 2;
}

inline void PutWireU32(std::uint8_t*& p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
    p += 4;
}

inline std::uint16_t GetWireU16(const std::uint8_t*& p) {
    const std::uint16_t v = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    p += 2;
    return v;
}

inline std::uint32_t GetWireU32(const std::uint8_t*& p) {
    const std::uint32_t v = static_cast<std::uint32_t>(p[0]) |
                            (static_cast<std::uint32_t>(p[1]) << 8) |
                            (static_cast<std::uint32_t>(p[2]) << 16) |
                            (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return v;
}

/// Writes `guid` as `kDirectPlayWireGuidSize` bytes. `Data1` keeps its low 32 bits, the
/// only bits a GUID has.
inline void PutWireGuid(std::uint8_t*& p, const GUID& guid) {
    PutWireU32(p, static_cast<std::uint32_t>(guid.Data1));
    PutWireU16(p, guid.Data2);
    PutWireU16(p, guid.Data3);
    std::memcpy(p, guid.Data4, 8);
    p += 8;
}

/// Reads a GUID written by `PutWireGuid`. The result is value-initialized first, so its
/// padding bytes (if the platform's `GUID` has any) are zero and `memcmp` comparisons work.
inline GUID GetWireGuid(const std::uint8_t*& p) {
    GUID guid{};
    guid.Data1 = GetWireU32(p);
    guid.Data2 = GetWireU16(p);
    guid.Data3 = GetWireU16(p);
    std::memcpy(guid.Data4, p, 8);
    p += 8;
    return guid;
}

/// Appends `header`'s flat wire representation to `out`, in field-declaration order.
/// Payload bytes (if any) are the caller's responsibility to append separately.
inline void SerializeDirectPlayWireHeader(const DirectPlayWirePacketHeader& header,
                                           std::vector<std::uint8_t>& out) {
    const std::size_t base = out.size();
    out.resize(base + kDirectPlayWireHeaderSize);
    std::uint8_t* p = out.data() + base;

    PutWireU32(p, header.magic);
    PutWireU32(p, header.version);
    PutWireU32(p, static_cast<std::uint32_t>(header.type));
    PutWireGuid(p, header.applicationGuid);
    PutWireGuid(p, header.sessionGuid);
    PutWireU32(p, static_cast<std::uint32_t>(header.idFrom));
    PutWireU32(p, static_cast<std::uint32_t>(header.idTo));
    PutWireU32(p, header.payloadLength);
}

/// Parses a flat wire header out of `data`. Precondition: `data` points at at least
/// `kDirectPlayWireHeaderSize` bytes. Callers that cannot guarantee this (e.g. an
/// untrusted, possibly-truncated receive buffer) must use
/// `TryDeserializeDirectPlayWireHeader` below instead.
inline DirectPlayWirePacketHeader DeserializeDirectPlayWireHeader(const std::uint8_t* data) {
    DirectPlayWirePacketHeader header;
    const std::uint8_t* p = data;

    header.magic = GetWireU32(p);
    header.version = GetWireU32(p);
    header.type = static_cast<DirectPlayWirePacketType>(GetWireU32(p));
    header.applicationGuid = GetWireGuid(p);
    header.sessionGuid = GetWireGuid(p);
    header.idFrom = GetWireU32(p);
    header.idTo = GetWireU32(p);
    header.payloadLength = GetWireU32(p);
    return header;
}

/// Validates an untrusted received buffer before trusting it as a wire header, then
/// parses it. Rejects (returns `std::nullopt`, without reading past `dataSize` bytes):
///  - a buffer smaller than `kDirectPlayWireHeaderSize` (too small to even hold a
///    header);
///  - a parsed `magic`/`version` that doesn't match `kDirectPlayWireMagic`/
///    `kDirectPlayWireProtocolVersion` (docs/audit_dplay.md §6.4/§9, D6, TASK-24H-0176) - every
///    real FreeDirect sender writes these via `SerializeDirectPlayWireHeader`, so this never
///    rejects legitimate FreeDirect-to-FreeDirect traffic;
///  - a buffer whose parsed `payloadLength` disagrees with the actual trailing byte
///    count (`dataSize - kDirectPlayWireHeaderSize`) - i.e. the header claims a payload
///    size that does not match what was actually received.
inline std::optional<DirectPlayWirePacketHeader> TryDeserializeDirectPlayWireHeader(
    const std::uint8_t* data, std::size_t dataSize) {
    if (dataSize < kDirectPlayWireHeaderSize) return std::nullopt;

    const DirectPlayWirePacketHeader header = DeserializeDirectPlayWireHeader(data);
    if (header.magic != kDirectPlayWireMagic || header.version != kDirectPlayWireProtocolVersion) {
        return std::nullopt;
    }
    const std::size_t actualPayloadSize = dataSize - kDirectPlayWireHeaderSize;
    if (static_cast<std::size_t>(header.payloadLength) != actualPayloadSize) return std::nullopt;

    return header;
}

} // namespace free_direct_directplay
