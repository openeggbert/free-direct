# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy. It is created now
because Phase 0 of `plan.md` — the first real `plan.md` work batch — has just been completed.

## Current state

- Branch: `develop`.
- Last commit on `develop`: `fc13d9a` ("Add CLAUDE.md project charter and plan.md
  DirectPlay-focused task plan").
- This batch's changes (Phase 0 audit) are made on top of that commit and are **not yet
  committed** as of writing this file — see "Build/test status" below for what was and was not
  done.
- No DirectPlay runtime code was touched in this batch. No DirectDraw/DirectSound code was
  touched. No game source in `../free-eggbert` or `../planetblupi` was touched. This was a
  documentation/investigation-only batch, matching the scope given for this run (Phase 0 only).

## Completed this batch (Phase 0 — Repository and call-site audit)

All Phase 0 tasks in `plan.md` are now checked except one guard task that did not trigger (see
"Blocked/incomplete" below). Highlights, all cited to exact files/lines in
`docs/directplay-callsite-audit.md`:

- Confirmed both sibling repositories exist and recorded their exact HEAD commits for
  reproducibility (`free-eggbert@dae5652f`, `planetblupi@db61ffe7`).
- Produced a full inventory of `include/dplay.h` declarations and `src/directplay/DirectPlay.cpp`
  stub behavior, including two previously-unremarked `QueryInterface` correctness bugs
  (`DirectPlay2AImpl::QueryInterface` rejects every `riid` unconditionally;
  `DirectPlayImpl::QueryInterface` accepts every `riid` unconditionally).
- Confirmed, with exact citations, which DirectPlay APIs `free-eggbert` calls: `QueryInterface`,
  `DirectPlayEnumerateA/W`, `EnumSessions`, `Open` (both `DPOPEN_CREATE` and `DPOPEN_OPENSESSION`),
  `CreatePlayer`, `Send`, `Receive`, `Close`. Confirmed no groups, no lobby APIs, no
  `IDirectPlay3A` usage anywhere.
- Confirmed `planetblupi` has zero DirectPlay usage of any kind — it is single-player only.
- **New finding, not in the original plan baseline:** the `WM_PHASE_DP_*` UI state-machine
  handlers in `free-eggbert/src/event.cpp` that would drive session hosting/joining/enumeration
  from user interaction are empty `// ...` placeholders (ten of them). This means
  `EnumSessions`/`Open`/`CreatePlayer`/service-provider enumeration are implemented in `CNetwork`
  but have **zero reachable callers** anywhere in the current `free-eggbert` source snapshot. Only
  the gameplay-time `Send`/`Receive` path (exercised once a match is already running) is confirmed
  reachable. This lowers confidence in "expected UI behavior on zero sessions / init failure" —
  those are now documented as verified only at the `CNetwork` level, with the UI-level behavior
  explicitly marked unverified/blocked rather than assumed.
- **New finding, more severe than the previously-known DPID-vs-array-index issue:** FreeDirect's
  `DPID` typedef (`DWORD_PTR`, 8 bytes on 64-bit) diverges from real DirectPlay's `DWORD` (4
  bytes). `free-eggbert/src/event.cpp`'s `NetSearchPlayer` and `NetStartPlay` walk arrays of
  `NetPlayer` using hardcoded 32-byte pointer-arithmetic strides that only match the original
  4-byte `DPID` layout. Building `free-eggbert` against FreeDirect's current `dplay.h` on a
  64-bit target would silently corrupt these lookups. This is **not fixed** (out of scope for this
  batch, and game source must not be modified) — it is recorded so Phase 1/2 make an explicit,
  documented decision instead of inheriting a silent bug.
- Corrected an imprecise prior informal count: real `IDirectDrawSurface::Restore()` call sites are
  5 in each game (not 8), once wrapper-method definitions and an unrelated `MouseBackRestore()`
  helper are excluded from the grep.
- Re-verified all other DirectDraw/DirectSound call-site counts cited in `CLAUDE.md`/`plan.md`
  (`BltFast`/`Blt`/`GetDC`/`ReleaseDC`/`IsLost`, `DSBPLAY_LOOPING`, `SetPan`) — all confirmed
  accurate as previously stated.
- Wrote `docs/directplay-callsite-audit.md` (new file) with all findings, each cited to a concrete
  file and function/line, including an audit-provenance table and a Phase-0-verdict summary table.
- Updated `plan.md`'s Phase 0 checkboxes to reflect what was genuinely verified, with inline notes
  where the real finding refined or corrected the plan's original baseline text.

## Blocked / incomplete

- One Phase 0 guard task — "if either sibling repository does not exist, stop and record
  blocked/partial" — did not trigger, since both `../free-eggbert` and `../planetblupi` were
  present. Left unchecked in `plan.md` rather than marked done, since the described action never
  ran (this is not a real blocker, just an inapplicable conditional).
- Genuinely open items carried into later phases (not blockers for Phase 0 itself, which is
  complete): the DPID-size decision (§5 of the audit doc) and the UI-reachability caveats (§2.3)
  both need explicit resolution in Phase 1/2/9, not silent handling.

## Files inspected this batch

- `include/dplay.h`, `src/directplay/DirectPlay.cpp` (this repository).
- `../free-eggbert`: `include/network.hpp`, `include/event.hpp`, `src/network.cpp`,
  `src/decnet.cpp`, `src/event.cpp`, plus whole-repository greps across all 23 files in `src/` and
  the vendored `dxsdk3/sdk/inc/dplay.h`/`dplobby.h` reference headers (read-only, for the real
  Microsoft `DPID` size comparison).
- `../planetblupi`: `src/pixmap.cpp`, `src/ddutil.cpp`, `src/sound.cpp`, `src/blupi.cpp`, plus
  whole-repository greps across `src/`/`include/`.
- No files were modified in either sibling repository.

## Build/test status

- Not applicable this batch — no source code was changed in `free-direct`, `free-eggbert`, or
  `planetblupi`. Nothing was built or tested, because there was nothing to build or test; this was
  a documentation/investigation-only phase, matching the "do not implement DirectPlay yet" scope
  given for this run.
- The last known build/test status of `free-direct` itself is unchanged from before this batch
  (no CI/test infrastructure exists yet — that is `plan.md` Phase 15 work, not yet started).

## Recommended next tasks

1. Start `plan.md` Phase 1 (DirectPlay API boundary cleanup). Prioritize the two `QueryInterface`
   correctness bugs found in this audit (`docs/directplay-callsite-audit.md` §1.2) since they are
   simple, well-understood, and already fully specified by Phase 1's existing tasks.
2. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5 (keep `DWORD_PTR` and accept 64-bit incompatibility with
   `free-eggbert`'s raw pointer-arithmetic helpers, or switch `DPID` back to a 4-byte type). Write
   the decision down in `docs/directplay-design.md` (created in Phase 16, but the decision itself
   should not wait that long — consider drafting a placeholder note earlier if Phase 2/9 work
   starts before Phase 16).
3. Given the UI-reachability findings in §2.3, treat `Send`/`Receive` (confirmed reachable) as the
   highest-confidence, highest-priority behavioral target for Phases 2-3-10-11; treat
   `EnumSessions`/`Open`/`CreatePlayer`/service-provider enumeration as correct-to-spec but
   currently unverifiable end-to-end against real `free-eggbert` UI behavior until/unless that
   game's `WM_PHASE_DP_*` handlers are filled in (not FreeDirect's job to fill in — game source is
   out of scope).
4. Commit this batch's changes (`docs/directplay-callsite-audit.md`, updated `plan.md`, this
   `NEXT.md`) as its own commit, separate from any future Phase 1 code changes.
