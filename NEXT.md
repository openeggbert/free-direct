# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `e3acac6` ("Fix DirectPlayCreate output initialization and
  aggregation error code").
- On top of that, one more change is now made and **not yet committed**: `DirectPlayEnumerateA`/
  `W` are now documented honestly, and the enumeration design decision they depend on has been
  made and recorded in a new `docs/directplay-design.md`. See "Completed this batch" below.
- `plan.md` Phase 0 is fully complete. Phase 1 now has 10 tasks done (the `QueryInterface` cluster,
  the `DirectPlayCreate` cluster, and the enumeration-documentation/decision cluster). Remaining
  Phase 1 work: the four new `src/directplay/*` scaffolding files, plus the still-pending
  follow-up task to test enumeration behavior once it is actually implemented (deferred to Phase 8).
- No DirectPlay *behavior* changed in this batch — `DirectPlayEnumerateA`/`W` still return `DP_OK`
  with zero callback invocations, exactly as before. This batch is documentation plus a decision
  record, matching what the corresponding `plan.md` tasks explicitly call for.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (enumeration documentation + design decision)

- **Expanded the `@note Status:` doc comments** on `DirectPlayEnumerateA`/`DirectPlayEnumerateW`
  in `include/dplay.h` to explain, in the header itself, why "always report zero providers" is a
  temporary stub rather than a permanent design choice, and to point at the concrete finding that
  forces the eventual fix.
- **Created `docs/directplay-design.md`** (new file, started early rather than waiting for Phase
  16, since Phase 1's decision task needed somewhere to record its answer) with "Decision 1": yes,
  enumeration must eventually report exactly one fake FreeDirect provider. The reasoning is
  call-site-driven, not aesthetic: `free-eggbert/src/network.cpp`'s `CNetwork::CreateProvider` has
  a hard bound check (`if (index >= m_providers.nb) return FALSE;`) that makes it **always fail**
  when zero providers have been enumerated, and `CreateProvider` is `free-eggbert`'s only call
  path to `DirectPlayCreate`. Without this fix (eventually), `free-eggbert`'s `CNetwork` wrapper
  could never obtain a working `IDirectPlay2A` object at all, regardless of what else FreeDirect
  implements.
- The design doc explicitly defers *implementation* of this decision to Phase 8 (session
  enumeration), records an open-but-undecided idea (one provider per transport backend, once
  multiple backends exist) without committing to it, and repeats the Phase 0 caveat that
  `free-eggbert`'s actual caller of this path (`CEvent::NetEnumSessions`) is itself unreachable in
  the current source snapshot — so this fix matters for any direct/integration-test exercise of
  `CNetwork`, not provably for the shipped UI as currently reconstructed.
- Updated `plan.md`: checked all three related tasks (`DirectPlayEnumerateA` doc,
  `DirectPlayEnumerateW` doc, the decision itself), each with a note on exactly what was recorded
  and where.
- Verified via `g++ -fsyntax-only` that the doc-comment-only header changes don't break
  compilation (they don't — no code behavior changed).

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3)
  both still need explicit resolution before Phase 2/9 land player/session state.
- The fake-provider enumeration *decision* is made, but **not implemented** — `DirectPlayEnumerateA`/
  `W` behavior is unchanged. Implementation is intentionally deferred to Phase 8.
- Remaining Phase 1 work: the four new `src/directplay/DirectPlaySession.*` / `DirectPlayPlayer.*`
  / `DirectPlayMessageQueue.*` / `DirectPlayTransport.hpp` scaffolding files (not yet created).

## Files inspected/changed this batch

- Changed: `include/dplay.h` (expanded doc comments only, no declarations added/removed),
  `plan.md` (checkboxes).
- New: `docs/directplay-design.md`.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only` only, since this batch changed comments/docs, not executable
  logic — there was no new runtime behavior to scratch-test. Still no committed automated test —
  Phase 15 (test infrastructure) has not been started.

## Recommended next tasks

1. Start creating the four Phase 1 scaffolding files (`src/directplay/DirectPlaySession.*`,
   `DirectPlayPlayer.*`, `DirectPlayMessageQueue.*`, `DirectPlayTransport.hpp`) as near-empty
   classes, per the plan — this is the last unstarted cluster of Phase 1 tasks and unblocks
   Phase 2 onward.
2. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from prior batches.
3. Before Phase 2/9 land any DPID-allocation code, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Commit this batch's changes (`include/dplay.h`, `docs/directplay-design.md`, `plan.md`, this
   `NEXT.md`).

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
returning `E_NOINTERFACE`.

**Phase 1 — `DirectPlayCreate` cluster (commit `e3acac6`):** output pointer safely initialized on
every failure path; `pUnkOuter != nullptr` now returns the new `DPERR_NOAGGREGATION` instead of
being folded into `DPERR_INVALIDPARAMS`.
