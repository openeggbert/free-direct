# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `3b38e0c` ("Wire Receive() and Close() to the real
  DirectPlayMessageQueue").
- On top of that, one more change is now made and **not yet committed**: `DirectPlayMessageQueue`
  now bounds its size and rejects oversize payloads. See "Completed this batch" below.
- `plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), and Phase 2 are complete.
  **Phase 3 now has 13 of its 16 tasks done — only the three "add a unit test" tasks remain**, and
  they are pending the still-open tests-location decision (see "Blocked / incomplete").
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 3 — queue size/oversize bounds)

- **Added `kMaxQueuedMessages = 256`** (`static constexpr std::size_t`) to
  `DirectPlayMessageQueue` — a round, generous placeholder, not tied to any specific real-DirectPlay
  or `free-eggbert` requirement.
- **Added `kMaxPayloadBytes = 4096`**, chosen to comfortably exceed every `free-eggbert` payload
  size found in the Phase 0 audit (a fixed 500-byte receive buffer; actual payloads in the low
  hundreds of bytes).
- **`Enqueue()`'s signature changed from `void` to `bool`**: returns `false` without enqueuing
  when the payload exceeds `kMaxPayloadBytes` (checked first) or the queue is already at
  `kMaxQueuedMessages`. Zero call-site impact today, since nothing calls `Enqueue()` yet.
  Deciding which `DPERR_*` code `Send()` should map each rejection reason to is explicitly left
  for Phase 10 — this batch only implements the bounds themselves, in the queue.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): a payload one byte over the limit is rejected and not queued; a payload exactly
  at the limit is accepted; filling the queue to exactly capacity succeeds, one more is rejected
  without disturbing what's already queued, and after draining one slot exactly one more succeeds.
- Updated `plan.md`: checked both bounds tasks with the reasoning above.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- **The only remaining Phase 3 work is the three "add a unit test" tasks, still pending the
  tests-location decision**, carried over unresolved for two batches now: create a standalone
  `tests/directplay_tests.cpp` (not wired into CMake yet — that's Phase 15) versus continuing
  throwaway scratch verification indefinitely. This should be decided the next time Phase 3 work
  resumes, rather than deferred again.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlayMessageQueue.hpp` (`kMaxQueuedMessages`/`kMaxPayloadBytes`
  constants, `Enqueue()` now returns `bool`), `plan.md` (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  every prior batch, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (no errors) plus an actual
  compiled-and-executed runtime check of both bounds. Still no committed automated test — Phase 15
  (test infrastructure) has not been started.

## Recommended next tasks

1. Decide the tests-location question (see "Blocked / incomplete") and act on it — create
   `tests/directplay_tests.cpp` (standalone, not yet CMake-wired) covering the three remaining
   Phase 3 unit-test tasks: `Receive` on an empty queue (`DPERR_NOMESSAGES`), `Receive` with a
   too-small buffer (packet preserved, meaningful error), and `Receive` performing a successful
   copy (payload/sender/recipient all correct). This closes out Phase 3 entirely.
2. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch.
4. Commit this batch's changes (`src/directplay/DirectPlayMessageQueue.hpp`, `plan.md`, this
   `NEXT.md`).

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

**Phase 3 (commits `f78ee61`, `3b38e0c`):** `DirectPlayMessagePacket` + FIFO queue implemented;
`Receive()`/`Close()` wired to the real queue.

**Phase 3 — queue size/oversize bounds (this batch, not yet committed):** see "Completed this
batch" above.
