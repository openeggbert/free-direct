# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `75d4de5` ("Add permanent DirectPlay unit tests; closes
  plan.md Phase 3").
- On top of that, one more change is now made and **not yet committed**: `LoopbackDirectPlayTransport`
  is implemented and wired into `Open`/`Close`/`Send`, with four new permanent tests. See
  "Completed this batch" below.
- **`plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), Phase 2, Phase 3, and
  Phase 4 are all fully complete.** `tests/directplay_tests.cpp` now has 7 passing tests total.
- **Phase 5 (ENet integration planning) has not been started.**
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 4 — loopback backend; closes Phase 4)

- **Implemented `LoopbackDirectPlayTransport`** (`src/directplay/LoopbackDirectPlayTransport.hpp`/
  `.cpp`): an in-memory `std::deque<std::vector<uint8_t>>` byte-buffer queue. `Send()` appends a
  copy; `Receive()` pops the front entry (or returns `false` without popping if the caller's
  buffer is too small); `Listen()`/`Connect()` trivially succeed; `Shutdown()` clears the buffer.
  Zero real socket/backend dependency — confirmed no `ENet`/`SDL_net` *identifier* appears in
  either file (only doc-comment mentions of the policy, not code).
- **`Open()` now unconditionally assigns a fresh `LoopbackDirectPlayTransport`** to
  `session_.transport` on success — it's the only backend that exists today, so there's no
  provider-selection step yet (that's Phase 5/6/8's job). **`Close()` now also calls
  `session_.transport->Shutdown()` and resets it to `nullptr`** — this makes `Release()`'s own
  `Shutdown()` call (added in Phase 2, previously dead code since `transport` was always null)
  actually meaningful for the "Release() without a prior Close()" case, confirmed by a new test.
- **`Send()` implements the self-send path** (`idTo == idFrom`): rather than enqueuing directly
  into the message queue, it deliberately round-trips the payload through
  `session_.transport->Send()`/`Receive()` first, so `LoopbackDirectPlayTransport`'s own methods
  are genuinely exercised (matching the eventual shape of a real backend) instead of being
  assigned and left idle. Any other recipient is still a silent no-op — general routing,
  player-ID validation, and payload validation remain Phase 10's job. A failed enqueue (queue
  full/oversize) currently returns `DPERR_SENDTOOBIG`, which is imprecise for the "queue full"
  case specifically — flagged for Phase 10 to refine when it implements full routing.
- **`CreatePlayer()`/`Receive()` needed no code changes** for this phase — neither ever referenced
  `session_.transport`, so they already worked correctly regardless of backend; re-verified with
  dedicated tests rather than just assumed.
- **Added four new permanent tests to `tests/directplay_tests.cpp`** (now 7 total, up from 3):
  `Test_LoopbackCreatePlayer_ReturnsUniqueNonZeroDpids`, `Test_LoopbackSendToSelf_ReturnsOk`,
  `Test_LoopbackReceiveAfterSelfSend_MatchesSentPayload`,
  `Test_LoopbackClose_SendAndReceiveReportNoConnection` — all four go through the real, public
  `IDirectPlay2A` interface end-to-end via a shared `OpenLoopbackSession` helper. No injection
  workaround was needed this time, since `Open()` itself now creates real, usable loopback state.
- **Actually built and ran the full test file** per its own updated documented command from the
  repository root: `OK: all DirectPlay tests passed.`, exit code 0 (all 7 tests, no failures).
- Updated `CMakeLists.txt` to add `LoopbackDirectPlayTransport.cpp` to `free-direct`'s sources.
- Updated `plan.md`: checked all 10 Phase 4 tasks with detailed notes on design choices (loopback
  round-trip vs. direct enqueue, the `DPERR_SENDTOOBIG` imprecision, why `CreatePlayer`/`Receive`
  needed no changes) and verification (actual build+run output, `ENet`/`SDL_net` grep result).

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Noted for Phase 10: the `DPERR_SENDTOOBIG`-for-every-`Enqueue()`-failure mapping in `Send()` is
  imprecise (conflates "oversize payload" with "queue full") and should be refined once Phase 10
  implements full routing with proper per-condition error codes.
- Phase 15 still needs to wire `tests/directplay_tests.cpp` into `CMakeLists.txt`/CTest.

## Files inspected/changed this batch

- New: `src/directplay/LoopbackDirectPlayTransport.hpp`, `src/directplay/
  LoopbackDirectPlayTransport.cpp`.
- Changed: `src/directplay/DirectPlay.cpp` (`Open`/`Close`/`Send`/`Release` updated),
  `CMakeLists.txt` (new source file), `tests/directplay_tests.cpp` (four new tests, updated
  header comment/build command), `plan.md` (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use (distinct from the real, committed `tests/directplay_tests.cpp`).

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  every prior batch, still unresolved).
- **`tests/directplay_tests.cpp` was actually built and run**, exactly per its own documented
  command:
  ```
  g++ -std=c++20 -Wall -Wextra \
      -I include -I ../free-api/include -I ../free-api/include_non_windows \
      -I src/directplay \
      src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
      tests/directplay_tests.cpp \
      -o directplay_tests
  ./directplay_tests
  ```
  Output: `OK: all DirectPlay tests passed.`, exit code 0 (7/7 tests). Not yet integrated into
  CTest (Phase 15).

## Recommended next tasks

1. Start `plan.md` Phase 5 (ENet integration planning): add the `FREE_DIRECT_ENABLE_ENET` CMake
   option and vendored/system ENet detection first, since everything else in that phase (the
   `EnetDirectPlayTransport` skeleton, the wire packet header) depends on ENet actually being
   available to compile against.
2. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch.
4. Commit this batch's changes (`src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp`,
   `src/directplay/DirectPlay.cpp`, `CMakeLists.txt`, `tests/directplay_tests.cpp`, `plan.md`,
   this `NEXT.md`).

---

### Prior batches (preserved for history)

**Phase 0 — Repository and call-site audit (commit `68f4643`):** confirmed both sibling
repositories and their exact HEAD commits; full inventory of `include/dplay.h` and
`src/directplay/DirectPlay.cpp`; confirmed which DirectPlay APIs `free-eggbert` calls and that
`planetblupi` has zero DirectPlay usage; found the `free-eggbert` DirectPlay lobby/session UI is
unwired; found a DPID size mismatch hazard; corrected an imprecise `Restore()` call-site count.
Full detail in `docs/directplay-callsite-audit.md`.

**Phase 1 (commits `8674dc3`, `9b4180c`, `af6d336`, `e3acac6`, `6480493`, `53b5ba4`):**
`QueryInterface` null-handling and `riid` dispatch fixed; `DirectPlayCreate` output/aggregation
fixed; `DirectPlayEnumerateA`/`W` documented and the fake-provider decision recorded; four
scaffolding files created.

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`, `efac4e6`, `c3e45a4`, `1b01a2d`):**
`DirectPlaySession` given real state; `Open`/`CreatePlayer`/`Close`/`EnumSessions`/`Send`/
`Receive`/`Release` all wired to real state. **Fully complete.**

**Phase 3 (commits `f78ee61`, `3b38e0c`, `1cde3d5`, `75d4de5`):** `DirectPlayMessagePacket` + FIFO
queue; `Receive()`/`Close()` wired to the real queue; size/oversize bounds; permanent unit tests
(`tests/directplay_tests.cpp` created). **Fully complete.**

**Phase 4 — loopback backend (this batch, not yet committed): closes Phase 4.** See "Completed
this batch" above.
