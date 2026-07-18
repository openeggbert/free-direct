# NEXT.md

_Last updated: 2026-07-18, after commit `478c7d7`. This file is a living status snapshot, not a
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
- **Current development phase**: the 24-Hour Stabilization Backlog is closed — **189/189 atomic
  `TASK-24H-XXXX` tasks `DONE`, 0 `TODO`, 0 `PARTIAL`, 0 `BLOCKED`** (`grep`-verified). That backlog
  and every original Phase 0-18 phase confirmed fully complete were archived to
  `archive/plan20260718.md` on 2026-07-18 (item-by-item re-verified first, not just
  checkbox-counted); `plan.md` itself was trimmed down to only the phases with at least one
  genuinely open item (Phase 1, 6-14, 18) - see `plan.md`'s own header for the full split
  rationale. **This means "no queued, tracked work left" is no longer accurate** as of 2026-07-18:
  `plan.md` now surfaces real open items that had previously been buried under a much larger file
  (notably DirectPlay's real networking layer - unfinished ENet join path, unimplemented
  broadcast-to-all despite it being `free-eggbert`'s actual live `Send()` pattern, and unstarted
  host-side routing to non-host recipients, all in Phase 10). Read `plan.md` directly for the
  current, accurate list rather than trusting this summary to stay in sync with it.
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

**Build status: working.** Last verified 2026-07-18 (commit `478c7d7`): fresh out-of-tree
configure+build with `cmake -B <dir> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`
exits 0. Both target games (`../free-eggbert`, `../planetblupi`) were rebuilt fresh out-of-tree
against the current library state (post-`DirectDraw.cpp` split) and both compiled and linked
cleanly, zero `error:` matches.

**Test status: passing.** Default (non-ENet) `ctest` run: **9/9** registered tests pass
(`directplay_tests`, `directdraw_tests`, `directsound_tests`, `directsound_nodriver_test`,
`integration_tests`, `header_smoke_ddraw`, `header_smoke_dsound`, `header_smoke_dplay`,
`header_hygiene`), re-verified 2026-07-18 after the `DirectDraw.cpp` split. `header_hygiene`
(`tests/check_header_hygiene.sh`) also re-verified separately. ENet-enabled build and sanitizer
build were not re-verified in this pass (last known-good: commit `b4902fb`) — re-check before
relying on those specific configurations.

**Available artifacts:**
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`). Confirmed to actually run headlessly (not just
  compile): stable render loop, no crash, over a multi-second observation window under
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`. The "Failed to load image: player.png" warning
  reported in earlier versions of this file was investigated 2026-07-18 and found to be a false
  alarm, not a missing asset: `player.png` is committed to the repo root and loads fine when the
  demo is run from there (exactly as this file's own Section 7 command has always documented) —
  the warning only appears if the binary is run from inside `build/` instead. No code or asset
  change was needed; see Section 5 for the corrected note.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/directsound_nodriver_test.cpp`, `tests/integration_tests.cpp`,
  `tests/enet_directplay_tests.cpp`, `tests/TestHelpers.hpp` (shared DirectDraw/DirectSound test
  scaffolding, not its own CTest target), `tests/header_smoke_*.cpp`,
  `tests/check_header_hygiene.sh` — all CMake/CTest-wired.

**Both target games were actually launched and observed for the first time, 2026-07-18** (this
file previously listed this as the single biggest verification gap in the project — it is now
closed):
- Building either game fresh and running the resulting binary from its own `build/bin/` directory
  fails fast with `[MessageBoxA] <Game>: Error (Game not correctly installed)` and exits — **not a
  FreeDirect defect**: `ReadConfig()` in both games' own source does a plain `fopen("data/config.def",
  "rb")` relative to the process's CWD, and neither game's CMake build deploys its game-data
  directories (`free-eggbert`'s `gamefiles/{DATA,IMAGE08,IMAGE16,SOUND}`; `planetblupi`'s
  `data/`/`image/`/`movie/`/`resource/`/`scripts/`/`sound/`/`user_data/`, all at that game's repo
  root) into the build output directory the way e.g. the MIDI soundfont is. This is a gap in those
  two sibling repos' own CMake asset-deployment step, out of `free-direct`'s scope to fix (and
  their game source must never be modified from here per Section 6's invariants).
- Working around this locally with scratch symlinks (see Section 7's new recipe; **not committed
  anywhere, not a code change**, purely to continue the observation), **both games ran successfully
  and continuously**, exercising FreeDirect for real for the first time:
  - `free-eggbert`'s `SPEEDY_BLUPI_WINDOWS`: got past its own truecolor→8-bit sprite-pack fallback
    logic (its shipped `gamefiles/` only has `IMAGE08`, not the `IMAGE16` sprites some code paths
    try first — `free-api LoadImageA`/`_lopen` correctly reported those as not-found, and the game
    recovered via its own fallback, exactly as designed), then ran its main loop stably for the
    full 8-second observation window: `FREE_DIRECT_PERF`/`FREE_EGGBERT_PERF` both showed a steady
    ~20 FPS, `present_count` climbing continuously (16→136), no crash.
  - `planetblupi`'s `PLANET_BLUPI_WINDOWS`: same result — stable main loop for the full 8-second
    window, `present_count` climbing continuously (10→68, ~8-11 FPS), no crash.
  - Neither run was a full playthrough or interactive session (headless, `SDL_VIDEODRIVER=dummy`,
    no input driven) — this confirms both games' startup/init/render path works end-to-end through
    FreeDirect, not full gameplay correctness.
- DirectPlay's real multiplayer path still has never been exercised by an actual running game
  session (unchanged — see below and Section 5): `free-eggbert`'s own packet pump
  (`CDecor::TreatNetData()`, `src/decnet.cpp:87`) still has its one call site commented out
  (`src/event.cpp:2045`), re-confirmed 2026-07-18. `free-eggbert` itself is under active, ongoing
  development (last commit there 2026-07-10 as of this check) — this is understood to be temporary,
  not permanent.
- Host migration (`DPSESSION_MIGRATEHOST`) is not implemented — `free-eggbert` sets the flag when
  hosting, but `Open()` never reads `DPSESSIONDESC2::dwFlags` at all. Unchanged this pass.

## 3. Recent changes

Most recent commits (newest first), all on `develop`:

- `478c7d7` — Fixed a dangling reference to `../freeapiissues.md` in `CLAUDE.md` after the user
  deleted that file (it lives outside this repo, in the non-git `openeggbert/` parent directory —
  see the `43b7a1a` entry below for how it was created).
- `d796c64` — Split `DirectDraw.cpp` (had grown to 1869 lines) into one file per class-family,
  user-approved, mirroring `src/directplay/`'s existing multi-file convention: new
  `DirectDrawInternal.hpp` (private, shared helpers + the mutually-`friend`ed
  `DirectDrawSurfaceImpl`/`DirectDrawImpl` declarations), `DirectDrawSurface.cpp` (840 lines, all
  `DirectDrawSurfaceImpl` methods), `DirectDrawPalette.hpp`/`.cpp` and `DirectDrawClipper.hpp`/`.cpp`
  (the two small auxiliary objects), and `DirectDraw.cpp` itself now down to 644 lines
  (`DirectDrawImpl` methods + the public `DirectDrawCreate` entry point). Behavior-preserving:
  method bodies moved via precise `sed` line-range extraction (not retyped), verified via a full
  clean build + `ctest` (9/9 passing, same suite as before) — no test file needed any change, since
  tests only ever went through the public `IDirectDraw*` interfaces. See Section 1 and Section 6
  for the resulting architecture.
- `43b7a1a` — Deleted `TODO.md` entirely (explicit user override of `CLAUDE.md`'s prior "not
  deleted" policy), after every item in it had been individually walked through with the user and
  resolved: fixed in code, confirmed as an intentional documented simplification (added to
  `docs/directdraw-limitations.md`), or confirmed stale. Its `free-api`-related content had
  already been split out to `../freeapiissues.md` (a sibling file outside this repo, in the
  non-git `openeggbert/` parent directory) in the preceding `5171ef1`/`8cefe2b`/`d2defe7` commits —
  that file was later deleted by the user, separately from this repo (see `478c7d7` above).
  `CLAUDE.md` and `plan.md` were updated so neither still claims `TODO.md` exists or must not be
  deleted.
- `d2defe7` — Walked all remaining `free-direct`-specific `TODO.md` items to closure one at a time
  with the user (`Flip` flip-chain, texture-per-frame, `DDBLTFX.dwFillColor`, `ddraw.h` header
  naming, `QueryInterface`, and the stale DirectSound/DirectPlay "not yet implemented" note).
  Corrected an earlier mistaken claim that `QueryInterface` was "implemented" for DirectDraw — all
  four DirectDraw classes' `QueryInterface` are honest stubs (`DDERR_UNSUPPORTED` unconditionally),
  confirmed via a full-source grep of both target games that neither ever calls it on a DirectDraw
  object (the only real call site, `free-eggbert/src/network.cpp:91`, is on DirectPlay, which *is*
  correctly implemented). Documented in `docs/directdraw-limitations.md`.
- `8cefe2b`, `5171ef1` — Split `TODO.md` (which used to cover both `free-api` and `free-direct`)
  down to `free-direct`-only content, moving everything about `free-api` out.
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

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker right now.** No failing command, no failing test:
the last verified state (commit `478c7d7`) is a clean build with all 9 default `ctest` checks
passing.

**Correction (2026-07-18, later the same day):** this section previously said "`plan.md` has zero
`TODO` tasks" — that was true only because `plan.md` at the time still contained the fully-done
24-Hour Backlog and Phase 0-18 material undifferentiated from the genuinely open items buried
alongside them. A same-day item-by-item re-verification split `plan.md` (archiving confirmed-done
material to `archive/plan20260718.md`) and surfaced real open work that had simply never been
checked off: most notably in DirectPlay's real networking layer (Phase 10 - unimplemented
broadcast-to-all despite it being `free-eggbert`'s actual live `Send()` call pattern, unstarted
host-side routing to non-host recipients; Phase 7 - the ENet join path is unfinished; Phase 6 - a
join-rejection gap). None of this is build/test-breaking (hence still "no blocker" above), but
"nothing left to do" was not an accurate summary and should not have stood unchallenged - see
`plan.md` directly for the current list, not this file.

The most significant **open risk**, not a blocker, is unchanged from before: DirectPlay's
real-world correctness is unproven by an actual running multiplayer session (only by test suites),
because the one game that would exercise it (`free-eggbert`) has that code path disconnected
pending an in-progress decompilation effort. Nothing in this repository can fix that from the
FreeDirect side — it depends on external progress in the `free-eggbert` decompilation, re-confirmed
still disconnected as of 2026-07-18.

The previous second-most-significant gap — **neither target game had ever actually been launched
and observed**, only compiled against — was closed 2026-07-18: both now confirmed to actually run
their startup/init/render path via FreeDirect (see Section 2). This leaves no other concrete,
internally-actionable verification gap identified right now; see Section 8.

## 5. Known bugs and limitations

- **[incomplete]** Host migration (`DPSESSION_MIGRATEHOST`) — `free-eggbert` sets this flag when
  hosting; `Open()` never reads `DPSESSIONDESC2::dwFlags` at all. Deliberately not implemented;
  not part of any resolved design decision.
- **[resolved 2026-07-18]** Both `free-eggbert` and `planetblupi` were run/observed with FreeDirect
  for the first time (headless, no gameplay interaction) — see Section 2. Each game's own CMake
  build does not deploy its game-data directories into the build output, so running the built
  binary as-is fails fast with a graceful `[MessageBoxA]` error, not a crash — this is a gap in
  those sibling repos' own build scripts, out of `free-direct`'s scope to fix.
- **[needs verification / temporary by design]** DirectPlay's real gameplay reachability:
  `free-eggbert`'s `CDecor::TreatNetData()` call site is commented out
  (`../free-eggbert/src/event.cpp:2045`), re-confirmed 2026-07-18, so all DirectPlay behavior is
  still proven only by test suites, never by a real running session. Expected to change as
  `free-eggbert`'s decompilation progresses (that repo is under active development — last commit
  there 2026-07-10 as of this check) — re-check this call site's status before assuming it's still
  true.
- **[false alarm, corrected 2026-07-18]** Earlier versions of this file claimed the `FREE_DIRECT`
  demo's "Failed to load image: player.png" warning meant the asset was missing. It is not:
  `player.png` is committed at the repo root and loads fine when the demo is run from there (as
  Section 7's own documented command always specified). The warning only appears if the binary is
  launched from inside `build/` instead of the repo root. No code or asset change was made or is
  needed.
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
- **DirectDraw** (`src/directdraw/`, `include/ddraw.h`): split 2026-07-18 into one file per
  class-family (see Section 1/Section 3) — `DirectDrawInternal.hpp` (private, shared helpers +
  the mutually-`friend`ed `DirectDrawSurfaceImpl`/`DirectDrawImpl` declarations),
  `DirectDrawSurface.cpp` (840 lines, all `DirectDrawSurfaceImpl` methods), `DirectDraw.cpp`
  (644 lines, `DirectDrawImpl` methods + the public `DirectDrawCreate` entry point), and small
  `DirectDrawPalette.hpp`/`.cpp`, `DirectDrawClipper.hpp`/`.cpp` files for the two auxiliary
  objects. CPU pixel buffers, uploaded to an SDL3 streaming texture and presented on
  `Blt`/`BltFast`/`Flip` (auto-present against the primary surface). All tests still go through
  the public interface only — the new internal header is not used by any test.
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

Actually run a target game's built binary (not just build it) — added 2026-07-18, since neither
game's own CMake build deploys its game-data directories into the build output, so the binary
fails fast with `[MessageBoxA] <Game>: Error (Game not correctly installed)` otherwise (see
Section 2/Section 5). This is a **local-only, uncommitted workaround** (symlinks, not a code or
game-source change) to get real evidence beyond "compiles" — clean it up afterward:
```bash
# free-eggbert: game data lives in gamefiles/{DATA,IMAGE08,IMAGE16,SOUND} (uppercase) at that
# repo's root; the game's own fopen() calls expect data/image08/image16/sound (lowercase)
# relative to CWD.
cd ../free-eggbert/build/bin   # wherever SPEEDY_BLUPI_WINDOWS actually landed
ln -sfn ../../gamefiles/DATA data
ln -sfn ../../gamefiles/IMAGE08 image08
ln -sfn ../../gamefiles/IMAGE16 image16
ln -sfn ../../gamefiles/SOUND sound
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 8 ./SPEEDY_BLUPI_WINDOWS

# planetblupi: game data already lives at data/image/movie/resource/scripts/sound/user_data/,
# lowercase, at that repo's root — same fopen()-relative-to-CWD issue, just needs those linked in.
cd ../planetblupi/build/bin   # wherever PLANET_BLUPI_WINDOWS actually landed
for d in data image movie resource scripts sound user_data; do ln -sfn ../../$d $d; done
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 8 ./PLANET_BLUPI_WINDOWS
```
Double-check the relative symlink depth against wherever your build actually places the binary
(`build/bin/` in a fresh CMake configure as of 2026-07-18) — an off-by-one here silently resolves
to the wrong directory instead of failing loudly, which cost real time during the 2026-07-18
verification pass.

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

**Correction (2026-07-18, later the same day):** this section previously said every prior task was
done and "no other internally-actionable item is currently known." That was based on `plan.md`
showing zero `TODO` tasks at the time — which was true only because `plan.md` still bundled
everything (done and not-done) into one 8000+ line file where genuinely open items were easy to
miss. A same-day item-by-item re-verification (not just a checkbox recount) split `plan.md` into
`archive/plan20260718.md` (confirmed-complete material) and a trimmed live `plan.md` that now
plainly shows real, unchecked work. Read `plan.md` directly for the authoritative, current list —
this section is a pointer into it, not a substitute for it.

**Highest-value concrete cluster** (`plan.md` Phase 10, "Send/Receive networking"): broadcast-to-all
delivery for `idTo == 0` is unimplemented — directly relevant because it's the exact `Send()`
pattern `free-eggbert`'s own source uses at its one real call site, not a hypothetical. Host-side
routing to non-host recipients is also unstarted. Phase 7 has 5 open ENet-joining-path items; Phase
6 has an unimplemented join-rejection path. These are the closest things to "real remaining
DirectPlay work" this project has surfaced in a while — but which one (if any) to pick up is a
prioritization call for the user, not something to start on unilaterally.

Lower-priority, still real:

1. **Periodically re-check `CDecor::TreatNetData()`'s call-site status in `../free-eggbert`**
   (`grep -n "TreatNetData" ../free-eggbert/src/event.cpp`, expect line ~2045). Externally blocked
   on that repo's own decompilation progress — worth re-confirming occasionally, since
   DirectPlay's real-session verification gap (Section 4) closes the moment that call site is
   reconnected.

2. **If a future change grows `DirectDrawSurface.cpp` (840 lines) or `DirectPlay.cpp`
   (972 lines) significantly further, consider whether either warrants further splitting** —
   not a mandate, only after asking the user first (Section 9).

3. **Several smaller, real gaps surfaced by the 2026-07-18 `plan.md` re-verification**, none
   individually urgent: `DPERR_UNSUPPORTED` is never actually returned for unrecognized DirectPlay
   flag combinations (Phase 11); DirectSound's `GetCurrentPosition` call-site audit was never
   performed or recorded (Phase 13); mixed 8-bit/32-bit DirectDraw blits silently no-op instead of
   erroring, and that choice was never documented as intentional (Phase 14). See `plan.md`'s Phase
   11/13/14 sections for the exact citations.

**Do not invent speculative work beyond what's listed in `plan.md`.** Per `CLAUDE.md`'s Safety
Rules, new API surface or behavior needs a real call-site need or an explicit user ask first.

## 9. Do not do yet

- **No broad refactor** of DirectDraw/DirectSound/DirectPlay files beyond what's already
  landed — each works and is extended incrementally by design. `DirectPlay.cpp`'s
  `Send()`/`Receive()` extraction and the 2026-07-18 `DirectDraw.cpp` multi-file split were both
  one-off, user-approved exceptions — don't treat either as an open door for more without asking
  again first.
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
- **No new features without a concrete need.** The "run the actual games" verification tasks are
  now done (2026-07-18, Section 2) and found no internally-actionable gap — don't manufacture one
  to justify new work; ask the user what to prioritize instead (Section 8).

## 10. Resume prompt

```
Read NEXT.md first (this file) in full before doing anything else. Section 8 ("Next smallest
tasks") currently has no concrete internally-actionable task queued - if that's still true,
say so plainly and ask the user what to prioritize next, rather than inventing speculative work.
If Section 8 does list a concrete task by the time you read this, inspect only the files that
task actually names - do not go exploring or refactoring unrelated code. Make one small, verified
improvement (or, for observational tasks, just do the observation and report what you found - do
not "fix" things that aren't broken). Run the exact verification command that task specifies
before considering it done. Do not touch ../free-eggbert or ../planetblupi source under any
circumstances. Do not start a second task in the same session unless the user explicitly asks for
it. When finished, update NEXT.md to reflect what actually changed (Sections 2/3/4/8 most likely),
commit, and push.
```
