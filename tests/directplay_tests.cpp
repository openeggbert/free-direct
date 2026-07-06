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

    LPDIRECTPLAY dpB = nullptr;
    CHECK(DirectPlayCreate(nullptr, &dpB, nullptr) == DP_OK);
    LPDIRECTPLAY2A dp2B = nullptr;
    CHECK(dpB->QueryInterface(IID_IDirectPlay2A, (void**)&dp2B) == DP_OK);
    DPSESSIONDESC2 descB{};
    std::memset(&descB, 0, sizeof(descB));
    descB.dwSize = sizeof(DPSESSIONDESC2);
    CHECK(dp2B->Open(&descB, DPOPEN_CREATE) == DP_OK);
    CHECK(std::memcmp(&descB.guidInstance, &zero, sizeof(GUID)) != 0);

    CHECK(std::memcmp(&descA.guidInstance, &descB.guidInstance, sizeof(GUID)) != 0);

    dp2A->Release();
    dpA->Release();
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
    Test_LoopbackCreatePlayer_ReturnsUniqueSequentialDpidsStartingAtZero();
    Test_LoopbackSendToSelf_ReturnsOk();
    Test_LoopbackSendWithoutGuaranteedFlag_StillSucceeds();
    Test_LoopbackReceiveAfterSelfSend_MatchesSentPayload();
    Test_LoopbackClose_SendAndReceiveReportNoConnection();
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
