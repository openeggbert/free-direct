# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `c285550` ("Wire CreatePlayer() and Close() to real
  DirectPlaySession state").
- On top of that, one more change is now made and **not yet committed**: `Open()` now validates
  `dwFlags`. See "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo two intentionally-deferred tasks).
  **Phase 2 now has 17 of its ~21 tasks done — only 4 remain**: `EnumSessions()`/`Send()` state
  wiring, `Receive()`'s `DPERR_NOMESSAGES` wiring (blocked on Phase 3's message queue not existing
  yet), and `Release()` calling `IDirectPlayTransport::Shutdown()`.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `dwFlags` validation)

- **`Open()`** now returns `DPERR_INVALIDFLAGS` for any `dwFlags` bit outside
  `DPOPEN_CREATE | DPOPEN_JOIN | DPOPEN_OPENSESSION` (checked right after the existing `dwSize`/
  null validation, before the already-open check). Does not additionally enforce that
  `DPOPEN_CREATE`/`DPOPEN_JOIN` are mutually exclusive — not named by this task, left as a possible
  future refinement rather than implemented speculatively.
- **Fixed a stale in-code comment** left over from the prior batch that claimed `Close()` "does
  not yet reset `session_`" — it does, as of the `CreatePlayer()`/`Close()` wiring commit.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): an unrecognized flag bit is rejected both alone and combined with a valid bit;
  `DPOPEN_CREATE` and `DPOPEN_JOIN` individually still succeed exactly as before.
- Updated `plan.md`: checked the `dwFlags` task with notes on what is and isn't enforced.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 2 work (4 tasks): wire `EnumSessions()`/`Send()` to check `session_.IsOpen()`
  (parameter/state validation only — full discovery/routing is Phase 8/10); wire `Receive()` to
  return `DPERR_NOMESSAGES` (genuinely blocked until Phase 3 gives `DirectPlaySession` a real
  message queue — `DirectPlayMessageQueue` is still an empty scaffold); wire `Release()` to call
  `IDirectPlayTransport::Shutdown()` (there is no transport instance on `DirectPlaySession` yet
  either — that's Phase 4+ — so this may also turn out to be blocked until then).

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`Open()` `dwFlags` check, stale comment fix),
  `plan.md` (checkbox).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check of the flag-validation behavior. Still no committed
  automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Wire `EnumSessions()` and `Send()` to check `session_.IsOpen()` and return a meaningful error
   when not open — the two Phase 2 tasks that are actually doable right now without waiting on
   another phase.
2. Investigate whether `Receive()`'s `DPERR_NOMESSAGES` task and `Release()`'s
   `IDirectPlayTransport::Shutdown()` task are better done now (with a reasonable stand-in — e.g.
   `Receive()` can honestly return `DPERR_NOMESSAGES` unconditionally today, since no queue exists
   yet to ever have a message in it) or deferred until Phase 3/4 respectively — worth a quick
   decision before Phase 2 is called "complete."
3. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
5. Commit this batch's changes (`src/directplay/DirectPlay.cpp`, `plan.md`, this `NEXT.md`).

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

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`):** `DirectPlaySession` given real
data-model fields; `Open()`/`CreatePlayer()` validate `dwSize`; `Open()` wired to real state
transitions with `DPERR_ALREADYINITIALIZED` on double-open; `CreatePlayer()`/`Close()` wired to
real player/session state.

**Phase 2 — `dwFlags` validation (this batch, not yet committed):** see "Completed this batch"
above.
