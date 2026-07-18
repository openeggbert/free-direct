# NEXT.md

_Last updated: 2026-07-09, after commit `b4902fb`. This file is a living status snapshot, not a
durable backlog — see `plan.md` for the full task-level history and `CLAUDE.md` for the project
charter/policy rules that govern this repository._

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows.

- **Target games** (sibling repos, present on disk as `../free-eggbert` and `../planetblupi`):
  `free-eggbert` (*Speedy Blupi* — DirectDraw + DirectSound + DirectPlay) and `planetblupi`
  (*Planet Blupi* — DirectDraw + DirectSound only; confirmed zero DirectPlay usage by `grep`).
- **Main goal**: implement exactly the DirectDraw/DirectSound/DirectPlay surface these two games
  actually call — not full DirectX 3 coverage. For DirectPlay specifically, the goal is
  FreeDirect-to-FreeDirect compatibility (two programs both built against this library can
  host/join/exchange messages with each other), explicitly **not** wire-compatibility with real
  Microsoft DirectPlay.
- **Current development phase**: the entire tracked backlog (`plan.md`'s 24-Hour Stabilization
  Backlog) is closed — **189/189 atomic `TASK-24H-XXXX` tasks `DONE`, 0 `TODO`, 0 `PARTIAL`, 0
  `BLOCKED`** (`grep`-verified against `plan.md` directly). There is no queued, tracked work left
  in the repository as of this writing.
- **Important architectural decisions**:
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only — no SDL3/SDL3_net/ENet symbol may ever appear in them. Enforced automatically by a CTest
    test (`header_hygiene`, backed by `tests/check_header_hygiene.sh`), not just manual review.
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`), with the concrete backend chosen at **build time**
    (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option: `LoopbackDirectPlayTransport`
    (in-process, always available, used by all default tests) or `EnetDirectPlayTransport` (real
    ENet UDP sockets, opt-in).
  - DirectSound's implementation class still lives in an anonymous namespace inside its own
    `.cpp` file with no separate internal header. DirectDraw changed on 2026-07-18: it now has a
    private internal header (`src/directdraw/DirectDrawInternal.hpp`, never included from
    `include/`, never installed) declaring the mutually-`friend`ed `DirectDrawSurfaceImpl`/
    `DirectDrawImpl` classes plus shared helpers, with method bodies split across
    `DirectDrawSurface.cpp` (Surface), `DirectDraw.cpp` (Impl + the public `DirectDrawCreate`
    entry point), and `DirectDrawPalette.cpp`/`DirectDrawClipper.cpp` (the two small auxiliary
    objects) — mirroring `src/directplay/`'s existing multi-file split. Every test still
    exercises both subsystems only through the real public `IDirectDraw*`/`IDirectSound*`
    interfaces (no whitebox testing) — that invariant is unchanged.
  - Scope is bounded strictly by real call sites in the two target games — adding any DirectX
    surface, flag, or behavior not demonstrably required by one of them requires asking the user
    first (`CLAUDE.md` Safety Rules); this has been followed consistently throughout the project's
    history.

## 2. Current status

**Build status: working.** Last verified (commit `b4902fb`): fresh out-of-tree configure+build with
`cmake -B <dir> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON` exits 0, as does the
same build with `-DFREE_DIRECT_ENABLE_ENET=ON` and with
`-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON`. Both target games
(`../free-eggbert`, `../planetblupi`) were confirmed to configure and build cleanly out-of-tree
against the current library state (`CONFIGURE_EXIT=0`, `BUILD_EXIT=0`, zero `error:` matches) —
**compilation only**, neither game has actually been run/played with this library.

**Test status: passing.** Default (non-ENet) `ctest` run: **9/9** registered tests pass
(`directplay_tests`, `directdraw_tests`, `directsound_tests`, `directsound_nodriver_test`,
`integration_tests`, `header_smoke_ddraw`, `header_smoke_dsound`, `header_smoke_dplay`,
`header_hygiene`). ENet-enabled build adds a 10th (`enet_directplay_tests`, run via `ctest -L
enet`), also passing. Across all binaries: **177 individual `Test_*` functions** (59 DirectDraw +
35 DirectSound + 70 DirectPlay + 5 integration + 8 ENet-transport), plus
`directsound_nodriver_test`'s own single top-level assertion, all passing.

**Available artifacts:**
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`). Confirmed to actually run headlessly (not just
  compile): stable ~52–53 FPS render loop, no crash, over a multi-second observation window under
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`. Two non-fatal "Failed to load image: player.png"
  warnings appear at startup — a missing demo asset in this environment, not a FreeDirect defect;
  the render loop continues normally regardless.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/directsound_nodriver_test.cpp`, `tests/integration_tests.cpp`,
  `tests/enet_directplay_tests.cpp`, `tests/TestHelpers.hpp` (shared DirectDraw/DirectSound test
  scaffolding, not its own CTest target), `tests/header_smoke_*.cpp`,
  `tests/check_header_hygiene.sh` — all CMake/CTest-wired.

**What does not work / has not been verified yet:**
- Neither `free-eggbert` nor `planetblupi` has actually been run/played using this library — only
  confirmed to compile and link against it.
- DirectPlay's real multiplayer path has never been exercised by an actual running game session.
  `free-eggbert`'s own packet pump (`CDecor::TreatNetData()`, `src/decnet.cpp:87`) has its one call
  site commented out (`src/event.cpp:2045`) as of the last check — DirectPlay's correctness rests
  entirely on test suites simulating the expected call pattern. This is understood to be temporary
  (tied to an in-progress decompilation of `free-eggbert`), not permanent.
- Host migration (`DPSESSION_MIGRATEHOST`) is not implemented — `free-eggbert` sets the flag when
  hosting, but `Open()` never reads `DPSESSIONDESC2::dwFlags` at all.

## 3. Recent changes

Most recent commits (newest first), all on `develop`:

- `b4902fb` — Fixed two stale `@note Status:` Doxygen tag pairs in `include/ddraw.h`:
  `GetDC`/`ReleaseDC` said `STUB` while the implementation was already functionally real (now
  `IMPLEMENTED`); `IsLost`/`Restore` said `IMPLEMENTED` while the implementation is an honest
  inert stub (now `STUB` again). Updated `README.md` and `docs/directdraw-limitations.md` to
  match. Verified DirectSound/DirectPlay headers have no equivalent mismatch.
- `8eb4c2f` — Extracted `DirectPlay2AImpl::Send()`/`Receive()` (previously 145/195 lines, the
  codebase's largest functions) into six small private helper methods
  (`SendBroadcast`/`SendSelf`/`SendUnicast`/`HandleDataPacket`/`HandleJoinAcceptPacket`/
  `DrainWirePackets`). Behavior-preserving — verified via `git diff --stat` (only
  `src/directplay/DirectPlay.cpp` changed) and five build configurations.
- `45809f9` — Added `tests/TestHelpers.hpp`, consolidating DirectDraw/DirectSound test helper
  functions that were duplicated across three test files.
- `83df7c7` — Added `FREE_DIRECT_DEBUG_DPLAY` debug logging to `DirectPlay.cpp` (previously the
  only subsystem with none), implemented with `std::getenv`/`std::vfprintf` rather than
  `SDL_getenv`/`SDL_Log` to avoid adding an unconditional SDL3 dependency to that file.
- `2256c16` — Closed the project's last open `PARTIAL` task: added
  `tests/directsound_nodriver_test.cpp`, a dedicated fresh-process CTest binary that forces and
  verifies `DirectSoundCreate`'s `DSERR_NODRIVER` graceful-failure path.
- `4b2cf93` — Added `tests/integration_tests.cpp`: DirectDraw and DirectSound had never been
  tested running together in one process, despite that being both target games' actual,
  unconditional startup behavior.
- Earlier in this session (see `git log` / `plan.md` for full detail): three deep subsystem audits
  (`docs/audit_ddraw.md`, `docs/audit_dsound.md`, `docs/audit_dplay.md`) produced and closed 29
  hardening tasks across DirectDraw/DirectSound/DirectPlay, including a real correctness fix in
  `Send()`'s self-send path (verified via a deliberate ASan revert/restore check) and a default
  `CMAKE_BUILD_TYPE=Release` fix that had been silently leaving every build unoptimized.

No files were added, modified, or removed by the current update — this pass only rewrote
`NEXT.md` itself, per the explicit instruction not to build or develop anything right now.

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker right now.** No failing command, no failing test:
the last verified state (commit `b4902fb`) is a clean build with all 9 default `ctest` checks
(177+ individual assertions) passing. `plan.md` has zero `TODO` tasks. This should be stated
plainly rather than inventing a problem to fill this section.

The most significant **open risk**, not a blocker, is described in Section 2: DirectPlay's
real-world correctness is unproven by an actual running multiplayer session (only by test suites),
because the one game that would exercise it (`free-eggbert`) has that code path disconnected
pending an in-progress decompilation effort. Nothing in this repository can fix that from the
FreeDirect side — it depends on external progress in the `free-eggbert` decompilation.

The second most significant gap: **neither target game has ever actually been launched and
observed** with this library, only compiled against it. This is the most concrete, actionable
verification gap in the project right now (see Section 8).

## 5. Known bugs and limitations

- **[incomplete]** Host migration (`DPSESSION_MIGRATEHOST`) — `free-eggbert` sets this flag when
  hosting; `Open()` never reads `DPSESSIONDESC2::dwFlags` at all. Deliberately not implemented;
  not part of any resolved design decision.
- **[needs verification]** Neither `free-eggbert` nor `planetblupi` has been run/played with
  FreeDirect — only confirmed to compile and link out-of-tree.
- **[needs verification / temporary by design]** DirectPlay's real gameplay reachability:
  `free-eggbert`'s `CDecor::TreatNetData()` call site is commented out
  (`../free-eggbert/src/event.cpp:2045`), so all DirectPlay behavior is proven only by test
  suites, never by a real running session. Expected to change as `free-eggbert`'s decompilation
  progresses — re-check this call site's status before assuming it's still true.
- **[cosmetic, confirmed cause]** `FREE_DIRECT` demo prints two "Failed to load image: player.png"
  warnings at startup (missing asset in this dev environment). The render loop itself is
  unaffected and continues normally.
- **[known limitation, documented]** `docs/directplay-protocol.md`'s wire header size is
  platform/ABI-dependent (72 bytes on this Linux/LP64 build; would be 64 on real Win32/LLP64) —
  harmless today since no two differently-built FreeDirect peers ever talk to each other.
- **[known limitation, deliberate]** `include/dplay.h` has several `@note Status: STUB` tags on
  macro groups (`DPERR_*` error codes, session/send flags) and type declarations (`DPNAME`,
  `DPSESSIONDESC2`, three callback typedefs) that were explicitly left unexamined during the last
  header-tag-consistency pass (`TASK-24H-0189`) — a status tag doesn't map cleanly onto a struct
  layout or a `#define` block, and resolving this would need real per-field judgment, not a
  mechanical check. Not fixed, not decided; flagged as a separate future task if ever wanted.
- **[known limitation, deliberate]** `GetDC`/`ReleaseDC` do not emulate arbitrary GDI drawing
  operations on the returned `HDC` — only the surface-buffer-wrapping/palette-round-trip pattern
  the two target games actually use is real. Documented in `docs/directdraw-limitations.md`.
- **[known limitation, deliberate]** DirectSound: mono-only `SetPan`, non-seekable
  `SetCurrentPosition`, no real audio looping — confirmed not needed by either target game.
- **[known limitation, deliberate]** `IsLost`/`Restore` always report "not lost" / always succeed
  unconditionally — this backend never loses surfaces the way a real GPU-backed DirectDraw device
  could, and no call site in either game needs real lost-surface recovery.

## 6. Architecture notes

**Three independent subsystems, one pattern each:**
- **DirectDraw** (`src/directdraw/DirectDraw.cpp`, ~1788 lines, `include/ddraw.h`): one
  anonymous-namespace file containing `DirectDrawImpl`, `DirectDrawSurfaceImpl`,
  `DirectDrawPaletteImpl`, `DirectDrawClipperImpl`. CPU pixel buffers, uploaded to an SDL3
  streaming texture and presented on `Blt`/`BltFast`/`Flip` (auto-present against the primary
  surface). No separate internal header — all tests go through the public interface only.
- **DirectSound** (`src/directsound/DirectSound.cpp`, `include/dsound.h`): SDL3-audio-backed,
  static PCM buffer playback only. `SharedAudioDevice` is a process-wide, ref-counted singleton;
  its chosen SDL audio driver is sticky for the process's entire lifetime once first initialized.
- **DirectPlay** (`src/directplay/`, split across ~9 files, `include/dplay.h`): the most
  architecturally elaborate of the three. `DirectPlay2AImpl` (`DirectPlay.cpp`) holds session/
  player/message-queue state and now dispatches to six small private helper methods for
  `Send`/`Receive` (see Section 3). Real network I/O is abstracted behind `IDirectPlayTransport`
  (13 methods), implemented by `LoopbackDirectPlayTransport` (in-process) or
  `EnetDirectPlayTransport` (real ENet UDP), selected at **build time only**.

**Invariants that must not be broken:**
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever — CTest-enforced
  (`header_hygiene`), treat a failure as a policy violation, not a style nit.
- `../free-eggbert` and `../planetblupi` game source must never be modified, under any
  circumstances.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never a runtime switch.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX API surface "for completeness" without asking first.
- Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
  transport entirely (a deliberate design decision, not an oversight).
- DPID `0` is assigned to the host's own first local player (not reserved as a real DirectPlay
  implementation would); `idTo == 0` in `Send()` always means broadcast, checked before the
  self-send branch.

## 7. Useful commands

Standalone build with FreeDirect's own tests:
```bash
cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build build -j8
cd build && ctest --output-on-failure
```

Through a target game (vendored SDL3, no extra flags needed):
```bash
cmake -B build -S ../free-eggbert    # or ../planetblupi
cmake --build build -j8
```

ENet-enabled (opt-in; the unfiltered `ctest` under this config is *expected* to fail
`directplay_tests` — always scope to `-L enet`):
```bash
cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build build -j8
ctest --test-dir build -L enet
```

Sanitizer build (opt-in):
```bash
cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Run one subsystem's tests only:
```bash
ctest -L directdraw   # or -L directsound / -L directplay / -L integration / -L headers / -L enet
```

Check the public-header/backend-leak invariant manually (also a CTest test):
```bash
bash tests/check_header_hygiene.sh include
```

Run the demo headlessly (used to verify it actually runs, not just compiles):
```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/FREE_DIRECT
```

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

1. **Source (or document the absence of) the demo's missing `player.png` asset.**
   Goal: eliminate the two "Failed to load image: player.png" warnings `FREE_DIRECT` prints at
   startup, or add a one-line note explaining why the asset isn't shipped.
   Files: `src/Main.cpp:140` (the load call), wherever demo assets are expected to live.
   Verify: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/FREE_DIRECT`, confirm zero
   "Failed to load image" lines in the output.

2. **Actually run `free-eggbert`'s built executable headlessly and observe whether it starts.**
   Goal: get real evidence beyond "compiles" for the game itself — the single biggest verification
   gap in the project (Section 4). Read-only/observational; do not modify game source.
   Files: none changed — build `../free-eggbert` per Section 7, run the resulting
   `SPEEDY_BLUPI_WINDOWS` binary.
   Verify: run under `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, confirm it starts without
   crashing within a bounded timeout; capture and read any error output.

3. **Same as above for `planetblupi`.**
   Goal / method: identical to task 2, for the other target game (`PLANET_BLUPI_WINDOWS`).
   Verify: same approach.

4. **Re-check `CDecor::TreatNetData()`'s call-site status in `../free-eggbert`.**
   Goal: confirm whether the decompilation-in-progress has reconnected DirectPlay's packet pump
   yet, since that fact drives how much DirectPlay work is actually worth prioritizing.
   Files: `../free-eggbert/src/event.cpp` (around line 2045), `../free-eggbert/src/decnet.cpp`.
   Verify: `grep -n "TreatNetData" ../free-eggbert/src/event.cpp`.

5. **If `DirectDraw.cpp` (currently ~1788 lines) has grown further, consider whether it warrants
   the same extract-method treatment `DirectPlay.cpp`'s `Send()`/`Receive()` got.**
   Goal: a maintainability check, not a mandate — only act if the file has genuinely grown and
   only after asking the user first (this reverses a standing "no broad refactor" default, exactly
   like the `DirectPlay.cpp` refactor did).
   Files: `src/directdraw/DirectDraw.cpp`.
   Verify: `wc -l src/directdraw/DirectDraw.cpp` first, to see if this is even still relevant.

## 9. Do not do yet

- **No broad refactor** of `DirectDraw.cpp`/`DirectSound.cpp` — each works and is extended
  incrementally by design. (`DirectPlay.cpp`'s `Send()`/`Receive()` already got its one
  user-approved exception this session — don't treat that as an open door for more.)
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances,
  including for the "run it and observe" tasks above — read-only only.
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule. Ask first.
- **Do not implement host migration** speculatively — it's a real, open gap, but implementing it
  without a concrete need would be scope expansion, not a bug fix.
- **Do not decide `include/dplay.h`'s macro-group/type-declaration `STUB` tag question
  unilaterally** — explicitly flagged as needing a real per-field judgment call, not a mechanical
  fix, by the task that most recently touched this area.
- **Do not claim full DirectX 3 compatibility or Microsoft DirectPlay wire-compatibility**, ever,
  in any comment, doc, or commit message — this project explicitly is neither.
- **No mass rewrites or speculative architecture changes.** Verify every change with an actual
  build+test run through at least one real consumer (standalone build or a target game) before
  considering it done.
- **No new features until the "run the actual games" verification tasks (Section 8) are done** —
  they're cheap, informational, and might change what's actually worth prioritizing next.

## 10. Resume prompt

```
Read NEXT.md first (this file) in full before doing anything else. Pick the first not-yet-done
task from Section 8 ("Next smallest tasks"). Inspect only the files that task actually names -
do not go exploring or refactoring unrelated code. Make one small, verified improvement (or, for
the observational tasks, just do the observation and report what you found - do not "fix" things
that aren't broken). Run the exact verification command that task specifies before considering it
done. Do not touch ../free-eggbert or ../planetblupi source under any circumstances. Do not start
a second task in the same session unless the user explicitly asks for it. When finished, update
NEXT.md to reflect what actually changed (Sections 2/3/4/8 most likely), commit, and push.
```
