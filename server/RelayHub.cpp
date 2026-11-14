/**
 * @file RelayHub.cpp
 * @brief Room and session logic of `free-direct-server`. See RelayHub.hpp.
 * @note Status: IMPLEMENTED
 */
#include "RelayHub.hpp"

#include <algorithm>
#include <utility>

namespace free_direct_relay {

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
    // Compares over the longer length so the time does not reveal a common prefix.
    const std::size_t n = std::max(a.size(), b.size());
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}

RelayHub::RelayHub(HubConfig config, RandomSource random, Logger log)
    : config_(std::move(config)), random_(std::move(random)), log_(std::move(log)) {}

std::size_t RelayHub::SessionCount() const {
    std::size_t n = 0;
    for (const auto& [id, room] : rooms_) n += room.sessions.size();
    return n;
}

void RelayHub::Send(std::vector<HubAction>& out, ConnId to, std::vector<std::uint8_t> bytes) {
    HubAction a;
    a.kind = HubAction::Kind::Send;
    a.conn = to;
    a.bytes = std::move(bytes);
    out.push_back(std::move(a));
}

void RelayHub::SendError(std::vector<HubAction>& out, ConnId to, MsgType request, ErrorCode code) {
    Send(out, to, Writer(MsgType::Error)
                      .U8(static_cast<std::uint8_t>(request))
                      .U16(static_cast<std::uint16_t>(code))
                      .Str(ErrorCodeText(code))
                      .Take());
}

void RelayHub::Close(std::vector<HubAction>& out, ConnId conn, std::uint16_t code, const std::string& reason) {
    HubAction a;
    a.kind = HubAction::Kind::Close;
    a.conn = conn;
    a.closeCode = code;
    a.reason = reason;
    out.push_back(std::move(a));
}

void RelayHub::Log(const std::string& line) const {
    if (log_) log_(line);
}

std::string RelayHub::MaskRoomId(const std::string& normalized) {
    // Logs show only the first four characters: enough to tell rooms apart, useless for joining.
    return normalized.substr(0, 4) + "...";
}

std::vector<HubAction> RelayHub::OnOpen(ConnId conn, const std::string& address, const std::string& origin,
                                        Clock::time_point now) {
    std::vector<HubAction> out;
    (void)now;
    if (conns_.size() >= config_.maxConnections) {
        SendError(out, conn, MsgType::Hello, ErrorCode::ServerFull);
        Close(out, conn, 1013, "server full");
        Log("refused connection " + std::to_string(conn) + " from " + address + ": server full");
        return out;
    }
    // A browser always sends its page's origin (http:// or https://, or "null"). Native clients
    // send none, or, like IXWebSocket, the server's own ws:// or wss:// URL; they are allowed.
    // The check only keeps other web pages away; it is no authentication
    // (docs/directplay-relay-protocol.md, "Origin").
    const bool nativeClient = origin.empty() || origin.rfind("ws://", 0) == 0 || origin.rfind("wss://", 0) == 0;
    if (!config_.allowedOrigins.empty() && !nativeClient &&
        std::find(config_.allowedOrigins.begin(), config_.allowedOrigins.end(), origin) ==
            config_.allowedOrigins.end()) {
        SendError(out, conn, MsgType::Hello, ErrorCode::OriginRejected);
        Close(out, conn, 1008, "origin not allowed");
        Log("refused connection " + std::to_string(conn) + " from " + address + ": origin " + origin);
        return out;
    }
    Conn c;
    c.address = address;
    conns_.emplace(conn, std::move(c));
    Log("connection " + std::to_string(conn) + " from " + address + (origin.empty() ? "" : " origin " + origin));
    return out;
}

std::vector<HubAction> RelayHub::OnClose(ConnId conn, Clock::time_point now) {
    std::vector<HubAction> out;
    auto it = conns_.find(conn);
    if (it == conns_.end()) return out;
    auto token = tokens_.find(it->second.token);
    if (token != tokens_.end() && token->second == conn) tokens_.erase(token);
    LeaveRoom(conn, it->second, now, out);
    conns_.erase(it);
    Log("connection " + std::to_string(conn) + " closed");
    return out;
}

std::vector<HubAction> RelayHub::Shutdown() {
    std::vector<HubAction> out;
    for (auto& [id, c] : conns_) {
        if (c.role == Role::Member) {
            Send(out, id, Writer(MsgType::SessionEnded)
                              .U8(static_cast<std::uint8_t>(SessionEndReason::ServerShutdown))
                              .Take());
        }
        Close(out, id, 1001, "server shutting down");
    }
    conns_.clear();
    rooms_.clear();
    return out;
}

void RelayHub::Reap(Clock::time_point now) {
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (it->second.members.empty() && now - it->second.emptySince >= config_.roomIdleTimeout) {
            Log("room " + MaskRoomId(it->first) + " deleted after being empty");
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

bool RelayHub::AddressRateLimited(const std::string& address, Clock::time_point now) {
    auto it = addressFailures_.find(address);
    if (it == addressFailures_.end()) return false;
    auto& times = it->second;
    while (!times.empty() && now - times.front() >= config_.failureWindow) times.pop_front();
    if (times.empty()) {
        addressFailures_.erase(it);
        return false;
    }
    return times.size() >= config_.maxFailuresPerAddress;
}

bool RelayHub::CountFailure(ConnId id, Conn& c, Clock::time_point now) {
    (void)id;
    ++c.failures;
    addressFailures_[c.address].push_back(now);
    return c.failures >= config_.maxFailuresPerConnection;
}

std::vector<HubAction> RelayHub::OnMessage(ConnId id, const std::uint8_t* data, std::size_t size,
                                           Clock::time_point now) {
    std::vector<HubAction> out;
    auto it = conns_.find(id);
    if (it == conns_.end()) return out;
    Conn& c = it->second;
    if (size == 0 || size > kMaxRelayMessageBytes) {
        Close(out, id, 1009, "message size");
        return out;
    }
    Reader r(data + 1, size - 1);
    const auto type = static_cast<MsgType>(data[0]);
    if (type != MsgType::Hello && !c.welcomed) {
        SendError(out, id, type, ErrorCode::NotWelcome);
        Close(out, id, 1008, "no hello");
        return out;
    }
    switch (type) {
        case MsgType::Hello: HandleHello(id, c, r, now, out); break;
        case MsgType::CreateRoom: HandleCreateRoom(id, c, now, out); break;
        case MsgType::JoinRoom: HandleJoinRoom(id, c, r, now, out); break;
        case MsgType::ListSessions: HandleListSessions(id, c, r, out); break;
        case MsgType::HostSession: HandleHostSession(id, c, r, out); break;
        case MsgType::CloseSession:
            if (c.role == Role::Host) LeaveSession(id, c, SessionEndReason::HostClosed, out);
            break;
        case MsgType::JoinSession: HandleJoinSession(id, c, r, out); break;
        case MsgType::LeaveSession:
            if (c.role == Role::Member) LeaveSession(id, c, SessionEndReason::HostClosed, out);
            break;
        case MsgType::Admit: HandleAdmitOrRefuse(id, c, r, true, out); break;
        case MsgType::Refuse: HandleAdmitOrRefuse(id, c, r, false, out); break;
        case MsgType::Data: HandleData(id, c, r, out); break;
        case MsgType::RtcOffer: {
            // The server's WebRTC front (RelayServer) answers; the hub only checks it may.
            const std::vector<std::uint8_t> sdp = r.Rest();
            if (!config_.webrtc || sdp.empty()) {
                SendError(out, id, type, ErrorCode::Malformed);
                break;
            }
            HubAction a;
            a.kind = HubAction::Kind::RtcOffer;
            a.conn = id;
            a.bytes = sdp;
            out.push_back(std::move(a));
            break;
        }
        default:
            SendError(out, id, type, ErrorCode::Malformed);
            break;
    }
    return out;
}

void RelayHub::HandleHello(ConnId id, Conn& c, Reader& r, Clock::time_point now, std::vector<HubAction>& out) {
    const std::uint32_t magic = r.U32();
    const std::uint16_t version = r.U16();
    const std::string compat = r.Str();
    const std::string password = r.Str();
    const std::string client = r.Str();
    if (!r.Ok() || magic != kRelayMagic || c.welcomed) {
        SendError(out, id, MsgType::Hello, ErrorCode::Malformed);
        Close(out, id, 1002, "bad hello");
        return;
    }
    if (version != kRelayProtocolVersion) {
        SendError(out, id, MsgType::Hello, ErrorCode::BadVersion);
        Close(out, id, 1002, "protocol version");
        Log("connection " + std::to_string(id) + ": protocol version " + std::to_string(version));
        return;
    }
    if (AddressRateLimited(c.address, now)) {
        SendError(out, id, MsgType::Hello, ErrorCode::RateLimited);
        Close(out, id, 1008, "rate limited");
        Log("connection " + std::to_string(id) + " from " + c.address + ": rate limited");
        return;
    }
    if (!config_.password.empty() && !ConstantTimeEquals(password, config_.password)) {
        CountFailure(id, c, now);
        SendError(out, id, MsgType::Hello, ErrorCode::BadPassword);
        Close(out, id, 1008, "bad password");
        Log("connection " + std::to_string(id) + " from " + c.address + ": wrong password");
        return;
    }
    if (compat.empty()) {
        SendError(out, id, MsgType::Hello, ErrorCode::Malformed);
        Close(out, id, 1002, "no compatibility identity");
        return;
    }
    c.welcomed = true;
    c.compat = compat;
    std::uint8_t flags = config_.password.empty() ? 0 : kWelcomeFlagPassword;
    const bool datagrams = config_.datagramPort != 0 || config_.webrtc;
    if (datagrams) {
        // Datagrams: the UDP port and this connection's token, after the flags. Clients that do not
        // know them ignore the rest of Welcome.
        do random_(c.token.data(), c.token.size()); while (tokens_.count(c.token) != 0);
        tokens_[c.token] = id;
        if (config_.datagramPort != 0) flags |= kWelcomeFlagDatagrams;
        if (config_.webrtc) flags |= kWelcomeFlagWebRtc;
    }
    Writer welcome(MsgType::Welcome);
    welcome.U16(kRelayProtocolVersion).U32(id).U8(flags);
    if (datagrams) welcome.U16(config_.datagramPort).Bytes(c.token.data(), c.token.size());
    Send(out, id, welcome.Take());
    Log("connection " + std::to_string(id) + " hello: " + compat + " (" + client + ")");
}

void RelayHub::HandleCreateRoom(ConnId id, Conn& c, Clock::time_point now, std::vector<HubAction>& out) {
    if (rooms_.size() >= config_.maxRooms) {
        SendError(out, id, MsgType::CreateRoom, ErrorCode::TooManyRooms);
        return;
    }
    std::string roomId;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::uint8_t random[kRoomIdLength];
        random_(random, sizeof(random));
        roomId = RoomIdFromRandomBytes(random, sizeof(random));
        if (rooms_.find(roomId) == rooms_.end()) break;
        roomId.clear();
    }
    if (roomId.empty()) {
        SendError(out, id, MsgType::CreateRoom, ErrorCode::TooManyRooms);
        return;
    }
    LeaveRoom(id, c, now, out);
    Room room;
    room.compat = c.compat;
    room.members.insert(id);
    rooms_.emplace(roomId, std::move(room));
    c.roomId = roomId;
    Send(out, id, Writer(MsgType::RoomCreated).Str(FormatRoomId(roomId)).Take());
    Log("room " + MaskRoomId(roomId) + " created by connection " + std::to_string(id) + " for " + c.compat);
}

void RelayHub::HandleJoinRoom(ConnId id, Conn& c, Reader& r, Clock::time_point now, std::vector<HubAction>& out) {
    const std::string typed = r.Str();
    if (!r.Ok()) {
        SendError(out, id, MsgType::JoinRoom, ErrorCode::Malformed);
        return;
    }
    if (AddressRateLimited(c.address, now)) {
        SendError(out, id, MsgType::JoinRoom, ErrorCode::RateLimited);
        Close(out, id, 1008, "rate limited");
        return;
    }
    const auto normalized = NormalizeRoomId(typed);
    auto roomIt = normalized ? rooms_.find(*normalized) : rooms_.end();
    if (roomIt == rooms_.end()) {
        const bool closeNow = CountFailure(id, c, now);
        SendError(out, id, MsgType::JoinRoom, ErrorCode::RoomNotFound);
        Log("connection " + std::to_string(id) + " from " + c.address + ": no such room");
        if (closeNow) Close(out, id, 1008, "too many wrong room IDs");
        return;
    }
    Room& room = roomIt->second;
    if (room.compat != c.compat) {
        SendError(out, id, MsgType::JoinRoom, ErrorCode::RoomIncompatible);
        return;
    }
    if (c.roomId == roomIt->first) {
        Send(out, id, Writer(MsgType::RoomJoined).Str(FormatRoomId(roomIt->first)).Take());
        return;
    }
    if (room.members.size() >= config_.maxMembersPerRoom) {
        SendError(out, id, MsgType::JoinRoom, ErrorCode::RoomFull);
        return;
    }
    const std::string key = roomIt->first;
    LeaveRoom(id, c, now, out);
    // LeaveRoom never erases a room (Reap does), so the iterator's room is still there.
    rooms_[key].members.insert(id);
    c.roomId = key;
    Send(out, id, Writer(MsgType::RoomJoined).Str(FormatRoomId(key)).Take());
    Log("connection " + std::to_string(id) + " joined room " + MaskRoomId(key));
}

void RelayHub::HandleListSessions(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out) {
    const std::uint32_t requestId = r.U32();
    if (!r.Ok()) {
        SendError(out, id, MsgType::ListSessions, ErrorCode::Malformed);
        return;
    }
    auto roomIt = rooms_.find(c.roomId);
    if (c.roomId.empty() || roomIt == rooms_.end()) {
        SendError(out, id, MsgType::ListSessions, ErrorCode::NotInRoom);
        return;
    }
    Writer w(MsgType::SessionList);
    w.U32(requestId).U16(static_cast<std::uint16_t>(roomIt->second.sessions.size()));
    for (const auto& [key, session] : roomIt->second.sessions) {
        SessionInfo info = session.info;
        info.currentPlayers = static_cast<std::uint32_t>(1 + session.members.size());
        w.Session(info);
    }
    Send(out, id, w.Take());
}

void RelayHub::HandleHostSession(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out) {
    SessionInfo info = r.Session();
    if (!r.Ok()) {
        SendError(out, id, MsgType::HostSession, ErrorCode::Malformed);
        return;
    }
    auto roomIt = rooms_.find(c.roomId);
    if (c.roomId.empty() || roomIt == rooms_.end()) {
        SendError(out, id, MsgType::HostSession, ErrorCode::NotInRoom);
        return;
    }
    if (c.role != Role::None) {
        SendError(out, id, MsgType::HostSession, ErrorCode::AlreadyInSession);
        return;
    }
    Room& room = roomIt->second;
    if (room.sessions.size() >= config_.maxSessionsPerRoom) {
        SendError(out, id, MsgType::HostSession, ErrorCode::TooManySessions);
        return;
    }
    if (room.sessions.count(info.instance) != 0) {
        SendError(out, id, MsgType::HostSession, ErrorCode::SessionExists);
        return;
    }
    Session session;
    info.currentPlayers = 1;
    session.info = info;
    session.host = id;
    room.sessions.emplace(info.instance, std::move(session));
    c.role = Role::Host;
    c.session = info.instance;
    Send(out, id, Writer(MsgType::SessionHosted).Take());
    Log("connection " + std::to_string(id) + " hosts a game in room " + MaskRoomId(c.roomId));
}

void RelayHub::HandleJoinSession(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out) {
    const RelayGuid instance = r.Guid();
    if (!r.Ok()) {
        SendError(out, id, MsgType::JoinSession, ErrorCode::Malformed);
        return;
    }
    auto roomIt = rooms_.find(c.roomId);
    if (c.roomId.empty() || roomIt == rooms_.end()) {
        SendError(out, id, MsgType::JoinSession, ErrorCode::NotInRoom);
        return;
    }
    if (c.role != Role::None) {
        SendError(out, id, MsgType::JoinSession, ErrorCode::AlreadyInSession);
        return;
    }
    auto sessionIt = roomIt->second.sessions.find(instance);
    if (sessionIt == roomIt->second.sessions.end()) {
        SendError(out, id, MsgType::JoinSession, ErrorCode::SessionNotFound);
        return;
    }
    Session& session = sessionIt->second;
    // The host refuses over dwMaxPlayers too; this only spares a round trip.
    if (session.info.maxPlayers != 0 && 1 + session.members.size() >= session.info.maxPlayers) {
        SendError(out, id, MsgType::JoinSession, ErrorCode::SessionFull);
        return;
    }
    session.members.emplace(id, false);
    c.role = Role::Member;
    c.session = instance;
    c.admitted = false;
    Send(out, id, Writer(MsgType::SessionJoined).U32(session.host).Take());
    Send(out, session.host, Writer(MsgType::PeerJoined).U32(id).Take());
}

void RelayHub::HandleAdmitOrRefuse(ConnId id, Conn& c, Reader& r, bool admit, std::vector<HubAction>& out) {
    const ConnId member = r.U32();
    if (!r.Ok()) {
        SendError(out, id, admit ? MsgType::Admit : MsgType::Refuse, ErrorCode::Malformed);
        return;
    }
    Session* session = c.role == Role::Host ? FindSession(c) : nullptr;
    if (!session) {
        SendError(out, id, admit ? MsgType::Admit : MsgType::Refuse, ErrorCode::NotHost);
        return;
    }
    auto memberIt = session->members.find(member);
    auto connIt = conns_.find(member);
    if (memberIt == session->members.end() || connIt == conns_.end()) return;  // already gone
    if (admit) {
        memberIt->second = true;
        connIt->second.admitted = true;
        return;
    }
    session->members.erase(memberIt);
    connIt->second.role = Role::None;
    connIt->second.admitted = false;
    Send(out, member, Writer(MsgType::SessionEnded).U8(static_cast<std::uint8_t>(SessionEndReason::Refused)).Take());
}

ConnId RelayHub::ConnForToken(const DatagramToken& token) const {
    auto it = tokens_.find(token);
    return it == tokens_.end() ? 0 : it->second;
}

std::vector<HubAction> RelayHub::OnDatagramData(ConnId conn, const std::uint8_t* body, std::size_t size,
                                                Clock::time_point) {
    std::vector<HubAction> out;
    auto it = conns_.find(conn);
    if (it == conns_.end() || !it->second.welcomed) return out;
    Reader r(body, size);
    HandleData(conn, it->second, r, out, true);
    return out;
}

void RelayHub::HandleData(ConnId id, Conn& c, Reader& r, std::vector<HubAction>& out, bool datagram) {
    const auto route = static_cast<Route>(r.U8());
    const ConnId target = r.U32();
    if (!r.Ok()) return;
    Session* session = FindSession(c);
    if (!session) return;  // e.g. a message that crossed the session's end: dropped
    const std::vector<std::uint8_t> payload = r.Rest();
    auto deliver = [&](ConnId to) {
        Send(out, to, Writer(MsgType::Deliver).U32(id).Bytes(payload.data(), payload.size()).Take());
        out.back().datagram = datagram;
    };
    switch (route) {
        case Route::ToHost:
            if (c.role == Role::Member) deliver(session->host);
            break;
        case Route::ToMember:
            if (c.role == Role::Host && session->members.count(target) != 0) deliver(target);
            break;
        case Route::ToAllOthers:
            if (c.role == Role::Member && !c.admitted) break;
            if (c.role == Role::Member) deliver(session->host);
            for (const auto& [member, admitted] : session->members) {
                if (admitted && member != id) deliver(member);
            }
            break;
        default:
            break;
    }
}

RelayHub::Session* RelayHub::FindSession(const Conn& c) {
    if (c.role == Role::None || c.roomId.empty()) return nullptr;
    auto roomIt = rooms_.find(c.roomId);
    if (roomIt == rooms_.end()) return nullptr;
    auto sessionIt = roomIt->second.sessions.find(c.session);
    return sessionIt == roomIt->second.sessions.end() ? nullptr : &sessionIt->second;
}

void RelayHub::LeaveSession(ConnId id, Conn& c, SessionEndReason reason, std::vector<HubAction>& out) {
    Session* session = FindSession(c);
    if (!session) {
        c.role = Role::None;
        c.admitted = false;
        return;
    }
    if (c.role == Role::Host) {
        for (const auto& [member, admitted] : session->members) {
            (void)admitted;
            auto memberIt = conns_.find(member);
            if (memberIt != conns_.end()) {
                memberIt->second.role = Role::None;
                memberIt->second.admitted = false;
            }
            Send(out, member, Writer(MsgType::SessionEnded).U8(static_cast<std::uint8_t>(reason)).Take());
        }
        rooms_[c.roomId].sessions.erase(c.session);
    } else {
        session->members.erase(id);
        Send(out, session->host, Writer(MsgType::PeerLeft).U32(id).Take());
    }
    c.role = Role::None;
    c.admitted = false;
}

void RelayHub::LeaveRoom(ConnId id, Conn& c, Clock::time_point now, std::vector<HubAction>& out) {
    if (c.roomId.empty()) return;
    LeaveSession(id, c, SessionEndReason::HostClosed, out);
    auto roomIt = rooms_.find(c.roomId);
    if (roomIt != rooms_.end()) {
        roomIt->second.members.erase(id);
        if (roomIt->second.members.empty()) roomIt->second.emptySince = now;
    }
    c.roomId.clear();
}

} // namespace free_direct_relay
