# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `f67dedd` ("Validate DPSESSIONDESC2.dwSize in Open() and
  DPNAME.dwSize in CreatePlayer()").
- On top of that, one more change is now made and **not yet committed**: `DirectPlay2AImpl` now
  owns a real `DirectPlaySession`, and `Open()` does real state transitions instead of always
  returning `DP_OK`. See "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo two intentionally-deferred tasks).
  Phase 2 now has 14 of its ~21 tasks done. `Open()` is the first method actually wired to
  `DirectPlaySession`; `EnumSessions`/`CreatePlayer`/`Send`/`Receive`/`Close`/`Release` are still
  unconditional-success stubs (aside from `CreatePlayer`'s `dwSize` check from the prior batch).
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `Open()` wired to real state)

- **`DirectPlay2AImpl` now owns a `free_direct_directplay::DirectPlaySession session_` member**
  (added `#include "DirectPlaySession.hpp"` to `DirectPlay.cpp`).
- **`Open()` rewritten**: rejects a second call with `DPERR_ALREADYINITIALIZED` only when
  `session_.IsOpen()` — a re-`Open()` after a (not-yet-real) `Close()` is deliberately still
  allowed, matching the task's specific "already-*open*" wording. On success: sets `isHost` from
  the `DPOPEN_CREATE` bit, deep-copies `applicationGuid`/`maxPlayers`/`currentPlayers`/
  `sessionName`/`password` from the caller's `DPSESSIONDESC2` into `session_` (never retaining the
  caller's `LPSTR` pointers), then transitions `state` to `Open`.
- **Explicitly not done in this batch** (separate tasks): `dwFlags` bits other than
  `DPOPEN_CREATE` are not validated yet; `EnumSessions`/`CreatePlayer`/`Send`/`Receive`/`Close`/
  `Release` do not read or write `session_` yet.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): first `Open()` succeeds for both `DPOPEN_CREATE` and `DPOPEN_OPENSESSION`; a
  second `Open()` on the same object returns `DPERR_ALREADYINITIALIZED`; the pre-existing
  `dwSize`/null validation from the prior batch still works unchanged.
- Updated `plan.md`: checked the `Open()` task with a detailed note on the "already-open vs.
  already-closed" design choice and what remains deliberately out of scope for this batch.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 2 work: `dwFlags` validation in `Open()`; wiring `EnumSessions`/`CreatePlayer`/
  `Send`/`Receive`/`Close`/`Release` to `session_`. Until `Close()` is wired, re-`Open()` after
  `Close()` is untested territory (no observable difference yet, since `Close()` is still a stub).

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`session_` member, `Open()` rewritten), `plan.md`
  (checkbox).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check of the state-transition behavior. Still no committed
  automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Wire `CreatePlayer()` to `session_`: allocate a DPID via a simple incrementing counter
   (Phase 9 will revisit correctness), append it to `localPlayerIds`, and increment
   `currentPlayers` — the plan explicitly allows a placeholder counter here.
2. Wire `Close()` to clear `session_` (player lists, session descriptor fields) and transition
   `state` to `Closed` — this is what will make the "re-`Open()` after `Close()`" path in the
   current `Open()` implementation actually meaningful/testable.
3. Add `dwFlags` validation to `Open()` (`DPERR_INVALIDFLAGS` for bits outside
   `DPOPEN_CREATE`/`DPOPEN_JOIN`/`DPOPEN_OPENSESSION`).
4. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
5. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
6. Commit this batch's changes (`src/directplay/DirectPlay.cpp`, `plan.md`, this `NEXT.md`).

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

**Phase 2 (commits `37febd4`, `f67dedd`):** `DirectPlaySession` given real data-model fields;
`Open()`/`CreatePlayer()` validate `dwSize` on their descriptor parameters.

**Phase 2 — `Open()` wired to real state (this batch, not yet committed):** see "Completed this
batch" above.
