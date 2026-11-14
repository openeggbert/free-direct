/**
 * @file RelayClient.cpp
 * @brief The process-wide Free Direct server connection. See RelayClient.hpp.
 * @note Status: IMPLEMENTED
 */
#include "RelayClient.hpp"

#include "DirectPlayWait.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace free_direct_directplay {

using namespace free_direct_relay;

namespace {
void RelayLog(const char* format, const std::string& text) {
    const char* flag = std::getenv("FREE_DIRECT_DEBUG_DPLAY");
    if (!flag || !*flag || *flag == '0') return;
    std::fprintf(stderr, "free-direct relay: ");
    std::fprintf(stderr, format, text.c_str());
    std::fputc('\n', stderr);
}
} // namespace

RelayClient& RelayClient::Instance() {
    static RelayClient client;
    return client;
}

long RelayClient::TimeoutMs() {
    const char* raw = std::getenv("FREE_DIRECT_RELAY_TIMEOUT_MS");
    if (raw && *raw) {
        char* end = nullptr;
        const long value = std::strtol(raw, &end, 10);
        if (end && *end == '\0' && value > 0) return value;
    }
    return 8000;
}

bool RelayClient::WaitFor(bool (*done)(const RelayClient&), long timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        Pump();
        if (done(*this)) return true;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) return false;
        DirectPlaySleepMs(2);
    }
}

bool RelayClient::Connect(const std::string& url, const std::string& password, const std::string& compat,
                          std::string& error) {
    Pump();
    if (connected_ && url == url_ && password == password_ && compat == compat_) return true;
    Disconnect();

    socket_ = CreateRelaySocket();
    if (!socket_ || !socket_->Open(url)) {
        socket_.reset();
        error = "cannot connect to " + url;
        return false;
    }
    url_ = url;
    closeReason_.clear();
    RelayLog("connecting to %s", url);
    const bool settled = WaitFor([](const RelayClient& c) { return c.opened_ || !c.socket_; }, TimeoutMs());
    if (!socket_ || !opened_) {
        error = !closeReason_.empty() ? closeReason_ : (settled ? "connection failed" : "no answer from the server");
        error = "cannot connect to " + url + ": " + error;
        Disconnect();
        return false;
    }

    std::vector<std::uint8_t> reply;
    Writer hello(MsgType::Hello);
    hello.U32(kRelayMagic).U16(kRelayProtocolVersion).Str(compat).Str(password);
#ifdef __EMSCRIPTEN__
    hello.Str("free-direct web");
#else
    hello.Str("free-direct native");
#endif
    if (!Request(std::move(hello), MsgType::Welcome, 0, reply, error)) {
        Disconnect();
        return false;
    }
    Reader r(reply.data(), reply.size());
    const std::uint16_t version = r.U16();
    if (!r.Ok() || version != kRelayProtocolVersion) {
        error = "incompatible Free Direct server version";
        Disconnect();
        return false;
    }
    r.U32();  // this connection's ID
    const std::uint8_t flags = r.U8();
    if (r.Ok() && (flags & kWelcomeFlagDatagrams)) {
        // The server's UDP port and this connection's token (a server before datagrams sends neither).
        const std::uint16_t port = r.U16();
        const std::vector<std::uint8_t> token = r.Rest();
        if (r.Ok() && token.size() >= kDatagramTokenBytes) {
            std::copy(token.begin(), token.begin() + kDatagramTokenBytes, datagramToken_.begin());
            datagrams_ = CreateRelayDatagrams();
            if (datagrams_ && !datagrams_->Start(RelayUrlHost(url), port, datagramToken_)) datagrams_.reset();
        }
    }
    password_ = password;
    compat_ = compat;
    connected_ = true;
    RelayLog("connected to %s", url);
    return true;
}

void RelayClient::Disconnect() {
    datagrams_.reset();
    if (socket_) {
        socket_->Close();
        socket_.reset();
    }
    const bool wasConnected = connected_ || opened_;
    opened_ = false;
    connected_ = false;
    roomId_.clear();
    password_.clear();
    compat_.clear();
    if (pending_.active && !pending_.done) {
        pending_.done = true;
        pending_.ok = false;
        pending_.error = "disconnected";
    }
    if (wasConnected && listener_) listener_->OnDisconnected();
}

bool RelayClient::Request(Writer message, MsgType successType, std::uint32_t requestId,
                          std::vector<std::uint8_t>& reply, std::string& error) {
    if (!socket_) {
        error = "not connected to a server";
        return false;
    }
    closeReason_.clear();
    pending_ = Pending{};
    pending_.active = true;
    pending_.requestType = static_cast<MsgType>(message.Data()[0]);
    pending_.successType = successType;
    pending_.requestId = requestId;
    if (!socket_->Send(message.Data())) {
        pending_.active = false;
        error = closeReason_.empty() ? "connection lost" : closeReason_;
        return false;
    }
    const bool answered = WaitFor([](const RelayClient& c) { return c.pending_.done; }, TimeoutMs());
    pending_.active = false;
    if (!answered) {
        error = "no answer from the server";
        return false;
    }
    if (!pending_.ok) {
        error = pending_.error;
        return false;
    }
    reply = std::move(pending_.reply);
    return true;
}

bool RelayClient::CreateRoom(std::string& outRoomId, std::string& error) {
    std::vector<std::uint8_t> reply;
    if (!Request(Writer(MsgType::CreateRoom), MsgType::RoomCreated, 0, reply, error)) return false;
    Reader r(reply.data(), reply.size());
    roomId_ = r.Str();
    outRoomId = roomId_;
    RelayLog("created room %s", roomId_.substr(0, 4) + "...");
    return r.Ok();
}

bool RelayClient::JoinRoom(const std::string& roomId, std::string& error) {
    if (!NormalizeRoomId(roomId)) {
        error = "a room ID has " + std::to_string(kRoomIdLength) + " letters and digits";
        return false;
    }
    std::vector<std::uint8_t> reply;
    if (!Request(std::move(Writer(MsgType::JoinRoom).Str(roomId)), MsgType::RoomJoined, 0, reply, error)) {
        return false;
    }
    Reader r(reply.data(), reply.size());
    roomId_ = r.Str();
    RelayLog("joined room %s", roomId_.substr(0, 4) + "...");
    return r.Ok();
}

bool RelayClient::ListSessions(std::vector<SessionInfo>& out, std::string& error) {
    const std::uint32_t requestId = nextRequestId_++;
    std::vector<std::uint8_t> reply;
    if (!Request(std::move(Writer(MsgType::ListSessions).U32(requestId)), MsgType::SessionList, requestId,
                 reply, error)) {
        return false;
    }
    Reader r(reply.data(), reply.size());
    r.U32();
    const std::uint16_t count = r.U16();
    out.clear();
    for (std::uint16_t i = 0; i < count && r.Ok(); ++i) out.push_back(r.Session());
    if (!r.Ok()) {
        error = "malformed session list";
        return false;
    }
    return true;
}

bool RelayClient::HostSession(const SessionInfo& info, std::string& error) {
    std::vector<std::uint8_t> reply;
    return Request(std::move(Writer(MsgType::HostSession).Session(info)), MsgType::SessionHosted, 0, reply, error);
}

bool RelayClient::JoinSession(const RelayGuid& instance, std::string& error) {
    std::vector<std::uint8_t> reply;
    return Request(std::move(Writer(MsgType::JoinSession).Guid(instance)), MsgType::SessionJoined, 0, reply, error);
}

void RelayClient::CloseSession() {
    if (socket_) socket_->Send(Writer(MsgType::CloseSession).Data());
}

void RelayClient::LeaveSession() {
    if (socket_) socket_->Send(Writer(MsgType::LeaveSession).Data());
}

void RelayClient::Admit(ConnId conn) {
    if (socket_) socket_->Send(Writer(MsgType::Admit).U32(conn).Data());
}

void RelayClient::Refuse(ConnId conn) {
    if (socket_) socket_->Send(Writer(MsgType::Refuse).U32(conn).Data());
}

bool RelayClient::SendData(Route route, ConnId target, const void* data, std::size_t size, bool reliable) {
    if (!socket_ || !connected_) return false;
    if (!reliable && DatagramsOn() && 4 + kDatagramTokenBytes + 1 + 1 + 4 + size <= kMaxDatagramBytes &&
        datagrams_->Send(DataDatagram(datagramToken_, static_cast<std::uint8_t>(route), target, data, size))) {
        return true;
    }
    Writer w(MsgType::Data);
    w.U8(static_cast<std::uint8_t>(route)).U32(target).Bytes(data, size);
    return socket_->Send(w.Data());
}

void RelayClient::Pump() {
    if (!socket_) return;
    IRelaySocket::Event e;
    // socket_ can be reset by OnClosed() while draining.
    while (socket_ && socket_->Poll(e)) {
        switch (e.kind) {
            case IRelaySocket::EventKind::Opened:
                opened_ = true;
                break;
            case IRelaySocket::EventKind::Message:
                if (!e.bytes.empty()) Handle(e.bytes);
                break;
            case IRelaySocket::EventKind::Closed:
                OnClosed(e.reason);
                return;
        }
    }
    if (datagrams_) {
        datagrams_->Pump([this](ConnId from, std::vector<std::uint8_t> payload) {
            if (listener_) listener_->OnDeliver(from, std::move(payload));
        });
    }
}

std::string RelayUrlHost(const std::string& url) {
    std::size_t at = url.find("://");
    at = at == std::string::npos ? 0 : at + 3;
    if (at < url.size() && url[at] == '[') {
        const std::size_t end = url.find(']', at);
        return end == std::string::npos ? std::string() : url.substr(at + 1, end - at - 1);
    }
    const std::size_t end = url.find_first_of(":/", at);
    return url.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

void RelayClient::OnClosed(const std::string& reason) {
    // An Error message just before the close says more than the close itself.
    if (closeReason_.empty()) closeReason_ = reason;
    RelayLog("connection closed: %s", closeReason_);
    const std::string savedReason = closeReason_;
    Disconnect();
    closeReason_ = savedReason;
}

void RelayClient::Handle(const std::vector<std::uint8_t>& message) {
    const auto type = static_cast<MsgType>(message[0]);
    Reader r(message.data() + 1, message.size() - 1);
    switch (type) {
        case MsgType::Error: {
            const auto requestType = static_cast<MsgType>(r.U8());
            const auto code = static_cast<ErrorCode>(r.U16());
            const std::string text = r.Str();
            const std::string error = !text.empty() ? text : ErrorCodeText(code);
            closeReason_ = error;
            if (pending_.active && !pending_.done && pending_.requestType == requestType) {
                pending_.done = true;
                pending_.ok = false;
                pending_.error = error;
            }
            RelayLog("server error: %s", error);
            return;
        }
        case MsgType::PeerJoined:
            if (listener_) listener_->OnPeerJoined(r.U32());
            return;
        case MsgType::PeerLeft:
            if (listener_) listener_->OnPeerLeft(r.U32());
            return;
        case MsgType::SessionEnded:
            if (listener_) listener_->OnSessionEnded(static_cast<SessionEndReason>(r.U8()));
            return;
        case MsgType::Deliver: {
            const ConnId from = r.U32();
            if (r.Ok() && listener_) listener_->OnDeliver(from, r.Rest());
            return;
        }
        default:
            break;
    }
    if (pending_.active && !pending_.done && pending_.successType == type) {
        if (type == MsgType::SessionList) {
            Reader idReader(message.data() + 1, message.size() - 1);
            if (idReader.U32() != pending_.requestId) return;  // an answer to an abandoned request
        }
        pending_.done = true;
        pending_.ok = true;
        pending_.reply.assign(message.begin() + 1, message.end());
    }
}

} // namespace free_direct_directplay
