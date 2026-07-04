# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `8674dc3` ("Fix QueryInterface null-pointer handling in
  DirectPlay stub (plan.md Phase 1, task 1)").
- On top of that, one more change is now made and **not yet committed**: `DirectPlayImpl`'s
  `QueryInterface` now actually checks `riid` and rejects unknown interfaces instead of
  unconditionally succeeding. See "Completed this batch" below.
- `plan.md` Phase 0 is fully complete. Phase 1 now has 4 of its tasks done (1 committed, 3 pending
  commit) plus 1 newly-discovered task added to the list (not yet started); the remaining ~11
  Phase 1 tasks have not been started.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 1, tasks 2-4 of the "QueryInterface" cluster)

- **Added a real `IID_IDirectPlay` constant** to `include/dplay.h` (`= {1}`), next to the existing
  `IID_IDirectPlay2A = {0}` placeholder, with a doc comment stating plainly that these are
  FreeDirect-internal identity tokens, not real Microsoft IIDs — no wire/binary-compatibility claim
  is made or implied.
- **Fixed `DirectPlayImpl::QueryInterface`'s unconditional-success bug.** Added a local
  `IsEqualGuid` helper (`memcmp`-based; no such helper existed anywhere in `free-api` or
  `free-direct` to reuse) and rewrote the method to: return `this` (with `AddRef()`) for
  `IID_IDirectPlay`; return a freshly-constructed `DirectPlay2AImpl` for `IID_IDirectPlay2A`
  (no extra `AddRef()` needed there — its constructor already starts `refCount_` at 1); and return
  `E_NOINTERFACE` with `*ppvObject = nullptr` for anything else.
- **Verified with a throwaway runtime scratch harness** (compiled and run outside the repository,
  not committed — no test infrastructure exists yet, that's Phase 15): confirmed null `ppvObject`
  is rejected, an unknown GUID is rejected and does not fabricate an object, self-`riid` query
  returns `this` with a correctly-balanced `AddRef()`/`Release()`, and the `IDirectPlay2A` path
  returns an independently-refcounted object that drops to 0 on a single `Release()`.
- **Discovered and added a new Phase 1 task** (not in the original plan): `DirectPlay2AImpl::
  QueryInterface` still always returns `E_NOINTERFACE`, even for its own `IID_IDirectPlay2A` — it
  has no self-identity success path at all. This means the pre-existing plan task "`AddRef()` in
  both `DirectPlayImpl` and `DirectPlay2AImpl`" only fully applies to `DirectPlayImpl` right now;
  a new task was added to `plan.md` Phase 1 to give `DirectPlay2AImpl` a self-QI success path
  before that older task can be considered fully satisfied.
- Updated `plan.md` checkboxes for all of the above, including full reasoning for why the "both"
  wording in the `AddRef()` task doesn't fully apply yet — this is intentional honesty, not an
  oversight.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2/9 land player/session state.
- New: `DirectPlay2AImpl::QueryInterface` self-identity success path (see above) — not started.
- The rest of Phase 1 (`DirectPlayCreate` output initialization and `DPERR_NOAGGREGATION`,
  enumeration-callback documentation and the fake-provider design decision, the four new
  `src/directplay/*` scaffolding files) has not been started.

## Files inspected/changed this batch

- Changed: `include/dplay.h` (new `IID_IDirectPlay` constant), `src/directplay/DirectPlay.cpp`
  (`IsEqualGuid` helper, rewritten `DirectPlayImpl::QueryInterface`), `plan.md` (checkboxes + one
  new task).
- Scratch-only, not committed: a throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — see prior batch note; unrelated to this
  change, blocks build-verifying *any* change here).
- This batch's change was verified more strongly than the previous one: not just
  `g++ -fsyntax-only`, but an actual compiled-and-executed runtime check of the refcounting and
  dispatch logic (see "Completed this batch"). Still no committed automated test — Phase 15
  (test infrastructure) has not been started.

## Recommended next tasks

1. Give `DirectPlay2AImpl::QueryInterface` a self-identity success path (the newly-added Phase 1
   task above) — natural continuation of the same `QueryInterface` cluster of work.
2. Fix `DirectPlayCreate`'s output initialization (`*lplpDP = nullptr` before any failure path) and
   its `DPERR_NOAGGREGATION` rejection code — the next two unstarted Phase 1 tasks, independent of
   the `QueryInterface` work.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from the prior batch, still weakening how strongly
   any Phase 1+ task can be verified.
4. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
5. Commit this batch's changes (`include/dplay.h`, `src/directplay/DirectPlay.cpp`, `plan.md`,
   this `NEXT.md`).

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
