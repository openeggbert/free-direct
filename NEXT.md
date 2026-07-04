# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `68f4643` ("Complete Phase 0 DirectPlay call-site audit; add
  NEXT.md").
- On top of that, one more small change is now made and **not yet committed**: the first task of
  `plan.md` Phase 1 (`QueryInterface` null-pointer handling) has been implemented in
  `src/directplay/DirectPlay.cpp`. See "Completed this batch" below.
- `plan.md` Phase 0 is fully complete (see prior batch summary, preserved below). Phase 1 has
  exactly one of its tasks done so far; the rest of Phase 1 has not been started.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 1, task 1 of many)

- **Fixed `QueryInterface` null-pointer handling** in both `DirectPlay2AImpl::QueryInterface` and
  `DirectPlayImpl::QueryInterface` (`src/directplay/DirectPlay.cpp`): both now return
  `DPERR_INVALIDPARAMS` when `ppvObject == nullptr`, before touching it, instead of
  `DirectPlayImpl` dereferencing it unconditionally (`DirectPlay2AImpl` did not previously
  dereference it, but the null check was added there too since Phase 1 names both methods and the
  guard is needed once a later Phase 1 task makes `DirectPlay2AImpl::QueryInterface` actually
  populate `*ppvObject`).
- This closes one of the two `QueryInterface` correctness bugs found during the Phase 0 audit
  (`docs/directplay-callsite-audit.md` §1.2). The other bug from that section —
  `DirectPlayImpl::QueryInterface` unconditionally *succeeding* for any requested `riid` — is
  **not yet fixed**; that is covered by two separate, not-yet-started Phase 1 tasks ("Define a
  real internal `IID_IDirectPlay` constant" and "Ensure `DirectPlayImpl::QueryInterface` returns
  `DPERR_NOINTERFACE`/`E_NOINTERFACE` for any `riid` other than...").
- Updated `plan.md`'s checkbox for this one task to `[x]` with a note on how it was verified.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2/9 land player/session state.
- The rest of Phase 1 (11 remaining tasks: real `IID_IDirectPlay` constant, `E_NOINTERFACE` for
  unrecognized `riid`, `AddRef()` on success, `DirectPlayCreate` output initialization and
  `DPERR_NOAGGREGATION`, enumeration-callback documentation and design decision, the four new
  `src/directplay/*` scaffolding files) has not been started.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (two `QueryInterface` overrides), `plan.md` (one
  checkbox).
- No other files were read or changed in this batch beyond what was needed to make and verify this
  edit.

## Build/test status

- **Could not run the full linked CMake build in this environment.** `cmake ..` in
  `cmake-build-debug/` fails at configure time with: `Missing vendored dependency 'SDL' in
  .../free-direct/third_party. Run: git submodule update --init --recursive` (from
  `free-eggbert/cmake/ThirdPartySDL.cmake:16`, invoked via `free-api/CMakeLists.txt:43`). This is a
  pre-existing environment/submodule-checkout issue, unrelated to this change — it would block
  building *any* change to this project in this environment, not just this one.
- **Verified the change compiles** via a standalone `g++ -std=c++20 -fsyntax-only -Wall -Wextra`
  check of `src/directplay/DirectPlay.cpp` against the real `include/dplay.h` and free-api's
  compatibility `windows.h` (`-I include -I ../free-api/include -I ../free-api/include_non_windows`).
  Zero new errors or warnings versus the pre-existing baseline (the only warnings shown are
  pre-existing: a missing-field-initializer warning on `IID_IDirectPlay2A`'s placeholder GUID, and
  an unused `riid` parameter that was already unused before this change).
- **No automated unit test exists for this yet** — DirectPlay test infrastructure is `plan.md`
  Phase 15, not started. This task is verified by compilation only, not by a passing test, and is
  recorded as such in `plan.md`.
- To get a real linked build working in this environment, someone needs to run
  `git submodule update --init --recursive` in `../free-eggbert` (or otherwise provide vendored
  SDL3) before any further FreeDirect change can be build-verified end-to-end.

## Recommended next tasks

1. Continue Phase 1: fix `DirectPlayImpl::QueryInterface`'s unconditional-success bug (define
   `IID_IDirectPlay`, then return `E_NOINTERFACE`/`DPERR_NOINTERFACE` for anything else, then add
   the missing `AddRef()` call on success) — this is the natural next atomic task and closes the
   second half of the Phase 0 `QueryInterface` finding.
2. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — without it, every future Phase 1+ task can only be
   syntax-checked, not fully build/test-verified, which weakens the "don't mark done unless it
   builds and tests pass" rule in `CLAUDE.md`.
3. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Commit this batch's changes (`src/directplay/DirectPlay.cpp`, updated `plan.md`, this
   `NEXT.md`).

---

### Prior batch summary (Phase 0 — Repository and call-site audit, commit `68f4643`)

All Phase 0 tasks in `plan.md` were completed and committed. Highlights (full detail in
`docs/directplay-callsite-audit.md`):

- Confirmed both sibling repositories exist, recorded exact HEAD commits
  (`free-eggbert@dae5652f`, `planetblupi@db61ffe7`).
- Full inventory of `include/dplay.h` and `src/directplay/DirectPlay.cpp`, surfacing the two
  `QueryInterface` correctness bugs referenced above.
- Confirmed exactly which DirectPlay APIs `free-eggbert` calls, and that `planetblupi` has zero
  DirectPlay usage.
- **Found that `free-eggbert`'s DirectPlay lobby/session UI (`WM_PHASE_DP_*` handlers in
  `event.cpp`) is unwired** — ten empty placeholder bodies, and `EnumSessions`/`Open`/
  `CreatePlayer`/provider enumeration have zero reachable callers anywhere in the source snapshot.
  Only `Send`/`Receive` are confirmed reachable/exercised.
- **Found a DPID size mismatch hazard**: FreeDirect's `DPID` is `DWORD_PTR` (8 bytes on 64-bit);
  real DirectPlay's is `DWORD` (4 bytes). `free-eggbert`'s `NetSearchPlayer`/`NetStartPlay` use
  hardcoded 32-byte pointer-arithmetic strides that assume the 4-byte layout — a real
  correctness/ABI risk on 64-bit builds, not yet resolved.
- Corrected an imprecise `Restore()` call-site count (5 real calls per game, not 8).
