# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `1b01a2d` ("Make Release() safely shut down a transport;
  closes plan.md Phase 2").
- On top of that, one more change is now made and **not yet committed**: `DirectPlayMessageQueue`
  now holds a real `DirectPlayMessagePacket` struct and a FIFO `std::deque` behind a minimal usage
  API. See "Completed this batch" below.
- `plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), and Phase 2 are complete.
  **Phase 3 (message queue semantics) has now started**: 6 of its 16 tasks are done (the data
  structure itself). Not yet done: wiring `Receive()` in `DirectPlay.cpp` to actually use this
  queue, queue size/oversize-packet limits, and the three "add a unit test" tasks.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 3 — `DirectPlayMessagePacket` + FIFO queue)

- **Implemented `DirectPlayMessagePacket`** in `DirectPlayMessageQueue.hpp`: `DPID idFrom`,
  `DPID idTo`, `DWORD flags` (no specific bit interpreted yet), and
  `std::vector<std::uint8_t> payload`.
- **Implemented the FIFO queue itself** (`std::deque<DirectPlayMessagePacket>`) inside
  `DirectPlayMessageQueue`, plus a minimal usage API not separately enumerated as its own task but
  necessary for the queue to be usable at all: `IsEmpty()`, `Enqueue()`, `Front()` (peek without
  removing), `PopFront()`. **Nothing calls `Enqueue()` yet** — that starts once `Receive()` is
  wired to this queue (next batch) needs something to have put a message there, and eventually
  once real delivery exists (loopback in Phase 4, routing in Phase 10).
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): two packets enqueued and dequeued in strict FIFO order, with
  `idFrom`/`idTo`/`payload` all surviving the round-trip intact; `IsEmpty()`/`Front()` correctly
  reflect empty-queue state before enqueuing and after fully draining.
- Confirmed `DirectPlayMessageQueue.hpp`/`.cpp` still have zero transport/backend dependency
  (only `dplay.h` and standard library headers — no `SDL_`/`ENet` identifiers).
- Updated `plan.md`: checked all 6 data-structure tasks with notes on the added (unenumerated but
  necessary) usage API.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 3 work: wire `Receive()` in `DirectPlay.cpp`'s `DirectPlay2AImpl` to actually
  read from `DirectPlayMessageQueue` (buffer-size query behavior, `DPERR_NOMESSAGES` on empty,
  copying `idFrom`/`idTo`, validating `lpdwDataSize`, rejecting a too-small output buffer without
  dequeuing); a maximum queued-message count; oversize-packet rejection; three "add a unit test"
  tasks — **a decision is still needed on where committed tests should live before Phase 15
  exists** (see "Recommended next tasks").

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlayMessageQueue.hpp` (real struct + queue + API),
  `src/directplay/DirectPlayMessageQueue.cpp` (status comment only), `plan.md` (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  every prior batch, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check of FIFO ordering and empty-queue behavior. Still no
  committed automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Wire `Receive()` in `DirectPlay.cpp` to `DirectPlayMessageQueue`: buffer-size query
   (`lpData == nullptr && *lpdwDataSize == 0` returns the required size), `DPERR_NOMESSAGES` when
   empty, copy `idFrom`/`idTo` on success, validate `lpdwDataSize != nullptr`, reject (without
   dequeuing) when the caller's buffer is smaller than the queued payload.
2. **Decide where committed unit tests should live before Phase 15 exists.** Three of Phase 3's
   tasks are literally "add a unit test," and the phase's acceptance criteria require them to
   "pass under CTest" — which doesn't exist yet. Options: (a) keep using throwaway scratch
   verification and leave those specific checkboxes unchecked until Phase 15 wires up a real test
   executable, or (b) start a `tests/` directory now with standalone, CMake-independent test
   files that Phase 15 later wires into CTest. Worth deciding explicitly rather than drifting into
   one or the other.
3. Add a maximum queued-message count and oversize-packet rejection to
   `DirectPlayMessageQueue`/its `Enqueue` path.
4. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
5. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch.
6. Commit this batch's changes (`src/directplay/DirectPlayMessageQueue.hpp`/`.cpp`, `plan.md`,
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
`QueryInterface` null-handling and `riid` dispatch fixed in both classes; `DirectPlayCreate`
output/aggregation fixed; `DirectPlayEnumerateA`/`W` documented and the fake-provider decision
recorded in `docs/directplay-design.md`; four scaffolding files created.

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`, `efac4e6`, `c3e45a4`, `1b01a2d`):**
`DirectPlaySession` given real state (fields, DPID allocation, transport slot);
`Open`/`CreatePlayer`/`Close`/`EnumSessions`/`Send`/`Receive`/`Release` all wired to real state
with meaningful `DPERR_*` codes instead of unconditional `DP_OK` stubs. **Phase 2 fully complete.**

**Phase 3 — `DirectPlayMessagePacket` + FIFO queue (this batch, not yet committed):** see
"Completed this batch" above.
