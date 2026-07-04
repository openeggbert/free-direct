# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `efac4e6` ("Validate dwFlags in Open(), return
  DPERR_INVALIDFLAGS for unrecognized bits").
- On top of that, one more change is now made and **not yet committed**: `EnumSessions()`,
  `Send()`, and `Receive()` are now wired to real state. See "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo two intentionally-deferred tasks).
  **Phase 2 now has 20 of its 21 tasks done — only 1 remains**: `Release()` tearing down transport
  resources via `IDirectPlayTransport::Shutdown()`. This is deliberately left open: there is no
  transport instance anywhere yet (that starts in Phase 4), so this task may turn out to be
  genuinely blocked until then rather than doable now — worth a quick check before assuming it's
  simply "next."
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `EnumSessions`/`Send`/`Receive` wired to real state)

- **`EnumSessions()`**: validates `lpEnumSessionsDesc`'s `dwSize` when a filter descriptor is
  provided (it's optional — null means "enumerate everything"); rejects a null
  `lpEnumSessionsCallback` with `DPERR_INVALIDPARAMS` (safety-necessary addition — there'd be no
  way to receive results without one). Still returns `DP_OK` with zero callback invocations, which
  is now understood to be *honestly correct* (Phase 6/7/8 hosting/discovery infrastructure doesn't
  exist yet, so there's genuinely nothing to discover), not a leftover placeholder.
- **Scope correction recorded in `plan.md`**: this task's wording referenced "the enumeration
  decision recorded in Phase 1," but that decision (`docs/directplay-design.md`) was specifically
  about the free functions `DirectPlayEnumerateA`/`W` (service *provider* enumeration), not about
  `IDirectPlay2A::EnumSessions` (session enumeration) — two distinct real-DirectPlay concepts the
  task text had conflated. Noted explicitly rather than silently glossed over.
- **`Send()`**: now returns `DPERR_NOCONNECTION` when the session isn't open. Deliberately does
  *not* validate sender/recipient player IDs or payload — those are Phase 10's own numbered tasks,
  left there rather than pulled forward even though `localPlayerIds` already exists to check
  against.
- **`Receive()`**: now returns `DPERR_NOCONNECTION` when not open, and `DPERR_NOMESSAGES`
  unconditionally otherwise — genuinely correct today since `DirectPlaySession` has no
  message-queue member until Phase 3, and this exactly matches what `free-eggbert`'s
  `CNetwork::Receive` checks for.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): before `Open()`, `Send`/`Receive` both return `DPERR_NOCONNECTION`;
  `EnumSessions` rejects a null callback and an undersized filter descriptor, and succeeds (zero
  results) with valid inputs with or without a filter; after `Open()`, `Send` succeeds and
  `Receive` returns `DPERR_NOMESSAGES`; after `Close()`, both return `DPERR_NOCONNECTION` again.
- Updated `plan.md`: checked all three tasks with detailed notes on what was and wasn't done, and
  the scope correction above.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Phase 2's last remaining task (`Release()` → `IDirectPlayTransport::Shutdown()`) may be blocked
  until Phase 4 gives `DirectPlaySession` an actual transport instance to shut down — needs a
  quick look before starting it, not an assumption either way.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`EnumSessions`/`Send`/`Receive` rewritten), `plan.md`
  (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check covering all three methods across the open/closed lifecycle.
  Still no committed automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Check whether Phase 2's last task (`Release()` transport shutdown) is actually doable now or
   genuinely blocked on Phase 4's transport work not existing yet — if blocked, say so plainly in
   `plan.md` rather than leaving it ambiguously "not started."
2. If Phase 2 is effectively done (pending only that one blocked task), consider starting Phase 3
   (message queue semantics) next, since `DirectPlayMessageQueue` is still an empty scaffold and
   `Receive()`'s current `DPERR_NOMESSAGES`-always behavior is exactly what Phase 3 needs to build
   real FIFO semantics on top of.
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

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`, `efac4e6`):** `DirectPlaySession`
given real data-model fields; `dwSize`/`dwFlags` validation added to `Open()`/`CreatePlayer()`;
`Open`/`CreatePlayer`/`Close` wired to real state with correct `DPERR_ALREADYINITIALIZED` and
DPID-allocation/reset behavior.

**Phase 2 — `EnumSessions`/`Send`/`Receive` wired (this batch, not yet committed):** see
"Completed this batch" above.
