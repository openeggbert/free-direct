# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `6480493` ("Document DirectPlayEnumerateA/W stub status;
  decide the fake-provider fix").
- On top of that, one more change is now made and **not yet committed**: the four Phase 1
  scaffolding files exist and are wired into the build. See "Completed this batch" below.
- **`plan.md` Phase 1 is now effectively complete.** Only 2 of its tasks remain unchecked, and
  both are intentionally deferred by their own wording, not oversights: (1) the enumeration
  UI-behavior follow-up test, gated on the fake-provider *implementation* which is deferred to
  Phase 8; (2) moving `DirectPlay2AImpl`/`DirectPlayImpl` into dedicated files, explicitly gated on
  Phase 2+ giving them real state. Every other Phase 1 task is done and committed/about to be
  committed.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 1 scaffolding — closes out Phase 1)

- **Created `src/directplay/DirectPlaySession.hpp`/`.cpp`**: an empty `DirectPlaySession` class
  (defaulted constructor/destructor only, no members), in a new `free_direct_directplay` namespace
  matching the existing flat, project-prefixed namespace convention already used by
  `free_direct_diag` (`Diagnostics.hpp`). Not yet included/used anywhere — real session/host/
  player-count state lands in Phase 2.
- **Created `src/directplay/DirectPlayPlayer.hpp`/`.cpp`** and
  **`src/directplay/DirectPlayMessageQueue.hpp`/`.cpp`**: same empty-scaffolding pattern, for
  Phase 2/9 and Phase 3 respectively.
- **Created `src/directplay/DirectPlayTransport.hpp`** declaring `IDirectPlayTransport`, an
  abstract interface (`Listen`/`Connect`/`Send`/`Receive`/`Shutdown`, all pure virtual), explicitly
  documented as a first pass whose signatures may be refined once Phase 4 implements the first
  concrete backend (`LoopbackDirectPlayTransport`). Header-only by design — a pure interface has
  nothing to compile into a `.cpp`.
- **Updated `CMakeLists.txt`** to add the three new `.cpp` files to `free-direct`'s
  `target_sources`. Only three, not four — `DirectPlayTransport.hpp` is intentionally not listed,
  matching the existing convention that only `.cpp` files appear there (e.g. `Diagnostics.hpp`
  isn't listed, only `Diagnostics.cpp` is). The corresponding `plan.md` task's own "four" wording
  was corrected in-place rather than followed blindly.
- **Verified all four new files**: each new `.cpp` syntax-checks clean with
  `-Wall -Wextra -Wpedantic`; all three compile to object files and link into a static library
  together with no ODR conflicts; `DirectPlayTransport.hpp`'s interface was verified implementable
  via a throwaway mock class (compiled and run outside the repo, not committed) that overrides all
  five pure virtuals and is called through a base-class reference. Re-confirmed the pre-existing
  `src/directplay/DirectPlay.cpp` still compiles unaffected.
- Updated `plan.md`: checked all five scaffolding-cluster tasks, each with a note on exactly what
  was created/verified and any correction to the task's own wording (the "four `.cpp` files"
  point above).

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2 lands player/session state into the new
  scaffolding files.
- The two intentionally-deferred Phase 1 tasks noted above (see "Current state") remain open by
  design, not as blockers.
- Phase 2 (DirectPlay state model) has not been started — the scaffolding files created this batch
  are still empty shells with no real members or logic.

## Files inspected/changed this batch

- New: `src/directplay/DirectPlaySession.hpp`, `src/directplay/DirectPlaySession.cpp`,
  `src/directplay/DirectPlayPlayer.hpp`, `src/directplay/DirectPlayPlayer.cpp`,
  `src/directplay/DirectPlayMessageQueue.hpp`, `src/directplay/DirectPlayMessageQueue.cpp`,
  `src/directplay/DirectPlayTransport.hpp`.
- Changed: `CMakeLists.txt` (three new sources added), `plan.md` (checkboxes).

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- This batch's new files were verified more thoroughly than usual, since they're new
  compilation units rather than edits to an existing one: individual `-fsyntax-only`
  `-Wall -Wextra -Wpedantic` checks (all clean), individual compilation to `.o` files, and a link
  of all three into a static archive to rule out ODR conflicts between the new
  `free_direct_directplay`-namespaced classes. `IDirectPlayTransport` was additionally verified
  implementable via a throwaway mock. Still no committed automated test — Phase 15 (test
  infrastructure) has not been started.

## Recommended next tasks

1. Start `plan.md` Phase 2 (DirectPlay state model) — the natural next phase now that Phase 1's
   scaffolding exists to hold the state Phase 2 adds. Recommend starting with the `DirectPlayObjectState`
   enum and the `DirectPlaySession` fields it gates, since most other Phase 2 tasks depend on them.
2. Before landing DPID-allocation-related Phase 2 fields, make the explicit DPID-size decision
   flagged in `docs/directplay-callsite-audit.md` §5 — this blocks doing Phase 9's DPID work
   correctly later and is cheaper to decide before code depends on the choice.
3. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches, and will matter more once
   Phase 2 starts wiring these scaffolding files into the actual `DirectPlay.cpp` implementation.
4. Commit this batch's changes (four new scaffolding files, `CMakeLists.txt`, `plan.md`, this
   `NEXT.md`).

---

### Prior batches (preserved for history)

**Phase 0 — Repository and call-site audit (commit `68f4643`):** confirmed both sibling
repositories and their exact HEAD commits; produced a full inventory of `include/dplay.h` and
`src/directplay/DirectPlay.cpp`; confirmed exactly which DirectPlay APIs `free-eggbert` calls and
that `planetblupi` has zero DirectPlay usage; found that `free-eggbert`'s DirectPlay lobby/session
UI is unwired; found a DPID size mismatch hazard; corrected an imprecise `Restore()` call-site
count. Full detail in `docs/directplay-callsite-audit.md`.

**Phase 1 — `QueryInterface` cluster (commits `8674dc3`, `9b4180c`, `af6d336`):** null-pointer
handling fixed in both `QueryInterface` overrides; added a real `IID_IDirectPlay` constant;
`DirectPlayImpl::QueryInterface` now dispatches on `riid` instead of always succeeding;
`DirectPlay2AImpl::QueryInterface` now has a self-identity success path.

**Phase 1 — `DirectPlayCreate` cluster (commit `e3acac6`):** output pointer safely initialized on
every failure path; `pUnkOuter != nullptr` now returns the new `DPERR_NOAGGREGATION`.

**Phase 1 — Enumeration documentation + design decision (commit `6480493`):** expanded
`DirectPlayEnumerateA`/`W` doc comments; created `docs/directplay-design.md` recording the decision
that enumeration must eventually report one fake provider, because `free-eggbert`'s
`CNetwork::CreateProvider` unconditionally fails otherwise. No behavior change; implementation
deferred to Phase 8.
