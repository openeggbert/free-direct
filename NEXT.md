# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `f78ee61` ("Start Phase 3: implement DirectPlayMessagePacket
  and the FIFO message queue").
- On top of that, one more change is now made and **not yet committed**: `Receive()` is now wired
  to the real message queue, and `Close()`'s previously-deferred message-state clearing is done.
  See "Completed this batch" below.
- `plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), and Phase 2 are complete.
  Phase 3 now has 11 of its 16 tasks done. Remaining: a max queued-message count, oversize-packet
  rejection, and three "add a unit test" tasks (still pending the tests-location decision below).
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 3 — `Receive()` wired to the real message queue)

- **Added `DirectPlayMessageQueue::Clear()`** and a `DirectPlayMessageQueue messageQueue` member
  on `DirectPlaySession` (neither separately enumerated as a task, both necessary to wire
  `Receive`/`Close` to a real queue).
- **`Receive()` rewritten** in `DirectPlay.cpp`: not-open → `DPERR_NOCONNECTION`; null
  `lpdwDataSize` → `DPERR_INVALIDPARAMS`; empty queue → `DPERR_NOMESSAGES`; buffer-size query
  (`lpData == nullptr && *lpdwDataSize == 0`) → writes the required size, `DP_OK`, no dequeue;
  `*lpdwDataSize` too small → writes the required size, `DPERR_INVALIDPARAMS` (confirmed no
  dedicated "buffer too small" code exists in `include/dplay.h`, and `free-eggbert`'s
  `CNetwork::Receive` doesn't distinguish this case either — reused `DPERR_INVALIDPARAMS` exactly
  as the task instructed), no dequeue; otherwise copies the payload, writes `idFrom`/`idTo`
  (both optional output pointers), dequeues, `DP_OK`.
- **Closed a gap explicitly left open in a prior Phase 2 batch**: `Close()` now also calls
  `session_.messageQueue.Clear()`, completing the "message state" part of Close()'s task that was
  deferred at the time because no queue existed yet. Went back and updated that earlier `plan.md`
  entry with a follow-up note rather than creating a confusing duplicate entry under Phase 3 (an
  editing mistake made and then caught/corrected during this batch).
- **Verification split in two, honestly, since nothing in the codebase can yet enqueue a message
  into a live object** (`Send()` doesn't enqueue — Phase 10; no transport delivers one either —
  Phase 4): (1) a throwaway scratch harness exercised every path reachable through the real
  `IDirectPlay2A::Receive()` today (not-open, null `lpdwDataSize`, open-but-empty, post-`Close()`);
  (2) a second scratch check verified the buffer-size-query/too-small/successful-copy logic by
  replicating `Receive()`'s exact logic against a bare, fully-public `DirectPlaySession` +
  pre-populated queue — proving the logic pattern is correct, but not yet exercising
  `DirectPlay2AImpl::Receive()`'s literal code path for those three specific cases end-to-end.
  That gap closes naturally once Phase 4/10 delivery exists.
- Updated `plan.md`: checked 5 tasks (the buffer-query/no-messages/copy-from/copy-to/dwSize-null
  cluster, described as one implementation) plus the Phase 2 `Close()` follow-up note.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 3 work: max queued-message count; oversize-packet rejection; three "add a unit
  test" tasks.
- **Still-open decision, carried over, not yet made**: where should committed unit tests live
  before Phase 15 exists? The recommendation on the table (from the prior batch, agreed to in
  principle but not yet acted on) is a standalone `tests/directplay_tests.cpp`, not wired into
  CMake, matching Phase 15's own naming (`tests/directplay_tests`). This directly affects whether
  the three "add a unit test" tasks in Phase 3 can be marked done with a permanent artifact rather
  than throwaway scratch verification.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`Receive()` rewritten, `Close()` extended),
  `src/directplay/DirectPlaySession.hpp` (`messageQueue` field), `src/directplay/
  DirectPlayMessageQueue.hpp` (`Clear()` method, updated doc comment), `plan.md` (checkboxes,
  including a correction to an editing mistake made and caught within this same batch).
- Scratch-only, not committed: a two-part throwaway runtime test harness under the session
  scratchpad directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  every prior batch, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean of errors; only pre-existing,
  unrelated warnings from `free-api` headers) plus two actual compiled-and-executed runtime
  checks (see "Completed this batch"). Still no committed automated test — Phase 15 (test
  infrastructure) has not been started.

## Recommended next tasks

1. Decide and act on the tests-location question above (create `tests/directplay_tests.cpp` now,
   standalone/CMake-independent, per the standing recommendation) — this unblocks marking Phase
   3's three "add a unit test" tasks done with a real, permanent artifact instead of repeatedly
   discarded scratch code.
2. Add a maximum queued-message count to `DirectPlayMessageQueue` (bound memory growth if a peer
   stops calling `Receive`) and oversize-packet rejection before a packet is ever queued — the two
   remaining non-test Phase 3 tasks, both self-contained additions to `DirectPlayMessageQueue`.
3. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch.
5. Commit this batch's changes (`src/directplay/DirectPlay.cpp`,
   `src/directplay/DirectPlaySession.hpp`, `src/directplay/DirectPlayMessageQueue.hpp`, `plan.md`,
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

**Phase 3 — `DirectPlayMessagePacket` + FIFO queue (commit `f78ee61`):** the message data
structure itself, with a minimal usage API.

**Phase 3 — `Receive()`/`Close()` wired to the real queue (this batch, not yet committed):** see
"Completed this batch" above.
