/**
 * @file RelayProtocol.hpp
 * @brief The Free Direct server protocol: messages between a Free Direct client and
 *        `free-direct-server`, carried as binary WebSocket messages.
 *
 * Shared by the client (`src/directplay/Relay*`) and the server (`server/`). Depends only on the
 * C++ standard library - no DirectPlay, WinAPI or SDL types - so the server builds without
 * Free API. Everything is little-endian and fixed-width; strings are a 16-bit byte count followed
 * by UTF-8 bytes. A GUID travels as its 16-byte DirectPlay wire form (`PutWireGuid`), opaque here.
 *
 * One WebSocket message is exactly one protocol message: a one-byte type, then its fields. See
 * `docs/directplay-relay-protocol.md` for the message table and the lifecycle.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace free_direct_relay {

/// "FDRL", little-endian, first field of Hello.
inline constexpr std::uint32_t kRelayMagic = 0x4C524446;
/// Bumped whenever a message below changes incompatibly; the server refuses other versions.
inline constexpr std::uint16_t kRelayProtocolVersion = 1;
/// Default TCP port of `free-direct-server` (next to ENet's 51321 and discovery's 51323).
inline constexpr std::uint16_t kDefaultRelayPort = 51325;
/// Largest WebSocket message either side accepts. A DirectPlay payload is at most 4096 bytes,
/// plus its 56-byte wire header and the relay's own few bytes.
inline constexpr std::size_t kMaxRelayMessageBytes = 8192;
/// Longest string field (session names, compatibility identities, passwords, room IDs).
inline constexpr std::size_t kMaxRelayStringBytes = 512;
/// Characters of a room ID without separators: 20 Crockford base32 characters, 100 random bits.
inline constexpr std::size_t kRoomIdLength = 20;

using RelayGuid = std::array<std::uint8_t, 16>;
using ConnId = std::uint32_t;

enum class MsgType : std::uint8_t {
    // Client -> server.
    Hello = 0x01,         ///< magic u32, version u16, compat str, password str, client str
    CreateRoom = 0x02,    ///< (no fields)
    JoinRoom = 0x03,      ///< roomId str
    ListSessions = 0x04,  ///< requestId u32
    HostSession = 0x05,   ///< SessionInfo (currentPlayers ignored)
    CloseSession = 0x06,  ///< (no fields) - the host stops hosting
    JoinSession = 0x07,   ///< instance guid
    LeaveSession = 0x08,  ///< (no fields) - a member leaves
    Admit = 0x09,         ///< conn u32 - the host accepts a pending member
    Refuse = 0x0A,        ///< conn u32 - the host turns a pending member away
    Data = 0x0B,          ///< route u8, target conn u32, payload (rest of the message)
    RtcOffer = 0x0C,      ///< SDP (rest of the message): a browser's WebRTC offer for datagrams

    // Server -> client.
    Welcome = 0x81,        ///< version u16, conn u32, flags u8 (bit 0: server has a password)
    Error = 0x82,          ///< request type u8, code u16, text str
    RoomCreated = 0x83,    ///< roomId str (formatted)
    RoomJoined = 0x84,     ///< roomId str (formatted)
    SessionList = 0x85,    ///< requestId u32, count u16, SessionInfo[count]
    SessionHosted = 0x86,  ///< (no fields)
    SessionJoined = 0x87,  ///< host conn u32
    PeerJoined = 0x88,     ///< conn u32 - to the host: a member is waiting for admission
    PeerLeft = 0x89,       ///< conn u32 - to the host: a pending or admitted member is gone
    SessionEnded = 0x8A,   ///< reason u8 (SessionEndReason) - to a member
    Deliver = 0x8B,        ///< from conn u32, payload (rest of the message)
    RtcAnswer = 0x8C,      ///< SDP (rest of the message): the server's answer to RtcOffer
};

/// Where a Data message goes. Always within the sender's own session.
enum class Route : std::uint8_t {
    ToHost = 0,       ///< member -> the session's host
    ToMember = 1,     ///< host -> one member (target conn)
    ToAllOthers = 2,  ///< host or admitted member -> the host and every admitted member but the sender
};

enum class SessionEndReason : std::uint8_t {
    HostClosed = 0,  ///< the host closed the session or left the room
    Refused = 1,     ///< the host turned this member away (e.g. the session is full)
    ServerShutdown = 2,
};

enum class ErrorCode : std::uint16_t {
    Malformed = 1,
    NotWelcome = 2,          ///< a request before a successful Hello
    BadVersion = 3,
    BadPassword = 4,
    RateLimited = 5,
    ServerFull = 6,
    OriginRejected = 7,
    TooManyRooms = 8,
    RoomNotFound = 9,
    RoomFull = 10,
    RoomIncompatible = 11,  ///< the room was created by another game or edition
    NotInRoom = 12,
    TooManySessions = 13,
    SessionExists = 14,
    SessionNotFound = 15,
    SessionFull = 16,
    AlreadyInSession = 17,
    NotInSession = 18,
    NotHost = 19,
    UnknownMember = 20,
};

/// Human-readable English text for an error code, for logs and the connection dialog.
inline const char* ErrorCodeText(ErrorCode code) {
    switch (code) {
        case ErrorCode::Malformed: return "malformed message";
        case ErrorCode::NotWelcome: return "no successful hello yet";
        case ErrorCode::BadVersion: return "incompatible Free Direct server version";
        case ErrorCode::BadPassword: return "wrong server password";
        case ErrorCode::RateLimited: return "too many failed attempts, try again later";
        case ErrorCode::ServerFull: return "the server is full";
        case ErrorCode::OriginRejected: return "this web page may not use the server";
        case ErrorCode::TooManyRooms: return "the server has too many rooms";
        case ErrorCode::RoomNotFound: return "no room with this ID";
        case ErrorCode::RoomFull: return "the room is full";
        case ErrorCode::RoomIncompatible: return "the room belongs to another game or edition";
        case ErrorCode::NotInRoom: return "not in a room";
        case ErrorCode::TooManySessions: return "the room has too many games";
        case ErrorCode::SessionExists: return "the game already exists";
        case ErrorCode::SessionNotFound: return "the game no longer exists";
        case ErrorCode::SessionFull: return "the game is full";
        case ErrorCode::AlreadyInSession: return "already in a game";
        case ErrorCode::NotInSession: return "not in a game";
        case ErrorCode::NotHost: return "only the game's host may do this";
        case ErrorCode::UnknownMember: return "unknown player";
    }
    return "unknown error";
}

/// A hosted session as the room lists it. `currentPlayers` is computed by the server: the host
/// plus every pending or admitted member.
struct SessionInfo {
    RelayGuid instance{};
    RelayGuid application{};
    std::uint32_t maxPlayers = 0;
    std::uint32_t currentPlayers = 0;
    std::string name;
};

// ===== Encoding =====

class Writer {
public:
    explicit Writer(MsgType type) { bytes_.push_back(static_cast<std::uint8_t>(type)); }

    Writer& U8(std::uint8_t v) {
        bytes_.push_back(v);
        return *this;
    }
    Writer& U16(std::uint16_t v) {
        bytes_.push_back(static_cast<std::uint8_t>(v));
        bytes_.push_back(static_cast<std::uint8_t>(v >> 8));
        return *this;
    }
    Writer& U32(std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) bytes_.push_back(static_cast<std::uint8_t>(v >> shift));
        return *this;
    }
    /// Strings longer than kMaxRelayStringBytes are truncated (a reader would refuse them).
    Writer& Str(const std::string& s) {
        const std::size_t n = s.size() < kMaxRelayStringBytes ? s.size() : kMaxRelayStringBytes;
        U16(static_cast<std::uint16_t>(n));
        bytes_.insert(bytes_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
        return *this;
    }
    Writer& Guid(const RelayGuid& g) {
        bytes_.insert(bytes_.end(), g.begin(), g.end());
        return *this;
    }
    Writer& Bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
        return *this;
    }
    Writer& Session(const SessionInfo& s) {
        return Guid(s.instance).Guid(s.application).U32(s.maxPlayers).U32(s.currentPlayers).Str(s.name);
    }

    const std::vector<std::uint8_t>& Data() const { return bytes_; }
    std::vector<std::uint8_t> Take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

/// Reads fields from one received message. Every read checks the remaining length; after the
/// first failure `Ok()` is false and every later read returns a zero value.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    bool Ok() const { return ok_; }
    bool AtEnd() const { return p_ == end_; }
    std::size_t Remaining() const { return static_cast<std::size_t>(end_ - p_); }
    const std::uint8_t* Cursor() const { return p_; }

    std::uint8_t U8() {
        if (!Need(1)) return 0;
        return *p_++;
    }
    std::uint16_t U16() {
        if (!Need(2)) return 0;
        const std::uint16_t v = static_cast<std::uint16_t>(p_[0] | (p_[1] << 8));
        p_ += 2;
        return v;
    }
    std::uint32_t U32() {
        if (!Need(4)) return 0;
        std::uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | p_[i];
        p_ += 4;
        return v;
    }
    std::string Str() {
        const std::uint16_t n = U16();
        if (!ok_ || n > kMaxRelayStringBytes || !Need(n)) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_), n);
        p_ += n;
        return s;
    }
    RelayGuid Guid() {
        RelayGuid g{};
        if (!Need(16)) return g;
        std::memcpy(g.data(), p_, 16);
        p_ += 16;
        return g;
    }
    SessionInfo Session() {
        SessionInfo s;
        s.instance = Guid();
        s.application = Guid();
        s.maxPlayers = U32();
        s.currentPlayers = U32();
        s.name = Str();
        return s;
    }
    /// The rest of the message (a Data/Deliver payload).
    std::vector<std::uint8_t> Rest() {
        std::vector<std::uint8_t> rest(p_, end_);
        p_ = end_;
        return rest;
    }

private:
    bool Need(std::size_t n) {
        if (!ok_ || static_cast<std::size_t>(end_ - p_) < n) {
            ok_ = false;
            return false;
        }
        return true;
    }

    const std::uint8_t* p_;
    const std::uint8_t* end_;
    bool ok_ = true;
};

// ===== Room IDs =====

/// Crockford base32 alphabet: no I, L, O, U, so a typed ID is hard to get wrong.
inline constexpr char kRoomIdAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/// Normalizes a typed or pasted room ID: drops spaces and dashes, upper-cases, and reads the
/// look-alike letters as Crockford does (O -> 0, I and L -> 1). Returns nullopt unless exactly
/// kRoomIdLength alphabet characters remain.
inline std::optional<std::string> NormalizeRoomId(const std::string& typed) {
    std::string out;
    for (char c : typed) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (!std::strchr(kRoomIdAlphabet, c) || c == '\0') return std::nullopt;
        out.push_back(c);
        if (out.size() > kRoomIdLength) return std::nullopt;
    }
    if (out.size() != kRoomIdLength) return std::nullopt;
    return out;
}

/// "ABCDE-FGHJK-MNPQR-STVWX": a normalized ID in four groups of five.
inline std::string FormatRoomId(const std::string& normalized) {
    std::string out;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (i != 0 && i % 5 == 0) out.push_back('-');
        out.push_back(normalized[i]);
    }
    return out;
}

/// Builds a room ID from kRoomIdLength random bytes (only the low 5 bits of each are used).
inline std::string RoomIdFromRandomBytes(const std::uint8_t* random, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count && out.size() < kRoomIdLength; ++i) {
        out.push_back(kRoomIdAlphabet[random[i] & 0x1F]);
    }
    return out;
}

} // namespace free_direct_relay
