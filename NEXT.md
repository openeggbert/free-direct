# NEXT.md — FreeDirect Status

This is the living status file described in `CLAUDE.md`'s `NEXT.md` Policy.

## Current state

- Branch: `develop`.
- Last commit pushed to `develop`: `2509b5d` ("Implement LoopbackDirectPlayTransport; closes
  plan.md Phase 4").
- On top of that, one more change is now made and **not yet committed**: the long-standing SDL3
  build blocker is resolved. See "Completed this batch" below.
- `plan.md` Phase 0, Phase 1 (modulo two intentionally-deferred tasks), Phase 2, Phase 3, and
  Phase 4 are all fully complete. `tests/directplay_tests.cpp` has 7 passing tests.
- **Phase 5 (ENet integration planning) has not been started.**
- No DirectDraw/DirectSound code has been touched (beyond the build-config change below, which
  touches no subsystem source). No game source in `../free-eggbert` or `../planetblupi` has been
  touched.

## Completed this batch (SDL3 build blocker resolved — not a `plan.md` task, an environment fix)

**Root cause found, not guessed at:** every batch since Phase 0 noted that `cmake ..` failed with
"Missing vendored dependency 'SDL' in .../free-direct/third_party" and could only be
syntax-checked, never fully built/linked. Investigated properly this time:

- `free-direct` has no `third_party/` directory and no `.gitmodules` — it was never going to
  self-vendor SDL. `free-api`'s own `CMakeLists.txt` documents three ways it can obtain
  `SDL3::SDL3`/`SDL3_image::SDL3_image`/`SDL3_mixer::SDL3_mixer`: reuse targets a parent already
  created, `-DFREE_API_USE_SYSTEM_SDL3=ON` (`find_package`), or (developer convenience) reuse
  `../free-eggbert`'s or `../planetblupi`'s own `cmake/ThirdPartySDL.cmake` vendoring script,
  which has its own separate `-DFREE_USE_SYSTEM_SDL=ON` flag.
- **Confirmed system SDL3/SDL3_image/SDL3_mixer are already installed** in this environment (found
  via `pkg-config`, with proper CMake `Config.cmake` files under `/usr/local/lib/cmake/`).
- Passing `-DFREE_USE_SYSTEM_SDL=ON` got past the "missing vendored dependency" error (confirmed
  via `--trace-expand` that `find_package(SDL3 REQUIRED)` etc. succeeded, populating
  `SDL3_DIR`/`SDL3_image_DIR`/`SDL3_mixer_DIR` in the cache) — but `free-direct`'s own
  `CMakeLists.txt` then still failed its own `if(NOT TARGET SDL3::SDL3 ...)` check immediately
  afterward.
- **Diagnosed precisely via `--trace-expand`, not by guessing:** `find_package()`'s imported
  targets are only visible in the directory scope where `find_package()` was called and below.
  That call happens inside `free-api`'s own `add_subdirectory()` scope — a *child* of
  `free-direct`'s top-level directory — so once `add_subdirectory(../free-api FREE_API)` returns,
  the targets do not exist at all in `free-direct`'s own scope (confirmed: `if(TARGET SDL3::SDL3)`
  evaluates false there, even though the identical check succeeds inside `free-api`'s own
  `CMakeLists.txt` moments earlier). A first attempt at a fix (promoting the targets to
  `IMPORTED_GLOBAL` from the parent scope) **did not work and was corrected** — you cannot call
  `set_target_properties()` on a target that doesn't exist in the calling scope in the first
  place; that fix was replaced before being committed.
- **Actual fix, entirely within `free-direct`'s own `CMakeLists.txt`** (no changes to `free-api` or
  `free-eggbert`, both separate sibling repositories out of scope for this fix): if the targets
  are still missing after `add_subdirectory(../free-api ...)`, call `find_package(SDL3 CONFIG
  QUIET)` / `find_package(SDL3_image CONFIG QUIET)` / `find_package(SDL3_mixer CONFIG QUIET)`
  again, directly in `free-direct`'s own scope. This is fast and reliable because the nested call
  already populated the `_DIR` cache variables, so it just re-locates the same config files and
  creates the same imported targets, this time visible where needed. When `free-api` instead
  vendors SDL as a real (non-`IMPORTED`) `add_subdirectory()`'d project — the default path — these
  targets are already visible everywhere, so every `find_package()` call here is a no-op.
- **Verified for real, not just syntax-checked:** configured a completely fresh, empty build
  directory from scratch with `cmake -DFREE_USE_SYSTEM_SDL=ON <repo>` — succeeded
  (`-- Generating done`). Built it with `cmake --build . -j4` — **`free-api`, `free-direct`, and
  the `FREE_DIRECT` executable all compiled and linked successfully**, first time in this whole
  session. Re-verified the same in the actual `cmake-build-debug/` directory used throughout this
  session's prior batches (reconfigured incrementally with the same flag, then built) — same
  clean, full, successful build.
- **Documented the fix** in `README.md`'s Build Instructions section:
  `cmake -B build -DFREE_USE_SYSTEM_SDL=ON` as the documented alternative to vendoring SDL as
  submodules.

## Blocked / incomplete

- Carried over from Phase 0 (still unresolved, still relevant): the DPID-size decision
  (`docs/directplay-callsite-audit.md` §5) and the `free-eggbert` UI-reachability caveats (§2.3).
- `tests/directplay_tests.cpp` is still not wired into CTest (Phase 15) — this batch didn't change
  that, but now that the full CMake build genuinely works, wiring it in should be straightforward
  whenever Phase 15 is reached.
- **This SDL3 fix was verified with `-DFREE_USE_SYSTEM_SDL=ON` (system packages) only.** The
  vendored-submodule path (the CMake default, no flag) was not re-tested and is still expected to
  fail in this environment, since `free-direct` has no `third_party/` submodules and none of the
  sibling repos' vendored SDL was symlinked/copied in. That's fine — system SDL is a fully
  supported, now-working path — but it's worth knowing the default (no-flag) `cmake -B build`
  invocation from `README.md`'s first example still won't work standalone in *this* environment
  without also passing the flag from the second example.

## Files inspected/changed this batch

- Changed: `CMakeLists.txt` (SDL target-visibility fix, replacing an incorrect first attempt
  before it was committed), `README.md` (documented `-DFREE_USE_SYSTEM_SDL=ON`).
- No `plan.md` change — this was an environment/build-tooling fix, not a DirectPlay/DirectDraw/
  DirectSound task, so there was no corresponding checkbox to check.
- Investigated but not modified: `../free-api/CMakeLists.txt`, `../free-eggbert/cmake/
  ThirdPartySDL.cmake` (both read-only, to understand the actual root cause — deliberately not
  edited, since both are separate sibling repositories out of this fix's scope).

## Build/test status

- **The full CMake build now genuinely works end-to-end**, for the first time this session:
  `cmake -B build -DFREE_USE_SYSTEM_SDL=ON && cmake --build build` succeeds, producing
  `libfree-api.a`, `libfree-direct.a`, and the `FREE_DIRECT` executable, all linked successfully.
  Verified twice: once from a completely fresh/empty build directory, once by reconfiguring the
  actual `cmake-build-debug/` directory used throughout this session.
- `tests/directplay_tests.cpp` was not re-run in this batch (no DirectPlay logic changed) — its
  last confirmed state is 7/7 passing, from the Phase 4 batch.

## Recommended next tasks

1. Start `plan.md` Phase 5 (ENet integration planning) — now with a real, working full build
   available for verification, not just syntax-checking. Begin with the `FREE_DIRECT_ENABLE_ENET`
   CMake option and vendored/system ENet detection, since everything else in that phase depends on
   ENet actually being available to compile against.
2. Before Phase 9 does real DPID allocation, make the explicit DPID-size decision flagged in
   `docs/directplay-callsite-audit.md` §5.
3. When Phase 15 is reached, wire `tests/directplay_tests.cpp` into `CMakeLists.txt`/CTest — now
   straightforward given the build actually works.
4. Commit this batch's changes (`CMakeLists.txt`, `README.md`, this `NEXT.md`).

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

**SDL3 build blocker resolved (this batch, not yet committed):** see "Completed this batch" above.
