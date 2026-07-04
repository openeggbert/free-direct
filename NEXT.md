# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `c3e45a4` ("Wire EnumSessions(), Send(), and Receive() to real
  DirectPlaySession state").
- On top of that, one more change is now made and **not yet committed**: `Release()` now safely
  tears down a (currently always-null) transport. See "Completed this batch" below.
- **`plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), and Phase 2 are all
  fully complete** — Phase 2 has zero remaining unchecked tasks as of this batch.
  `DirectPlay2AImpl` now has a real `DirectPlaySession` driving `Open`/`CreatePlayer`/`Close`/
  `EnumSessions`/`Send`/`Receive`/`Release`, all with meaningful state transitions and error codes
  instead of unconditional `DP_OK` stubs.
- **Phase 3 (message queue semantics) has not been started.** `DirectPlayMessageQueue` is still
  an empty scaffold; `Receive()` currently returns `DPERR_NOMESSAGES` unconditionally (correctly,
  since there is genuinely no queue yet), which is exactly the behavior Phase 3 needs to build
  real FIFO semantics on top of.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 2 — `Release()` transport shutdown; closes Phase 2)

- **Added `std::unique_ptr<IDirectPlayTransport> transport`** to `DirectPlaySession` (always null
  today — no concrete backend exists until `LoopbackDirectPlayTransport` in Phase 4 /
  `EnetDirectPlayTransport` in Phase 5).
- **`Release()` on `DirectPlay2AImpl`** now calls `session_.transport->Shutdown()` when non-null,
  before `delete this`. Currently a no-op in practice (transport is always null), but the correct,
  safe shutdown call is now in place and will activate automatically once a future phase
  constructs and assigns a real transport during `Open()` — no further change to `Release()`
  itself should be needed then.
- **Verification was split into two parts, deliberately, since there's no way yet to inject a
  transport into a real `DirectPlay2AImpl`** (that only becomes possible once Phase 4/5 give
  `Open()` logic to construct one): (1) a throwaway scratch harness confirmed `Release()`'s
  refcounting/deletion still works correctly with the always-null transport; (2) a second,
  separate scratch check attached a standalone mock `IDirectPlayTransport` directly to a bare
  `DirectPlaySession` (bypassing `DirectPlay2AImpl`, since there's no injection path) to confirm
  the null-check-then-`Shutdown()` pattern itself is correct. Both are honestly reported as
  distinct, partial verifications in `plan.md`, not conflated into a single "fully tested" claim.
- Updated `plan.md`: checked the final Phase 2 task with this two-part verification note. **Phase
  2 now has zero remaining unchecked tasks.**

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
  Should be resolved before Phase 9 does real DPID allocation.
- Phase 1's two intentionally-deferred tasks remain open by design (gated on Phase 2/8 work that
  is itself now mostly done for one of them — worth re-checking whether the enumeration
  UI-behavior follow-up test is unblockable yet, though its actual gate is the fake-provider
  *implementation*, which is still Phase 8, not yet started).
- Phase 3 (message queue semantics) has not been started at all.

## Files inspected/changed this batch

- Changed: `src/directplay/DirectPlay.cpp` (`Release()` rewritten),
  `src/directplay/DirectPlaySession.hpp` (`transport` field, updated doc comment),
  `src/directplay/DirectPlayTransport.hpp` (updated doc comment), `plan.md` (checkbox).
- Scratch-only, not committed: a throwaway two-part runtime test harness under the session
  scratchpad directory, deleted after use.

## Build/test status

- Still cannot run the full linked CMake build in this environment (vendored SDL3 submodule under
  `../free-eggbert/third_party` is not checked out — unrelated to this change, carried over from
  prior batches, still unresolved).
- Verified via `g++ -fsyntax-only -Wall -Wextra -Wpedantic` (clean) plus two actual
  compiled-and-executed runtime checks (see "Completed this batch"). Still no committed automated
  test — Phase 15 (test infrastructure) has not been started. Every Phase 2 behavior change this
  session has been verified this way; none were marked done on syntax-checking alone.

## Recommended next tasks

1. Start `plan.md` Phase 3 (message queue semantics): implement `DirectPlayMessagePacket`
   (source/destination DPID, flags, payload bytes) in `DirectPlayMessageQueue`, then a FIFO
   receive queue, then wire `Receive()`'s buffer-size-query and copy-out behavior to it. This is
   the natural next phase now that Phase 2 is fully closed.
2. Investigate and fix the missing SDL3 submodule checkout so the real CMake/ninja build can run
   end-to-end in this environment — still open from every prior batch, and will matter
   increasingly as more of DirectPlay is implemented without ever running the real build.
3. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
4. Commit this batch's changes (`src/directplay/DirectPlay.cpp`,
   `src/directplay/DirectPlaySession.hpp`, `src/directplay/DirectPlayTransport.hpp`, `plan.md`,
   this `NEXT.md`).

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

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`, `efac4e6`, `c3e45a4`):**
`DirectPlaySession` given real data-model fields; `dwSize`/`dwFlags` validation added;
`Open`/`CreatePlayer`/`Close`/`EnumSessions`/`Send`/`Receive` all wired to real state with
meaningful `DPERR_*` codes.

**Phase 2 — `Release()` transport shutdown (this batch, not yet committed):** see "Completed this
batch" above. **Closes Phase 2.**
