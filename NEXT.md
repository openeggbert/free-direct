# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `9b4180c` ("Fix DirectPlayImpl::QueryInterface
  unconditional-success bug (plan.md Phase 1)").
- On top of that, one more change is now made and **not yet committed**: `DirectPlay2AImpl::
  QueryInterface` now has a self-identity success path instead of always returning
  `E_NOINTERFACE`. See "Completed this batch" below.
- `plan.md` Phase 0 is fully complete. Phase 1's entire "QueryInterface cluster" (5 tasks: null
  handling, `IID_IDirectPlay` constant, `DirectPlayImpl` riid dispatch, `AddRef()` correctness, and
  `DirectPlay2AImpl` self-QI) is now done. The remaining ~10 Phase 1 tasks (`DirectPlayCreate`
  output/aggregation handling, enumeration-callback documentation and design decision, the four new
  `src/directplay/*` scaffolding files) have not been started.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (closes the QueryInterface cluster)

- **Gave `DirectPlay2AImpl::QueryInterface` a self-identity success path.** It now returns `this`
  (as `IDirectPlay2A*`) with an `AddRef()` call when `riid` matches `IID_IDirectPlay2A`, instead of
  always returning `E_NOINTERFACE` as it did before. Any other `riid` (including `IID_IDirectPlay`,
  which this class does not implement) still returns `E_NOINTERFACE` with `*ppvObject = nullptr`.
  No `IID_IUnknown`-equivalent exists anywhere in this codebase, so the plan task's optional
  "and IUnknown if one is ever defined" clause was correctly left undone — nothing calls for it.
- **Verified with a second throwaway runtime scratch harness** (compiled and run outside the
  repository, not committed): self-QI on the `IDirectPlay2A` object returns the same pointer with
  a correctly-balanced `AddRef()`/`Release()` (constructor's initial ref + the self-QI `AddRef()`
  both accounted for before it drops to 0); an unknown GUID is rejected without fabricating an
  object; a null `ppvObject` is still rejected.
- Updated `plan.md`: checked the new self-QI task, and revisited the earlier "`AddRef()` in both"
  task's note to confirm it is now genuinely true of both classes' success paths (it was only
  partially true before this batch, and the plan said so honestly rather than being checked
  prematurely).
- This closes out every `QueryInterface`-related finding from the Phase 0 audit
  (`docs/directplay-callsite-audit.md` §1.2) and every task in the "QueryInterface cluster" that
  grew out of it.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2/9 land player/session state.
- The rest of Phase 1 (`DirectPlayCreate` output initialization and `DPERR_NOAGGREGATION`,
  enumeration-callback documentation and the fake-provider design decision, the four new
  `src/directplay/*` scaffolding files) has not been started.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`DirectPlay2AImpl::QueryInterface` rewritten),
  `plan.md` (checkboxes + note revisions).
- Scratch-only, not committed: a second throwaway runtime test harness under the session
  scratchpad directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, blocks
  build-verifying *any* change here; carried over from prior batches, still unresolved).
- This batch's change was verified the same way as the previous one: `g++ -fsyntax-only` (clean,
  only the pre-existing `_GUID` missing-field-initializer warning class) plus an actual
  compiled-and-executed runtime check of the refcounting and dispatch logic. Still no committed
  automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Fix `DirectPlayCreate`'s output initialization (`*lplpDP = nullptr` before any failure path) and
   its `DPERR_NOAGGREGATION` rejection code (adding that error code to `include/dplay.h` if
   missing) — the next unstarted Phase 1 tasks, independent of the now-closed `QueryInterface`
   cluster.
2. Decide (and document in `docs/directplay-design.md`, or a placeholder note if Phase 16 hasn't
   started yet) whether `DirectPlayEnumerateA`/`DirectPlayEnumerateW` should invoke their callback
   once with a fake "FreeDirect" provider — a decision-only task, no enumeration behavior change
   yet.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches, still weakening how strongly any
   Phase 1+ task can be verified beyond syntax/scratch-runtime checks.
4. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
5. Commit this batch's changes (`src/directplay/DirectPlay.cpp`, `plan.md`, this `NEXT.md`).

---

### Prior batches (preserved for history)

**Phase 0 — Repository and call-site audit (commit `68f4643`):** confirmed both sibling
repositories and their exact HEAD commits; produced a full inventory of `include/dplay.h` and
`src/directplay/DirectPlay.cpp`; confirmed exactly which DirectPlay APIs `free-eggbert` calls and
that `planetblupi` has zero DirectPlay usage; found that `free-eggbert`'s DirectPlay lobby/session
UI is unwired (empty `WM_PHASE_DP_*` placeholders, zero reachable callers for
`EnumSessions`/`Open`/`CreatePlayer`); found a DPID size mismatch hazard (FreeDirect's `DWORD_PTR`
vs. real DirectPlay's `DWORD`) that could corrupt `free-eggbert`'s raw pointer-arithmetic helpers
on 64-bit; corrected an imprecise `Restore()` call-site count. Full detail in
`docs/directplay-callsite-audit.md`.

**Phase 1, task 1 — `QueryInterface` null-pointer handling (commit `8674dc3`):** both
`DirectPlay2AImpl::QueryInterface` and `DirectPlayImpl::QueryInterface` now reject a null
`ppvObject` with `DPERR_INVALIDPARAMS` before touching it.

**Phase 1 — `DirectPlayImpl::QueryInterface` riid dispatch (commit `9b4180c`):** added a real
`IID_IDirectPlay` constant; `DirectPlayImpl::QueryInterface` now returns `this` for
`IID_IDirectPlay`, a fresh `DirectPlay2AImpl` for `IID_IDirectPlay2A`, and `E_NOINTERFACE`
otherwise, instead of always fabricating an object regardless of the requested `riid`.
