# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `474b78f` ("Fix SDL3 CMake target visibility so the full build
  actually works").
- On top of that, one more change is now made and **not yet committed**: the `FREE_DIRECT_ENABLE_ENET`
  CMake infrastructure exists and is verified via three real configurations. See "Completed this
  batch" below.
- `plan.md` Phase 0-4 are complete (Phase 1 modulo two intentionally-deferred tasks). **Phase 5
  (ENet integration planning) has started**: 5 of its 24 tasks are done (the CMake option/
  detection/policy cluster). Not started: the `EnetDirectPlayTransport` class skeleton and
  everything that depends on it (host/client creation, send/receive, the wire packet header).
- No DirectPlay behavior changed in this batch (no `.cpp`/`.hpp` DirectPlay source touched) —
  this was pure CMake/build-infrastructure work.
- No DirectDraw/DirectSound code has been touched. No game source in `../free-eggbert` or
  `../planetblupi` has been touched.

## Completed this batch (Phase 5 — ENet CMake option/detection/policy cluster)

- **Added `FREE_DIRECT_ENABLE_ENET`** (default `OFF`) and **`FREE_DIRECT_USE_SYSTEM_ENET`**
  (default `OFF`) options to `CMakeLists.txt`.
- **Vendored-ENet detection**: when enabled without the system flag, checks for
  `third_party/enet/CMakeLists.txt` and fails with a specific, actionable message (naming the
  expected path and `https://github.com/lsalzman/enet`) rather than a generic CMake error if
  missing — matching the existing `ThirdPartySDL.cmake` convention already used elsewhere in this
  ecosystem, rather than `FetchContent`.
- **System-ENet detection**: `pkg_check_modules(... REQUIRED IMPORTED_TARGET libenet)` — ENet has
  no upstream CMake config package, only a pkg-config module (`libenet`, the name Debian/Ubuntu's
  `libenet-dev` ships).
- Both detection paths converge on one internal `FreeDirect::ENet` ALIAS target, linked as
  `PRIVATE` to `free-direct` — its include directories/usage requirements never propagate to
  anything linking against `free-direct` (`CLAUDE.md`'s Internal Backend Policy).
- **Confirmed via `grep -rliE "enet" include/`** (zero matches) that no header under `include/`
  references ENet in any way — the "review-time check" task.
- **Verified all three reachable configurations for real**, in fresh temporary build directories,
  not just reasoned about: (1) default (`FREE_DIRECT_ENABLE_ENET=OFF`) configures and builds
  end-to-end exactly as before; (2) `ENET=ON` with no vendored copy and no system flag fails with
  exactly the intended custom error message; (3) `ENET=ON` + `USE_SYSTEM_ENET=ON` with no
  `libenet` installed in this environment fails cleanly via CMake's own `FindPkgConfig` error.
  Also re-confirmed the actual `cmake-build-debug/` directory used all session still configures
  and builds cleanly after these changes.
- **Honestly could not verify**: no real ENet copy (vendored or system) exists anywhere in this
  environment, so neither detection method's *success* path (actually finding and linking real
  ENet) was verified end-to-end — only their failure paths, and the default-off path. Fetching a
  real ENet would require network access (git submodule / `FetchContent`) not attempted
  speculatively — see "Recommended next tasks."
- **Deliberately deferred**: the `EnetDirectPlayTransport` class skeleton and its `target_sources()`
  wiring — writing code that calls real `enet_*` functions can't be compile-verified without ENet
  headers actually present, so it's left for a later batch (see next tasks below for an
  ENet-independent alternative to work on meanwhile).
- Updated `plan.md`: checked all 5 tasks in this cluster with detailed verification notes and an
  explicit statement of what wasn't (and couldn't be) verified.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- **Decision needed from the user, not yet made**: should a real ENet copy actually be vendored
  (e.g. `git submodule add https://github.com/lsalzman/enet third_party/enet`, requiring network
  access) or installed as a system package, so the `EnetDirectPlayTransport` skeleton and its
  ENet-calling code can be genuinely compile- and run-verified in this environment? Without this,
  all future ENet-calling code in this phase can only be reasoned about, not built.
- The remaining ~19 Phase 5 tasks (skeleton class, init/shutdown, host/client creation, peer
  connect/disconnect, reliable send, channel layout decision, and the entire wire packet header)
  have not been started.

## Files inspected/changed this batch

- Changed: `CMakeLists.txt` (ENet option/detection/linkage block), `plan.md` (checkboxes).
- No new source files created this batch (the `EnetDirectPlayTransport` skeleton is deferred).

## Build/test status

- The full CMake build (from the SDL3 fix in the prior batch) continues to work. This batch added
  three more real, verified configurations on top of it (see "Completed this batch"). All
  temporary test build directories were cleaned up after verification.
- `tests/directplay_tests.cpp` was not re-run this batch (no DirectPlay logic changed); its last
  confirmed state is 7/7 passing, from the Phase 4 batch.

## Recommended next tasks

1. **Ask the user** whether to actually vendor/install a real ENet for full build verification of
   upcoming ENet-calling code, or continue reasoning-only for now.
2. **Independent of that decision**, start the internal wire packet header (protocol version,
   magic number, application GUID, session GUID, sender/recipient player ID, payload length,
   defensive size validation on receive) as a pure data structure — this needs no real ENet at
   all to implement and test, similar to how `DirectPlayMessagePacket` was built and tested before
   any transport existed. This also directly satisfies the phase's acceptance criteria ("a unit
   test serializes and deserializes the internal packet header and asserts round-trip equality").
3. Decide the default ENet channel layout (also independent of ENet actually being present —
   it's a design decision to document, not code to compile).
4. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
5. Commit this batch's changes (`CMakeLists.txt`, `plan.md`, this `NEXT.md`).

---

### Prior batches (preserved for history)

**Phase 0 — Repository and call-site audit (commit `68f4643`):** confirmed both sibling
repositories and their exact HEAD commits; full inventory of `include/dplay.h` and
`src/directplay/DirectPlay.cpp`; confirmed which DirectPlay APIs `free-eggbert` calls and that
`planetblupi` has zero DirectPlay usage; found the `free-eggbert` DirectPlay lobby/session UI is
unwired; found a DPID size mismatch hazard; corrected an imprecise `Restore()` call-site count.
Full detail in `docs/directplay-callsite-audit.md`.

**Phase 1 (commits `8674dc3`, `9b4180c`, `af6d336`, `e3acac6`, `6480493`, `53b5ba4`):**
`QueryInterface` null-handling and `riid` dispatch fixed; `DirectPlayCreate` output/aggregation
fixed; `DirectPlayEnumerateA`/`W` documented and the fake-provider decision recorded; four
scaffolding files created.

**Phase 2 (commits `37febd4`, `f67dedd`, `188cfab`, `c285550`, `efac4e6`, `c3e45a4`, `1b01a2d`):**
`DirectPlaySession` given real state; `Open`/`CreatePlayer`/`Close`/`EnumSessions`/`Send`/
`Receive`/`Release` all wired to real state. **Fully complete.**

**Phase 3 (commits `f78ee61`, `3b38e0c`, `1cde3d5`, `75d4de5`):** `DirectPlayMessagePacket` + FIFO
queue; `Receive()`/`Close()` wired to the real queue; size/oversize bounds; permanent unit tests
(`tests/directplay_tests.cpp` created). **Fully complete.**

**Phase 4 (commit `2509b5d`):** `LoopbackDirectPlayTransport` implemented and wired into
`Open`/`Close`/`Send`; four more permanent tests added (7 total). **Fully complete.**

**SDL3 build blocker resolved (commit `474b78f`):** fixed CMake target-visibility bug preventing
any full build in this environment; documented `-DFREE_USE_SYSTEM_SDL=ON` in `README.md`.

**Phase 5 — ENet CMake option/detection/policy (this batch, not yet committed):** see "Completed
this batch" above.
