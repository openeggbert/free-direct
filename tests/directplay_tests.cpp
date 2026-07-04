/**
 * @file directplay_tests.cpp
 * @brief Standalone DirectPlay unit tests (`plan.md` Phase 3).
 *
 * Not yet wired into CMake/CTest - that is `plan.md` Phase 15's job ("Add a
 * DirectPlay unit test executable"). Until then, build and run this file
 * directly from the repository root, e.g.:
 *
 *   g++ -std=c++20 -Wall -Wextra \
 *       -I include -I ../free-api/include -I ../free-api/include_non_windows \
 *       -I src/directplay \
 *       src/directplay/DirectPlay.cpp tests/directplay_tests.cpp \
 *       -o directplay_tests
 *   ./directplay_tests
 *
 * Prints "OK: ..." and exits 0 on success; prints one line per failed
 * check and exits nonzero otherwise.
 */
#include "dplay.h"
#include "DirectPlayMessageQueue.hpp"

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
// session-open check - since nothing in the codebase can yet enqueue a message into a live
// IDirectPlay2A object (Send() doesn't enqueue - Phase 10; no transport delivers one either -
// Phase 4). This is not a re-implementation of Receive()'s logic: it is the same method.
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

} // namespace

int main() {
    Test_ReceiveOnEmptyQueue_ReturnsNoMessages();
    Test_ReceiveWithTooSmallBuffer_PreservesPacket();
    Test_ReceiveSuccessfulCopy_MatchesQueuedPacket();

    if (g_failures == 0) {
        std::printf("OK: all DirectPlay tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return 1;
}
