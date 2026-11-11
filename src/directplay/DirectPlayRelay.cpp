/**
 * @file DirectPlayRelay.cpp
 * @brief The "FreeDirect server" provider's glue. See DirectPlayRelay.hpp.
 * @note Status: IMPLEMENTED
 */
#include "DirectPlayRelay.hpp"

#include "DirectPlayWireProtocol.hpp"
#include "RelayClient.hpp"
#include "free_api_bridge.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace free_direct_directplay {

using namespace free_direct_relay;

namespace {

#ifdef __EMSCRIPTEN__
EM_JS(int, FreeDirectPageIsSecure, (), { return (typeof location !== 'undefined' && location.protocol === 'https:') ? 1 : 0; });
#endif

std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool StartsWithNoCase(const std::string& s, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    }
    return true;
}

bool ValidPort(const std::string& port) {
    if (port.empty() || port.size() > 5) return false;
    for (char c : port) {
        if (c < '0' || c > '9') return false;
    }
    const long v = std::strtol(port.c_str(), nullptr, 10);
    return v >= 1 && v <= 65535;
}

void CopyField(char* dest, std::size_t size, const std::string& value) {
    std::snprintf(dest, size, "%s", value.c_str());
}

struct Request {
    std::string server;
    std::string port;
    std::string password;
    std::string roomId;
    bool createRoom = false;
};

/// The last request that succeeded, to prefill the dialog and to reconnect silently.
Request g_last;
bool g_haveLast = false;

Request FromEnvironment() {
    Request r;
    r.server = Env("FREE_DIRECT_SERVER");
    r.port = Env("FREE_DIRECT_SERVER_PORT");
    r.password = Env("FREE_DIRECT_SERVER_PASSWORD");
    const std::string room = Trim(Env("FREE_DIRECT_ROOM"));
    if (StartsWithNoCase(room, "new") && room.size() == 3) {
        r.createRoom = true;
    } else {
        r.roomId = room;
        r.createRoom = room.empty();
    }
    return r;
}

bool Automatic() { return !Env("FREE_DIRECT_SERVER").empty() && !Trim(Env("FREE_DIRECT_ROOM")).empty(); }

void AnnounceRoom(const std::string& roomId, const std::string& url) {
    std::fprintf(stderr, "free-direct: created room %s on %s\n", roomId.c_str(), url.c_str());
    const std::string file = Env("FREE_DIRECT_ROOM_FILE");
    if (!file.empty()) {
        // Plain fopen: Free API's windows.h redefines it as a macro (path normalization).
        if (FILE* f = fopen(file.c_str(), "w")) {
            std::fprintf(f, "%s\n", roomId.c_str());
            fclose(f);
        }
    }
    if (!Automatic()) FreeApiShowRoomCreated(roomId.c_str(), url.c_str());
}

/// Connects and enters the requested room; on success remembers the request.
bool TryEnter(Request req, std::string& error) {
    const std::string url = BuildRelayUrl(req.server, req.port, error);
    if (url.empty()) return false;
    RelayClient& client = RelayClient::Instance();
    if (!client.Connect(url, req.password, RelayCompatIdentity(), error)) return false;
    if (req.createRoom) {
        std::string roomId;
        if (!client.CreateRoom(roomId, error)) return false;
        AnnounceRoom(roomId, url);
    } else {
        if (Trim(req.roomId).empty()) {
            error = "enter the room's ID, or choose to create a room";
            return false;
        }
        const auto wanted = NormalizeRoomId(req.roomId);
        const auto current = NormalizeRoomId(client.RoomId());
        if (!(client.InRoom() && wanted && current && *wanted == *current)) {
            if (!client.JoinRoom(req.roomId, error)) return false;
        }
    }
    req.createRoom = false;
    req.roomId = client.RoomId();
    g_last = req;
    g_haveLast = true;
    return true;
}

/// Shows the dialog until the player connects or cancels.
HRESULT DialogLoop(Request req, std::string message) {
    for (;;) {
        FreeApiServerConnection dialog{};
        CopyField(dialog.server, sizeof(dialog.server), req.server);
        CopyField(dialog.port, sizeof(dialog.port), req.port.empty() ? std::to_string(kDefaultRelayPort) : req.port);
        CopyField(dialog.password, sizeof(dialog.password), req.password);
        CopyField(dialog.roomId, sizeof(dialog.roomId), req.roomId);
        dialog.createRoom = req.createRoom ? 1 : 0;
        CopyField(dialog.message, sizeof(dialog.message), message);
        if (!FreeApiPromptServerConnection(&dialog)) return DPERR_USERCANCEL;
        req.server = dialog.server;
        req.port = dialog.port;
        req.password = dialog.password;
        req.roomId = dialog.roomId;
        req.createRoom = dialog.createRoom != 0;
        std::string error;
        if (TryEnter(req, error)) return DP_OK;
        message = error;
    }
}

} // namespace

std::string BuildRelayUrl(const std::string& serverIn, const std::string& portIn, std::string& error) {
    std::string server = Trim(serverIn);
    const std::string port = Trim(portIn);
    if (server.empty()) {
        error = "enter the server's address";
        return {};
    }
    std::string scheme;
    std::string rest;
    if (StartsWithNoCase(server, "ws://")) {
        scheme = "ws";
        rest = server.substr(5);
    } else if (StartsWithNoCase(server, "wss://")) {
        scheme = "wss";
        rest = server.substr(6);
    } else if (StartsWithNoCase(server, "http://")) {
        scheme = "ws";
        rest = server.substr(7);
    } else if (StartsWithNoCase(server, "https://")) {
        scheme = "wss";
        rest = server.substr(8);
    } else if (server.find("://") != std::string::npos) {
        error = "the server address must be a host name, an IP address or a ws:// or wss:// URL";
        return {};
    } else {
#ifdef __EMSCRIPTEN__
        scheme = FreeDirectPageIsSecure() ? "wss" : "ws";
#else
        scheme = "ws";
#endif
        rest = server;
    }
    const std::size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty() || authority.find_first_of(" \t@") != std::string::npos) {
        error = "invalid server address";
        return {};
    }
    // The authority's own port, if any: after the last ':' for a host name or IPv4 address,
    // after "]:" for a bracketed IPv6 address. A bare IPv6 address needs brackets.
    std::string host = authority;
    std::string ownPort;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos) {
            error = "invalid IPv6 address";
            return {};
        }
        host = authority.substr(0, close + 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                error = "invalid server address";
                return {};
            }
            ownPort = authority.substr(close + 2);
        }
    } else {
        const std::size_t colon = authority.find(':');
        if (colon != std::string::npos) {
            if (authority.find(':', colon + 1) != std::string::npos) {
                error = "write an IPv6 address in brackets, e.g. [::1]";
                return {};
            }
            host = authority.substr(0, colon);
            ownPort = authority.substr(colon + 1);
        }
    }
    std::string usePort = !ownPort.empty() ? ownPort : (!port.empty() ? port : std::to_string(kDefaultRelayPort));
    if (host.empty() || !ValidPort(usePort)) {
        error = "invalid port (1-65535)";
        return {};
    }
#ifdef __EMSCRIPTEN__
    if (scheme == "ws" && FreeDirectPageIsSecure() && host != "localhost" && host != "127.0.0.1" && host != "[::1]") {
        error = "this page uses https, so the browser only allows secure servers (wss://, a server with TLS)";
        return {};
    }
#endif
    return scheme + "://" + host + ":" + usePort + path;
}

std::string RelayCompatIdentity() {
    const std::string env = Env("FREE_DIRECT_RELAY_COMPAT");
    if (!env.empty()) return env;
#ifdef FREE_DIRECT_RELAY_COMPAT
    return FREE_DIRECT_RELAY_COMPAT;
#else
    return "free-direct";
#endif
}

RelayGuid ToRelayGuid(const GUID& guid) {
    RelayGuid out{};
    std::uint8_t* p = out.data();
    PutWireGuid(p, guid);
    return out;
}

GUID FromRelayGuid(const RelayGuid& guid) {
    const std::uint8_t* p = guid.data();
    return GetWireGuid(p);
}

HRESULT EnsureRelayRoom(bool confirmWithPlayer) {
    RelayClient& client = RelayClient::Instance();
    client.Pump();
    Request req = g_haveLast ? g_last : FromEnvironment();
    if (client.InRoom()) {
        req.roomId = client.RoomId();
        req.createRoom = false;
    }
    if (Automatic()) {
        if (client.InRoom()) return DP_OK;
        std::string error;
        if (TryEnter(g_haveLast ? g_last : FromEnvironment(), error)) return DP_OK;
        std::fprintf(stderr, "free-direct: cannot enter the room: %s\n", error.c_str());
        return DPERR_NOCONNECTION;
    }
    if (confirmWithPlayer) return DialogLoop(req, std::string());
    if (client.InRoom()) return DP_OK;
    std::string error;
    if (g_haveLast && TryEnter(g_last, error)) return DP_OK;
    return DialogLoop(req, error);
}

} // namespace free_direct_directplay
