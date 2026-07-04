# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `1cde3d5` ("Bound DirectPlayMessageQueue size and reject
  oversize payloads").
- On top of that, one more change is now made and **not yet committed**: `Receive()`'s logic was
  refactored into a testable `DirectPlayMessageQueue::TryReceive()` method, and a real, permanent
  `tests/directplay_tests.cpp` was created and verified to pass. See "Completed this batch" below.
- **`plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), Phase 2, and Phase 3 are
  all fully complete.** Phase 3 has zero remaining unchecked tasks as of this batch.
- **Phase 4 (loopback backend) has not been started.**
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 3 — permanent unit tests; closes Phase 3)

- **Solved the "no way to inject a message into a live object" problem properly, instead of
  working around it with a duplicate-logic test.** `Receive()`'s buffer-size-query/
  `DPERR_NOMESSAGES`/too-small-buffer/successful-copy logic was extracted out of `DirectPlay.cpp`
  into a new `DirectPlayMessageQueue::TryReceive(lpidFrom, lpidTo, lpData, lpdwDataSize)` method.
  `DirectPlay2AImpl::Receive()` now only checks `session_.IsOpen()` and then delegates to
  `session_.messageQueue.TryReceive(...)`. This refactor was verified to preserve identical
  observable behavior via a throwaway regression scratch harness (re-running the exact
  not-open/null-`lpdwDataSize`/empty-queue/post-`Close()` checks from the prior batch) before
  writing any new permanent test.
- **Created `tests/directplay_tests.cpp`** — a standalone, dependency-light file with its own
  `main()`, containing the three tests Phase 3 asked for:
  - `Test_ReceiveOnEmptyQueue_ReturnsNoMessages` — goes through the real, public `IDirectPlay2A`
    interface end-to-end (this path is fully reachable today).
  - `Test_ReceiveWithTooSmallBuffer_PreservesPacket` and
    `Test_ReceiveSuccessfulCopy_MatchesQueuedPacket` — call `DirectPlayMessageQueue::TryReceive()`
    directly on a queue they construct and populate via `Enqueue()`. Because of the refactor
    above, this is **the exact same method production `Receive()` calls**, not a
    re-implementation — these tests will catch a real regression in `Receive()`'s core logic, not
    just in a parallel copy of it.
  - The file documents its own build/run command in a header comment. **Actually built and run
    exactly as documented**, from the repository root: all three checks pass
    (`OK: all DirectPlay tests passed.`, exit code 0).
  - **Not yet wired into CMake/CTest** — that is explicitly `plan.md` Phase 15's job. Added
    `tests/directplay_tests` to `.gitignore` so the compiled binary is never accidentally
    committed.
- Updated `plan.md`: checked all three unit-test tasks plus the standing tests-location decision
  (now acted on, not just decided), with a detailed note on the refactor and why it was the right
  fix rather than a workaround. The Phase 3 acceptance criteria note is honest that "passes under
  CTest" is not yet literally true (no CTest target exists), while "the tests exist, build, and
  pass" is verified true.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Phase 15 still needs to wire `tests/directplay_tests.cpp` (and future DirectDraw/DirectSound
  test files) into `CMakeLists.txt`/CTest — tracked there, not a blocker for Phase 4+.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`Receive()` simplified to delegate),
  `src/directplay/DirectPlayMessageQueue.hpp` (`TryReceive()` method added), `.gitignore`
  (ignore the compiled test binary), `plan.md` (checkboxes).
- New: `tests/directplay_tests.cpp` (permanent, committed test file).
- Scratch-only, not committed: a throwaway regression-check harness under the session scratchpad
  directory, deleted after use (distinct from `tests/directplay_tests.cpp`, which *is* committed).

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  every prior batch, still unresolved).
- **For the first time this session, a real committed test file was built and run**, exactly per
  its own documented command, from the repository root:
  ```
  g++ -std=c++20 -Wall -Wextra \
      -I include -I ../free-api/include -I ../free-api/include_non_windows \
      -I src/directplay \
      src/directplay/DirectPlay.cpp tests/directplay_tests.cpp \
      -o directplay_tests
  ./directplay_tests
  ```
  Output: `OK: all DirectPlay tests passed.`, exit code 0. This is not yet integrated into CTest
  (Phase 15), but it is a genuine, reproducible, standalone test run — stronger evidence than the
  throwaway scratch harnesses used for every earlier batch.

## Recommended next tasks

1. Start `plan.md` Phase 4 (loopback backend): implement `LoopbackDirectPlayTransport` — the
   natural next phase now that Phases 1-3 give it real session/player/message state to route
   packets between two in-process `DirectPlaySession` instances.
2. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch.
4. Commit this batch's changes (`src/directplay/DirectPlay.cpp`,
   `src/directplay/DirectPlayMessageQueue.hpp`, `tests/directplay_tests.cpp`, `.gitignore`,
   `plan.md`, this `NEXT.md`).

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

**Phase 3 (commits `f78ee61`, `3b38e0c`, `1cde3d5`):** `DirectPlayMessagePacket` + FIFO queue;
`Receive()`/`Close()` wired to the real queue; size/oversize bounds added.

**Phase 3 — permanent unit tests (this batch, not yet committed): closes Phase 3.** See
"Completed this batch" above.
