# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `37febd4` ("Start Phase 2: give DirectPlaySession real state
  fields").
- On top of that, one more change is now made and **not yet committed**: `Open()` and
  `CreatePlayer()` in `DirectPlay.cpp` now validate `dwSize` on their descriptor parameters. See
  "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo two intentionally-deferred tasks).
  Phase 2 now has 13 of its ~21 tasks done (11 data-model fields + these 2 validation tasks). Still
  not done: wiring `Open`/`EnumSessions`/`CreatePlayer`/`Send`/`Receive`/`Close`/`Release` to
  actually construct/use a `DirectPlaySession` instead of being unconditional-success stubs — that
  is the next piece of work, starting with `Open()`.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `dwSize` validation)

- **`Open()`** now returns `DPERR_INVALIDPARAMS` for a null `lpSessionDesc` or one whose `dwSize`
  doesn't equal `sizeof(DPSESSIONDESC2)`. The null check was added as a safety necessity (not
  asked for by name in the `plan.md` task, but required to read `->dwSize` without undefined
  behavior) and is noted as such in `plan.md`.
- **`CreatePlayer()`** now returns `DPERR_INVALIDPARAMS` when a non-null `lpPlayerName`'s `dwSize`
  doesn't equal `sizeof(DPNAME)`. A null `lpPlayerName` is still accepted (real DirectPlay allows
  creating a player without name info) — the size check only applies when a `DPNAME*` is actually
  provided.
- Neither method does anything else yet (no state transition, no real player tracking) — that is
  deliberately deferred to the next batch, which wires `DirectPlaySession` into `DirectPlay2AImpl`.
- Verified with a throwaway runtime scratch harness (compiled and run outside the repository, not
  committed): null/undersized descriptors rejected with `DPERR_INVALIDPARAMS`; correctly-sized
  descriptors (and a null, optional `lpPlayerName`) accepted with `DP_OK`.
- Updated `plan.md`: checked both validation tasks with notes on the null-check addition and the
  optional-`lpPlayerName` decision.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- Remaining Phase 2 work: `dwFlags` validation in `Open()`; wiring `Open`/`EnumSessions`/
  `CreatePlayer`/`Send`/`Receive`/`Close`/`Release` to a real `DirectPlaySession` instance.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`Open`/`CreatePlayer` validation), `plan.md`
  (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus an actual
  compiled-and-executed runtime check of both validation paths. Still no committed automated
  test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Wire `DirectPlaySession` into `DirectPlay2AImpl` (an owned member) and rewrite `Open()` to do
   real state transitions plus `DPERR_ALREADYINITIALIZED` on a second `Open()` call — the next
   task in progress this session.
2. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
4. Commit this batch's changes (`src/directplay/DirectPlay.cpp`, `plan.md`, this `NEXT.md`).

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
recorded in `docs/directplay-design.md`; four scaffolding files created
(`DirectPlaySession`/`DirectPlayPlayer`/`DirectPlayMessageQueue`/`IDirectPlayTransport`).

**Phase 2 — `DirectPlaySession` data model (commit `37febd4`):** added `DirectPlayObjectState`,
`IsOpen()`/`IsClosed()`, `isHost`, local/remote player DPID vectors, and owned session-descriptor
fields (`sessionName`, `password`, `applicationGuid`, `maxPlayers`, `currentPlayers`) — not yet
wired into `DirectPlay.cpp`.
