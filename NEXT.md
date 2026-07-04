# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `188cfab` ("Wire Open() to real DirectPlaySession state").
- On top of that, one more change is now made and **not yet committed**: `CreatePlayer()` and
  `Close()` are now wired to `session_` too. See "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo two intentionally-deferred tasks).
  Phase 2 now has 16 of its ~21 tasks done. `Open`/`CreatePlayer`/`Close` all use real
  `DirectPlaySession` state now; `EnumSessions`/`Send`/`Receive`/`Release` still don't.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `CreatePlayer()`/`Close()` wired to real state)

- **Added `DPID nextPlayerId = 1` to `DirectPlaySession`** — a new field, not one of the originally
  enumerated Phase 2 data-model fields, but necessary to implement the `CreatePlayer` DPID
  allocation task. Noted explicitly in `plan.md` rather than added silently. Starts at `1`,
  skipping `0` (matching real DirectPlay's `DPID_SYSMSG`/`DPID_ALLPLAYERS` convention); Phase 9
  revisits correctness against the Phase 0 DPID-vs-array-index finding.
- **`CreatePlayer()` rewritten**: allocates `session_.nextPlayerId++`, appends it to
  `localPlayerIds`, increments `currentPlayers`, and writes the new DPID to `*lpidPlayer` only
  when that output pointer is non-null (preserving the original stub's tolerance of a null
  `lpidPlayer`). Does not check whether the session is open first — not named by this task,
  deliberately left for a later validation sweep.
- **`Close()` rewritten**: clears `localPlayerIds`/`remotePlayerIds`, resets `nextPlayerId` back
  to `1` (so a closed-then-reopened session gets a fresh DPID sequence, not a continuation),
  clears `sessionName`/`password`, zeroes `applicationGuid`/`maxPlayers`/`currentPlayers`, resets
  `isHost`, and transitions `state` to `Closed`. Message-queue clearing is explicitly deferred
  (documented in the header) since `DirectPlaySession` has no message-queue member until Phase 3.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): sequential `CreatePlayer` calls yield distinct, incrementing DPIDs starting at 1;
  a null `lpidPlayer` is still tolerated; after `Close()`, a subsequent `Open()` no longer returns
  `DPERR_ALREADYINITIALIZED` (proving `state` actually left `Open`), and a player created after
  that re-`Open()` gets DPID `1` again (proving the allocator and player list were genuinely reset,
  not just a top-level flag).
- Updated `plan.md`: checked both tasks with notes on the new field, the "null `lpidPlayer` still
  tolerated" decision, and exactly what `Close()` does and doesn't clear yet.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 2 work: `dwFlags` validation in `Open()`; `EnumSessions`/`Send`/`Receive`
  rewired to real state (`Receive`'s `DPERR_NOMESSAGES` wiring explicitly depends on Phase 3's
  message queue, which doesn't exist yet); `Release()` calling `IDirectPlayTransport::Shutdown()`.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`CreatePlayer`/`Close` rewritten),
  `src/directplay/DirectPlaySession.hpp` (`nextPlayerId` field, updated file-level doc comment),
  `plan.md` (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check covering DPID allocation, null-pointer tolerance, and the
  `Close()`-then-reopen reset behavior. Still no committed automated test — Phase 15 (test
  infrastructure) has not been started.

## Recommended next tasks

1. Add `dwFlags` validation to `Open()` (`DPERR_INVALIDFLAGS` for bits outside
   `DPOPEN_CREATE`/`DPOPEN_JOIN`/`DPOPEN_OPENSESSION`) — the task the user explicitly queued up
   next this session.
2. Wire `EnumSessions()`/`Send()` to check `session_.IsOpen()` (parameter/state validation only,
   per their Phase 2 task wording — full discovery/routing logic is Phase 8/10).
3. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
5. Commit this batch's changes (`src/directplay/DirectPlay.cpp`,
   `src/directplay/DirectPlaySession.hpp`, `plan.md`, this `NEXT.md`).

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

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`):** `DirectPlaySession` given real data-model
fields; `Open()`/`CreatePlayer()` validate `dwSize`; `Open()` wired to real state transitions with
`DPERR_ALREADYINITIALIZED` on double-open.

**Phase 2 — `CreatePlayer()`/`Close()` wired (this batch, not yet committed):** see "Completed
this batch" above.
