/**
 * @file enet_directplay_tests.cpp
 * @brief Transport-level `EnetDirectPlayTransport` tests (`plan.md`'s 24-Hour Stabilization
 * Backlog, TASK-24H-0097/0098/0108).
 *
 * Compiled and run **only** when `FREE_DIRECT_ENABLE_ENET=ON` (see `tests/CMakeLists.txt` -
 * gated at the CMake level, not just at runtime, matching `EnetDirectPlayTransport.cpp`'s own
 * `FREE_DIRECT_ENABLE_ENET`-gated compilation). Never added to the default build/CTest run
 * (CLAUDE.md Testing Policy: "real-socket ENet integration tests are opt-in ... never run by
 * default in a way that could flake on a shared/CI network namespace").
 *
 *   cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ENET=ON
 *   cmake --build <build>
 *   ctest --test-dir <build> -L enet
 *
 * Design notes:
 * - Most tests here exercise the transport class directly (whitebox, matching
 *   `directplay_tests.cpp`'s own `LoopbackDirectPlayTransport` tests), not through `IDirectPlay2A`
 *   - every whitebox test hardcodes both the host's and the client's addresses/ports directly,
 *   sidestepping ENet's fixed hosting port (`kDefaultDirectPlayEnetPort`, Decision 5) entirely.
 *   The `FREE_DIRECT_ENET_HOST_ADDRESS`-driven tests near the end of this file are the exception:
 *   they go through the real public `IDirectPlay2A::Open()` API (Decision 22, TASK-24H-0149,
 *   resolving what was TASK-24H-0132's BLOCKED "how does a joining Open() call learn a host
 *   address" question) - specifically because that capability lives in `DirectPlay.cpp`'s
 *   `Open()`, not in the transport, so a whitebox transport-only test could not exercise it.
 * - `tests/directplay_tests.cpp`'s 61 tests must **not** be run against an ENet-enabled build:
 *   confirmed during this session that doing so fails 29/61 checks, because those tests assume
 *   `LoopbackDirectPlayTransport`'s synchronous semantics (e.g. an immediate `DPERR_NOSESSIONS`
 *   on a failed join), which do not hold for `EnetDirectPlayTransport`'s real, asynchronous
 *   network model. This is expected and correct, not a regression - `directplay_tests.cpp` is
 *   scoped to the loopback backend by design (CLAUDE.md Testing Policy), and this separate file
 *   is where ENet-specific behavior belongs instead.
 * - ENet is fundamentally asynchronous (`Service()` must be polled for connection/receive events
 *   to actually happen), unlike loopback's synchronous registry lookups - every test below polls
 *   in a bounded loop (a generous 2-second budget, `kEnetPollTimeoutMs`) rather than assuming a
 *   single `Service()` call is enough, to avoid flaking under real (if local-only, 127.0.0.1)
 *   network scheduling jitter.
 */
#include "EnetDirectPlayTransport.hpp"
#include "dplay.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s (%s:%d)\n", expr, file, line);
        ++g_failures;
    }
}

} // namespace

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

namespace {

using free_direct_directplay::EnetDirectPlayTransport;

constexpr int kEnetPollTimeoutMs = 2000;
constexpr int kEnetPollIntervalMs = 5;

// Repeatedly calls Service() on both transports (ENet event delivery needs both sides pumped -
// a lone client Service() call cannot itself complete the host's accept, and vice versa) until
// `predicate` is true or the timeout budget is exhausted. Returns whether it succeeded in time.
template <typename Predicate>
bool PollUntil(EnetDirectPlayTransport& a, EnetDirectPlayTransport& b, Predicate predicate) {
    for (int elapsed = 0; elapsed <= kEnetPollTimeoutMs; elapsed += kEnetPollIntervalMs) {
        a.Service();
        b.Service();
        if (predicate()) return true;
        SDL_Delay(kEnetPollIntervalMs);
    }
    return false;
}

} // namespace

void Test_EnetTransport_ListenAndConnect_EstablishesConnection() {
    EnetDirectPlayTransport host;
    CHECK(host.IsEnetReady());
    CHECK(host.Listen(52101));
    CHECK(host.HasHost());

    EnetDirectPlayTransport client;
    CHECK(client.IsEnetReady());
    CHECK(client.Connect("127.0.0.1", 52101));
    CHECK(client.HasHost());

    const bool connected = PollUntil(host, client, [&]() {
        return host.HasPendingConnection() && client.IsConnectedToHost();
    });
    CHECK(connected);
    CHECK(host.HasPendingConnection());
    CHECK(client.IsConnectedToHost());

    CHECK(host.AssignPendingConnection(0));
    CHECK(host.ConnectedPeerCount() == 1);

    host.Shutdown();
    client.Shutdown();
}

// The real reliable-delivery smoke test this backlog originally asked for (TASK-24H-0097),
// covering host-to-client delivery with DPSEND_GUARANTEED-equivalent reliable framing.
void Test_EnetTransport_ReliableSend_HostToClient_DeliversPayload() {
    EnetDirectPlayTransport host;
    CHECK(host.Listen(52102));

    EnetDirectPlayTransport client;
    CHECK(client.Connect("127.0.0.1", 52102));

    const bool connected = PollUntil(host, client, [&]() {
        return host.HasPendingConnection() && client.IsConnectedToHost();
    });
    CHECK(connected);

    const DPID clientId = 7;
    CHECK(host.AssignPendingConnection(clientId));

    const char msg[] = "reliable-hello";
    CHECK(host.Send(clientId, msg, sizeof(msg), /*reliable=*/true));

    char buf[64] = {};
    std::size_t receivedSize = 0;
    const bool received = PollUntil(host, client, [&]() {
        return client.Receive(buf, sizeof(buf), &receivedSize);
    });
    CHECK(received);
    CHECK(receivedSize == sizeof(msg));
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);

    host.Shutdown();
    client.Shutdown();
}

// Same shape as above, but client-to-host, and unreliable (ENET_PACKET_FLAG_UNSEQUENCED) -
// proving the reliable flag is not the only path that delivers on a healthy local connection,
// and that the hosting role's Receive() (draining its own shared inbox regardless of which
// connectedPeers_ entry sent it) works for a message the host itself did not initiate.
void Test_EnetTransport_UnreliableSend_ClientToHost_DeliversPayload() {
    EnetDirectPlayTransport host;
    CHECK(host.Listen(52103));

    EnetDirectPlayTransport client;
    CHECK(client.Connect("127.0.0.1", 52103));

    const bool connected = PollUntil(host, client, [&]() {
        return host.HasPendingConnection() && client.IsConnectedToHost();
    });
    CHECK(connected);

    const DPID clientId = 3;
    CHECK(host.AssignPendingConnection(clientId));

    const char msg[] = "unreliable-hi";
    // Client sends to targetId 0 - the joining role's Send() always addresses hostPeer_
    // regardless of the target id value (there is only ever one possible target for a client).
    CHECK(client.Send(0, msg, sizeof(msg), /*reliable=*/false));

    char buf[64] = {};
    std::size_t receivedSize = 0;
    const bool received = PollUntil(host, client, [&]() {
        return host.Receive(buf, sizeof(buf), &receivedSize);
    });
    CHECK(received);
    CHECK(receivedSize == sizeof(msg));
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);

    host.Shutdown();
    client.Shutdown();
}

// plan.md Phase 10: "Add a reliable-delivery smoke test gated behind FREE_DIRECT_ENABLE_ENET,
// sending a batch of packets over a real local ENet host/client pair on 127.0.0.1 and asserting
// all arrive." Distinct from Test_EnetTransport_ReliableSend_HostToClient_DeliversPayload above,
// which only proves a single reliable send works at all - this proves a batch survives real ENet
// transport intact: none dropped, none duplicated, and (since all packets here are sent
// DPSEND_GUARANTEED-equivalent/reliable on ENet's single channel 0, which per its own protocol
// guarantees in-order delivery for that combination - see plan.md Phase 10's corrected note)
// arrive in the same order they were sent.
void Test_EnetTransport_ReliableBatchSend_AllPacketsArriveInOrder() {
    EnetDirectPlayTransport host;
    CHECK(host.Listen(52105));

    EnetDirectPlayTransport client;
    CHECK(client.Connect("127.0.0.1", 52105));

    const bool connected = PollUntil(host, client, [&]() {
        return host.HasPendingConnection() && client.IsConnectedToHost();
    });
    CHECK(connected);

    const DPID clientId = 9;
    CHECK(host.AssignPendingConnection(clientId));

    constexpr int kPacketCount = 50;
    for (int i = 0; i < kPacketCount; ++i) {
        const auto payload = static_cast<unsigned char>(i);
        CHECK(host.Send(clientId, &payload, sizeof(payload), /*reliable=*/true));
    }

    int nextExpected = 0;
    const bool allReceived = PollUntil(host, client, [&]() {
        unsigned char buf = 0xFF;
        std::size_t receivedSize = 0;
        while (client.Receive(&buf, sizeof(buf), &receivedSize)) {
            CHECK(receivedSize == sizeof(buf));
            CHECK(buf == static_cast<unsigned char>(nextExpected));
            ++nextExpected;
        }
        return nextExpected == kPacketCount;
    });
    CHECK(allReceived);
    CHECK(nextExpected == kPacketCount);

    // Nothing left over - proves no duplication, not just "at least kPacketCount arrived."
    unsigned char drainBuf = 0;
    std::size_t drainSize = 0;
    host.Service();
    client.Service();
    CHECK(!client.Receive(&drainBuf, sizeof(drainBuf), &drainSize));

    host.Shutdown();
    client.Shutdown();
}

// TASK-24H-0108: Shutdown() must cleanly close a connection on both sides - the client
// observably transitions out of "connected" once the host-side graceful disconnect completes.
void Test_EnetTransport_Shutdown_ClosesConnectionCleanly() {
    EnetDirectPlayTransport host;
    CHECK(host.Listen(52104));

    EnetDirectPlayTransport client;
    CHECK(client.Connect("127.0.0.1", 52104));

    const bool connected = PollUntil(host, client, [&]() {
        return host.HasPendingConnection() && client.IsConnectedToHost();
    });
    CHECK(connected);
    CHECK(host.AssignPendingConnection(0));

    host.Shutdown();
    CHECK(!host.HasHost());

    // The client must observe the disconnect once its own Service() drains the resulting
    // ENET_EVENT_TYPE_DISCONNECT event.
    bool disconnected = false;
    for (int elapsed = 0; elapsed <= kEnetPollTimeoutMs; elapsed += kEnetPollIntervalMs) {
        client.Service();
        if (!client.IsConnectedToHost()) { disconnected = true; break; }
        SDL_Delay(kEnetPollIntervalMs);
    }
    CHECK(disconnected);

    client.Shutdown();
}

// 24-Hour Stabilization Backlog TASK-24H-0149 (plan.md), docs/directplay-design.md Decision 22:
// with FREE_DIRECT_ENET_HOST_ADDRESS unset, Open(..., DPOPEN_JOIN) must fail fast with
// DPERR_NOSESSIONS - no real network attempt, no crash - since ParseEnetHostAddressEnvVar()
// (DirectPlay.cpp) rejects an unset/empty env var before ever calling Connect().
void Test_OpenAsJoinOverEnet_WithNoHostAddressEnvVar_ReturnsNoSessions() {
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_ENET_HOST_ADDRESS");

    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_JOIN) == DPERR_NOSESSIONS);

    dp2->Release();
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0149, Decision 22: a real end-to-end join over ENet
// using the env var - a host Open()s (DPOPEN_CREATE, listening on the fixed ENet port per
// Decision 5), a client sets FREE_DIRECT_ENET_HOST_ADDRESS=127.0.0.1 (no explicit port, so the
// same default port is implied) and Open()s (DPOPEN_JOIN). Polls both sides' real public
// Receive() (which internally drives Service()/the assignment loop/the join handshake, Decisions
// 6/7/16) until the client's assigned DPID is observably adopted - proven the same way
// directplay_tests.cpp's own loopback join test proves it: a self-send with the deterministically
// -expected assigned id (1, the first remote connection after the host's own DPID-0 local player,
// Decision 3) succeeds only once real adoption has happened.
void Test_OpenAsJoinOverEnet_WithHostAddressEnvVar_JoinsSuccessfully() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);
    DPID hostPlayer = 0;
    CHECK(hostDp2->CreatePlayer(&hostPlayer, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_ENET_HOST_ADDRESS",
                                "127.0.0.1", /*overwrite=*/true);

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    const DPID expectedClientPlayer = 1;
    const char probe[] = "p";
    bool adopted = false;
    for (int elapsed = 0; elapsed <= kEnetPollTimeoutMs; elapsed += kEnetPollIntervalMs) {
        DPID from = 0, to = 0;
        char buf[8];
        DWORD size = sizeof(buf);
        hostDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size);
        size = sizeof(buf);
        clientDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size);
        if (clientDp2->Send(expectedClientPlayer, expectedClientPlayer, DPSEND_GUARANTEED,
                             (LPVOID)probe, sizeof(probe)) == DP_OK) {
            adopted = true;
            break;
        }
        SDL_Delay(kEnetPollIntervalMs);
    }
    CHECK(adopted);

    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FREE_DIRECT_ENET_HOST_ADDRESS");
    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

namespace {
// DPSESSIONDESC2::lpszSessionNameA is only valid for the duration of the callback invocation
// (matches Decision 18's own documented contract for the loopback path) - a caller that wants to
// keep it must copy the string content itself, during the callback, into an owned std::string
// (matching tests/directplay_tests.cpp's own EnumSessionsResult::lastSessionName pattern) rather
// than retaining the raw DPSESSIONDESC2 (and its now-dangling-after-return pointer) as-is.
struct DiscoveredEnumResult {
    DPSESSIONDESC2 desc{};
    std::string sessionName;
};

BOOL CollectingEnumSessionsCallback(LPDPSESSIONDESC2 lpThisSD, LPDWORD, DWORD, LPVOID lpContext) {
    auto* results = static_cast<std::vector<DiscoveredEnumResult>*>(lpContext);
    DiscoveredEnumResult result;
    result.desc = *lpThisSD;
    result.sessionName = lpThisSD->lpszSessionNameA ? lpThisSD->lpszSessionNameA : "";
    results->push_back(std::move(result));
    return TRUE;
}

// EnumSessions() (ENet-enabled builds) blocks internally for up to dwTimeout while collecting
// real UDP DiscoveryResponse replies (docs/directplay-design.md Decision 23) - but nothing
// generates a reply unless something calls the *host's* Receive() during that exact window
// (Decision 6's polling model). Since a real application would run the host's own event loop on
// its own thread/process independently of whatever process is enumerating, this helper spins up
// a background thread doing exactly that for the duration of the enclosing test, mirroring that
// real-world shape rather than trying to interleave single-threaded polling around a call this
// project's public API does not expose as non-blocking.
class BackgroundHostServicer {
public:
    explicit BackgroundHostServicer(LPDIRECTPLAY2A hostDp2)
        : thread_([hostDp2, stop = &stop_]() {
              while (!stop->load()) {
                  DPID from = 0, to = 0;
                  char buf[8];
                  DWORD size = sizeof(buf);
                  hostDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size);
                  SDL_Delay(5);
              }
          }) {}
    ~BackgroundHostServicer() {
        stop_.store(true);
        thread_.join();
    }
    BackgroundHostServicer(const BackgroundHostServicer&) = delete;
    BackgroundHostServicer& operator=(const BackgroundHostServicer&) = delete;

private:
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
} // namespace

// 24-Hour Stabilization Backlog TASK-24H-0150 (plan.md), docs/directplay-design.md Decision 23:
// a real end-to-end LAN discovery round-trip through the public API - a host Open()s
// (DPOPEN_CREATE, which also starts DirectPlayDiscoveryService::StartListening() per this
// decision), a separate object EnumSessions()s with a real dwTimeout, and the host's real
// DiscoveryResponse reply must be found with accurate fields. This exercises
// DirectPlayDiscoveryService::RespondToPendingRequests() and ::BroadcastAndCollect() together -
// there is no separate whitebox test of the class, since the public-API round-trip already
// proves the full mechanism end-to-end more realistically than a whitebox test could.
void Test_EnumSessionsOverEnet_FindsRealHostedSession() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);

    GUID appGuid{};
    appGuid.Data1 = 0x1150; // arbitrary, distinguishable application id for this test

    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.guidApplication = appGuid;
    hostDesc.dwMaxPlayers = 4;
    char sessionName[] = "discoverable-session";
    hostDesc.lpszSessionNameA = sessionName;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    {
        BackgroundHostServicer servicer(hostDp2);

        LPDIRECTPLAY enumDp = nullptr;
        CHECK(DirectPlayCreate(nullptr, &enumDp, nullptr) == DP_OK);
        LPDIRECTPLAY2A enumDp2 = nullptr;
        CHECK(enumDp->QueryInterface(IID_IDirectPlay2A, (void**)&enumDp2) == DP_OK);

        std::vector<DiscoveredEnumResult> found;
        DPSESSIONDESC2 filter{};
        std::memset(&filter, 0, sizeof(filter));
        filter.dwSize = sizeof(DPSESSIONDESC2);
        filter.guidApplication = appGuid;
        CHECK(enumDp2->EnumSessions(&filter, /*dwTimeout=*/1000, CollectingEnumSessionsCallback,
                                     &found, 0) == DP_OK);

        CHECK(found.size() == 1);
        if (!found.empty()) {
            CHECK(std::memcmp(&found[0].desc.guidApplication, &appGuid, sizeof(GUID)) == 0);
            CHECK(found[0].desc.dwMaxPlayers == 4);
            CHECK(found[0].sessionName == "discoverable-session");
        }

        enumDp2->Release();
        enumDp->Release();
    }

    hostDp2->Release();
    hostDp->Release();
}

// Same rationale as TASK-24H-0018/Decision 18's loopback filter test - a non-matching
// guidApplication filter must exclude a real, live, discoverable session, not just an absent one.
void Test_EnumSessionsOverEnet_FiltersByApplicationGuid() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);

    GUID hostedAppGuid{};
    hostedAppGuid.Data1 = 0x1151;
    GUID differentAppGuid{};
    differentAppGuid.Data1 = 0x1152;

    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.guidApplication = hostedAppGuid;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    {
        BackgroundHostServicer servicer(hostDp2);

        LPDIRECTPLAY enumDp = nullptr;
        CHECK(DirectPlayCreate(nullptr, &enumDp, nullptr) == DP_OK);
        LPDIRECTPLAY2A enumDp2 = nullptr;
        CHECK(enumDp->QueryInterface(IID_IDirectPlay2A, (void**)&enumDp2) == DP_OK);

        std::vector<DiscoveredEnumResult> found;
        DPSESSIONDESC2 filter{};
        std::memset(&filter, 0, sizeof(filter));
        filter.dwSize = sizeof(DPSESSIONDESC2);
        filter.guidApplication = differentAppGuid;
        CHECK(enumDp2->EnumSessions(&filter, /*dwTimeout=*/300, CollectingEnumSessionsCallback,
                                     &found, 0) == DP_OK);

        CHECK(found.empty());

        enumDp2->Release();
        enumDp->Release();
    }

    hostDp2->Release();
    hostDp->Release();
}

int main() {
    Test_EnetTransport_ListenAndConnect_EstablishesConnection();
    Test_EnetTransport_ReliableSend_HostToClient_DeliversPayload();
    Test_EnetTransport_UnreliableSend_ClientToHost_DeliversPayload();
    Test_EnetTransport_ReliableBatchSend_AllPacketsArriveInOrder();
    Test_EnetTransport_Shutdown_ClosesConnectionCleanly();
    Test_OpenAsJoinOverEnet_WithNoHostAddressEnvVar_ReturnsNoSessions();
    Test_OpenAsJoinOverEnet_WithHostAddressEnvVar_JoinsSuccessfully();
    Test_EnumSessionsOverEnet_FindsRealHostedSession();
    Test_EnumSessionsOverEnet_FiltersByApplicationGuid();

    if (g_failures == 0) {
        std::printf("OK: all ENet DirectPlay transport tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
