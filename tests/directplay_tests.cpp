/**
 * @file directplay_tests.cpp
 * @brief Standalone DirectPlay unit tests (`plan.md` Phase 3 and Phase 4).
 *
 * Wired into CMake/CTest since the 24-Hour Stabilization Backlog's TASK-24H-0001..0004
 * (`tests/CMakeLists.txt`, label `directplay`). For fast iteration without CMake/SDL, it can
 * still be built and run directly from the repository root:
 *
 *   g++ -std=c++20 -Wall -Wextra \
 *       -I include -I ../free-api/include -I ../free-api/include_non_windows \
 *       -I src/directplay \
 *       src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
 *       tests/directplay_tests.cpp \
 *       -o directplay_tests
 *   ./directplay_tests
 *
 * Prints "OK: ..." and exits 0 on success; prints one line per failed
 * check and exits nonzero otherwise.
 *
 * **Important**: this suite assumes the default (`FREE_DIRECT_ENABLE_ENET=OFF`) build, where
 * `Open()` always uses `LoopbackDirectPlayTransport`'s synchronous semantics (e.g. an immediate
 * `DPERR_NOSESSIONS` on a failed join). Building `free-direct` itself with
 * `-DFREE_DIRECT_ENABLE_ENET=ON` switches `Open()` to `EnetDirectPlayTransport` instead
 * (Decision 4: build-time-only backend selection, no runtime switch) - confirmed this session
 * that running this exact suite against such a build fails 29/61 checks, because
 * `EnetDirectPlayTransport`'s real, asynchronous network model does not honor the same timing/
 * ordering guarantees loopback's synchronous registry lookups do. This is expected and correct,
 * not a regression to chase: this file is deliberately scoped to the loopback backend
 * (CLAUDE.md Testing Policy), and `tests/enet_directplay_tests.cpp` is where ENet-specific
 * transport behavior is tested instead, gated behind the same `FREE_DIRECT_ENABLE_ENET` option.
 * A CI setup that enables both `FREE_DIRECT_BUILD_TESTS` and `FREE_DIRECT_ENABLE_ENET` together
 * should expect this file's CTest entry to fail for this structural reason, not treat it as a
 * signal of a new bug.
 */
#include "dplay.h"
#include "DirectPlayMessageQueue.hpp"
#include "DirectPlayWireProtocol.hpp"
#include "LoopbackDirectPlayTransport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

// plan.md Phase 3: "Add a unit test for Receive on an empty queue, asserting DPERR_NOMESSAGES."
//
// Goes through the real, public IDirectPlay2A interface end-to-end - this path is fully
// reachable today without needing to inject a message into a live object.
void Test_ReceiveOnEmptyQueue_ReturnsNoMessages() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    DPID from = 0, to = 0;
    char buf[16];
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DPERR_NOMESSAGES);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 3: "Add a unit test for Receive with a too-small caller-provided buffer,
// asserting the queued packet is preserved (not dequeued) and a meaningful error is returned."
//
// Exercises DirectPlayMessageQueue::TryReceive() directly - the exact method
// DirectPlay2AImpl::Receive() (src/directplay/DirectPlay.cpp) delegates to after its own
// session-open check. Written before Phase 4 added a real (self-send-only) delivery path, and
// kept this way since it tests TryReceive() itself in isolation rather than the full Send/Open
// lifecycle the Phase 4 loopback tests below exercise. This is not a re-implementation of
// Receive()'s logic: it is the same method.
void Test_ReceiveWithTooSmallBuffer_PreservesPacket() {
    using namespace free_direct_directplay;

    DirectPlayMessageQueue queue;
    DirectPlayMessagePacket packet;
    packet.idFrom = 7;
    packet.idTo = 8;
    packet.payload = {1, 2, 3, 4};
    CHECK(queue.Enqueue(packet));

    char smallBuf[2];
    DWORD size = sizeof(smallBuf);
    HRESULT hr = queue.TryReceive(nullptr, nullptr, smallBuf, &size);
    CHECK(hr == DPERR_INVALIDPARAMS);
    CHECK(size == 4);          // required size reported back
    CHECK(!queue.IsEmpty());   // packet must not have been dequeued
}

// plan.md Phase 3: "Add a unit test for Receive performing a successful copy, asserting payload
// bytes, sender DPID, and recipient DPID all match what was queued."
//
// Same rationale as the too-small-buffer test above: exercises the real
// DirectPlayMessageQueue::TryReceive() directly.
void Test_ReceiveSuccessfulCopy_MatchesQueuedPacket() {
    using namespace free_direct_directplay;

    DirectPlayMessageQueue queue;
    DirectPlayMessagePacket packet;
    packet.idFrom = 11;
    packet.idTo = 22;
    packet.payload = {9, 8, 7};
    CHECK(queue.Enqueue(packet));

    char buf[8] = {};
    DPID from = 0, to = 0;
    DWORD size = sizeof(buf);
    HRESULT hr = queue.TryReceive(&from, &to, buf, &size);
    CHECK(hr == DP_OK);
    CHECK(size == 3);
    CHECK(from == 11);
    CHECK(to == 22);
    CHECK(buf[0] == 9 && buf[1] == 8 && buf[2] == 7);
    CHECK(queue.IsEmpty()); // a successful receive must dequeue
}

// Shared helper for the Phase 4 loopback tests below: opens a fresh DirectPlay2AImpl session
// (DPOPEN_CREATE assigns it a LoopbackDirectPlayTransport - see DirectPlay.cpp's Open()).
// Caller owns the returned pointers and must Release() both.
void OpenLoopbackSession(LPDIRECTPLAY* outDp, LPDIRECTPLAY2A* outDp2) {
    CHECK(DirectPlayCreate(nullptr, outDp, nullptr) == DP_OK);
    CHECK((*outDp)->QueryInterface(IID_IDirectPlay2A, (void**)outDp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.dwMaxPlayers = 4;
    CHECK((*outDp2)->Open(&desc, DPOPEN_CREATE) == DP_OK);
}

// plan.md Phase 6: "Create a session instance GUID (guidInstance) when hosting, if the caller
// did not already supply one." Asserts an all-zero caller-supplied guidInstance is replaced
// with a real, non-zero, written-back value, and that two separate hosted sessions get
// different generated GUIDs (not some fixed/degenerate placeholder).
//
// The two hosted sessions are opened and closed sequentially, not held open
// simultaneously: docs/directplay-design.md Decisions 11/12 wired Open(DPOPEN_CREATE) to
// call Listen() on a single fixed loopback port, so only one loopback-hosted session can
// exist per process at a time (mirroring the same constraint Decision 5 already accepted
// for the real ENet port) - this doesn't weaken what the test actually verifies (GUID
// uniqueness across generations, not simultaneous liveness).
void Test_OpenAsHostWithZeroGuidInstance_GeneratesNonZeroGuid() {
    GUID zero{};

    LPDIRECTPLAY dpA = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dpA, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2A = nullptr;
    CHECK(dpA->QueryInterface(IID_IDirectPlay2A, (void**)&dp2A) == DP_OK);
    DPSESSIONDESC2 descA{};
    std::memset(&descA, 0, sizeof(descA));
    descA.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2A->Open(&descA, DPOPEN_CREATE) == DP_OK);
    CHECK(std::memcmp(&descA.guidInstance, &zero, sizeof(GUID)) != 0);
    const GUID guidA = descA.guidInstance;
    dp2A->Release();
    dpA->Release();

    LPDIRECTPLAY dpB = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dpB, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2B = nullptr;
    CHECK(dpB->QueryInterface(IID_IDirectPlay2A, (void**)&dp2B) == DP_OK);
    DPSESSIONDESC2 descB{};
    std::memset(&descB, 0, sizeof(descB));
    descB.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2B->Open(&descB, DPOPEN_CREATE) == DP_OK);
    CHECK(std::memcmp(&descB.guidInstance, &zero, sizeof(GUID)) != 0);

    CHECK(std::memcmp(&guidA, &descB.guidInstance, sizeof(GUID)) != 0);

    dp2B->Release();
    dpB->Release();
}

// plan.md Phase 6: same task as above - a caller-supplied, already-non-zero guidInstance
// must be preserved as-is, not silently overwritten.
void Test_OpenAsHostWithNonZeroGuidInstance_PreservesCallerValue() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.guidInstance.Data1 = 0x12345678;
    const GUID original = desc.guidInstance;

    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);
    CHECK(std::memcmp(&desc.guidInstance, &original, sizeof(GUID)) == 0);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 6: "Add a test for host session creation over loopback, asserting
// Open(..., DPOPEN_CREATE) returns DP_OK and the session reports itself as host."
//
// IDirectPlay2A exposes no public way to query "is this session the host" - there is no
// getter, and DPOPEN_JOIN/DPOPEN_OPENSESSION currently also return DP_OK (Phase 7's real
// Connect() doesn't exist yet, so nothing observably distinguishes host from join today).
// Adding such a query would be new public API surface with no free-eggbert/planetblupi call
// site behind it - CLAUDE.md requires asking the user before adding that speculatively. This
// test therefore only asserts the half that genuinely is observable through the real public
// interface: Open(..., DPOPEN_CREATE) succeeds over the default (loopback) transport.
void Test_OpenAsHostOverLoopback_ReturnsOk() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 7: "Add a test for a failed join (no host present), asserting
// DPERR_NOSESSIONS."
//
// No loopback host is ever started in this test, so Connect() over the well-known
// loopback port fails immediately and deterministically - no real network, no timeout
// needed for this backend (docs/directplay-design.md Decision 11).
void Test_OpenAsJoinWithNoHostPresent_ReturnsNoSessions() {
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

// plan.md Phase 7: "Add a test for a successful join using a local host/client pair over
// loopback..." - partially covered here, honestly: docs/directplay-design.md Decision 12
// made it safe for Open(DPOPEN_CREATE) to call Listen() (self-send no longer routes
// through the transport), so a joining Open(DPOPEN_JOIN) can now genuinely find and
// connect to a real hosted loopback session, asserted here via DP_OK. This does **not**
// yet cover the full Phase 7 acceptance criterion ("both peers agree on the assigned
// DPIDs and session descriptor") - that needs the join-request/join-accepted wire
// handshake, still blocked on per-DPID-addressed Send() (Phase 10, see NEXT.md).
void Test_OpenAsJoinWithHostPresent_Succeeds() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 7: "Add a test for max-players rejection: a third loopback client joining a
// two-player-max session receives a rejected outcome (a DPERR_* code, not DP_OK)."
//
// Reachable end-to-end now thanks to docs/directplay-design.md Decision 13
// (IsConnectedToHost(), wired into DirectPlay2AImpl::Receive()): the host's own Receive()
// call is what actually runs the DPID-assignment/rejection loop (Decision 6's polling
// model - nothing happens until something calls Receive()), so the host must poll once
// before the rejection is real. The rejected third client only observes DPERR_NOCONNECTION
// on its own next Receive() call - Open() itself already returned DP_OK, since the
// connection was merely pending, not yet rejected, at Open() time.
void Test_OpenAsJoinOverMaxPlayers_ThirdClientReceivesNoConnection() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.dwMaxPlayers = 2;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    auto openClient = [](LPDIRECTPLAY* outDp, LPDIRECTPLAY2A* outDp2) {
        CHECK(DirectPlayCreate(nullptr, outDp, nullptr) == DP_OK);
        CHECK((*outDp)->QueryInterface(IID_IDirectPlay2A, (void**)outDp2) == DP_OK);
        DPSESSIONDESC2 desc{};
        std::memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(DPSESSIONDESC2);
        CHECK((*outDp2)->Open(&desc, DPOPEN_JOIN) == DP_OK);
    };

    LPDIRECTPLAY dpA = nullptr, dpB = nullptr, dpC = nullptr;
    LPDIRECTPLAY2A dp2A = nullptr, dp2B = nullptr, dp2C = nullptr;
    openClient(&dpA, &dp2A);
    openClient(&dpB, &dp2B);
    openClient(&dpC, &dp2C);

    // Drives the host's assignment/rejection loop for real - see comment above.
    DPID from = 0, to = 0;
    char buf[8];
    DWORD size = sizeof(buf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DPERR_NOMESSAGES);

    size = sizeof(buf);
    CHECK(dp2C->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DPERR_NOCONNECTION);

    dp2C->Release();
    dpC->Release();
    dp2B->Release();
    dpB->Release();
    dp2A->Release();
    dpA->Release();
    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 10 (docs/directplay-design.md Decision 15): "Implement Send from a local
// player to a specific remote player, routed through the configured transport." Host role
// only, over loopback - the joining role cannot yet address any specific remote DPID (see
// Test_JoiningRoleSendToNonSelf_ReturnsInvalidPlayer below).
//
// nextPlayerId allocation is deterministic (Decision 3): the host's own CreatePlayer() call
// consumes DPID 0, so the one connecting client - the next thing to consume an ID, via the
// host's own Receive()-driven assignment loop (Decision 7) - is assigned DPID 1.
void Test_SendToSpecificRemotePlayer_HostDeliversToAssignedClient() {
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

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    // Drives the host's pending-connection assignment loop for real (Decision 6/7).
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    const DPID assignedClientId = 1;

    const char msg[] = "hello-client";
    CHECK(hostDp2->Send(hostPlayer, assignedClientId, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DP_OK);

    char buf[32] = {};
    DWORD size = sizeof(buf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == sizeof(msg));
    CHECK(from == hostPlayer);
    CHECK(to == assignedClientId);
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

void Test_SendToUnknownRemotePlayer_ReturnsInvalidPlayer() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "x";
    CHECK(dp2->Send(player, 999, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DPERR_INVALIDPLAYER);

    dp2->Release();
    dp->Release();
}

void Test_SendFromUnknownLocalPlayer_ReturnsInvalidPlayer() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    const char msg[] = "x";
    CHECK(dp2->Send(999, 1, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DPERR_INVALIDPLAYER);

    dp2->Release();
    dp->Release();
}

// The joining role has no way to learn any remote DPID (including the host's own) before the
// join-accepted handshake exists (still blocked, see NEXT.md) - a non-self Send() is always
// DPERR_INVALIDPLAYER for it today, an honest "not supported yet" rather than the old silent
// no-op.
void Test_JoiningRoleSendToNonSelf_ReturnsInvalidPlayer() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    DPID clientPlayer = 0;
    CHECK(clientDp2->CreatePlayer(&clientPlayer, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "x";
    CHECK(clientDp2->Send(clientPlayer, clientPlayer + 1, DPSEND_GUARANTEED, (LPVOID)msg,
                           sizeof(msg)) == DPERR_INVALIDPLAYER);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

void Test_SendOversizedPayloadToRemotePlayer_ReturnsSendTooBig() {
    using namespace free_direct_directplay;

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

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    const DPID assignedClientId = 1;

    std::vector<char> oversized(DirectPlayMessageQueue::kMaxPayloadBytes + 1, 'x');
    CHECK(hostDp2->Send(hostPlayer, assignedClientId, DPSEND_GUARANTEED, oversized.data(),
                         static_cast<DWORD>(oversized.size())) == DPERR_SENDTOOBIG);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 7 (docs/directplay-design.md Decision 16): the join-request/join-accepted
// handshake. The client's Open(DPOPEN_JOIN) already sent a join-request (fire-and-forget);
// the host's own Receive() call assigns a DPID (as before) and now also sends a real
// join-accepted packet back; the client's own Receive() call drains and adopts it. Proven via
// the client's own self-send idFrom validation (Decision 16 also added this check): only a
// truly-adopted DPID passes it, which the test could not observe any other way (no public
// getter exists for a session's own DPID).
void Test_JoinHandshake_ClientAdoptsHostAssignedDpid() {
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

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    // Host processes the join-request (dropped, unused) and assigns + sends join-accepted.
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    const DPID assignedClientId = 1;

    // Client processes the join-accepted packet (adopts assignedClientId as its own).
    pollSize = sizeof(pollBuf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    // Proof of adoption: self-send with the adopted id succeeds; an unadopted id doesn't.
    const char selfMsg[] = "self";
    CHECK(clientDp2->Send(assignedClientId, assignedClientId, DPSEND_GUARANTEED, (LPVOID)selfMsg,
                           sizeof(selfMsg)) == DP_OK);
    CHECK(clientDp2->Send(999, 999, DPSEND_GUARANTEED, (LPVOID)selfMsg, sizeof(selfMsg)) ==
          DPERR_INVALIDPLAYER);

    // Drain the self-sent message (queued ahead of anything else, FIFO) before checking the
    // host's separately-sent message below.
    char selfBuf[32] = {};
    DWORD selfSize = sizeof(selfBuf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, selfBuf, &selfSize) == DP_OK);
    CHECK(selfSize == sizeof(selfMsg));
    CHECK(std::memcmp(selfBuf, selfMsg, sizeof(selfMsg)) == 0);

    // The host can still reach the client by the same id it assigned.
    const char msg[] = "hello";
    CHECK(hostDp2->Send(hostPlayer, assignedClientId, DPSEND_GUARANTEED, (LPVOID)msg,
                         sizeof(msg)) == DP_OK);
    char buf[32] = {};
    DWORD size = sizeof(buf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == sizeof(msg));
    CHECK(from == hostPlayer);
    CHECK(to == assignedClientId);
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 6: "Add a test for invalid host parameters (e.g. dwMaxPlayers == 0, malformed
// DPSESSIONDESC2.dwSize), asserting a meaningful DPERR_* rather than DP_OK."
//
// dwMaxPlayers == 0 is deliberately NOT exercised here as an error case: it is a real,
// intentional "no limit" value (docs/directplay-design.md Decision 9), and Open() never
// validates it as invalid - asserting a DPERR_* for it would pin down wrong behavior, not
// verify correct behavior. The one real gap this test covers is dwSize itself: Open()
// (DirectPlay.cpp) already checks lpSessionDesc->dwSize != sizeof(DPSESSIONDESC2), but no
// committed test exercised that check through the public interface before this one.
void Test_OpenWithMalformedDwSize_ReturnsInvalidParams() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2) - 1; // malformed: real struct size is required exactly
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DPERR_INVALIDPARAMS);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 4/9: originally "asserting a non-zero DPID is returned and is unique" -
// updated per docs/directplay-design.md Decision 3, now implemented: the host's first
// local player gets DPID 0 (not an error, the correct, expected value, breaking with
// real DirectPlay's DPID_SYSMSG/DPID_ALLPLAYERS reservation to match free-eggbert's own
// comparison pattern), so this asserts the real allocation sequence (0, 1, ...) and
// uniqueness, not "non-zero".
void Test_LoopbackCreatePlayer_ReturnsUniqueSequentialDpidsStartingAtZero() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    DPID p1 = 0, p2 = 0;
    CHECK(dp2->CreatePlayer(&p1, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(p1 == 0); // Decision 3: the host's first local player gets DPID 0
    CHECK(dp2->CreatePlayer(&p2, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(p2 == 1);
    CHECK(p1 != p2);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 9: "Validate player count against dwMaxPlayers before allocating a new DPID
// in CreatePlayer, returning DPERR_CANTCREATEPLAYER when the session is full."
//
// dwMaxPlayers bounds the session's *total* player count, not just remote ones - a local
// CreatePlayer() call counts against the same cap Receive()'s remote-assignment loop already
// enforces (docs/directplay-design.md Decision 9).
void Test_CreatePlayerOverMaxPlayers_ReturnsCantCreatePlayer() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.dwMaxPlayers = 1;
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    DPID p1 = 0, p2 = 0;
    CHECK(dp2->CreatePlayer(&p1, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->CreatePlayer(&p2, nullptr, nullptr, nullptr, 0, 0) == DPERR_CANTCREATEPLAYER);

    dp2->Release();
    dp->Release();
}

// dwMaxPlayers == 0 is the real, intentional "no limit" case (Decision 9) - confirmed here
// through CreatePlayer() specifically, not just Receive()'s remote-assignment loop. Uses its
// own Open() call (rather than the OpenLoopbackSession helper, whose desc.dwMaxPlayers is 4,
// a real limit that wouldn't prove "no limit" for the handful of players created here).
void Test_CreatePlayerWithNoMaxPlayersLimit_NeverRejects() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2->Open(&desc, DPOPEN_CREATE) == DP_OK);

    DPID p1 = 0, p2 = 0, p3 = 0;
    CHECK(dp2->CreatePlayer(&p1, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->CreatePlayer(&p2, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->CreatePlayer(&p3, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 9: "Add a test asserting player removal (via Close or disconnect) updates
// dwCurrentPlayers..." - dwCurrentPlayers itself has no public getter (same observability gap
// as player names, docs/directplay-design.md Decision 17), so this proves the decrement
// indirectly through Decision 17's own dwMaxPlayers cap check on CreatePlayer(): a session at
// capacity rejects a new local player; once the one remote player that filled that capacity
// disconnects and the host's Receive() processes it (Decision 8), the same CreatePlayer() call
// succeeds - which could only happen if dwCurrentPlayers genuinely went back down.
// "...and removes the player from future EnumSessions/roster queries" is not covered - no
// roster-query API exists in this narrow IDirectPlay2A subset, and EnumSessions() is Phase 8,
// not started.
void Test_RemotePlayerDisconnect_DecrementsCurrentPlayers() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.dwMaxPlayers = 1;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    // Assigns the client, filling the dwMaxPlayers = 1 cap.
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    DPID hostPlayer = 0;
    CHECK(hostDp2->CreatePlayer(&hostPlayer, nullptr, nullptr, nullptr, 0, 0) ==
          DPERR_CANTCREATEPLAYER);

    // The client disconnects (Release() tears down its transport, notifying the host).
    clientDp2->Release();
    clientDp->Release();

    pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    // dwCurrentPlayers is genuinely back down - the same call that failed above now succeeds.
    CHECK(hostDp2->CreatePlayer(&hostPlayer, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    hostDp2->Release();
    hostDp->Release();
}

namespace {
struct EnumSessionsResult {
    int callCount = 0;
    DPSESSIONDESC2 lastDesc{};
    std::string lastSessionName;
};

BOOL CountingEnumSessionsCallback(LPDPSESSIONDESC2 desc, LPDWORD, DWORD, LPVOID lpContext) {
    auto* result = static_cast<EnumSessionsResult*>(lpContext);
    ++result->callCount;
    result->lastDesc = *desc;
    result->lastSessionName = desc->lpszSessionNameA ? desc->lpszSessionNameA : "";
    return TRUE;
}
} // namespace

// plan.md Phase 8: "Add a test for EnumSessions finding zero sessions."
void Test_EnumSessions_FindsZeroSessions() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2 = nullptr;
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, (void**)&dp2) == DP_OK);

    EnumSessionsResult result;
    CHECK(dp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &result, 0) == DP_OK);
    CHECK(result.callCount == 0);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 8 (docs/directplay-design.md Decision 18): "Add a test for EnumSessions finding
// exactly one local (loopback-hosted) session, asserting the exact DPSESSIONDESC2 fields the
// callback received."
//
// Note: plan.md's third Phase 8 test ("a callback returning FALSE after the first result must
// prevent a second invocation even when two sessions exist") is not implemented - it cannot be,
// under the current design: only one loopback-hosted session can exist per process at a time
// (Decisions 11/12's fixed-port constraint), so there is no way to construct a real
// two-simultaneous-sessions scenario to exercise the stop-on-FALSE behavior against
// (24-Hour Stabilization Backlog TASK-24H-0099, plan.md: this comment is that task's required
// documented-constructibility-limitation deliverable).
void Test_EnumSessions_FindsOneHostedSession() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.guidApplication.Data1 = 0xABCD1234;
    hostDesc.dwMaxPlayers = 4;
    static char kSessionName[] = "Test Session";
    hostDesc.lpszSessionNameA = kSessionName;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    EnumSessionsResult result;
    CHECK(hostDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &result, 0) == DP_OK);
    CHECK(result.callCount == 1);
    CHECK(std::memcmp(&result.lastDesc.guidApplication, &hostDesc.guidApplication, sizeof(GUID)) == 0);
    CHECK(std::memcmp(&result.lastDesc.guidInstance, &hostDesc.guidInstance, sizeof(GUID)) == 0);
    CHECK(result.lastDesc.dwMaxPlayers == 4);
    CHECK(result.lastDesc.dwCurrentPlayers == 0);
    CHECK(result.lastSessionName == "Test Session");

    hostDp2->Release();
    hostDp->Release();
}

// Real, call-site-backed behavior (docs/directplay-callsite-audit.md: free-eggbert's
// CNetwork::EnumSessions() supplies a real guidApplication filter).
void Test_EnumSessions_FiltersByApplicationGuid() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.guidApplication.Data1 = 0x11111111;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    DPSESSIONDESC2 filterDesc{};
    std::memset(&filterDesc, 0, sizeof(filterDesc));
    filterDesc.dwSize = sizeof(DPSESSIONDESC2);
    filterDesc.guidApplication.Data1 = 0x22222222; // does not match the hosted session

    EnumSessionsResult mismatched;
    CHECK(hostDp2->EnumSessions(&filterDesc, 0, CountingEnumSessionsCallback, &mismatched, 0) ==
          DP_OK);
    CHECK(mismatched.callCount == 0);

    filterDesc.guidApplication.Data1 = 0x11111111; // matches
    EnumSessionsResult matched;
    CHECK(hostDp2->EnumSessions(&filterDesc, 0, CountingEnumSessionsCallback, &matched, 0) ==
          DP_OK);
    CHECK(matched.callCount == 1);

    hostDp2->Release();
    hostDp->Release();
}

// Real, call-site-backed behavior: free-eggbert's CNetwork::EnumSessions() passes
// DPENUMSESSIONS_AVAILABLE, which excludes sessions that are already full.
void Test_EnumSessions_AvailableFlagExcludesFullSessions() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.dwMaxPlayers = 1;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    DPID hostPlayer = 0;
    CHECK(hostDp2->CreatePlayer(&hostPlayer, nullptr, nullptr, nullptr, 0, 0) == DP_OK); // fills the cap

    EnumSessionsResult withFlag;
    CHECK(hostDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &withFlag,
                                 DPENUMSESSIONS_AVAILABLE) == DP_OK);
    CHECK(withFlag.callCount == 0);

    EnumSessionsResult withoutFlag;
    CHECK(hostDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &withoutFlag, 0) ==
          DP_OK);
    CHECK(withoutFlag.callCount == 1);

    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 6: "Add a test for closing a host session, asserting a subsequent EnumSessions
// from another loopback peer no longer finds it." - the last remaining Phase 6 task, finally
// unblocked by Phase 8's real EnumSessions().
void Test_EnumSessions_NoLongerFindsSessionAfterClose() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    EnumSessionsResult before;
    CHECK(hostDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &before, 0) == DP_OK);
    CHECK(before.callCount == 1);

    CHECK(hostDp2->Close() == DP_OK);

    EnumSessionsResult after;
    CHECK(hostDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &after, 0) == DP_OK);
    CHECK(after.callCount == 0);

    hostDp2->Release();
    hostDp->Release();
}

// plan.md Phase 4: "Add a unit test for Send to self over loopback, asserting DP_OK."
void Test_LoopbackSendToSelf_ReturnsOk() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID, distinct from
    // the DPID_ALLPLAYERS collision this test predates.
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "loopback";
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DP_OK);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 5: "Map DPSEND_GUARANTEED to ENET_PACKET_FLAG_RELIABLE in the transport
// layer." DirectPlay2AImpl::Send() now computes reliable = (dwFlags & DPSEND_GUARANTEED)
// != 0 and passes it to the transport instead of hardcoding true - this exercises that
// real code path with dwFlags = 0 (the "not guaranteed" case). LoopbackDirectPlayTransport
// itself ignores the reliable parameter (no packet-loss model), so this cannot observe a
// different *outcome* the way the EnetDirectPlayTransport smoke tests did - it pins down
// that the new flag-derived call path still succeeds end-to-end without regressing.
void Test_LoopbackSendWithoutGuaranteedFlag_StillSucceeds() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID.
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "not-guaranteed";
    const DWORD msgLen = sizeof(msg);
    CHECK(dp2->Send(player, player, 0, (LPVOID)msg, msgLen) == DP_OK);

    char buf[32] = {};
    DPID from = 0, to = 0;
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == msgLen);
    CHECK(from == player && to == player);
    CHECK(std::memcmp(buf, msg, msgLen) == 0);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 4: "Add a unit test for Receive after a loopback self-send, asserting the
// received payload matches the sent payload byte-for-byte."
void Test_LoopbackReceiveAfterSelfSend_MatchesSentPayload() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID.
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "byte-for-byte";
    const DWORD msgLen = sizeof(msg);
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, (LPVOID)msg, msgLen) == DP_OK);

    char buf[32] = {};
    DPID from = 0, to = 0;
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == msgLen);
    CHECK(from == player);
    CHECK(to == player);
    CHECK(std::memcmp(buf, msg, msgLen) == 0);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 4: "Add a unit test for Close on a loopback-backed session, asserting a
// subsequent Send/Receive returns DPERR_NOCONNECTION (Phase 11) rather than crashing or
// silently succeeding."
void Test_LoopbackClose_SendAndReceiveReportNoConnection() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->Close() == DP_OK);

    const char msg[] = "x";
    CHECK(dp2->Send(player, player, 0, (LPVOID)msg, sizeof(msg)) == DPERR_NOCONNECTION);

    char buf[8];
    DPID from = 0, to = 0;
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DPERR_NOCONNECTION);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 7 groundwork (docs/directplay-design.md Decision 10): whitebox tests against
// free_direct_directplay::LoopbackDirectPlayTransport directly (same style as the
// DirectPlayMessageQueue tests above), verifying the new multi-instance connection lifecycle
// that DirectPlay2AImpl::Open()'s DPOPEN_JOIN wiring will build on top of later. Each test uses
// a distinct literal port to avoid any inter-test ordering dependence on the shared registry.

void Test_LoopbackConnect_FindsListeningHostAndQueuesPendingConnection() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20001));

    LoopbackDirectPlayTransport client;
    CHECK(client.Connect("ignored", 20001));
    CHECK(host.HasPendingConnection());
    CHECK(client.IsConnectedToHost());
}

void Test_LoopbackConnect_WithNoListeningHost_Fails() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport client;
    CHECK(!client.Connect("ignored", 20002));
    CHECK(!client.IsConnectedToHost());
}

void Test_LoopbackListen_OnAlreadyRegisteredPort_Fails() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport hostA;
    CHECK(hostA.Listen(20003));

    LoopbackDirectPlayTransport hostB;
    CHECK(!hostB.Listen(20003));
}

void Test_LoopbackAssignPendingConnection_MovesFromPendingToConnected() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20004));
    LoopbackDirectPlayTransport client;
    CHECK(client.Connect("ignored", 20004));

    CHECK(host.AssignPendingConnection(42));
    CHECK(!host.HasPendingConnection());
    CHECK(host.ConnectedPeerCount() == 1);
}

void Test_LoopbackRejectPendingConnection_PopsPendingAndFailsWhenEmpty() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20005));
    LoopbackDirectPlayTransport client;
    CHECK(client.Connect("ignored", 20005));

    CHECK(host.RejectPendingConnection());
    CHECK(!client.IsConnectedToHost());
    CHECK(!host.RejectPendingConnection()); // nothing left pending
}

void Test_LoopbackAssignedPeerDisconnect_ReportsExactDpidOnce() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20006));
    {
        LoopbackDirectPlayTransport client;
        CHECK(client.Connect("ignored", 20006));
        CHECK(host.AssignPendingConnection(7));
    } // client destroyed here - its destructor must notify the host

    CHECK(host.HasDisconnectedPeer());
    DPID disconnected = 0;
    CHECK(host.TakeDisconnectedPeer(&disconnected));
    CHECK(disconnected == 7);
    CHECK(!host.HasDisconnectedPeer()); // reported exactly once
}

void Test_LoopbackPendingPeerDisconnect_NotReported() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20007));
    {
        LoopbackDirectPlayTransport client;
        CHECK(client.Connect("ignored", 20007));
        // Never assigned a DPID before going out of scope.
    }

    CHECK(!host.HasDisconnectedPeer());
}

void Test_LoopbackThirdClientOverTwoPlayerCap_IsRejected() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20008));

    LoopbackDirectPlayTransport clientA;
    LoopbackDirectPlayTransport clientB;
    LoopbackDirectPlayTransport clientC;
    CHECK(clientA.Connect("ignored", 20008));
    CHECK(clientB.Connect("ignored", 20008));
    CHECK(clientC.Connect("ignored", 20008));

    CHECK(host.AssignPendingConnection(0));
    CHECK(host.AssignPendingConnection(1));
    CHECK(host.ConnectedPeerCount() == 2); // the two-player cap

    CHECK(host.RejectPendingConnection()); // clientC, the third, is turned away
    CHECK(!host.HasPendingConnection());
    CHECK(!clientC.IsConnectedToHost());
    CHECK(clientA.IsConnectedToHost());
    CHECK(clientB.IsConnectedToHost());
}

void Test_LoopbackHostShutdown_ClearsPeersHostConnection() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20009));
    LoopbackDirectPlayTransport connected;
    LoopbackDirectPlayTransport pending;
    CHECK(connected.Connect("ignored", 20009));
    CHECK(host.AssignPendingConnection(0));
    CHECK(pending.Connect("ignored", 20009));

    host.Shutdown();

    CHECK(!connected.IsConnectedToHost());
    CHECK(!pending.IsConnectedToHost());
}

void Test_LoopbackShutdown_UnregistersPortForReuse() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport hostA;
    CHECK(hostA.Listen(20010));
    hostA.Shutdown();

    LoopbackDirectPlayTransport hostB;
    CHECK(hostB.Listen(20010));
}

// plan.md Phase 10 (docs/directplay-design.md Decision 14): a hosting-role instance's
// Send(targetId, ...) addresses one specific assigned peer directly, delivering into
// that peer's own inbox.
void Test_LoopbackSend_HostToAssignedClient_DeliversPayload() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20011));
    LoopbackDirectPlayTransport client;
    CHECK(client.Connect("ignored", 20011));
    CHECK(host.AssignPendingConnection(7));

    const char msg[] = "host-to-client";
    CHECK(host.Send(7, msg, sizeof(msg), true));

    char buf[32] = {};
    std::size_t outSize = 0;
    CHECK(client.Receive(buf, sizeof(buf), &outSize));
    CHECK(outSize == sizeof(msg));
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);
}

// A joining-role instance's Send() has exactly one destination - the host - so targetId
// is accepted but ignored.
void Test_LoopbackSend_ClientToHost_DeliversPayload() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20012));
    LoopbackDirectPlayTransport client;
    CHECK(client.Connect("ignored", 20012));

    const char msg[] = "client-to-host";
    CHECK(client.Send(0, msg, sizeof(msg), true)); // targetId ignored on the joining role

    char buf[32] = {};
    std::size_t outSize = 0;
    CHECK(host.Receive(buf, sizeof(buf), &outSize));
    CHECK(outSize == sizeof(msg));
    CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);
}

// Sending to a DPID that names no currently-connected (assigned) peer fails.
void Test_LoopbackSend_ToUnknownTargetId_Fails() {
    using namespace free_direct_directplay;

    LoopbackDirectPlayTransport host;
    CHECK(host.Listen(20013));
    const char msg[] = "nobody-home";
    CHECK(!host.Send(999, msg, sizeof(msg), true));
}

// plan.md Phase 5: "a unit test serializes and deserializes the internal packet header
// and asserts round-trip equality."
//
// Pure data-structure test - no ENet dependency exists yet, and this header has none.
void Test_WireHeaderRoundTrip_PreservesAllFields() {
    using namespace free_direct_directplay;

    DirectPlayWirePacketHeader header;
    header.type = DirectPlayWirePacketType::Discovery;
    header.applicationGuid = {0x11223344, 0x5566, 0x7788, {1, 2, 3, 4, 5, 6, 7, 8}};
    header.sessionGuid = {0xaabbccdd, 0xeeff, 0x0011, {9, 8, 7, 6, 5, 4, 3, 2}};
    header.idFrom = 42;
    header.idTo = 99;
    header.payloadLength = 1234;

    std::vector<std::uint8_t> wire;
    SerializeDirectPlayWireHeader(header, wire);
    CHECK(wire.size() == kDirectPlayWireHeaderSize);

    const DirectPlayWirePacketHeader roundTripped =
        DeserializeDirectPlayWireHeader(wire.data());

    CHECK(roundTripped.magic == kDirectPlayWireMagic);
    CHECK(roundTripped.version == kDirectPlayWireProtocolVersion);
    CHECK(roundTripped.type == DirectPlayWirePacketType::Discovery);
    CHECK(std::memcmp(&roundTripped.applicationGuid, &header.applicationGuid, sizeof(GUID)) == 0);
    CHECK(std::memcmp(&roundTripped.sessionGuid, &header.sessionGuid, sizeof(GUID)) == 0);
    CHECK(roundTripped.idFrom == 42);
    CHECK(roundTripped.idTo == 99);
    CHECK(roundTripped.payloadLength == 1234);
}

// plan.md Phase 5: "Add defensive packet size validation on receive: reject packets
// smaller than the fixed header size, ... both expected to fail cleanly (not crash)."
void Test_WireHeaderTryDeserialize_RejectsTruncatedBuffer() {
    using namespace free_direct_directplay;

    std::vector<std::uint8_t> wire(kDirectPlayWireHeaderSize - 1, 0);
    CHECK(!TryDeserializeDirectPlayWireHeader(wire.data(), wire.size()).has_value());

    std::vector<std::uint8_t> empty;
    CHECK(!TryDeserializeDirectPlayWireHeader(empty.data(), empty.size()).has_value());
}

// plan.md Phase 5: "... and reject a stated payload length that does not match the
// actual received byte count."
void Test_WireHeaderTryDeserialize_RejectsMismatchedPayloadLength() {
    using namespace free_direct_directplay;

    DirectPlayWirePacketHeader header;
    header.payloadLength = 5; // claims 5 payload bytes...

    std::vector<std::uint8_t> wire;
    SerializeDirectPlayWireHeader(header, wire);
    wire.resize(wire.size() + 3, 0xAB); // ...but only 3 actually follow.

    CHECK(!TryDeserializeDirectPlayWireHeader(wire.data(), wire.size()).has_value());
}

// docs/audit_dplay.md §6.4/§9 (D6), TASK-24H-0176: magic/version validation was deliberately
// deferred until a real transport existed to receive real UDP packets - EnetDirectPlayTransport
// now is that real transport, so a wrong magic or wrong version must now be rejected the same way
// as any other malformed buffer, not silently accepted and parsed as if it were a real FreeDirect
// packet.
void Test_WireHeaderTryDeserialize_RejectsWrongMagic() {
    using namespace free_direct_directplay;

    DirectPlayWirePacketHeader header; // correct version, wrong magic
    header.magic = kDirectPlayWireMagic ^ 0xFFFFFFFFu;
    header.payloadLength = 0;

    std::vector<std::uint8_t> wire;
    SerializeDirectPlayWireHeader(header, wire);

    CHECK(!TryDeserializeDirectPlayWireHeader(wire.data(), wire.size()).has_value());
}

void Test_WireHeaderTryDeserialize_RejectsWrongVersion() {
    using namespace free_direct_directplay;

    DirectPlayWirePacketHeader header; // correct magic, wrong version
    header.version = kDirectPlayWireProtocolVersion + 1;
    header.payloadLength = 0;

    std::vector<std::uint8_t> wire;
    SerializeDirectPlayWireHeader(header, wire);

    CHECK(!TryDeserializeDirectPlayWireHeader(wire.data(), wire.size()).has_value());
}

void Test_WireHeaderTryDeserialize_AcceptsConsistentBuffer() {
    using namespace free_direct_directplay;

    DirectPlayWirePacketHeader header;
    header.idFrom = 3;
    header.idTo = 4;
    header.payloadLength = 2;

    std::vector<std::uint8_t> wire;
    SerializeDirectPlayWireHeader(header, wire);
    wire.push_back(0x01);
    wire.push_back(0x02);

    const std::optional<DirectPlayWirePacketHeader> parsed =
        TryDeserializeDirectPlayWireHeader(wire.data(), wire.size());
    CHECK(parsed.has_value());
    CHECK(parsed->idFrom == 3);
    CHECK(parsed->idTo == 4);
    CHECK(parsed->payloadLength == 2);
}

// 24-Hour Stabilization Backlog TASK-24H-0148 (plan.md), docs/directplay-design.md Decisions
// 20/21. Supersedes what was TASK-24H-0092's characterization test
// (Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf) - that test locked in the *old*, since-fixed
// collision (a host's broadcast call hit the self-send branch and never reached any remote
// client) as a documented-but-not-endorsed bug. Decision 20/21 fixed exactly that: idTo == 0
// (DPID_ALLPLAYERS) is now checked before the self-send branch and delivered as a real broadcast,
// with the host relaying a non-host sender's broadcast to every other peer.
//
// free-eggbert's only reachable Send() call pattern is Send(m_dpid, 0, ...) - this test uses that
// exact call shape from the host role (whose own DPID is always 0, Decision 3) and asserts the
// new, correct behavior: the remote client receives it, and the host's own queue does not (a
// broadcast never reaches its own sender, Decision 20).
void Test_HostBroadcast_ReachesAllRemoteClientsNotSelf() {
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
    CHECK(hostPlayer == 0); // Decision 3: the host's first local player gets DPID 0

    LPDIRECTPLAY clientDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &clientDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A clientDp2 = nullptr;
    CHECK(clientDp->QueryInterface(IID_IDirectPlay2A, (void**)&clientDp2) == DP_OK);
    DPSESSIONDESC2 clientDesc{};
    std::memset(&clientDesc, 0, sizeof(clientDesc));
    clientDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(clientDp2->Open(&clientDesc, DPOPEN_JOIN) == DP_OK);

    // Drives the host's pending-connection assignment loop and the join handshake for real.
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    pollSize = sizeof(pollBuf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    // The exact call shape of free-eggbert's src/network.cpp:254 (Send(m_dpid, 0, ...)), made by
    // the host, whose own m_dpid-equivalent (hostPlayer) is also 0.
    const char msg[] = "broadcast-shaped";
    CHECK(hostDp2->Send(hostPlayer, 0, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DP_OK);

    // The connected remote client receives it - real broadcast delivery (Decision 20/21).
    char clientBuf[32] = {};
    DWORD clientBufSize = sizeof(clientBuf);
    CHECK(clientDp2->Receive(&from, &to, DPRECEIVE_ALL, clientBuf, &clientBufSize) == DP_OK);
    CHECK(clientBufSize == sizeof(msg));
    CHECK(from == hostPlayer);
    CHECK(to == 0); // DPID_ALLPLAYERS - the wire marker, not resolved to any specific address
    CHECK(std::memcmp(clientBuf, msg, sizeof(msg)) == 0);

    // The host's own queue does not get a copy - broadcast never reaches its own sender
    // (Decision 20).
    char hostBuf[32] = {};
    DWORD hostBufSize = sizeof(hostBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, hostBuf, &hostBufSize) == DPERR_NOMESSAGES);

    clientDp2->Release();
    clientDp->Release();
    hostDp2->Release();
    hostDp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0148 (plan.md), docs/directplay-design.md Decision 20:
// a host's broadcast must fan out to *every* connected remote client, not just one - proves the
// loop over remotePlayerIds in Send()'s broadcast branch actually iterates, not just addresses
// the first entry.
void Test_HostBroadcast_ReachesMultipleRemoteClients() {
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

    auto openClient = [](LPDIRECTPLAY* outDp, LPDIRECTPLAY2A* outDp2) {
        CHECK(DirectPlayCreate(nullptr, outDp, nullptr) == DP_OK);
        CHECK((*outDp)->QueryInterface(IID_IDirectPlay2A, (void**)outDp2) == DP_OK);
        DPSESSIONDESC2 desc{};
        std::memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(DPSESSIONDESC2);
        CHECK((*outDp2)->Open(&desc, DPOPEN_JOIN) == DP_OK);
    };

    LPDIRECTPLAY dpA = nullptr, dpB = nullptr;
    LPDIRECTPLAY2A dp2A = nullptr, dp2B = nullptr;
    openClient(&dpA, &dp2A);
    openClient(&dpB, &dp2B);

    // Drives the host's assignment loop for both pending connections.
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    pollSize = sizeof(pollBuf);
    CHECK(dp2A->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    pollSize = sizeof(pollBuf);
    CHECK(dp2B->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    const char msg[] = "fan-out";
    CHECK(hostDp2->Send(hostPlayer, 0, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DP_OK);

    char bufA[32] = {};
    DWORD sizeA = sizeof(bufA);
    CHECK(dp2A->Receive(&from, &to, DPRECEIVE_ALL, bufA, &sizeA) == DP_OK);
    CHECK(sizeA == sizeof(msg));
    CHECK(std::memcmp(bufA, msg, sizeof(msg)) == 0);

    char bufB[32] = {};
    DWORD sizeB = sizeof(bufB);
    CHECK(dp2B->Receive(&from, &to, DPRECEIVE_ALL, bufB, &sizeB) == DP_OK);
    CHECK(sizeB == sizeof(msg));
    CHECK(std::memcmp(bufB, msg, sizeof(msg)) == 0);

    // Host's own queue still gets nothing - broadcast never reaches its own sender.
    char hostBuf[8];
    DWORD hostBufSize = sizeof(hostBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, hostBuf, &hostBufSize) == DPERR_NOMESSAGES);

    dp2B->Release();
    dpB->Release();
    dp2A->Release();
    dpA->Release();
    hostDp2->Release();
    hostDp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0148 (plan.md), docs/directplay-design.md Decision 21:
// a non-host peer's broadcast has no direct connection to any other non-host peer - only the host
// relays it. Proves the relay reaches the *other* client and the host itself, but never echoes
// back to the original sender.
void Test_ClientBroadcast_RelayedByHostToOtherClientAndHost() {
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

    auto openClient = [](LPDIRECTPLAY* outDp, LPDIRECTPLAY2A* outDp2) {
        CHECK(DirectPlayCreate(nullptr, outDp, nullptr) == DP_OK);
        CHECK((*outDp)->QueryInterface(IID_IDirectPlay2A, (void**)outDp2) == DP_OK);
        DPSESSIONDESC2 desc{};
        std::memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(DPSESSIONDESC2);
        CHECK((*outDp2)->Open(&desc, DPOPEN_JOIN) == DP_OK);
    };

    LPDIRECTPLAY dpA = nullptr, dpB = nullptr;
    LPDIRECTPLAY2A dp2A = nullptr, dp2B = nullptr;
    openClient(&dpA, &dp2A);
    openClient(&dpB, &dp2B);

    // Drives the host's assignment loop (both clients connect and are assigned DPIDs - 1 for A,
    // 2 for B, deterministic connection order per LoopbackDirectPlayTransport's synchronous
    // registry) and each client's own join-accept processing.
    DPID from = 0, to = 0;
    char pollBuf[8];
    DWORD pollSize = sizeof(pollBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    pollSize = sizeof(pollBuf);
    CHECK(dp2A->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);
    pollSize = sizeof(pollBuf);
    CHECK(dp2B->Receive(&from, &to, DPRECEIVE_ALL, pollBuf, &pollSize) == DPERR_NOMESSAGES);

    const DPID clientAPlayer = 1;
    const char msg[] = "relay-me";
    CHECK(dp2A->Send(clientAPlayer, 0, DPSEND_GUARANTEED, (LPVOID)msg, sizeof(msg)) == DP_OK);

    // The host's own Receive() must run once to actually process/relay the incoming packet
    // (Decision 6's polling model - nothing happens until something calls Receive() on the host).
    char hostBuf[32] = {};
    DWORD hostBufSize = sizeof(hostBuf);
    CHECK(hostDp2->Receive(&from, &to, DPRECEIVE_ALL, hostBuf, &hostBufSize) == DP_OK);
    CHECK(hostBufSize == sizeof(msg));
    CHECK(from == clientAPlayer);
    CHECK(std::memcmp(hostBuf, msg, sizeof(msg)) == 0);

    // Client B receives the relayed copy.
    char bufB[32] = {};
    DWORD sizeB = sizeof(bufB);
    CHECK(dp2B->Receive(&from, &to, DPRECEIVE_ALL, bufB, &sizeB) == DP_OK);
    CHECK(sizeB == sizeof(msg));
    CHECK(from == clientAPlayer);
    CHECK(std::memcmp(bufB, msg, sizeof(msg)) == 0);

    // Client A (the original sender) never gets its own broadcast echoed back.
    char bufA[8];
    DWORD sizeA = sizeof(bufA);
    CHECK(dp2A->Receive(&from, &to, DPRECEIVE_ALL, bufA, &sizeA) == DPERR_NOMESSAGES);

    dp2B->Release();
    dpB->Release();
    dp2A->Release();
    dpA->Release();
    hostDp2->Release();
    hostDp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0080 (plan.md): Receive()'s buffer-size-query contract
// (lpData == nullptr && *lpdwDataSize == 0 reports the required size without dequeuing) is
// implemented in DirectPlayMessageQueue::TryReceive() (exercised directly by
// Test_ReceiveWithTooSmallBuffer_PreservesPacket above) but had no test going through the real
// public IDirectPlay2A::Receive() end-to-end. This one does, over a loopback self-send.
void Test_Receive_BufferSizeQuery_ReportsRequiredSizeWithoutConsuming() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID.
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    const char msg[] = "query-my-size";
    const DWORD msgLen = sizeof(msg);
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, (LPVOID)msg, msgLen) == DP_OK);

    // Buffer-size query: null data pointer, zero size.
    DPID from = 0, to = 0;
    DWORD querySize = 0;
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, nullptr, &querySize) == DP_OK);
    CHECK(querySize == msgLen);

    // The message must still be queued - a real receive right after gets the same payload.
    char buf[32] = {};
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == msgLen);
    CHECK(std::memcmp(buf, msg, msgLen) == 0);

    dp2->Release();
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0081 (plan.md): Release()'s defensive cleanup path for a
// caller that never called Close() first (DirectPlay.cpp's Release() unregisters from the
// EnumSessions registry and shuts down the transport unconditionally on last release) had no test
// proving it actually runs. Mirrors Test_LoopbackShutdown_UnregistersPortForReuse's port-reuse
// proof, but through the public IDirectPlay2A API rather than the whitebox transport.
void Test_Release_WithoutPriorClose_CleansUpTransportAndRegistry() {
    LPDIRECTPLAY hostDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &hostDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A hostDp2 = nullptr;
    CHECK(hostDp->QueryInterface(IID_IDirectPlay2A, (void**)&hostDp2) == DP_OK);
    DPSESSIONDESC2 hostDesc{};
    std::memset(&hostDesc, 0, sizeof(hostDesc));
    hostDesc.dwSize = sizeof(DPSESSIONDESC2);
    hostDesc.guidApplication.Data1 = 0xCAFEF00D;
    CHECK(hostDp2->Open(&hostDesc, DPOPEN_CREATE) == DP_OK);

    // Release() WITHOUT a prior Close() call - the defensive cleanup path under test.
    hostDp2->Release();
    hostDp->Release();

    // A fresh instance must see the registry/port as fully cleaned up: EnumSessions finds
    // nothing, and a brand-new host can bind the same (fixed) loopback port again.
    LPDIRECTPLAY checkDp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &checkDp, nullptr) == DP_OK);
    LPDIRECTPLAY2A checkDp2 = nullptr;
    CHECK(checkDp->QueryInterface(IID_IDirectPlay2A, (void**)&checkDp2) == DP_OK);

    EnumSessionsResult result;
    CHECK(checkDp2->EnumSessions(nullptr, 0, CountingEnumSessionsCallback, &result, 0) == DP_OK);
    CHECK(result.callCount == 0);

    DPSESSIONDESC2 newDesc{};
    std::memset(&newDesc, 0, sizeof(newDesc));
    newDesc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(checkDp2->Open(&newDesc, DPOPEN_CREATE) == DP_OK); // proves the port was freed

    checkDp2->Release();
    checkDp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0077 (plan.md): DirectPlayCreate's failure paths were
// previously verified only via uncommitted scratch harnesses (Phase 1/2 notes).
void Test_DirectPlayCreate_NullOutParam_ReturnsInvalidParams() {
    CHECK(DirectPlayCreate(nullptr, nullptr, nullptr) == DPERR_INVALIDPARAMS);
}

void Test_DirectPlayCreate_NonNullOuter_ReturnsNoAggregation() {
    LPDIRECTPLAY dp = nullptr;
    int dummyOuter = 0;
    CHECK(DirectPlayCreate(nullptr, &dp, reinterpret_cast<IUnknown*>(&dummyOuter)) == DPERR_NOAGGREGATION);
    CHECK(dp == nullptr); // *lplpDP is zeroed before the pUnkOuter check
}

// 24-Hour Stabilization Backlog TASK-24H-0078/0079 (plan.md): QueryInterface's rejection paths
// were previously exercised only with the correct IID_IDirectPlay2A value across all other tests.
void Test_QueryInterface_UnknownGuid_ReturnsNoInterfaceAndNullsOutParam() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);

    GUID unknownGuid{0x11111111, 0x2222, 0x3333, {0, 0, 0, 0, 0, 0, 0, 0}};
    void* out = reinterpret_cast<void*>(0x1); // sentinel - must be nulled, not left untouched
    CHECK(dp->QueryInterface(unknownGuid, &out) == E_NOINTERFACE);
    CHECK(out == nullptr);

    dp->Release();
}

void Test_QueryInterface_NullOutParam_ReturnsInvalidParams() {
    LPDIRECTPLAY dp = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp, nullptr) == DP_OK);
    CHECK(dp->QueryInterface(IID_IDirectPlay2A, nullptr) == DPERR_INVALIDPARAMS);
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0082 (plan.md): CreatePlayer validates DPNAME.dwSize
// when a non-null name is given (DirectPlay.cpp), but no test exercised the rejection path.
void Test_CreatePlayer_MalformedDpNameSize_ReturnsInvalidParams() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    DPNAME name{};
    std::memset(&name, 0, sizeof(name));
    name.dwSize = sizeof(DPNAME) - 1; // malformed
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, &name, nullptr, nullptr, 0, 0) == DPERR_INVALIDPARAMS);

    dp2->Release();
    dp->Release();
}

namespace {
struct EnumDpCallbackRecordA {
    int callCount = 0;
    GUID lastGuid{};
    bool lastGuidWasNull = true;
    std::string lastName;
};
BOOL RecordingEnumDpCallbackA(LPGUID lpguidSP, LPSTR lpSPName, DWORD, DWORD, LPVOID lpContext) {
    auto* record = static_cast<EnumDpCallbackRecordA*>(lpContext);
    ++record->callCount;
    record->lastGuidWasNull = (lpguidSP == nullptr);
    if (lpguidSP) record->lastGuid = *lpguidSP;
    record->lastName = lpSPName ? lpSPName : "";
    return TRUE;
}

struct EnumDpCallbackRecordW {
    int callCount = 0;
    GUID lastGuid{};
    bool lastGuidWasNull = true;
    bool lastNameWasNull = true;
    std::size_t lastNameLength = 0;
};
BOOL RecordingEnumDpCallbackW(LPGUID lpguidSP, LPWSTR lpSPName, DWORD, DWORD, LPVOID lpContext) {
    auto* record = static_cast<EnumDpCallbackRecordW*>(lpContext);
    ++record->callCount;
    record->lastGuidWasNull = (lpguidSP == nullptr);
    if (lpguidSP) record->lastGuid = *lpguidSP;
    record->lastNameWasNull = (lpSPName == nullptr);
    if (lpSPName) {
        std::size_t len = 0;
        while (lpSPName[len] != 0) ++len;
        record->lastNameLength = len;
    }
    return TRUE;
}
} // namespace

// 24-Hour Stabilization Backlog TASK-24H-0100 (plan.md): DirectPlayEnumerateA/W now implement
// Decision 1's already-decided shape for real (previously TASK-24H-0093's tests here documented
// the interim zero-invocation stub, per that task's own now-superseded doc comment). Asserts the
// callback fires exactly once with a non-null, non-zero (i.e. distinguishable, not a default-
// constructed empty GUID) provider GUID and a non-empty, human-readable name - matching this
// task's own acceptance criteria wording exactly.
void Test_DirectPlayEnumerateA_InvokesCallbackExactlyOnceWithValidProvider() {
    EnumDpCallbackRecordA record;
    CHECK(DirectPlayEnumerateA(RecordingEnumDpCallbackA, &record) == DP_OK);
    CHECK(record.callCount == 1);
    CHECK(!record.lastGuidWasNull);
    const GUID zeroGuid{};
    CHECK(std::memcmp(&record.lastGuid, &zeroGuid, sizeof(GUID)) != 0);
    CHECK(record.lastName == "FreeDirect");
}

void Test_DirectPlayEnumerateW_InvokesCallbackExactlyOnceWithValidProvider() {
    EnumDpCallbackRecordW record;
    CHECK(DirectPlayEnumerateW(RecordingEnumDpCallbackW, &record) == DP_OK);
    CHECK(record.callCount == 1);
    CHECK(!record.lastGuidWasNull);
    const GUID zeroGuid{};
    CHECK(std::memcmp(&record.lastGuid, &zeroGuid, sizeof(GUID)) != 0);
    CHECK(!record.lastNameWasNull);
    CHECK(record.lastNameLength == 10); // "FreeDirect"

    // Both encodings must describe the same underlying placeholder provider.
    EnumDpCallbackRecordA recordA;
    CHECK(DirectPlayEnumerateA(RecordingEnumDpCallbackA, &recordA) == DP_OK);
    CHECK(std::memcmp(&record.lastGuid, &recordA.lastGuid, sizeof(GUID)) == 0);
}

// 24-Hour Stabilization Backlog TASK-24H-0100: a null callback must not be dereferenced/called.
void Test_DirectPlayEnumerateA_NullCallback_ReturnsInvalidParams() {
    CHECK(DirectPlayEnumerateA(nullptr, nullptr) == DPERR_INVALIDPARAMS);
}

void Test_DirectPlayEnumerateW_NullCallback_ReturnsInvalidParams() {
    CHECK(DirectPlayEnumerateW(nullptr, nullptr) == DPERR_INVALIDPARAMS);
}

// 24-Hour Stabilization Backlog TASK-24H-0101 (plan.md): no test verified calling Close() twice
// in a row is safe.
void Test_Close_CalledTwice_SecondCallIsSafe() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    CHECK(dp2->Close() == DP_OK);
    CHECK(dp2->Close() == DP_OK); // second call must not crash or misbehave

    dp2->Release();
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0103 (plan.md): oversized-payload rejection was only
// covered for the remote-unicast path (Test_SendOversizedPayloadToRemotePlayer_ReturnsSendTooBig
// above) - this confirms the same limit applies to the self-send path too.
void Test_SelfSend_OversizedPayload_ReturnsSendTooBig() {
    using namespace free_direct_directplay;

    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID. (The size
    // limit is enforced identically on both paths, so this test's assertion would still have
    // passed either way - fixed anyway so a test literally named "SelfSend" cannot silently
    // become a broadcast test underneath its own name.)
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    std::vector<char> oversized(DirectPlayMessageQueue::kMaxPayloadBytes + 1, 'x');
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, oversized.data(),
                     static_cast<DWORD>(oversized.size())) == DPERR_SENDTOOBIG);

    dp2->Release();
    dp->Release();
}

// docs/audit_dplay.md §6.5 (D2), TASK-24H-0172: unlike the test above (an honestly-sized
// oversized buffer), this passes a dwDataSize that *overstates* a genuinely small real buffer -
// exactly the shape that, before this fix, would have made Send()'s self-send path read past the
// real buffer via packet.payload.assign() before Enqueue() ever got a chance to reject it.
// Return-value-only checking can't distinguish "rejected safely" from "read out of bounds, then
// rejected" - this test's real value is as a regression guard under an ASan build
// (FREE_DIRECT_ENABLE_ASAN), which would flag the out-of-bounds read directly if the size check
// were ever moved back below the .assign() call.
void Test_SelfSend_DwDataSizeOverstatesRealBuffer_ReturnsSendTooBigNoOverread() {
    using namespace free_direct_directplay;

    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);

    // The real buffer is tiny; dwDataSize claims it is far larger than both the real buffer and
    // kMaxPayloadBytes.
    char smallRealBuffer[4] = {'a', 'b', 'c', 'd'};
    const DWORD claimedSize = static_cast<DWORD>(DirectPlayMessageQueue::kMaxPayloadBytes) + 1000;
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, smallRealBuffer, claimedSize) == DPERR_SENDTOOBIG);

    dp2->Release();
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0106/0107 (plan.md): a real null-pointer gap found by
// sweeping IDirectPlay2A's methods - Send() never checked lpData for null before pointer
// arithmetic (`bytes + dwDataSize`) when dwDataSize > 0, undefined behavior on a null lpData with
// a nonzero size. Fixed in DirectPlay.cpp (a single check covering both the self-send and
// unicast paths); these tests prove the fix and the still-valid null+zero-size no-payload case.
void Test_Send_NullPayloadWithNonzeroSize_ReturnsInvalidParams() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, nullptr, 5) == DPERR_INVALIDPARAMS);

    dp2->Release();
    dp->Release();
}

// plan.md Phase 10: "Validate a null payload with zero length ... as an accepted no-payload
// send" - a null lpData with dwDataSize == 0 must still succeed (no bytes to read, no UB).
void Test_SelfSend_NullPayloadWithZeroSize_ReturnsOk() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

    // A host's first CreatePlayer() always returns DPID 0 (Decision 3), which now always means
    // broadcast when used as Send()'s idTo (docs/directplay-design.md Decision 20) - a second
    // player is created here so `player` is a real, non-zero, self-send-safe DPID.
    DPID unused = 0;
    CHECK(dp2->CreatePlayer(&unused, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    DPID player = 0;
    CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
    CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, nullptr, 0) == DP_OK);

    char buf[8] = {};
    DPID from = 0, to = 0;
    DWORD size = sizeof(buf);
    CHECK(dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size) == DP_OK);
    CHECK(size == 0);

    dp2->Release();
    dp->Release();
}

// 24-Hour Stabilization Backlog TASK-24H-0109 (plan.md): DirectPlaySession's transport being
// nulled after Close() cannot be observed via whitebox access (DirectPlay2AImpl is in an
// anonymous namespace with no separate header) - this proves it indirectly instead, the same way
// Test_LoopbackShutdown_UnregistersPortForReuse proves it at the transport level: if Close()
// genuinely shuts down and releases the transport (rather than leaking the port binding), a new
// host can bind the same fixed loopback port immediately afterward.
void Test_Close_ThenNewHostCanRebindSamePort_ProvesTransportShutdown() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);
    CHECK(dp2->Close() == DP_OK);

    LPDIRECTPLAY dp2Inst = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dp2Inst, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2Second = nullptr;
    CHECK(dp2Inst->QueryInterface(IID_IDirectPlay2A, (void**)&dp2Second) == DP_OK);
    DPSESSIONDESC2 desc{};
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2Second->Open(&desc, DPOPEN_CREATE) == DP_OK); // proves the port was freed

    dp2Second->Release();
    dp2Inst->Release();
    dp2->Release();
    dp->Release();
}

// Redirects stderr to a temp file for the duration of `body`, then returns the byte count written
// to it - portable via dup/dup2 (POSIX) / _dup/_dup2 (Windows), since ISO C++ has no portable
// "query/swap the current stderr target" API. This is a different technique from
// directdraw_tests.cpp's/directsound_tests.cpp's own zero-log tests (which use
// SDL_GetLogOutputFunction/SDL_SetLogOutputFunction) because DirectPlayLog is deliberately plain
// std::fprintf(stderr, ...), not SDL_Log - DirectPlay.cpp keeps no unconditional SDL3 dependency
// (see DirectPlay.cpp's own comment on this exact tradeoff, TASK-24H-0183), so this test file
// cannot rely on SDL's log-interception API either without reintroducing that same dependency.
template <typename Func>
long CaptureStderrByteCount(Func&& body) {
    std::fflush(stderr);
#ifdef _WIN32
    const int savedFd = _dup(_fileno(stderr));
#else
    const int savedFd = dup(fileno(stderr));
#endif
    FILE* tmp = std::tmpfile();
#ifdef _WIN32
    _dup2(_fileno(tmp), _fileno(stderr));
#else
    dup2(fileno(tmp), fileno(stderr));
#endif

    body();

    std::fflush(stderr);
#ifdef _WIN32
    _dup2(savedFd, _fileno(stderr));
    _close(savedFd);
#else
    dup2(savedFd, fileno(stderr));
    close(savedFd);
#endif
    const long size = std::ftell(tmp);
    std::fclose(tmp);
    return size;
}

// A representative sweep of Open/CreatePlayer/Send/Receive/Close - the exact decision points
// TASK-24H-0183 added DirectPlayLog(...) calls at - run with FREE_DIRECT_DEBUG_DPLAY deliberately
// unset (best-effort regardless of the invoking environment, matching
// directdraw_tests.cpp/directsound_tests.cpp's own equivalent tests). No production code touched
// by this test - it exercises only the real public IDirectPlay2A interface.
void Test_DirectPlayOperations_NoUnconditionalLogOutput_WhenDebugFlagUnset() {
#ifdef _WIN32
    _putenv_s("FREE_DIRECT_DEBUG_DPLAY", "");
#else
    unsetenv("FREE_DIRECT_DEBUG_DPLAY");
#endif

    const long bytes = CaptureStderrByteCount([]() {
        LPDIRECTPLAY dp = nullptr;
        LPDIRECTPLAY2A dp2 = nullptr;
        OpenLoopbackSession(&dp, &dp2);
        DPID player = 0;
        CHECK(dp2->CreatePlayer(&player, nullptr, nullptr, nullptr, 0, 0) == DP_OK);
        CHECK(dp2->Send(player, player, DPSEND_GUARANTEED, nullptr, 0) == DP_OK);
        char buf[8] = {};
        DPID from = 0, to = 0;
        DWORD size = sizeof(buf);
        dp2->Receive(&from, &to, DPRECEIVE_ALL, buf, &size);
        CHECK(dp2->Close() == DP_OK);
        dp2->Release();
        dp->Release();
    });

    CHECK(bytes == 0);
}

// The other half of the same proof: the identical operation sweep, with the flag set, must
// produce real output - confirming the test above passes because logging is correctly gated, not
// because DirectPlayLog is silently broken/unreachable. Restores the env var afterward so test
// execution order never affects the test above.
void Test_DirectPlayOperations_ProducesLogOutput_WhenDebugFlagSet() {
#ifdef _WIN32
    _putenv_s("FREE_DIRECT_DEBUG_DPLAY", "1");
#else
    setenv("FREE_DIRECT_DEBUG_DPLAY", "1", 1);
#endif

    const long bytes = CaptureStderrByteCount([]() {
        LPDIRECTPLAY dp = nullptr;
        LPDIRECTPLAY2A dp2 = nullptr;
        OpenLoopbackSession(&dp, &dp2);
        dp2->Close();
        dp2->Release();
        dp->Release();
    });

#ifdef _WIN32
    _putenv_s("FREE_DIRECT_DEBUG_DPLAY", "");
#else
    unsetenv("FREE_DIRECT_DEBUG_DPLAY");
#endif

    CHECK(bytes > 0);
}

} // namespace

int main() {
    Test_ReceiveOnEmptyQueue_ReturnsNoMessages();
    Test_ReceiveWithTooSmallBuffer_PreservesPacket();
    Test_ReceiveSuccessfulCopy_MatchesQueuedPacket();
    Test_OpenAsHostWithZeroGuidInstance_GeneratesNonZeroGuid();
    Test_OpenAsHostWithNonZeroGuidInstance_PreservesCallerValue();
    Test_OpenAsHostOverLoopback_ReturnsOk();
    Test_OpenAsJoinWithNoHostPresent_ReturnsNoSessions();
    Test_OpenAsJoinWithHostPresent_Succeeds();
    Test_OpenAsJoinOverMaxPlayers_ThirdClientReceivesNoConnection();
    Test_SendToSpecificRemotePlayer_HostDeliversToAssignedClient();
    Test_SendToUnknownRemotePlayer_ReturnsInvalidPlayer();
    Test_SendFromUnknownLocalPlayer_ReturnsInvalidPlayer();
    Test_JoiningRoleSendToNonSelf_ReturnsInvalidPlayer();
    Test_SendOversizedPayloadToRemotePlayer_ReturnsSendTooBig();
    Test_JoinHandshake_ClientAdoptsHostAssignedDpid();
    Test_OpenWithMalformedDwSize_ReturnsInvalidParams();
    Test_LoopbackCreatePlayer_ReturnsUniqueSequentialDpidsStartingAtZero();
    Test_CreatePlayerOverMaxPlayers_ReturnsCantCreatePlayer();
    Test_CreatePlayerWithNoMaxPlayersLimit_NeverRejects();
    Test_RemotePlayerDisconnect_DecrementsCurrentPlayers();
    Test_EnumSessions_FindsZeroSessions();
    Test_EnumSessions_FindsOneHostedSession();
    Test_EnumSessions_FiltersByApplicationGuid();
    Test_EnumSessions_AvailableFlagExcludesFullSessions();
    Test_EnumSessions_NoLongerFindsSessionAfterClose();
    Test_LoopbackSendToSelf_ReturnsOk();
    Test_LoopbackSendWithoutGuaranteedFlag_StillSucceeds();
    Test_LoopbackReceiveAfterSelfSend_MatchesSentPayload();
    Test_LoopbackClose_SendAndReceiveReportNoConnection();
    Test_LoopbackConnect_FindsListeningHostAndQueuesPendingConnection();
    Test_LoopbackConnect_WithNoListeningHost_Fails();
    Test_LoopbackListen_OnAlreadyRegisteredPort_Fails();
    Test_LoopbackAssignPendingConnection_MovesFromPendingToConnected();
    Test_LoopbackRejectPendingConnection_PopsPendingAndFailsWhenEmpty();
    Test_LoopbackAssignedPeerDisconnect_ReportsExactDpidOnce();
    Test_LoopbackPendingPeerDisconnect_NotReported();
    Test_LoopbackThirdClientOverTwoPlayerCap_IsRejected();
    Test_LoopbackHostShutdown_ClearsPeersHostConnection();
    Test_LoopbackShutdown_UnregistersPortForReuse();
    Test_LoopbackSend_HostToAssignedClient_DeliversPayload();
    Test_LoopbackSend_ClientToHost_DeliversPayload();
    Test_LoopbackSend_ToUnknownTargetId_Fails();
    Test_WireHeaderRoundTrip_PreservesAllFields();
    Test_WireHeaderTryDeserialize_RejectsTruncatedBuffer();
    Test_WireHeaderTryDeserialize_RejectsMismatchedPayloadLength();
    Test_WireHeaderTryDeserialize_RejectsWrongMagic();
    Test_WireHeaderTryDeserialize_RejectsWrongVersion();
    Test_WireHeaderTryDeserialize_AcceptsConsistentBuffer();
    Test_HostBroadcast_ReachesAllRemoteClientsNotSelf();
    Test_HostBroadcast_ReachesMultipleRemoteClients();
    Test_ClientBroadcast_RelayedByHostToOtherClientAndHost();
    Test_Receive_BufferSizeQuery_ReportsRequiredSizeWithoutConsuming();
    Test_Release_WithoutPriorClose_CleansUpTransportAndRegistry();
    Test_DirectPlayCreate_NullOutParam_ReturnsInvalidParams();
    Test_DirectPlayCreate_NonNullOuter_ReturnsNoAggregation();
    Test_QueryInterface_UnknownGuid_ReturnsNoInterfaceAndNullsOutParam();
    Test_QueryInterface_NullOutParam_ReturnsInvalidParams();
    Test_CreatePlayer_MalformedDpNameSize_ReturnsInvalidParams();
    Test_DirectPlayEnumerateA_InvokesCallbackExactlyOnceWithValidProvider();
    Test_DirectPlayEnumerateW_InvokesCallbackExactlyOnceWithValidProvider();
    Test_DirectPlayEnumerateA_NullCallback_ReturnsInvalidParams();
    Test_DirectPlayEnumerateW_NullCallback_ReturnsInvalidParams();
    Test_Close_CalledTwice_SecondCallIsSafe();
    Test_SelfSend_OversizedPayload_ReturnsSendTooBig();
    Test_SelfSend_DwDataSizeOverstatesRealBuffer_ReturnsSendTooBigNoOverread();
    Test_Send_NullPayloadWithNonzeroSize_ReturnsInvalidParams();
    Test_SelfSend_NullPayloadWithZeroSize_ReturnsOk();
    Test_Close_ThenNewHostCanRebindSamePort_ProvesTransportShutdown();

    Test_DirectPlayOperations_NoUnconditionalLogOutput_WhenDebugFlagUnset();
    Test_DirectPlayOperations_ProducesLogOutput_WhenDebugFlagSet();

    if (g_failures == 0) {
        std::printf("OK: all DirectPlay tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
