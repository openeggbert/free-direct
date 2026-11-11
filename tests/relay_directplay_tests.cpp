/**
 * @file relay_directplay_tests.cpp
 * @brief End-to-end test of the "FreeDirect server" provider over a real free-direct-server.
 *
 * The test process runs a relay server on a free local port, then starts copies of itself as
 * separate processes (each process has one server connection, like a game): a host, two
 * clients, and processes that must be refused. Every child uses only the public DirectPlay API
 * in the games' call order (blupi_recon src/network.cpp): DirectPlayEnumerate, DirectPlayCreate
 * with the server provider's GUID, EnumSessions, Open, CreatePlayer, Send(m_dpid, 0, ...),
 * Receive. The connection settings come from the environment (no dialog):
 * FREE_DIRECT_SERVER, FREE_DIRECT_SERVER_PASSWORD, FREE_DIRECT_ROOM, FREE_DIRECT_ROOM_FILE.
 *
 * Checked: room creation and joining by secret ID, distinct host-assigned DPIDs, sender
 * identities, a client's broadcast reaching the host and the other client exactly once and never
 * its sender, the server password, a wrong room ID, another game's identity, and room isolation.
 */
#include "dplay.h"

#include "RelayServer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "[%d] FAILED: %s (%s:%d)\n", static_cast<int>(getpid()), expr, file, line);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

using Clock = std::chrono::steady_clock;

const GUID kAppGuid = {0xbe4af9a0, 0xc311, 0x11d1, {0xbe, 0x62, 0x00, 0x40, 0xf6, 0x94, 0x48, 0x38}};
constexpr char kSessionName[] = "Relay test";

struct ProviderList {
    std::vector<std::pair<GUID, std::string>> items;
};

BOOL CALLBACK CollectProvider(LPGUID guid, LPSTR name, DWORD, DWORD, LPVOID context) {
    static_cast<ProviderList*>(context)->items.emplace_back(*guid, name);
    return TRUE;
}

struct SessionList {
    std::vector<std::pair<GUID, std::string>> items;
};

BOOL CALLBACK CollectSession(LPDPSESSIONDESC2 desc, LPDWORD, DWORD flags, LPVOID context) {
    if (flags & DPESC_TIMEDOUT) return FALSE;
    static_cast<SessionList*>(context)->items.emplace_back(desc->guidInstance, desc->lpszSessionNameA);
    return TRUE;
}

/// The server provider's IDirectPlay2A, created the way CNetwork::CreateProvider does.
LPDIRECTPLAY2A CreateServerProvider() {
    ProviderList providers;
    CHECK(DirectPlayEnumerateA(CollectProvider, &providers) == DP_OK);
    const GUID* serverGuid = nullptr;
    for (const auto& [guid, name] : providers.items) {
        if (name == "FreeDirect server") serverGuid = &guid;
    }
    CHECK(serverGuid != nullptr);
    if (!serverGuid) return nullptr;
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(const_cast<GUID*>(serverGuid), &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, reinterpret_cast<void**>(&dp2)) == DP_OK);
    dp->Release();
    return dp2;
}

HRESULT Enum(LPDIRECTPLAY2A dp, SessionList& out) {
    out.items.clear();
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.guidApplication = kAppGuid;
    return dp->EnumSessions(&desc, 0, CollectSession, &out, DPENUMSESSIONS_AVAILABLE);
}

bool Elapsed(Clock::time_point start, int seconds) {
    return Clock::now() - start > std::chrono::seconds(seconds);
}

bool ReceiveOne(LPDIRECTPLAY2A dp, DPID& from, std::string& text) {
    DPID to = 0;
    char buf[512] = {};
    DWORD size = sizeof(buf) - 1;
    from = 0;
    if (dp->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) != DP_OK) return false;
    text.assign(buf, size);
    return true;
}

void SendText(LPDIRECTPLAY2A dp, DPID self, const std::string& text) {
    CHECK(dp->Send(self, 0, DPSEND_GUARANTEED, const_cast<char*>(text.data()), static_cast<DWORD>(text.size())) == DP_OK);
}

// ===== Child roles =====

int RunHost() {
    LPDIRECTPLAY2A dp = CreateServerProvider();
    if (!dp) return 1;
    SessionList sessions;
    CHECK(Enum(dp, sessions) == DP_OK);  // creates the room (FREE_DIRECT_ROOM=new)
    CHECK(sessions.items.empty());

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST;
    desc.guidApplication = kAppGuid;
    desc.dwMaxPlayers = 4;
    desc.lpszSessionNameA = const_cast<char*>(kSessionName);
    CHECK(dp->Open(&desc, DPOPEN_CREATE) == DP_OK);
    DPID self = 99;
    CHECK(dp->CreatePlayer(&self, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(self == 0);

    std::set<DPID> hellos, peers, byes;
    bool welcomed = false;
    const auto start = Clock::now();
    while (byes.size() < 2 && !Elapsed(start, 25)) {
        DPID from = 0;
        std::string text;
        if (!ReceiveOne(dp, from, text)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (text == "hello") hellos.insert(from);
        else if (text.rfind("peer:", 0) == 0) {
            CHECK(text == "peer:" + std::to_string(from));  // the payload names its true sender
            peers.insert(from);
        } else if (text == "bye") byes.insert(from);
        else CHECK(!"unexpected message at the host");
        if (!welcomed && hellos.size() == 2) {
            SendText(dp, self, "welcome");
            welcomed = true;
        }
    }
    CHECK(hellos == (std::set<DPID>{1, 2}));
    CHECK(peers == (std::set<DPID>{1, 2}));  // each client's broadcast reached the host once
    CHECK(byes == (std::set<DPID>{1, 2}));
    // Let the last messages leave before closing.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(dp->Close() == DP_OK);
    dp->Release();
    return g_failures == 0 ? 0 : 1;
}

std::string WaitForRoomFile(const std::string& path) {
    const auto start = Clock::now();
    while (!Elapsed(start, 15)) {
        std::ifstream f(path);
        std::string id;
        if (f && std::getline(f, id) && !id.empty()) return id;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return {};
}

int RunClient() {
    const std::string room = WaitForRoomFile(std::getenv("FREE_DIRECT_ROOM_FILE"));
    CHECK(!room.empty());
    setenv("FREE_DIRECT_ROOM", room.c_str(), 1);
    unsetenv("FREE_DIRECT_ROOM_FILE");
    LPDIRECTPLAY2A dp = CreateServerProvider();
    if (!dp) return 1;

    SessionList sessions;
    GUID instance{};
    bool found = false;
    const auto start = Clock::now();
    while (!found && !Elapsed(start, 15)) {
        CHECK(Enum(dp, sessions) == DP_OK);
        for (const auto& [guid, name] : sessions.items) {
            if (name == kSessionName) {
                instance = guid;
                found = true;
            }
        }
        if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(found);
    if (!found) return 1;

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.guidInstance = instance;
    CHECK(dp->Open(&desc, DPOPEN_OPENSESSION) == DP_OK);
    DPID self = 99;
    CHECK(dp->CreatePlayer(&self, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(self == 1 || self == 2);
    const DPID other = self == 1 ? 2 : 1;
    SendText(dp, self, "hello");

    bool welcome = false, otherPeer = false;
    int welcomes = 0, otherPeers = 0;
    const auto loopStart = Clock::now();
    while (!Elapsed(loopStart, 20) && !(welcome && otherPeer)) {
        DPID from = 0;
        std::string text;
        if (!ReceiveOne(dp, from, text)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        CHECK(from != self);  // never our own broadcast back
        if (text == "welcome") {
            CHECK(from == 0);
            ++welcomes;
            welcome = true;
            SendText(dp, self, "peer:" + std::to_string(self));
        } else if (text == "peer:" + std::to_string(other)) {
            CHECK(from == other);
            ++otherPeers;
            otherPeer = true;
        } else if (text == "hello") {
            CHECK(from == other);  // the other client's hello, if it was admitted before it
        } else {
            CHECK(!"unexpected message at a client");
        }
    }
    CHECK(welcome);
    CHECK(otherPeer);
    SendText(dp, self, "bye");
    // Anything else that still arrives must not be a duplicate.
    const auto lingerStart = Clock::now();
    while (Clock::now() - lingerStart < std::chrono::milliseconds(300)) {
        DPID from = 0;
        std::string text;
        if (ReceiveOne(dp, from, text)) {
            if (text == "welcome") ++welcomes;
            if (text == "peer:" + std::to_string(other)) ++otherPeers;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    CHECK(welcomes == 1);
    CHECK(otherPeers == 1);
    CHECK(dp->Close() == DP_OK);
    dp->Release();
    return g_failures == 0 ? 0 : 1;
}

/// Must not get in: wrong password, wrong room, or another game.
int RunRefused() {
    const char* file = std::getenv("FREE_DIRECT_ROOM_FILE");
    if (file) {
        const std::string room = WaitForRoomFile(file);
        if (!std::getenv("FREE_DIRECT_ROOM")) setenv("FREE_DIRECT_ROOM", room.c_str(), 1);
        unsetenv("FREE_DIRECT_ROOM_FILE");
    }
    LPDIRECTPLAY2A dp = CreateServerProvider();
    if (!dp) return 1;
    SessionList sessions;
    CHECK(Enum(dp, sessions) == DPERR_NOCONNECTION);
    CHECK(sessions.items.empty());
    dp->Release();
    return g_failures == 0 ? 0 : 1;
}

/// In a room of its own: sees no session of the host's room.
int RunIsolated() {
    LPDIRECTPLAY2A dp = CreateServerProvider();
    if (!dp) return 1;
    // Give the host time to create its session in the other room.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    SessionList sessions;
    CHECK(Enum(dp, sessions) == DP_OK);
    CHECK(sessions.items.empty());
    dp->Release();
    return g_failures == 0 ? 0 : 1;
}

/// Interoperability probe, run by blupi_recon's freeapi/tools/net_web.mjs against a browser
/// game: finds the session named NET_PROBE_SESSION in FREE_DIRECT_ROOM, joins it (the WASM
/// host admits this native process and assigns its DPID), and leaves without sending game data.
int RunProbe() {
    const char* wanted = std::getenv("NET_PROBE_SESSION");
    CHECK(wanted != nullptr);
    if (!wanted) return 1;
    LPDIRECTPLAY2A dp = CreateServerProvider();
    if (!dp) return 1;
    SessionList sessions;
    GUID instance{};
    bool found = false;
    const auto start = Clock::now();
    while (!found && !Elapsed(start, 15)) {
        CHECK(Enum(dp, sessions) == DP_OK);
        for (const auto& [guid, name] : sessions.items) {
            if (name == wanted) {
                instance = guid;
                found = true;
            }
        }
        if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(found);
    if (!found) return 1;
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.guidInstance = instance;
    CHECK(dp->Open(&desc, DPOPEN_OPENSESSION) == DP_OK);
    DPID self = 0;
    CHECK(dp->CreatePlayer(&self, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(self >= 1);
    std::printf("probe: joined session '%s' as DPID %lu\n", wanted, static_cast<unsigned long>(self));
    CHECK(dp->Close() == DP_OK);
    dp->Release();
    return g_failures == 0 ? 0 : 1;
}

// ===== Parent =====

struct Child {
    std::string role;
    pid_t pid = -1;
};

pid_t Spawn(const std::string& self, const std::string& role, const std::map<std::string, std::string>& env) {
    std::map<std::string, std::string> merged;
    for (char** e = environ; *e; ++e) {
        const std::string kv = *e;
        const auto eq = kv.find('=');
        if (eq != std::string::npos) merged[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    for (const auto& [k, v] : env) {
        if (v.empty()) merged.erase(k);
        else merged[k] = v;
    }
    std::vector<std::string> envStrings;
    for (const auto& [k, v] : merged) envStrings.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : envStrings) envp.push_back(s.data());
    envp.push_back(nullptr);
    std::string arg0 = self, arg1 = "--child", arg2 = role;
    char* argv[] = {arg0.data(), arg1.data(), arg2.data(), nullptr};
    pid_t pid = -1;
    if (posix_spawn(&pid, self.c_str(), nullptr, nullptr, argv, envp.data()) != 0) return -1;
    return pid;
}

bool WaitAll(std::vector<Child>& children, int seconds) {
    bool ok = true;
    const auto start = Clock::now();
    for (auto& child : children) {
        int status = 0;
        for (;;) {
            const pid_t r = waitpid(child.pid, &status, WNOHANG);
            if (r == child.pid) break;
            if (Elapsed(start, seconds)) {
                kill(child.pid, SIGKILL);
                waitpid(child.pid, &status, 0);
                std::fprintf(stderr, "child '%s' timed out\n", child.role.c_str());
                ok = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::fprintf(stderr, "child '%s' failed\n", child.role.c_str());
            ok = false;
        }
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--child") == 0) {
        const std::string role = argv[2];
        if (role == "host") return RunHost();
        if (role == "client") return RunClient();
        if (role == "refused") return RunRefused();
        if (role == "isolated") return RunIsolated();
        if (role == "probe") return RunProbe();
        return 2;
    }

    free_direct_relay::ServerOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.hub.password = "pw";
    options.hub.maxFailuresPerAddress = 1000;  // every child connects from 127.0.0.1
    const bool verbose = std::getenv("FREE_DIRECT_DEBUG_DPLAY") != nullptr;
    free_direct_relay::RelayServer server(options, [verbose](const std::string& line) {
        if (verbose) std::fprintf(stderr, "server: %s\n", line.c_str());
    });
    std::string error;
    if (!server.Start(error)) {
        std::fprintf(stderr, "cannot start the relay server: %s\n", error.c_str());
        return 1;
    }

    char selfPath[4096] = {};
    const ssize_t n = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
    if (n <= 0) return 1;
    const std::string self(selfPath, static_cast<std::size_t>(n));
    const std::string roomFile = "relay_directplay_tests.room." + std::to_string(getpid());
    std::remove(roomFile.c_str());

    const std::map<std::string, std::string> common = {
        {"FREE_DIRECT_SERVER", "127.0.0.1:" + std::to_string(server.Port())},
        {"FREE_DIRECT_SERVER_PASSWORD", "pw"},
        {"FREE_DIRECT_RELAY_COMPAT", "relay-test/1"},
        {"FREE_DIRECT_ROOM", ""},
        {"FREE_DIRECT_ROOM_FILE", roomFile},
        {"FREE_DIRECT_RELAY_TIMEOUT_MS", "5000"},
    };
    auto with = [&](std::map<std::string, std::string> overrides) {
        std::map<std::string, std::string> env = common;
        for (auto& [k, v] : overrides) env[k] = v;
        return env;
    };

    std::vector<Child> children;
    children.push_back({"host", Spawn(self, "host", with({{"FREE_DIRECT_ROOM", "new"}}))});
    children.push_back({"client", Spawn(self, "client", common)});
    children.push_back({"client", Spawn(self, "client", common)});
    children.push_back({"refused (wrong password)",
                        Spawn(self, "refused", with({{"FREE_DIRECT_SERVER_PASSWORD", "wrong"}}))});
    children.push_back({"refused (wrong room)",
                        Spawn(self, "refused", with({{"FREE_DIRECT_ROOM", "ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ"}}))});
    children.push_back({"refused (other game)",
                        Spawn(self, "refused", with({{"FREE_DIRECT_RELAY_COMPAT", "another-game/1"}}))});
    children.push_back({"isolated", Spawn(self, "isolated", with({{"FREE_DIRECT_ROOM", "new"}, {"FREE_DIRECT_ROOM_FILE", ""}}))});
    for (const auto& c : children) CHECK(c.pid > 0);

    CHECK(WaitAll(children, 60));
    std::remove(roomFile.c_str());
    server.Stop();
    if (g_failures == 0) {
        std::printf("OK: all relay DirectPlay tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d relay DirectPlay check(s) failed.\n", g_failures);
    return 1;
}
