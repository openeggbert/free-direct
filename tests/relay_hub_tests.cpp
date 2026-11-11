/**
 * @file relay_hub_tests.cpp
 * @brief Unit tests of free-direct-server's room logic (`server/RelayHub`) and of the relay
 *        protocol encoding, without any network.
 *
 * Each test drives the hub with the messages a client would send and checks the actions it
 * returns: who receives what, and which connections it closes.
 */
#include "RelayHub.hpp"
#include "RelayProtocol.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace free_direct_relay;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s (%s:%d)\n", expr, file, line);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

using Clock = RelayHub::Clock;

/// A hub plus the messages each connection has received so far.
struct Harness {
    explicit Harness(HubConfig config = {})
        : hub(config, [this](std::uint8_t* out, std::size_t n) {
              for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(++counter * 7 + i);
          }) {}

    RelayHub hub;
    unsigned counter = 0;
    Clock::time_point now = Clock::now();
    std::map<ConnId, std::vector<std::vector<std::uint8_t>>> inbox;
    std::map<ConnId, bool> closed;

    void Apply(const std::vector<HubAction>& actions) {
        for (const auto& a : actions) {
            if (a.kind == HubAction::Kind::Send) inbox[a.conn].push_back(a.bytes);
            else closed[a.conn] = true;
        }
    }
    void Open(ConnId c, const std::string& address = "10.0.0.1", const std::string& origin = "") {
        Apply(hub.OnOpen(c, address, origin, now));
    }
    void Msg(ConnId c, const Writer& w) {
        const auto& d = w.Data();
        Apply(hub.OnMessage(c, d.data(), d.size(), now));
    }
    void Close(ConnId c) { Apply(hub.OnClose(c, now)); }
    void Hello(ConnId c, const std::string& compat = "sb1", const std::string& password = "") {
        Msg(c, std::move(Writer(MsgType::Hello).U32(kRelayMagic).U16(kRelayProtocolVersion).Str(compat).Str(password).Str("test")));
    }
    /// Pops the oldest message for `c`; returns its type, or 0 if there is none.
    std::uint8_t Next(ConnId c, std::vector<std::uint8_t>* body = nullptr) {
        auto& q = inbox[c];
        if (q.empty()) return 0;
        auto m = q.front();
        q.erase(q.begin());
        if (body) body->assign(m.begin() + 1, m.end());
        return m[0];
    }
    std::string CreateRoom(ConnId c) {
        Msg(c, Writer(MsgType::CreateRoom));
        std::vector<std::uint8_t> body;
        if (Next(c, &body) != static_cast<std::uint8_t>(MsgType::RoomCreated)) return {};
        Reader r(body.data(), body.size());
        return r.Str();
    }
    std::uint16_t ErrorCodeOf(const std::vector<std::uint8_t>& body) {
        Reader r(body.data(), body.size());
        r.U8();
        return r.U16();
    }
};

constexpr std::uint8_t T(MsgType t) { return static_cast<std::uint8_t>(t); }

RelayGuid Guid(std::uint8_t seed) {
    RelayGuid g{};
    for (std::size_t i = 0; i < g.size(); ++i) g[i] = static_cast<std::uint8_t>(seed + i);
    return g;
}

SessionInfo Session(std::uint8_t seed, std::uint32_t maxPlayers, const std::string& name) {
    SessionInfo s;
    s.instance = Guid(seed);
    s.application = Guid(100);
    s.maxPlayers = maxPlayers;
    s.name = name;
    return s;
}

void Test_RoomIdNormalization() {
    CHECK(NormalizeRoomId("abcde-fghjk-mnpqr-stvwx").value_or("") == "ABCDEFGHJKMNPQRSTVWX");
    // Crockford look-alikes: O is zero, I and L are one.
    CHECK(NormalizeRoomId("OOOOO IIIII LLLLL 22222").value_or("") == "00000111111111122222");
    CHECK(!NormalizeRoomId("ABCDE-FGHJK-MNPQR-STVW"));     // too short
    CHECK(!NormalizeRoomId("ABCDE-FGHJK-MNPQR-STVWXY"));   // too long
    CHECK(!NormalizeRoomId("ABCDE-FGHJK-MNPQR-STVWU"));    // U is not in the alphabet
    CHECK(FormatRoomId("ABCDEFGHJKMNPQRSTVWX") == "ABCDE-FGHJK-MNPQR-STVWX");
}

void Test_Hello_WrongVersionAndMagicAreRefused() {
    Harness h;
    h.Open(1);
    h.Msg(1, std::move(Writer(MsgType::Hello).U32(kRelayMagic).U16(kRelayProtocolVersion + 1).Str("sb1").Str("").Str("t")));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(1, &body) == T(MsgType::Error));
    CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::BadVersion));
    CHECK(h.closed[1]);

    h.Open(2);
    h.Msg(2, std::move(Writer(MsgType::Hello).U32(0x12345678).U16(kRelayProtocolVersion).Str("sb1").Str("").Str("t")));
    CHECK(h.Next(2) == T(MsgType::Error));
    CHECK(h.closed[2]);
}

void Test_RequestBeforeHello_IsRefused() {
    Harness h;
    h.Open(1);
    h.Msg(1, Writer(MsgType::CreateRoom));
    CHECK(h.Next(1) == T(MsgType::Error));
    CHECK(h.closed[1]);
    CHECK(h.hub.RoomCount() == 0);
}

void Test_Password_RequiredAndChecked() {
    HubConfig config;
    config.password = "secret";
    Harness h(config);
    h.Open(1);
    h.Hello(1, "sb1", "wrong");
    std::vector<std::uint8_t> body;
    CHECK(h.Next(1, &body) == T(MsgType::Error));
    CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::BadPassword));
    CHECK(h.closed[1]);

    h.Open(2);
    h.Hello(2, "sb1", "secret");
    CHECK(h.Next(2, &body) == T(MsgType::Welcome));
    Reader r(body.data(), body.size());
    CHECK(r.U16() == kRelayProtocolVersion);
    CHECK(r.U32() == 2);
    CHECK(r.U8() == 1);  // the server has a password
}

void Test_Password_RepeatedFailuresRateLimitTheAddress() {
    HubConfig config;
    config.password = "secret";
    config.maxFailuresPerAddress = 3;
    Harness h(config);
    for (ConnId c = 1; c <= 3; ++c) {
        h.Open(c, "192.0.2.7");
        h.Hello(c, "sb1", "guess");
        h.Close(c);
    }
    h.Open(4, "192.0.2.7");
    h.Hello(4, "sb1", "secret");  // right, but too late
    std::vector<std::uint8_t> body;
    CHECK(h.Next(4, &body) == T(MsgType::Error));
    CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::RateLimited));
    // Another address is not affected.
    h.Open(5, "192.0.2.8");
    h.Hello(5, "sb1", "secret");
    CHECK(h.Next(5) == T(MsgType::Welcome));
    // The window passes.
    h.now += config.failureWindow;
    h.Open(6, "192.0.2.7");
    h.Hello(6, "sb1", "secret");
    CHECK(h.Next(6) == T(MsgType::Welcome));
}

void Test_Room_CreateAndJoinWithSecretId() {
    Harness h;
    h.Open(1);
    h.Hello(1);
    h.Next(1);
    const std::string id = h.CreateRoom(1);
    CHECK(id.size() == kRoomIdLength + 3);
    CHECK(NormalizeRoomId(id).has_value());

    h.Open(2);
    h.Hello(2);
    h.Next(2);
    std::string lower = id;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(lower)));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(2, &body) == T(MsgType::RoomJoined));
    Reader r(body.data(), body.size());
    CHECK(r.Str() == id);
}

void Test_Room_WrongIdsCloseTheConnection() {
    HubConfig config;
    config.maxFailuresPerConnection = 3;
    Harness h(config);
    h.Open(1);
    h.Hello(1);
    h.Next(1);
    h.CreateRoom(1);
    h.Open(2);
    h.Hello(2);
    h.Next(2);
    std::vector<std::uint8_t> body;
    for (int i = 0; i < 3; ++i) {
        h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str("ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ")));
        CHECK(h.Next(2, &body) == T(MsgType::Error));
        CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::RoomNotFound));
    }
    CHECK(h.closed[2]);
}

void Test_Room_OtherGameOrEditionIsRefused() {
    Harness h;
    h.Open(1);
    h.Hello(1, "sb1/blupi");
    h.Next(1);
    const std::string id = h.CreateRoom(1);
    h.Open(2);
    h.Hello(2, "sb2/eggbert");
    h.Next(2);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(id)));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(2, &body) == T(MsgType::Error));
    CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::RoomIncompatible));
}

/// Two rooms, a host and a member in each; returns the room IDs.
void SetUpTwoRooms(Harness& h) {
    for (ConnId c = 1; c <= 4; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string a = h.CreateRoom(1);
    const std::string b = h.CreateRoom(3);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(a)));
    h.Next(2);
    h.Msg(4, std::move(Writer(MsgType::JoinRoom).Str(b)));
    h.Next(4);
}

void Test_Sessions_ListedOnlyInTheirRoom() {
    Harness h;
    SetUpTwoRooms(h);
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(1, 4, "Room A game"))));
    CHECK(h.Next(1) == T(MsgType::SessionHosted));

    std::vector<std::uint8_t> body;
    h.Msg(2, std::move(Writer(MsgType::ListSessions).U32(77)));
    CHECK(h.Next(2, &body) == T(MsgType::SessionList));
    Reader r(body.data(), body.size());
    CHECK(r.U32() == 77);
    CHECK(r.U16() == 1);
    const SessionInfo s = r.Session();
    CHECK(s.name == "Room A game");
    CHECK(s.currentPlayers == 1);
    CHECK(s.maxPlayers == 4);

    h.Msg(4, std::move(Writer(MsgType::ListSessions).U32(78)));
    CHECK(h.Next(4, &body) == T(MsgType::SessionList));
    Reader r2(body.data(), body.size());
    r2.U32();
    CHECK(r2.U16() == 0);
}

void Test_Sessions_JoinAdmitAndRoute() {
    Harness h;
    for (ConnId c = 1; c <= 3; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string id = h.CreateRoom(1);
    for (ConnId c = 2; c <= 3; ++c) {
        h.Msg(c, std::move(Writer(MsgType::JoinRoom).Str(id)));
        h.Next(c);
    }
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(9, 4, "G"))));
    h.Next(1);

    std::vector<std::uint8_t> body;
    for (ConnId c = 2; c <= 3; ++c) {
        h.Msg(c, std::move(Writer(MsgType::JoinSession).Guid(Guid(9))));
        CHECK(h.Next(c, &body) == T(MsgType::SessionJoined));
        Reader r(body.data(), body.size());
        CHECK(r.U32() == 1);
        CHECK(h.Next(1, &body) == T(MsgType::PeerJoined));
        Reader p(body.data(), body.size());
        CHECK(p.U32() == c);
    }

    // A pending member reaches the host (its Join packet), but not the others.
    h.Msg(2, std::move(Writer(MsgType::Data).U8(static_cast<std::uint8_t>(Route::ToHost)).U32(0).Bytes("J", 1)));
    CHECK(h.Next(1, &body) == T(MsgType::Deliver));
    h.Msg(2, std::move(Writer(MsgType::Data).U8(static_cast<std::uint8_t>(Route::ToAllOthers)).U32(0).Bytes("x", 1)));
    CHECK(h.Next(1) == 0);
    CHECK(h.Next(3) == 0);

    h.Msg(1, std::move(Writer(MsgType::Admit).U32(2)));
    h.Msg(1, std::move(Writer(MsgType::Admit).U32(3)));

    // Host -> one member.
    h.Msg(1, std::move(Writer(MsgType::Data).U8(static_cast<std::uint8_t>(Route::ToMember)).U32(3).Bytes("hi", 2)));
    CHECK(h.Next(3, &body) == T(MsgType::Deliver));
    {
        Reader r(body.data(), body.size());
        CHECK(r.U32() == 1);
        const auto payload = r.Rest();
        CHECK(payload.size() == 2 && payload[0] == 'h');
    }
    CHECK(h.Next(2) == 0);

    // A member's broadcast: host and the other member exactly once, never back to the sender.
    h.Msg(2, std::move(Writer(MsgType::Data).U8(static_cast<std::uint8_t>(Route::ToAllOthers)).U32(0).Bytes("all", 3)));
    CHECK(h.Next(1, &body) == T(MsgType::Deliver));
    CHECK(h.Next(1) == 0);
    CHECK(h.Next(3, &body) == T(MsgType::Deliver));
    {
        Reader r(body.data(), body.size());
        CHECK(r.U32() == 2);
    }
    CHECK(h.Next(3) == 0);
    CHECK(h.Next(2) == 0);

    // A member cannot address another member directly.
    h.Msg(2, std::move(Writer(MsgType::Data).U8(static_cast<std::uint8_t>(Route::ToMember)).U32(3).Bytes("no", 2)));
    CHECK(h.Next(3) == 0);
}

void Test_Sessions_FullSessionIsRefused() {
    Harness h;
    for (ConnId c = 1; c <= 3; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string id = h.CreateRoom(1);
    for (ConnId c = 2; c <= 3; ++c) {
        h.Msg(c, std::move(Writer(MsgType::JoinRoom).Str(id)));
        h.Next(c);
    }
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(5, 2, "Two"))));
    h.Next(1);
    h.Msg(2, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    CHECK(h.Next(2) == T(MsgType::SessionJoined));
    h.Msg(3, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(3, &body) == T(MsgType::Error));
    CHECK(h.ErrorCodeOf(body) == static_cast<std::uint16_t>(ErrorCode::SessionFull));
}

void Test_Sessions_RefuseEndsTheMembersSession() {
    Harness h;
    for (ConnId c = 1; c <= 2; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string id = h.CreateRoom(1);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(id)));
    h.Next(2);
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(5, 4, "S"))));
    h.Next(1);
    h.Msg(2, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    h.Next(2);
    h.Next(1);
    h.Msg(1, std::move(Writer(MsgType::Refuse).U32(2)));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(2, &body) == T(MsgType::SessionEnded));
    CHECK(body.size() == 1 && body[0] == static_cast<std::uint8_t>(SessionEndReason::Refused));
    // It can join again later (e.g. once a place is free).
    h.Msg(2, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    CHECK(h.Next(2) == T(MsgType::SessionJoined));
}

void Test_Sessions_HostLeavingEndsTheSession() {
    Harness h;
    for (ConnId c = 1; c <= 2; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string id = h.CreateRoom(1);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(id)));
    h.Next(2);
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(5, 4, "S"))));
    h.Next(1);
    h.Msg(2, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    h.Next(2);
    h.Next(1);
    h.Close(1);
    std::vector<std::uint8_t> body;
    CHECK(h.Next(2, &body) == T(MsgType::SessionEnded));
    CHECK(h.hub.SessionCount() == 0);
    // The room survives its creator; the member is still in it.
    CHECK(h.hub.RoomCount() == 1);
    h.Msg(2, std::move(Writer(MsgType::ListSessions).U32(1)));
    CHECK(h.Next(2) == T(MsgType::SessionList));
}

void Test_Sessions_MemberLeavingNotifiesTheHost() {
    Harness h;
    for (ConnId c = 1; c <= 2; ++c) {
        h.Open(c);
        h.Hello(c);
        h.Next(c);
    }
    const std::string id = h.CreateRoom(1);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(id)));
    h.Next(2);
    h.Msg(1, std::move(Writer(MsgType::HostSession).Session(Session(5, 4, "S"))));
    h.Next(1);
    h.Msg(2, std::move(Writer(MsgType::JoinSession).Guid(Guid(5))));
    h.Next(2);
    h.Next(1);
    h.Msg(2, Writer(MsgType::LeaveSession));
    std::vector<std::uint8_t> body;
    CHECK(h.Next(1, &body) == T(MsgType::PeerLeft));
    Reader r(body.data(), body.size());
    CHECK(r.U32() == 2);
}

void Test_Rooms_EmptyRoomIsDeletedAfterTimeout() {
    HubConfig config;
    config.roomIdleTimeout = std::chrono::seconds(60);
    Harness h(config);
    h.Open(1);
    h.Hello(1);
    h.Next(1);
    const std::string id = h.CreateRoom(1);
    h.Close(1);
    CHECK(h.hub.RoomCount() == 1);
    h.now += std::chrono::seconds(30);
    h.hub.Reap(h.now);
    CHECK(h.hub.RoomCount() == 1);
    // Rejoining within the timeout keeps the room.
    h.Open(2);
    h.Hello(2);
    h.Next(2);
    h.Msg(2, std::move(Writer(MsgType::JoinRoom).Str(id)));
    CHECK(h.Next(2) == T(MsgType::RoomJoined));
    h.Close(2);
    h.now += std::chrono::seconds(61);
    h.hub.Reap(h.now);
    CHECK(h.hub.RoomCount() == 0);
}

void Test_Origins_OnlyAllowedBrowserPagesButAnyNativeClient() {
    HubConfig config;
    config.allowedOrigins = {"https://games.example.org"};
    Harness h(config);
    h.Open(1, "10.0.0.1", "https://evil.example.net");
    CHECK(h.closed[1]);
    h.Open(2, "10.0.0.1", "https://games.example.org");
    h.Hello(2);
    CHECK(h.Next(2) == T(MsgType::Welcome));
    h.Open(3, "10.0.0.1", "");  // a native client sends no Origin...
    h.Hello(3);
    CHECK(h.Next(3) == T(MsgType::Welcome));
    h.Open(4, "10.0.0.1", "wss://relay.example.org:51325");  // ...or, like IXWebSocket, the server's URL
    h.Hello(4);
    CHECK(h.Next(4) == T(MsgType::Welcome));
    h.Open(5, "10.0.0.1", "null");  // a sandboxed or file:// page
    CHECK(h.closed[5]);
}

void Test_Limits_OversizedMessageClosesAndServerFullRefuses() {
    HubConfig config;
    config.maxConnections = 1;
    Harness h(config);
    h.Open(1);
    h.Hello(1);
    h.Next(1);
    std::vector<std::uint8_t> huge(kMaxRelayMessageBytes + 1, 0x0B);
    h.Apply(h.hub.OnMessage(1, huge.data(), huge.size(), h.now));
    CHECK(h.closed[1]);
    h.Open(2);
    CHECK(h.closed[2]);
}

void Test_Reader_RejectsTruncatedFields() {
    const std::uint8_t data[] = {5, 0, 'a', 'b'};  // string length 5, only 2 bytes
    Reader r(data, sizeof(data));
    r.Str();
    CHECK(!r.Ok());
    CHECK(r.U32() == 0);
}

void Test_ConstantTimeEquals() {
    CHECK(ConstantTimeEquals("abc", "abc"));
    CHECK(!ConstantTimeEquals("abc", "abd"));
    CHECK(!ConstantTimeEquals("abc", "abcd"));
    CHECK(ConstantTimeEquals("", ""));
}

} // namespace

int main() {
    Test_RoomIdNormalization();
    Test_Hello_WrongVersionAndMagicAreRefused();
    Test_RequestBeforeHello_IsRefused();
    Test_Password_RequiredAndChecked();
    Test_Password_RepeatedFailuresRateLimitTheAddress();
    Test_Room_CreateAndJoinWithSecretId();
    Test_Room_WrongIdsCloseTheConnection();
    Test_Room_OtherGameOrEditionIsRefused();
    Test_Sessions_ListedOnlyInTheirRoom();
    Test_Sessions_JoinAdmitAndRoute();
    Test_Sessions_FullSessionIsRefused();
    Test_Sessions_RefuseEndsTheMembersSession();
    Test_Sessions_HostLeavingEndsTheSession();
    Test_Sessions_MemberLeavingNotifiesTheHost();
    Test_Rooms_EmptyRoomIsDeletedAfterTimeout();
    Test_Origins_OnlyAllowedBrowserPagesButAnyNativeClient();
    Test_Limits_OversizedMessageClosesAndServerFullRefuses();
    Test_Reader_RejectsTruncatedFields();
    Test_ConstantTimeEquals();
    if (g_failures == 0) {
        std::printf("OK: all relay hub tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d relay hub check(s) failed.\n", g_failures);
    return 1;
}
