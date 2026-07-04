# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `af6d336` ("Give DirectPlay2AImpl::QueryInterface a
  self-identity success path").
- On top of that, one more change is now made and **not yet committed**: `DirectPlayCreate` now
  initializes its output pointer safely and distinguishes "invalid params" from "no aggregation
  supported". See "Completed this batch" below.
- `plan.md` Phase 0 is fully complete. Phase 1's `QueryInterface` cluster (5 tasks) and the
  `DirectPlayCreate` output/aggregation cluster (2 tasks) are now done — 7 of Phase 1's tasks
  complete. Remaining ~9 Phase 1 tasks (enumeration-callback documentation and design decision,
  the four new `src/directplay/*` scaffolding files) have not been started.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (`DirectPlayCreate` output/aggregation cluster)

- **`DirectPlayCreate` now initializes `*lplpDP = nullptr`** immediately after the `!lplpDP`
  null-pointer check and before any other failure path, so a caller who ignores the `HRESULT` never
  sees an untouched/garbage pointer.
- **Added `DPERR_NOAGGREGATION` to `include/dplay.h`** (`0x88770033`, the next unused value after
  the existing `DPERR_*` sequence, which topped out at `DPERR_UNSUPPORTED = 0x88770032`), matching
  the naming convention already used by `DSERR_NOAGGREGATION` in `dsound.h`.
- **`DirectPlayCreate` now returns `DPERR_NOAGGREGATION`** for `pUnkOuter != nullptr`, separately
  from `DPERR_INVALIDPARAMS` for `!lplpDP` — previously both cases were collapsed into a single
  `DPERR_INVALIDPARAMS` check.
- **Verified with a third throwaway runtime scratch harness** (compiled and run outside the
  repository, not committed): null `lplpDP` rejected with `DPERR_INVALIDPARAMS`; non-null
  `pUnkOuter` rejected with `DPERR_NOAGGREGATION` and `*lplpDP` provably reset to `nullptr` (started
  from a poison pointer value to prove the overwrite happens); the normal success path still
  returns a working object.
- Updated `plan.md`: checked both tasks with notes on exactly what changed and how it was verified.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2/9 land player/session state.
- Remaining Phase 1 work: the enumeration-callback `@note Status:` documentation and the
  fake-provider design decision (decision-only, no behavior change), and the four new
  `src/directplay/DirectPlaySession.*` / `DirectPlayPlayer.*` / `DirectPlayMessageQueue.*` /
  `DirectPlayTransport.hpp` scaffolding files (still not created).

## Files inspected/changed this batch

- Changed: `include/dplay.h` (new `DPERR_NOAGGREGATION` constant), `src/directplay/DirectPlay.cpp`
  (`DirectPlayCreate` rewritten), `plan.md` (checkboxes).
- Scratch-only, not committed: a third throwaway runtime test harness under the session scratchpad
  directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified the same way as the last two batches: `g++ -fsyntax-only` (clean, only the pre-existing
  `_GUID` missing-field-initializer warning class) plus an actual compiled-and-executed runtime
  check. Still no committed automated test — Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Make the enumeration-callback design decision: should `DirectPlayEnumerateA`/`W` invoke their
   callback once with a fake "FreeDirect" provider so `free-eggbert`'s `CNetwork::EnumProviders`
   sees a selectable entry? This is decision-only (record it, e.g. in a placeholder note, since
   `docs/directplay-design.md` itself is Phase 16 work) — no enumeration behavior change yet.
2. Add the `@note Status:` documentation comments to `DirectPlayEnumerateA`/`W` describing their
   current behavior as an intentional interim stub pending Phase 8 — small, doc-only task.
3. Start creating the four Phase 1 scaffolding files (`src/directplay/DirectPlaySession.*`,
   `DirectPlayPlayer.*`, `DirectPlayMessageQueue.*`, `DirectPlayTransport.hpp`) as near-empty
   classes, per the plan — these unblock Phase 2 onward.
4. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
5. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
6. Commit this batch's changes (`include/dplay.h`, `src/directplay/DirectPlay.cpp`, `plan.md`,
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

**Phase 1 — `QueryInterface` cluster (commits `8674dc3`, `9b4180c`, `af6d336`):** null-pointer
handling fixed in both `QueryInterface` overrides; added a real `IID_IDirectPlay` constant;
`DirectPlayImpl::QueryInterface` now dispatches on `riid` instead of always succeeding;
`DirectPlay2AImpl::QueryInterface` now has a self-identity success path instead of always
returning `E_NOINTERFACE`. Every `QueryInterface`-related finding from the Phase 0 audit is closed.
