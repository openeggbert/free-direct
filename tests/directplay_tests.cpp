/**
 * @file directplay_tests.cpp
 * @brief Standalone DirectPlay unit tests (`plan.md` Phase 3 and Phase 4).
 *
 * Not yet wired into CMake/CTest - that is `plan.md` Phase 15's job ("Add a
 * DirectPlay unit test executable"). Until then, build and run this file
 * directly from the repository root, e.g.:
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
 */
#include "dplay.h"
#include "DirectPlayMessageQueue.hpp"
#include "DirectPlayWireProtocol.hpp"
#include "LoopbackDirectPlayTransport.hpp"

#include <cstdio>
#include <cstring>

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

// plan.md Phase 4: "Add a unit test for Send to self over loopback, asserting DP_OK."
void Test_LoopbackSendToSelf_ReturnsOk() {
    LPDIRECTPLAY dp = nullptr;
    LPDIRECTPLAY2A dp2 = nullptr;
    OpenLoopbackSession(&dp, &dp2);

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
    Test_WireHeaderTryDeserialize_AcceptsConsistentBuffer();

    if (g_failures == 0) {
        std::printf("OK: all DirectPlay tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
