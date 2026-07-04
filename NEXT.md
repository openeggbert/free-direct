# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `2e17265` ("Add FREE_DIRECT_ENABLE_ENET CMake infrastructure
  (plan.md Phase 5, partial)").
- On top of that, one more change is now made and **not yet committed**: a real ENet copy is now
  vendored as a git submodule, and the vendored-ENet build path is genuinely verified end-to-end
  (not just its failure path, as in the prior batch). See "Completed this batch" below.
- `plan.md` Phase 0-4 are complete (Phase 1 modulo two intentionally-deferred tasks). Phase 5 has
  5 of its 24 tasks done, but the ENet-availability question that blocked full verification of
  those 5 is now resolved (real ENet is vendored and builds/links/functions correctly).
- No DirectPlay behavior changed in this batch. No DirectDraw/DirectSound code touched. No game
  source in `../free-eggbert` or `../planetblupi` touched.

## Completed this batch (real ENet vendored; Phase 5's CMake tasks now fully verified)

- **Asked the user** whether to vendor a real ENet for full build verification, since the prior
  batch could only verify the two *failure* paths of the ENet CMake detection logic (no real ENet
  existed anywhere in this environment). The user chose: vendor as a git submodule.
- **Added `third_party/enet` as a real git submodule** (`git submodule add
  https://github.com/lsalzman/enet third_party/enet`), pinned at `v1.3.18-17-g5a9c537`. Confirmed
  network access to GitHub first (`git ls-remote`) before attempting the clone.
- **Found and fixed a real bug during verification, not before:** upstream ENet's own
  `CMakeLists.txt` adds its include directory via the old directory-scoped
  `include_directories()`, not `target_include_directories()`, so it was **not** carried as a
  usage requirement of the `enet` CMake target — `free-direct` (a consumer outside ENet's own
  directory scope) would not have seen `enet/enet.h` without an explicit fix. Added
  `target_include_directories(enet PUBLIC .../third_party/enet/include)` immediately after
  `add_subdirectory(third_party/enet EXCLUDE_FROM_ALL)` to correct this.
- **Verified the vendored path completely end-to-end**, for real: configured a fresh build
  directory with `-DFREE_DIRECT_ENABLE_ENET=ON` (and `-DFREE_USE_SYSTEM_SDL=ON` for SDL) —
  succeeded. Built it — ENet's own C sources compiled and linked into `libenet.a`, and
  `free-api`/`free-direct`/`FREE_DIRECT` all built on top of it, all four targets linking
  successfully.
- **Went one step further than "it links"**: wrote a standalone smoke test (compiled and run
  outside the repository, not committed) that calls `enet_initialize()`, `enet_host_create()`,
  `enet_host_destroy()`, and `enet_deinitialize()` against the vendored copy — all succeeded,
  confirming real ENet *functionality*, not just successful linkage.
- **What remains unverified, honestly**: the *system*-ENet success path (`-DFREE_DIRECT_USE_SYSTEM_ENET=ON`
  actually finding a real `libenet` package) is still unverified, since no such system package was
  installed in this environment — the user's choice was specifically to vendor, not install
  system-wide. Only that one path was pursued.
- Updated `plan.md`'s existing notes for the CMake detection tasks with this follow-up
  verification, rather than opening new duplicate checkboxes for already-checked tasks.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- The remaining ~19 Phase 5 tasks (the `EnetDirectPlayTransport` class skeleton, init/shutdown,
  host/client creation, peer connect/disconnect, reliable send, channel layout decision, and the
  entire wire packet header) have not been started — but are now genuinely buildable/testable
  against real ENet, removing the main uncertainty from the prior batch.
- The system-ENet success path remains unverified (see above) — not currently a blocker, since the
  vendored path is now the proven, working default for this environment.

## Files inspected/changed this batch

- New: `.gitmodules`, `third_party/enet` (git submodule, pinned commit).
- Changed: `CMakeLists.txt` (added the missing `target_include_directories(enet PUBLIC ...)`
  fix), `plan.md` (follow-up verification notes on already-checked tasks).
- Scratch-only, not committed: a standalone ENet host-creation smoke test under the session
  scratchpad directory, deleted after use.

## Build/test status

- **The full CMake build now works with `FREE_DIRECT_ENABLE_ENET=ON` and a real ENet present**,
  verified from a fresh build directory: `enet`, `free-api`, `free-direct`, and `FREE_DIRECT` all
  build and link successfully.
- The default (`FREE_DIRECT_ENABLE_ENET=OFF`) build continues to work unaffected (re-confirmed
  implicitly — the ENet block is entirely skipped when the option is off, and no other
  `CMakeLists.txt` code changed outside that block in this batch).
- `tests/directplay_tests.cpp` was not re-run this batch (no DirectPlay logic changed); its last
  confirmed state is 7/7 passing, from the Phase 4 batch.

## Recommended next tasks

1. Add the `EnetDirectPlayTransport` class skeleton (`src/directplay/EnetDirectPlayTransport.hpp`/
   `.cpp`), implementing `IDirectPlayTransport` with method bodies to be filled in by later tasks —
   now fully buildable and testable against the real vendored ENet.
2. Add ENet initialization/shutdown handling (`enet_initialize`/`enet_deinitialize`, once per
   process) as the skeleton's first real behavior.
3. Independent of ENet: start the internal wire packet header (protocol version, magic number,
   application GUID, session GUID, sender/recipient player ID, payload length, defensive size
   validation) as a pure data structure — still needs no ENet at all, and directly satisfies the
   phase's "packet header round-trip" acceptance criterion.
4. Decide the default ENet channel layout (a design decision, not code).
5. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
6. Commit this batch's changes (`.gitmodules`, `third_party/enet`, `CMakeLists.txt`, `plan.md`,
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

**Phase 5 — ENet CMake option/detection/policy (commit `2e17265`):** `FREE_DIRECT_ENABLE_ENET`/
`FREE_DIRECT_USE_SYSTEM_ENET` options added; both failure paths and the default-off path verified
(no real ENet available yet at that point).

**Real ENet vendored; vendored path fully verified (this batch, not yet committed):** see
"Completed this batch" above.
