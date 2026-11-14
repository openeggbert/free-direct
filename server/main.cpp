/**
 * @file main.cpp
 * @brief `free-direct-server`: the Free Direct relay server for Internet multiplayer.
 *
 * A WebSocket server (IXWebSocket) in front of `RelayHub`. Games built with Free Direct's relay
 * provider connect to it from native builds and from browsers. See `docs/free-direct-server.md`
 * for the options, deployment and TLS.
 * @note Status: IMPLEMENTED
 */
#include "RelayServer.hpp"


#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>


using namespace free_direct_relay;

namespace {

constexpr const char* kVersionText = "free-direct-server 1.0 (protocol 1)";
// Set from the signal handler, which may do nothing else (async-signal safety); the main thread
// polls it.
volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int) { g_stop = 1; }

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

std::mutex g_logMutex;
void LogLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::fprintf(stderr, "%s %s\n", Timestamp().c_str(), line.c_str());
}

using Options = ServerOptions;

void PrintUsage() {
    std::printf(
        "Usage: free-direct-server [options]\n"
        "\n"
        "Free Direct relay server: Internet multiplayer for games built with Free Direct.\n"
        "Players connect with the game's \"FreeDirect server\" provider, enter this server's\n"
        "address and port, then create a room or join one with its secret ID.\n"
        "\n"
        "  --host ADDRESS            listen address (default 0.0.0.0)\n"
        "  --port PORT               listen port (default %u)\n"
        "  --password-file FILE      require the password in FILE (first line)\n"
        "                            (or set FREE_DIRECT_SERVER_PASSWORD)\n"
        "  --tls-cert FILE           serve wss:// with this PEM certificate chain\n"
        "  --tls-key FILE            ... and this PEM private key\n"
        "  --allow-origin ORIGIN     accept browser pages from ORIGIN only (repeatable,\n"
        "                            e.g. https://example.org); default: any origin\n"
        "  --max-connections N       default %zu\n"
        "  --max-rooms N             default %zu\n"
        "  --max-room-members N      default %zu\n"
        "  --max-room-games N        default %zu\n"
        "  --room-idle-timeout SECS  delete a room this long after its last member left\n"
        "                            (default %lld)\n"
        "  --ping-interval SECS      WebSocket ping interval, 0 disables (default 20)\n"
        "  --udp-port PORT           UDP port for datagrams (default: the same number as --port)\n"
        "  --no-udp                  no datagrams: everything over the WebSocket\n"
        "  --rtc-port PORT           UDP port of the WebRTC data channels for browsers\n"
        "                            (default: the datagram port + 1; built with libdatachannel only)\n"
        "  --no-webrtc               no WebRTC: browsers send everything over the WebSocket\n"
        "  --version                 print the version\n"
        "  --help                    this text\n",
        static_cast<unsigned>(kDefaultRelayPort), HubConfig{}.maxConnections, HubConfig{}.maxRooms,
        HubConfig{}.maxMembersPerRoom, HubConfig{}.maxSessionsPerRoom,
        static_cast<long long>(HubConfig{}.roomIdleTimeout.count()));
}

bool ParseSize(const char* text, std::size_t& out) {
    char* end = nullptr;
    const long long v = std::strtoll(text, &end, 10);
    if (!end || *end != '\0' || v <= 0) return false;
    out = static_cast<std::size_t>(v);
    return true;
}

int ParseOptions(int argc, char** argv, Options& o) {
    if (const char* env = std::getenv("FREE_DIRECT_SERVER_PASSWORD")) o.hub.password = env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "free-direct-server: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        const char* v = nullptr;
        std::size_t n = 0;
        if (a == "--help" || a == "-h") {
            PrintUsage();
            return 0;
        } else if (a == "--version") {
            std::printf("%s\n", kVersionText);
            return 0;
        } else if (a == "--host") {
            if (!(v = value("--host"))) return 2;
            o.host = v;
        } else if (a == "--port") {
            if (!(v = value("--port")) || !ParseSize(v, n) || n > 65535) return 2;
            o.port = static_cast<int>(n);
        } else if (a == "--password-file") {
            if (!(v = value("--password-file"))) return 2;
            std::ifstream f(v);
            if (!f || !std::getline(f, o.hub.password)) {
                std::fprintf(stderr, "free-direct-server: cannot read %s\n", v);
                return 2;
            }
            if (!o.hub.password.empty() && o.hub.password.back() == '\r') o.hub.password.pop_back();
        } else if (a == "--tls-cert") {
            if (!(v = value("--tls-cert"))) return 2;
            o.tlsCert = v;
        } else if (a == "--tls-key") {
            if (!(v = value("--tls-key"))) return 2;
            o.tlsKey = v;
        } else if (a == "--allow-origin") {
            if (!(v = value("--allow-origin"))) return 2;
            o.hub.allowedOrigins.emplace_back(v);
        } else if (a == "--max-connections") {
            if (!(v = value(a.c_str())) || !ParseSize(v, o.hub.maxConnections)) return 2;
        } else if (a == "--max-rooms") {
            if (!(v = value(a.c_str())) || !ParseSize(v, o.hub.maxRooms)) return 2;
        } else if (a == "--max-room-members") {
            if (!(v = value(a.c_str())) || !ParseSize(v, o.hub.maxMembersPerRoom)) return 2;
        } else if (a == "--max-room-games") {
            if (!(v = value(a.c_str())) || !ParseSize(v, o.hub.maxSessionsPerRoom)) return 2;
        } else if (a == "--room-idle-timeout") {
            if (!(v = value(a.c_str())) || !ParseSize(v, n)) return 2;
            o.hub.roomIdleTimeout = std::chrono::seconds(n);
        } else if (a == "--ping-interval") {
            if (!(v = value(a.c_str()))) return 2;
            o.pingSeconds = std::atoi(v);
        } else if (a == "--udp-port") {
            if (!(v = value(a.c_str()))) return 2;
            o.udpPort = std::atoi(v);
            if (o.udpPort < 1 || o.udpPort > 65535) {
                std::fprintf(stderr, "free-direct-server: --udp-port wants 1..65535\n");
                return 2;
            }
        } else if (a == "--no-udp") {
            o.datagrams = false;
        } else if (a == "--rtc-port") {
            if (!(v = value(a.c_str()))) return 2;
            o.rtcPort = std::atoi(v);
            if (o.rtcPort < 1 || o.rtcPort > 65535) {
                std::fprintf(stderr, "free-direct-server: --rtc-port wants 1..65535\n");
                return 2;
            }
        } else if (a == "--no-webrtc") {
            o.webrtc = false;
        } else {
            std::fprintf(stderr, "free-direct-server: unknown option %s (see --help)\n", a.c_str());
            return 2;
        }
    }
    if (o.tlsCert.empty() != o.tlsKey.empty()) {
        std::fprintf(stderr, "free-direct-server: --tls-cert and --tls-key go together\n");
        return 2;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    const int parsed = ParseOptions(argc, argv, options);
    if (parsed >= 0) return parsed;

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    RelayServer server(options, LogLine);
    std::string error;
    if (!server.Start(error)) {
        std::fprintf(stderr, "free-direct-server: %s\n", error.c_str());
        return 1;
    }
    LogLine(std::string(kVersionText) + " listening on " + (options.tlsCert.empty() ? "ws://" : "wss://") +
            options.host + ":" + std::to_string(server.Port()) +
            (options.hub.password.empty() ? ", no password" : ", password required") +
            (options.hub.allowedOrigins.empty() ? ", any web origin" : ""));

    // Delete idle rooms once a second until a signal arrives.
    for (int tick = 0; !g_stop; ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (tick % 10 == 0) server.Reap();
    }
    LogLine("shutting down");
    server.Stop();
    return 0;
}
