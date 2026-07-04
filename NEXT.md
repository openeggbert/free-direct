# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `53b5ba4` ("Add Phase 1 DirectPlay scaffolding files (Session,
  Player, MessageQueue, Transport)").
- On top of that, one more change is now made and **not yet committed**: `DirectPlaySession` now
  has real state fields (Phase 2 has begun). See "Completed this batch" below.
- `plan.md` Phase 0 and Phase 1 are complete (Phase 1 modulo the two intentionally-deferred tasks
  noted in prior batches). **Phase 2 has now started**: 11 of its ~21 tasks are done (the pure
  data-model fields on `DirectPlaySession`); the remaining Phase 2 tasks (validation, and wiring
  `Open`/`EnumSessions`/`CreatePlayer`/`Send`/`Receive`/`Close`/`Release` to actually use this
  state) are not yet done — `DirectPlay.cpp` itself is unchanged so far.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — DirectPlaySession data model)

- **Added `DirectPlayObjectState`** (`Created`/`Open`/`Closed`), a scoped `enum class` at
  namespace scope in `DirectPlaySession.hpp` (not nested in the class, so `DirectPlay.cpp` can
  reference it later without `DirectPlaySession::` qualification).
- **Added `DirectPlaySession::IsOpen()`/`IsClosed()`** — both derived from `state`, no redundant
  boolean fields, matching the plan's explicit "as derived from the state enum" wording.
- **Added `bool isHost`** (chose boolean over enum, matching `free-eggbert`'s own `BOOL m_bHost`
  shape from the Phase 0 audit), **`std::vector<DPID> localPlayerIds`** and **`remotePlayerIds`**
  (two distinct vectors, not one shared list).
- **Added session-descriptor-derived fields**: `sessionName` and `password` (both owned
  `std::string`, not caller-owned pointers), `applicationGuid`, `maxPlayers`, `currentPlayers`.
  **Deliberately did not add a raw `DPSESSIONDESC2`-shaped member** — documented in the header
  why: that struct's `lpszSessionName`/`lpszPassword` pointer fields are caller-owned and must not
  be retained, so keeping a struct copy around would invite someone to read those dangling/stale
  pointers later. The plain fields above are the safe replacement.
- `currentPlayers` is a field only in this batch — the actual increment/decrement behavior on
  player create/remove is deferred to the not-yet-done `CreatePlayer`/`Close` wiring tasks later in
  Phase 2.
- Updated the file's own `@note Status:` from `STUB` to `PARTIAL` in both `DirectPlaySession.hpp`
  and `.cpp`, reflecting that it now holds real (if not yet wired-in) state.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed): default-constructed state is `Created`/not-open/not-closed/host=false/empty
  containers/empty strings/zero counters; `IsOpen()`/`IsClosed()` correctly track `state`
  transitions; both player-ID vectors are independently mutable.
- Updated `plan.md`: checked all 11 data-model tasks, each with a short note on the exact design
  choice made and its justification.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
  Neither blocks this batch's `std::vector<DPID>` fields (their size doesn't depend on the
  decision), but the decision should still be made before Phase 9 does real DPID allocation.
- Remaining Phase 2 work, not yet started: `dwSize`/`dwFlags` validation in `Open()`/
  `CreatePlayer()`; rewriting `Open`/`EnumSessions`/`CreatePlayer`/`Send`/`Receive`/`Close` in
  `DirectPlay.cpp` to actually construct and use a `DirectPlaySession` instead of being
  unconditional-success stubs; `Release()` calling `IDirectPlayTransport::Shutdown()`.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlaySession.hpp` (real fields added), `src/directplay/
  DirectPlaySession.cpp` (status comment only), `plan.md` (checkboxes).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean, only the pre-existing `_GUID`
  missing-field-initializer warning class) plus an actual compiled-and-executed runtime check of
  default state, state transitions, and container independence. Still no committed automated
  test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Add the two Phase 2 validation tasks: `Open()`'s `DPSESSIONDESC2.dwSize` check and
   `CreatePlayer()`'s `DPNAME.dwSize` check, both returning `DPERR_INVALIDPARAMS` — these are
   small, self-contained, and don't require `DirectPlaySession` to be wired into `DirectPlay.cpp`
   yet on their own (though they'll naturally live in the same methods once that wiring happens).
2. Then wire `DirectPlaySession` into `DirectPlay2AImpl` in `DirectPlay.cpp`: give
   `DirectPlay2AImpl` an owned `DirectPlaySession` member, and rewrite `Open()` first (state
   transitions, `DPERR_ALREADYINITIALIZED` on double-open) since most other methods' correct
   behavior depends on there being real open/closed state to check.
3. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5 — still open, still not urgent for the current batch.
4. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
5. Commit this batch's changes (`src/directplay/DirectPlaySession.hpp`/`.cpp`, `plan.md`, this
   `NEXT.md`).

---

### Prior batches (preserved for history)

**Phase 0 — Repository and call-site audit (commit `68f4643`):** confirmed both sibling
repositories and their exact HEAD commits; full inventory of `include/dplay.h` and
`src/directplay/DirectPlay.cpp`; confirmed which DirectPlay APIs `free-eggbert` calls and that
`planetblupi` has zero DirectPlay usage; found the `free-eggbert` DirectPlay lobby/session UI is
unwired; found a DPID size mismatch hazard; corrected an imprecise `Restore()` call-site count.
Full detail in `docs/directplay-callsite-audit.md`.

**Phase 1 — `QueryInterface` cluster (commits `8674dc3`, `9b4180c`, `af6d336`):** null-pointer
handling fixed in both `QueryInterface` overrides; added `IID_IDirectPlay`; `DirectPlayImpl::
QueryInterface` dispatches on `riid`; `DirectPlay2AImpl::QueryInterface` has a self-identity path.

**Phase 1 — `DirectPlayCreate` cluster (commit `e3acac6`):** output pointer safely initialized;
`pUnkOuter != nullptr` returns the new `DPERR_NOAGGREGATION`.

**Phase 1 — Enumeration documentation + design decision (commit `6480493`):** documented
`DirectPlayEnumerateA`/`W`'s stub status; `docs/directplay-design.md` created, recording the
decision that enumeration must eventually report one fake provider (implementation deferred to
Phase 8).

**Phase 1 — Scaffolding files (commit `53b5ba4`):** created `DirectPlaySession`, `DirectPlayPlayer`,
`DirectPlayMessageQueue` (all empty at the time) and `IDirectPlayTransport`; wired the three
`.cpp` files into `CMakeLists.txt`.
